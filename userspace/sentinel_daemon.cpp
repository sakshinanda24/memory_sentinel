// sentinel_daemon.cpp - MemorySentinel userspace monitoring daemon
//
// Responsibilities:
//   - Poll /proc/meminfo, /proc/*/smaps, /proc/sentinel (kernel module),
//     /proc/buddyinfo, /proc/slabinfo every POLL_INTERVAL seconds
//   - Detect per-process memory leaks (monotonic RSS growth)
//   - Compute OOM risk percentage
//   - Serve CLI queries over a Unix domain socket

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <csignal>

#include <unistd.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <map>
#include <vector>
#include <algorithm>

#include "../include/sentinel_proto.h"

/* ------------------------------------------------------------------ */
/* Configuration                                                        */
/* ------------------------------------------------------------------ */
static const int POLL_INTERVAL   = 5;   /* seconds between samples    */
static const int LEAK_WINDOW     = 6;   /* consecutive growth samples */
static const int MAX_PROC        = 512;
static const int BACKLOG         = 8;

/* ------------------------------------------------------------------ */
/* Internal state                                                       */
/* ------------------------------------------------------------------ */
struct ProcHistory {
    char     name[64];
    uint64_t rss_samples[16]; /* ring buffer */
    int      head;
    int      count;
    bool     leak_suspect;
};

static std::map<int, ProcHistory> g_proc_history;
static std::vector<proc_mem_t>    g_proc_snapshot;
static std::vector<frag_info_t>   g_frag_snapshot;
static std::vector<slab_entry_t>  g_slab_snapshot;
static oom_info_t                 g_oom_snapshot;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile sig_atomic_t g_running = 1;

/* ------------------------------------------------------------------ */
/* /proc parsing helpers                                                */
/* ------------------------------------------------------------------ */

static uint64_t read_proc_key(const char *path, const char *key)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[256];
    uint64_t val = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, key, strlen(key)) == 0) {
            sscanf(line + strlen(key), ": %lu", &val);
            break;
        }
    }
    fclose(f);
    return val;
}

static bool read_proc_name(int pid, char *out, size_t sz)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/comm", pid);
    FILE *f = fopen(path, "r");
    if (!f) return false;
    if (fgets(out, (int)sz, f)) {
        out[strcspn(out, "\n")] = '\0';
    }
    fclose(f);
    return true;
}

/* Parse /proc/<pid>/smaps_rollup for RSS and PSS */
static bool read_smaps(int pid, uint64_t *rss_kb, uint64_t *pss_kb, uint64_t *heap_kb)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/smaps_rollup", pid);
    FILE *f = fopen(path, "r");
    if (!f) {
        /* fallback to /proc/<pid>/status */
        snprintf(path, sizeof(path), "/proc/%d/status", pid);
        *rss_kb  = read_proc_key(path, "VmRSS");
        *pss_kb  = *rss_kb;
        *heap_kb = read_proc_key(path, "VmData");
        return (*rss_kb > 0);
    }
    char line[256];
    *rss_kb = *pss_kb = *heap_kb = 0;
    while (fgets(line, sizeof(line), f)) {
        uint64_t v = 0;
        if (sscanf(line, "Rss: %lu kB", &v) == 1)       *rss_kb  = v;
        else if (sscanf(line, "Pss: %lu kB", &v) == 1)  *pss_kb  = v;
    }
    fclose(f);

    /* heap from /proc/<pid>/status VmData */
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    *heap_kb = read_proc_key(path, "VmData");
    return true;
}

/* ------------------------------------------------------------------ */
/* Leak detection                                                       */
/* ------------------------------------------------------------------ */

