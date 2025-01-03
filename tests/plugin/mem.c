/*
 * Copyright (C) 2018, Emilio G. Cota <cota@braap.org>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#include <inttypes.h>
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <glib.h>

#include <qemu-plugin.h>
#include <sys/syscall.h>
#include <sys/mman.h>

#ifndef likely
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x)   __builtin_expect(!!(x), 0)
#endif

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

char* filename = NULL;

#define MEM_ACCESS_HASHMAP_SIZE     1024
#define MEM_ACCESS_MMAP_SIZE_NR     (1 << 22)
#define MEM_ACCESS_MMAP_SIZE_BYTES  (MEM_ACCESS_MMAP_SIZE_NR * sizeof(struct mem_access))

struct mem_access {
    uint64_t addr:56;
    uint64_t type:8;
    uint64_t time;
    uint64_t basic_block;
    /* uint32_t type; */
    /* uint32_t padding; */
};

struct mem_access_bucket {
    struct mem_access *array;
    uint64_t count;
    pid_t tid;
    int fd;
    int rounds;
    char *path;
};

static struct mem_access_bucket *mem_access_hashmap;

static struct mem_access_bucket *alloc_mem_access_hashmap(void)
{
    struct mem_access_bucket *hm = g_new0(struct mem_access_bucket, MEM_ACCESS_HASHMAP_SIZE);

    if (!hm) {
        perror("Failed to allocate hashmap");
        exit(2);
    }

    return hm;
}

static inline void init_mem_access_bucket(struct mem_access_bucket *bucket,
                                          pid_t tid)
{
    size_t namelen = strlen(filename) + 10;

    bucket->path = malloc(namelen);
    if (!bucket->path) {
        perror("Failed to allocate hashmap bucket");
        exit(2);
    }

    bucket->tid = tid;

    snprintf(bucket->path, namelen,
             "%s/%d",
             filename, tid);
    bucket->fd = open(bucket->path, O_CREAT | O_APPEND | O_RDWR | O_TRUNC,
                      0664);
    if (bucket->fd == -1) {
        perror("Failed to open file");
        exit(-2);
    }
    if (truncate(bucket->path, MEM_ACCESS_MMAP_SIZE_BYTES)) {
        perror("Truncate failed");
        exit(2);
    }
    // qemu_log_mask(LOG_ST_LD, "truncate size: %ld bytes\n",
    //               MEM_ACCESS_MMAP_SIZE_BYTES);

    bucket->array = mmap(NULL, MEM_ACCESS_MMAP_SIZE_BYTES,
                         PROT_READ | PROT_WRITE, MAP_SHARED, bucket->fd, 0);
    if (bucket->array == MAP_FAILED) {
        perror("Failed to mmap array");
        exit(2);
    }
}

static void mem_access_bucket_remap(struct mem_access_bucket *bucket)
{
    /* Unmap previous memory area */
    if (munmap(bucket->array, MEM_ACCESS_MMAP_SIZE_BYTES)) {
        perror("munmap");
        exit(2);
    }
    bucket->rounds++;

    /* Map the following region in the file */
    if (truncate(bucket->path, (bucket->rounds + 1) * MEM_ACCESS_MMAP_SIZE_BYTES)) {
        perror("Re-truncate failed");
        exit(2);
    }
    bucket->array = mmap(NULL, MEM_ACCESS_MMAP_SIZE_BYTES,
                         PROT_READ | PROT_WRITE, MAP_SHARED, bucket->fd,
                         bucket->rounds * MEM_ACCESS_MMAP_SIZE_BYTES);
    if (bucket->array == MAP_FAILED) {
        perror("Failed to remmap array");
        exit(2);
    }
    // qemu_log_mask(LOG_ST_LD, "%s:%d: mmap: @ %p offset: %ld (%ld bytes)\n",
    //               __func__, __LINE__,
    //               bucket->array, bucket->rounds * MEM_ACCESS_MMAP_SIZE_BYTES,
    //               MEM_ACCESS_MMAP_SIZE_BYTES);
    bucket->count = 0;
}

static inline void mem_access_add(uint64_t addr, pid_t tid,
                                  uint64_t basic_block, uint32_t type)
{
    int hash = tid % MEM_ACCESS_HASHMAP_SIZE;
    struct mem_access_bucket *bucket = mem_access_hashmap + hash;
    struct timespec ts;

    if (unlikely(bucket->tid == 0)) {
        init_mem_access_bucket(bucket, tid);
    }

    if (unlikely(bucket->tid != tid)) {
        fprintf(stderr, "Hashmap collision [%d != %d] (size=%d)\n",
                tid, bucket->tid, MEM_ACCESS_HASHMAP_SIZE);
        exit(2);
    }

    /* If the curent mmap is full, remap */
    if (unlikely(bucket->count == MEM_ACCESS_MMAP_SIZE_NR)) {
        mem_access_bucket_remap(bucket);
    }

    /* commit the memory access to array */
    clock_gettime(CLOCK_MONOTONIC, &ts);
    bucket->array[bucket->count] = (struct mem_access) {
        .addr = addr,
        .type = type,
        .time = ts.tv_sec * 1000000000 + ts.tv_nsec,
        .basic_block = basic_block
    };
    bucket->count++;
}


static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    g_free(p);
    g_free(filename);
}

#ifndef thread_local
// Shut up IDE
#define thread_local
#endif

__thread pid_t tid = 0;

static void vcpu_mem(unsigned int cpu_index, qemu_plugin_meminfo_t meminfo,
                     uint64_t vaddr, void *udata)
{
    uint64_t basic_block = (uintptr_t)udata;

    if (unlikely(tid == 0))
    {
        tid = syscall(SYS_gettid);
    }

    mem_access_add(vaddr, tid, basic_block,
                   qemu_plugin_mem_size_shift(meminfo) | qemu_plugin_mem_is_store(meminfo) << 7);
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n = qemu_plugin_tb_n_insns(tb);

    for (size_t i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);

        qemu_plugin_register_vcpu_mem_cb(insn, vcpu_mem,
                                             QEMU_PLUGIN_CB_NO_REGS,
                                             QEMU_PLUGIN_MEM_RW, (void*) qemu_plugin_insn_vaddr(insn));
    }
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{

    for (int i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_auto(GStrv) tokens = g_strsplit(opt, "=", 2);

        if (g_strcmp0(tokens[0], "stld-dir") == 0) {
            filename = g_strdup(tokens[1]);
        } else {
            fprintf(stderr, "option parsing failed: %s\n", opt);
            return -1;
        }
    }
    if (!filename)
    {
        fprintf(stderr, "Did not find stld-dir option. It is required when using this plugin.\n");
        return -1;
    }

    mem_access_hashmap = alloc_mem_access_hashmap();
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, mem_access_hashmap);
    return 0;
}
