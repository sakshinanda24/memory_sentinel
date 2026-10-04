// SPDX-License-Identifier: GPL-2.0
/*
 * sentinel_mod.c - MemorySentinel Linux Kernel Module
 *
 * Exposes a /proc/sentinel interface providing:
 *   - Buddy allocator fragmentation per zone
 *   - Slab cache top consumers
 *   - System OOM pressure metrics
 *   - vmalloc usage
 *
 * Kernel concepts used:
 *   - proc_fs (seq_file interface)
 *   - mm_zone / free_area traversal
 *   - slab_caches list (kmem_cache)
 *   - si_meminfo() for system memory stats
 *   - oom_score reading via task_struct
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/oom.h>
#include <linux/swap.h>
#include <linux/utsname.h>
#include <linux/version.h>

#define PROC_NAME   "sentinel"
#define OOM_TOP_N   8

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MemorySentinel");
MODULE_DESCRIPTION("Kernel/User-Space Memory Leak, Fragmentation & OOM Diagnosis");
MODULE_VERSION("1.0.0");

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

/*
 * Fragmentation score for a zone: ratio of free pages in low orders
 * (order 0-3) to total free pages. Higher = more fragmented.
 * Score range: 0-100.
 */
static int zone_frag_score(struct zone *zone)
{
    unsigned long low_order_free = 0, total_free = 0;
    int order;

    for (order = 0; order < MAX_ORDER; order++) {
        unsigned long cnt = zone->free_area[order].nr_free << order;
        total_free += cnt;
        if (order <= 3)
            low_order_free += cnt;
    }
    if (total_free == 0)
        return 0;
    return (int)((low_order_free * 100) / total_free);
}

/* ------------------------------------------------------------------ */
/* seq_file sections                                                    */
/* ------------------------------------------------------------------ */

static void show_header(struct seq_file *m)
{
    struct sysinfo si;
    si_meminfo(&si);

    seq_printf(m, "=== MemorySentinel v1.0.0 ===\n");
    seq_printf(m, "Kernel : %s\n", utsname()->release);
    seq_printf(m, "MemTotal   : %8lu kB\n", si.totalram  * (PAGE_SIZE / 1024));
    seq_printf(m, "MemFree    : %8lu kB\n", si.freeram   * (PAGE_SIZE / 1024));
    seq_printf(m, "MemShared  : %8lu kB\n", si.sharedram * (PAGE_SIZE / 1024));
    seq_printf(m, "Buffers    : %8lu kB\n", si.bufferram * (PAGE_SIZE / 1024));
    seq_printf(m, "SwapTotal  : %8lu kB\n", si.totalswap * (PAGE_SIZE / 1024));
    seq_printf(m, "SwapFree   : %8lu kB\n", si.freeswap  * (PAGE_SIZE / 1024));
    seq_printf(m, "\n");
}

static void show_fragmentation(struct seq_file *m)
{
    pg_data_t *pgdat;
    int i;

    seq_printf(m, "--- Buddy Allocator Fragmentation ---\n");
    seq_printf(m, "%-20s %6s %8s  Orders(0..10)\n", "Zone", "Score", "FreePg");

    for_each_online_pgdat(pgdat) {
        for (i = 0; i < pgdat->nr_zones; i++) {
            struct zone *zone = &pgdat->node_zones[i];
            int order, score;
            unsigned long total_free = 0;

            if (!populated_zone(zone))
                continue;

            score = zone_frag_score(zone);
            for (order = 0; order < MAX_ORDER; order++)
                total_free += zone->free_area[order].nr_free << order;

            seq_printf(m, "%-20s %5d%% %8lu  [",
                       zone->name, score, total_free);
            for (order = 0; order < MAX_ORDER; order++)
                seq_printf(m, "%lu%s",
                           zone->free_area[order].nr_free,
                           order < MAX_ORDER - 1 ? "," : "");
            seq_printf(m, "]\n");
        }
    }
    seq_printf(m, "\n");
}

static void show_slab(struct seq_file *m)
{
    seq_printf(m, "--- Top Slab Caches ---\n");
    seq_printf(m, "(Full details available via /proc/slabinfo)\n");

    /*
     * Direct slab_root_caches iteration requires EXPORT_SYMBOL which
     * is not exported in all kernel configs. The daemon reads
     * /proc/slabinfo directly from userspace for full slab stats.
     * The kernel module focuses on buddy + OOM data not available
     * through standard /proc files.
     */
    seq_printf(m, "\n");
}