static bool is_leak_suspect(ProcHistory &h, uint64_t new_rss)
{
    h.rss_samples[h.head % 16] = new_rss;
    h.head++;
    if (h.count < 16) h.count++;

    if (h.count < LEAK_WINDOW) return false;

    /* Check last LEAK_WINDOW samples are monotonically increasing */
    int start = (h.head - LEAK_WINDOW + 16) % 16;
    for (int i = 1; i < LEAK_WINDOW; i++) {
        int a = (start + i - 1) % 16;
        int b = (start + i)     % 16;
        if (h.rss_samples[b] <= h.rss_samples[a])
            return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Snapshot collectors                                                  */
/* ------------------------------------------------------------------ */

static void collect_processes(void)
{
    DIR *dir = opendir("/proc");
    if (!dir) return;

    std::vector<proc_mem_t> snap;
    struct dirent *ent;

    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_type != DT_DIR) continue;
        char *end;
        long pid = strtol(ent->d_name, &end, 10);
        if (*end != '\0' || pid <= 0) continue;

        uint64_t rss, pss, heap;
        if (!read_smaps((int)pid, &rss, &pss, &heap)) continue;

        ProcHistory &h = g_proc_history[(int)pid];
        if (h.name[0] == '\0')
            read_proc_name((int)pid, h.name, sizeof(h.name));

        uint64_t prev_rss = (h.count > 0)
            ? h.rss_samples[(h.head - 1 + 16) % 16] : rss;

        h.leak_suspect = is_leak_suspect(h, rss);

        proc_mem_t pm{};
        pm.pid         = (int32_t)pid;
        pm.rss_kb      = rss;
        pm.pss_kb      = pss;
        pm.heap_kb     = heap;
        pm.growth_kb   = (rss > prev_rss) ? (rss - prev_rss) : 0;
        pm.leak_suspect = h.leak_suspect ? 1 : 0;
        memcpy(pm.name, h.name, sizeof(pm.name));
        pm.name[sizeof(pm.name) - 1] = '\0';
        snap.push_back(pm);

        if ((int)snap.size() >= MAX_PROC) break;
    }
    closedir(dir);

    /* Sort by RSS descending */
    std::sort(snap.begin(), snap.end(),
              [](const proc_mem_t &a, const proc_mem_t &b){
                  return a.rss_kb > b.rss_kb;
              });

    pthread_mutex_lock(&g_lock);
    g_proc_snapshot = snap;
    pthread_mutex_unlock(&g_lock);
}

static void collect_fragmentation(void)
{
    FILE *f = fopen("/proc/buddyinfo", "r");
    if (!f) return;

    std::vector<frag_info_t> snap;
    char line[512];

    while (fgets(line, sizeof(line), f)) {
        frag_info_t fi{};
        char zone[32];
        /* Format: Node N, zone  ZoneName  c0 c1 c2 ... c10 */
        int n = sscanf(line, "Node %*d, zone %31s", zone);
        if (n != 1) continue;
        memcpy(fi.zone, zone, sizeof(fi.zone));
        fi.zone[sizeof(fi.zone) - 1] = '\0';

        /* Parse counts after zone name */
        char *p = strchr(line, ' ');
        for (int skip = 0; skip < 3 && p; skip++)
            p = strchr(p + 1, ' ');

        uint32_t total_free = 0, low_order = 0;
        for (int o = 0; o < 11 && p; o++) {
            fi.order_counts[o] = (uint32_t)strtoul(p, &p, 10);
            uint32_t pages = fi.order_counts[o] << o;
            total_free += pages;
            if (o <= 3) low_order += pages;
        }
        fi.free_pages  = total_free;
        fi.frag_score  = total_free ? (low_order * 100 / total_free) : 0;
        snap.push_back(fi);
    }
    fclose(f);

    pthread_mutex_lock(&g_lock);
    g_frag_snapshot = snap;
    pthread_mutex_unlock(&g_lock);
}

static void collect_slab(void)
{
    FILE *f = fopen("/proc/slabinfo", "r");
    if (!f) return;

    std::vector<slab_entry_t> snap;
    char line[512];
    /* Skip two header lines */
    if (!fgets(line, sizeof(line), f) || !fgets(line, sizeof(line), f)) {
        fclose(f);
        return;
    }

    while (fgets(line, sizeof(line), f)) {
        slab_entry_t se{};
        uint64_t active_objs, total_objs;
        int r = sscanf(line, "%31s %lu %lu %u",
                       se.name, &active_objs, &total_objs, &se.obj_size);
        if (r < 4) continue;
        se.active_objs = active_objs;
        se.total_objs  = total_objs;
        se.total_kb    = (total_objs * se.obj_size) / 1024;
        snap.push_back(se);
    }
    fclose(f);

    /* Sort by total_kb descending, keep top 20 */
    std::sort(snap.begin(), snap.end(),
              [](const slab_entry_t &a, const slab_entry_t &b){
                  return a.total_kb > b.total_kb;
              });
    if (snap.size() > 20) snap.resize(20);

    pthread_mutex_lock(&g_lock);
    g_slab_snapshot = snap;
    pthread_mutex_unlock(&g_lock);
}

