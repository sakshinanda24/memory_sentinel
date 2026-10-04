// sentinel_cli.cpp - MemorySentinel command-line interface
//
// Usage:
//   sentinel status          Overall memory health
//   sentinel leaks           Show leak suspects
//   sentinel frag            Buddy allocator fragmentation
//   sentinel oom             OOM risk assessment
//   sentinel slab            Top slab cache consumers
//   sentinel stop            Stop the daemon

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "../include/sentinel_proto.h"

/* ------------------------------------------------------------------ */
/* ANSI colours                                                         */
/* ------------------------------------------------------------------ */
#define COL_RESET  "\033[0m"
#define COL_RED    "\033[31m"
#define COL_YELLOW "\033[33m"
#define COL_GREEN  "\033[32m"
#define COL_CYAN   "\033[36m"
#define COL_BOLD   "\033[1m"

static const char *sev_color(severity_t s)
{
    switch (s) {
    case SEV_CRITICAL: return COL_RED;
    case SEV_WARN:     return COL_YELLOW;
    default:           return COL_GREEN;
    }
}

static const char *sev_str(severity_t s)
{
    switch (s) {
    case SEV_CRITICAL: return "CRITICAL";
    case SEV_WARN:     return "WARNING";
    default:           return "OK";
    }
}

/* ------------------------------------------------------------------ */
/* Socket helpers                                                       */
/* ------------------------------------------------------------------ */

static int connect_daemon(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SENTINEL_SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Cannot connect to sentinel daemon (%s).\n"
                        "Start it with: sudo sentinel-daemon\n",
                SENTINEL_SOCK_PATH);
        close(fd);
        return -1;
    }
    return fd;
}

static bool recv_all(int fd, void *buf, size_t len)
{
    char *p = (char *)buf;
    while (len > 0) {
        ssize_t n = read(fd, p, len);
        if (n <= 0) return false;
        p   += n;
        len -= (size_t)n;
    }
    return true;
}

static bool send_req(int fd, cmd_t cmd, int pid = -1)
{
    sentinel_req_t req{};
    req.cmd = (uint32_t)cmd;
    req.pid = pid;
    return write(fd, &req, sizeof(req)) == sizeof(req);
}

static bool recv_hdr(int fd, sentinel_resp_hdr_t *hdr)
{
    return recv_all(fd, hdr, sizeof(*hdr));
}

/* ------------------------------------------------------------------ */
/* Display helpers                                                      */
/* ------------------------------------------------------------------ */

static void print_banner(void)
{
    printf(COL_BOLD COL_CYAN
           "╔══════════════════════════════════════════╗\n"
           "║       MemorySentinel v%-6s            ║\n"
           "╚══════════════════════════════════════════╝\n"
           COL_RESET, SENTINEL_VERSION);
}

static void cmd_status(int fd)
{
    send_req(fd, CMD_STATUS);
    sentinel_resp_hdr_t hdr{};
    if (!recv_hdr(fd, &hdr)) { fprintf(stderr, "Read error\n"); return; }

    oom_info_t oi{};
    recv_all(fd, &oi, sizeof(oi));

    print_banner();
    printf("%sSeverity : %s%s\n", sev_color(hdr.severity),
           sev_str(hdr.severity), COL_RESET);
    printf("Summary  : %s\n\n", hdr.message);
    printf("  MemTotal    : %8lu MB\n", (unsigned long)(oi.total_kb     / 1024));
    printf("  MemFree     : %8lu MB\n", (unsigned long)(oi.free_kb      / 1024));
    printf("  MemAvailable: %8lu MB\n", (unsigned long)(oi.available_kb / 1024));
    printf("  SwapTotal   : %8lu MB\n", (unsigned long)(oi.swap_total_kb/ 1024));
    printf("  SwapFree    : %8lu MB\n", (unsigned long)(oi.swap_free_kb / 1024));
    printf("  OOM Risk    : %s%u%%%s\n",
           sev_color(oi.severity), oi.oom_risk_pct, COL_RESET);
}

static void cmd_leaks(int fd)
{
    send_req(fd, CMD_LEAKS);
    sentinel_resp_hdr_t hdr{};
    if (!recv_hdr(fd, &hdr)) return;

    print_banner();
    printf(COL_BOLD "Leak Suspects (%u process(es))\n" COL_RESET, hdr.count);
    if (hdr.count == 0) {
        printf(COL_GREEN "  No leak suspects detected.\n" COL_RESET);
        return;
    }
    printf("%-8s %-20s %10s %10s %10s\n",
           "PID", "Name", "RSS(kB)", "PSS(kB)", "Growth(kB)");
    printf("%-8s %-20s %10s %10s %10s\n",
           "--------", "--------------------", "----------",
           "----------", "----------");

    for (uint32_t i = 0; i < hdr.count; i++) {
        proc_mem_t pm{};
        if (!recv_all(fd, &pm, sizeof(pm))) break;
        printf(COL_YELLOW "%-8d %-20s %10lu %10lu %10lu\n" COL_RESET,
               pm.pid, pm.name,
               (unsigned long)pm.rss_kb,
               (unsigned long)pm.pss_kb,
               (unsigned long)pm.growth_kb);
    }
}