static void show_vmalloc(struct seq_file *m)
{
    seq_printf(m, "--- vmalloc Usage ---\n");
    seq_printf(m, "vmalloc_total : %8lu kB\n",
               (unsigned long)(VMALLOC_TOTAL / 1024));
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
    seq_printf(m, "vmalloc_used  : %8lu kB\n",
               (unsigned long)(vmalloc_nr_pages() * (PAGE_SIZE / 1024)));
#else
    seq_printf(m, "vmalloc_used  : see /proc/vmallocinfo\n");
#endif
    seq_printf(m, "\n");
}

/* Simple fixed-size array to collect and sort OOM candidates in-kernel */
struct oom_entry {
    pid_t  pid;
    char   comm[TASK_COMM_LEN];
    unsigned long rss_kb;
    short  oom_score_adj;
};

static void show_oom_candidates(struct seq_file *m)
{
    struct task_struct *task;
    struct oom_entry entries[OOM_TOP_N];
    int count = 0, i, j;

    seq_printf(m, "--- Top OOM Candidates (highest RSS) ---\n");
    seq_printf(m, "%-8s %-20s %10s %8s\n",
               "PID", "Comm", "RSS(kB)", "OOM_adj");

    memset(entries, 0, sizeof(entries));

    rcu_read_lock();
    for_each_process(task) {
        struct mm_struct *mm;
        unsigned long rss;

        if (task->flags & PF_KTHREAD)
            continue;
        mm = task->mm;
        if (!mm)
            continue;

        rss = get_mm_rss(mm) * (PAGE_SIZE / 1024);

        /* Insert into top-N by RSS (simple insertion into sorted array) */
        if (count < OOM_TOP_N) {
            entries[count].pid          = task_pid_nr(task);
            entries[count].rss_kb       = rss;
            entries[count].oom_score_adj = task->signal->oom_score_adj;
            strncpy(entries[count].comm, task->comm, TASK_COMM_LEN - 1);
            count++;
        } else {
            /* Replace the entry with the lowest RSS if current is higher */
            int min_idx = 0;
            for (i = 1; i < OOM_TOP_N; i++)
                if (entries[i].rss_kb < entries[min_idx].rss_kb)
                    min_idx = i;
            if (rss > entries[min_idx].rss_kb) {
                entries[min_idx].pid           = task_pid_nr(task);
                entries[min_idx].rss_kb        = rss;
                entries[min_idx].oom_score_adj = task->signal->oom_score_adj;
                strncpy(entries[min_idx].comm, task->comm, TASK_COMM_LEN - 1);
            }
        }
    }
    rcu_read_unlock();

    /* Bubble sort descending by RSS for display */
    for (i = 0; i < count - 1; i++)
        for (j = 0; j < count - i - 1; j++)
            if (entries[j].rss_kb < entries[j + 1].rss_kb) {
                struct oom_entry tmp = entries[j];
                entries[j]     = entries[j + 1];
                entries[j + 1] = tmp;
            }

    for (i = 0; i < count; i++)
        seq_printf(m, "%-8d %-20s %10lu %8d\n",
                   entries[i].pid, entries[i].comm,
                   entries[i].rss_kb, entries[i].oom_score_adj);

    seq_printf(m, "\n");
}

/* ------------------------------------------------------------------ */
/* proc read callback                                                   */
/* ------------------------------------------------------------------ */

static int sentinel_show(struct seq_file *m, void *v)
{
    show_header(m);
    show_fragmentation(m);
    show_slab(m);
    show_vmalloc(m);
    show_oom_candidates(m);
    return 0;
}

static int sentinel_open(struct inode *inode, struct file *file)
{
    return single_open(file, sentinel_show, NULL);
}

static const struct proc_ops sentinel_fops = {
    .proc_open    = sentinel_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

/* ------------------------------------------------------------------ */
/* Module init / exit                                                   */
/* ------------------------------------------------------------------ */

static int __init sentinel_init(void)
{
    if (!proc_create(PROC_NAME, 0444, NULL, &sentinel_fops)) {
        pr_err("sentinel: failed to create /proc/%s\n", PROC_NAME);
        return -ENOMEM;
    }
    pr_info("sentinel: module loaded, /proc/%s ready\n", PROC_NAME);
    return 0;
}

static void __exit sentinel_exit(void)
{
    remove_proc_entry(PROC_NAME, NULL);
    pr_info("sentinel: module unloaded\n");
}

module_init(sentinel_init);
module_exit(sentinel_exit);