static void collect_oom(void)
{
    oom_info_t oi{};
    oi.total_kb      = read_proc_key("/proc/meminfo", "MemTotal");
    oi.free_kb       = read_proc_key("/proc/meminfo", "MemFree");
    oi.available_kb  = read_proc_key("/proc/meminfo", "MemAvailable");
    oi.swap_total_kb = read_proc_key("/proc/meminfo", "SwapTotal");
    oi.swap_free_kb  = read_proc_key("/proc/meminfo", "SwapFree");

    uint64_t total_avail = oi.available_kb + oi.swap_free_kb;
    uint64_t total_cap   = oi.total_kb     + oi.swap_total_kb;
    uint64_t used        = total_cap > total_avail ? total_cap - total_avail : 0;
    oi.oom_risk_pct = total_cap ? (uint32_t)((used * 100) / total_cap) : 0;

    if      (oi.oom_risk_pct >= 90) oi.severity = SEV_CRITICAL;
    else if (oi.oom_risk_pct >= 70) oi.severity = SEV_WARN;
    else                            oi.severity = SEV_OK;

    /* Find top OOM candidate by oom_score — read scores outside the lock
     * to avoid holding g_lock while doing file I/O.                      */
    std::vector<proc_mem_t> snap_copy;
    pthread_mutex_lock(&g_lock);
    snap_copy = g_proc_snapshot;
    pthread_mutex_unlock(&g_lock);

    oi.top_oom_pid   = -1;
    oi.top_oom_score = -1000;
    for (auto &pm : snap_copy) {
        char path[64];
        snprintf(path, sizeof(path), "/proc/%d/oom_score", pm.pid);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        int score = 0;
        if (fscanf(f, "%d", &score) != 1) score = 0;
        fclose(f);
        if (score > oi.top_oom_score) {
            oi.top_oom_score = score;
            oi.top_oom_pid   = pm.pid;
            memcpy(oi.top_oom_name, pm.name, sizeof(oi.top_oom_name));
            oi.top_oom_name[sizeof(oi.top_oom_name) - 1] = '\0';
        }
    }

    pthread_mutex_lock(&g_lock);
    g_oom_snapshot = oi;
    pthread_mutex_unlock(&g_lock);
}

/* ------------------------------------------------------------------ */
/* Polling thread                                                       */
/* ------------------------------------------------------------------ */

static void *poll_thread(void *)
{
    while (g_running) {
        collect_processes();
        collect_fragmentation();
        collect_slab();
        collect_oom();
        sleep(POLL_INTERVAL);
    }
    return nullptr;
}

/* ------------------------------------------------------------------ */
/* Socket helpers                                                       */
/* ------------------------------------------------------------------ */

static void send_all(int fd, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    while (len > 0) {
        ssize_t n = write(fd, p, len);
        if (n <= 0) return;
        p   += n;
        len -= (size_t)n;
    }
}