static void cmd_frag(int fd)
{
    send_req(fd, CMD_FRAG);
    sentinel_resp_hdr_t hdr{};
    if (!recv_hdr(fd, &hdr)) return;

    print_banner();
    printf(COL_BOLD "Buddy Allocator Fragmentation (%u zone(s))\n" COL_RESET,
           hdr.count);
    printf("%-20s %8s %10s  Orders 0..10\n", "Zone", "Score", "FreePg");
    printf("%-20s %8s %10s  %s\n", "----", "-----", "------",
           "-------------------------------------------");

    for (uint32_t i = 0; i < hdr.count; i++) {
        frag_info_t fi{};
        if (!recv_all(fd, &fi, sizeof(fi))) break;
        const char *col = fi.frag_score >= 70 ? COL_RED :
                          fi.frag_score >= 40 ? COL_YELLOW : COL_GREEN;
        printf("%s%-20s %7u%% %10u  [", col, fi.zone,
               fi.frag_score, fi.free_pages);
        for (int o = 0; o < 11; o++)
            printf("%u%s", fi.order_counts[o], o < 10 ? "," : "");
        printf("]%s\n", COL_RESET);
    }
}

static void cmd_oom(int fd)
{
    send_req(fd, CMD_OOM);
    sentinel_resp_hdr_t hdr{};
    if (!recv_hdr(fd, &hdr)) return;

    oom_info_t oi{};
    recv_all(fd, &oi, sizeof(oi));

    print_banner();
    printf(COL_BOLD "OOM Risk Assessment\n" COL_RESET);
    printf("  %s\n\n", hdr.message);
    printf("  Risk Level  : %s%s (%u%%)%s\n",
           sev_color(oi.severity), sev_str(oi.severity),
           oi.oom_risk_pct, COL_RESET);
    if (oi.top_oom_pid > 0)
        printf("  Top Victim  : %s (pid %d, oom_score %d)\n",
               oi.top_oom_name, oi.top_oom_pid, oi.top_oom_score);
}

static void cmd_slab(int fd)
{
    send_req(fd, CMD_SLAB);
    sentinel_resp_hdr_t hdr{};
    if (!recv_hdr(fd, &hdr)) return;

    print_banner();
    printf(COL_BOLD "Top Slab Cache Consumers\n" COL_RESET);
    printf("%-24s %12s %12s %8s %10s\n",
           "Cache", "ActiveObj", "TotalObj", "ObjSize", "TotalKB");
    printf("%-24s %12s %12s %8s %10s\n",
           "------------------------", "------------",
           "------------", "--------", "----------");

    for (uint32_t i = 0; i < hdr.count; i++) {
        slab_entry_t se{};
        if (!recv_all(fd, &se, sizeof(se))) break;
        printf("%-24s %12lu %12lu %8u %10lu\n",
               se.name,
               (unsigned long)se.active_objs,
               (unsigned long)se.total_objs,
               se.obj_size,
               (unsigned long)se.total_kb);
    }
}

static void cmd_watch(int fd)
{
    send_req(fd, CMD_WATCH);
    sentinel_resp_hdr_t hdr{};
    if (!recv_hdr(fd, &hdr)) return;

    print_banner();
    printf(COL_BOLD "Live Process Memory (Top %u by RSS)\n" COL_RESET, hdr.count);
    printf("%-8s %-20s %10s %10s %10s %6s\n",
           "PID", "Name", "RSS(kB)", "PSS(kB)", "Heap(kB)", "Leak?");
    printf("%-8s %-20s %10s %10s %10s %6s\n",
           "--------", "--------------------", "----------",
           "----------", "----------", "------");

    for (uint32_t i = 0; i < hdr.count; i++) {
        proc_mem_t pm{};
        if (!recv_all(fd, &pm, sizeof(pm))) break;
        const char *leak_col = pm.leak_suspect ? COL_YELLOW : COL_GREEN;
        printf("%s%-8d %-20s %10lu %10lu %10lu %6s%s\n",
               leak_col,
               pm.pid, pm.name,
               (unsigned long)pm.rss_kb,
               (unsigned long)pm.pss_kb,
               (unsigned long)pm.heap_kb,
               pm.leak_suspect ? "YES" : "no",
               COL_RESET);
    }
}

static void cmd_stop(int fd)
{
    send_req(fd, CMD_STOP);
    sentinel_resp_hdr_t hdr{};
    if (!recv_hdr(fd, &hdr)) return;
    printf("%s\n", hdr.message);
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s <command>\n"
            "Commands:\n"
            "  status   Overall memory health summary\n"
            "  leaks    Show per-process leak suspects\n"
            "  frag     Buddy allocator fragmentation\n"
            "  oom      OOM risk assessment\n"
            "  slab     Top slab cache consumers\n"
            "  watch    Live top-25 processes by RSS\n"
            "  stop     Stop the daemon\n",
            prog);
}

int main(int argc, char *argv[])
{
    if (argc < 2) { usage(argv[0]); return 1; }

    int fd = connect_daemon();
    if (fd < 0) return 1;

    const char *cmd = argv[1];
    if      (strcmp(cmd, "status") == 0) cmd_status(fd);
    else if (strcmp(cmd, "leaks")  == 0) cmd_leaks(fd);
    else if (strcmp(cmd, "frag")   == 0) cmd_frag(fd);
    else if (strcmp(cmd, "oom")    == 0) cmd_oom(fd);
    else if (strcmp(cmd, "slab")   == 0) cmd_slab(fd);
    else if (strcmp(cmd, "watch")  == 0) cmd_watch(fd);
    else if (strcmp(cmd, "stop")   == 0) cmd_stop(fd);
    else { usage(argv[0]); close(fd); return 1; }

    close(fd);
    return 0;
}