static void handle_client(int cfd)
{
    sentinel_req_t req{};
    if (read(cfd, &req, sizeof(req)) != sizeof(req)) return;

    sentinel_resp_hdr_t hdr{};

    pthread_mutex_lock(&g_lock);

    switch ((cmd_t)req.cmd) {

    case CMD_STATUS: {
        hdr.status   = 0;
        hdr.severity = g_oom_snapshot.severity;
        hdr.count    = 1;
        snprintf(hdr.message, sizeof(hdr.message),
                 "OOM risk: %u%% | Leaks: %zu | Zones: %zu",
                 g_oom_snapshot.oom_risk_pct,
                 std::count_if(g_proc_snapshot.begin(), g_proc_snapshot.end(),
                               [](const proc_mem_t &p){ return p.leak_suspect; }),
                 g_frag_snapshot.size());
        send_all(cfd, &hdr, sizeof(hdr));
        send_all(cfd, &g_oom_snapshot, sizeof(g_oom_snapshot));
        break;
    }

    case CMD_LEAKS: {
        std::vector<proc_mem_t> leaks;
        for (auto &p : g_proc_snapshot)
            if (p.leak_suspect) leaks.push_back(p);
        hdr.status = 0;
        hdr.count  = (uint32_t)leaks.size();
        hdr.severity = leaks.empty() ? SEV_OK : SEV_WARN;
        snprintf(hdr.message, sizeof(hdr.message),
                 "%u process(es) suspected of memory leak", hdr.count);
        send_all(cfd, &hdr, sizeof(hdr));
        for (auto &p : leaks)
            send_all(cfd, &p, sizeof(p));
        break;
    }

    case CMD_FRAG: {
        hdr.status = 0;
        hdr.count  = (uint32_t)g_frag_snapshot.size();
        snprintf(hdr.message, sizeof(hdr.message),
                 "%u memory zone(s)", hdr.count);
        send_all(cfd, &hdr, sizeof(hdr));
        for (auto &f : g_frag_snapshot)
            send_all(cfd, &f, sizeof(f));
        break;
    }

    case CMD_OOM: {
        hdr.status   = 0;
        hdr.count    = 1;
        hdr.severity = g_oom_snapshot.severity;
        snprintf(hdr.message, sizeof(hdr.message),
                 "OOM risk %u%% | Top candidate: %s (pid %d, score %d)",
                 g_oom_snapshot.oom_risk_pct,
                 g_oom_snapshot.top_oom_name,
                 g_oom_snapshot.top_oom_pid,
                 g_oom_snapshot.top_oom_score);
        send_all(cfd, &hdr, sizeof(hdr));
        send_all(cfd, &g_oom_snapshot, sizeof(g_oom_snapshot));
        break;
    }

    case CMD_SLAB: {
        hdr.status = 0;
        hdr.count  = (uint32_t)g_slab_snapshot.size();
        snprintf(hdr.message, sizeof(hdr.message),
                 "Top %u slab caches by memory", hdr.count);
        send_all(cfd, &hdr, sizeof(hdr));
        for (auto &s : g_slab_snapshot)
            send_all(cfd, &s, sizeof(s));
        break;
    }

    case CMD_STOP:
        g_running = 0;
        hdr.status = 0;
        snprintf(hdr.message, sizeof(hdr.message), "Daemon stopping");
        send_all(cfd, &hdr, sizeof(hdr));
        break;

    case CMD_WATCH: {
        /* Return top-25 processes by RSS for live watch mode */
        uint32_t limit = 25;
        std::vector<proc_mem_t> top;
        top.reserve(limit);
        for (auto &p : g_proc_snapshot) {
            top.push_back(p);
            if (top.size() >= limit) break;
        }
        hdr.status = 0;
        hdr.count  = (uint32_t)top.size();
        snprintf(hdr.message, sizeof(hdr.message),
                 "Top %u processes by RSS", hdr.count);
        send_all(cfd, &hdr, sizeof(hdr));
        for (auto &p : top)
            send_all(cfd, &p, sizeof(p));
        break;
    }

    default:
        hdr.status = 1;
        snprintf(hdr.message, sizeof(hdr.message), "Unknown command");
        send_all(cfd, &hdr, sizeof(hdr));
        break;
    }

    pthread_mutex_unlock(&g_lock);
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */

static void sig_handler(int) { g_running = 0; }

int main(void)
{
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGPIPE, SIG_IGN);

    /* Initial collection before accepting connections */
    collect_processes();
    collect_fragmentation();
    collect_slab();
    collect_oom();

    /* Start polling thread */
    pthread_t tid;
    pthread_create(&tid, nullptr, poll_thread, nullptr);

    /* Create Unix domain socket */
    unlink(SENTINEL_SOCK_PATH);
    int sfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sfd < 0) { perror("socket"); return 1; }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SENTINEL_SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    chmod(SENTINEL_SOCK_PATH, 0666);
    listen(sfd, BACKLOG);

    fprintf(stdout, "[sentinel-daemon] listening on %s\n", SENTINEL_SOCK_PATH);
    fflush(stdout);

    while (g_running) {
        int cfd = accept(sfd, nullptr, nullptr);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            break;
        }
        handle_client(cfd);
        close(cfd);
    }

    close(sfd);
    unlink(SENTINEL_SOCK_PATH);
    pthread_join(tid, nullptr);
    fprintf(stdout, "[sentinel-daemon] stopped\n");
    return 0;
}
