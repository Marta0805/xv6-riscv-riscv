// User-space slab allocator test (javni test).
//
// Mirrors the official public test (main.c) but runs from user space
// using per-function syscalls.  Object memory lives in kernel space,
// so all accesses go through slab_read / slab_write syscalls.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define RUN_NUM      5
#define ITERATIONS   1000
#define SHARED_SIZE  7
#define MASK         0xA5

struct data_s {
    int id;
    kmem_cache_t shared;
    int iterations;
};

const char * const CACHE_NAMES[] = {
    "tc_0", "tc_1", "tc_2", "tc_3", "tc_4"
};

// Check that all bytes in the kernel object equal MASK.
// We read them into a local buffer via slab_read.
int check(uint64 kobj, int size)
{
    unsigned char buf[256];
    int ret = 1;
    int off = 0;

    while (off < size) {
        int chunk = size - off;
        if (chunk > (int)sizeof(buf))
            chunk = (int)sizeof(buf);
        if (slab_read(buf, kobj + off, chunk) < 0)
            return 0;
        for (int i = 0; i < chunk; i++) {
            if (buf[i] != MASK)
                ret = 0;
        }
        off += chunk;
    }
    return ret;
}

// Write MASK into all bytes of a kernel object via slab_write.
void fill(uint64 kobj, int size)
{
    unsigned char buf[256];
    memset(buf, MASK, sizeof(buf));
    int off = 0;
    while (off < size) {
        int chunk = size - off;
        if (chunk > (int)sizeof(buf))
            chunk = (int)sizeof(buf);
        slab_write(kobj + off, buf, chunk);
        off += chunk;
    }
}

// We store (cache_handle, obj_handle) pairs in kernel memory via kmalloc.
// Each entry is 16 bytes (two uint64 values).
#define ENTRY_SIZE 16

static void write_entry(uint64 arr, int idx, uint64 cache, uint64 obj)
{
    uint64 pair[2];
    pair[0] = cache;
    pair[1] = obj;
    slab_write(arr + (uint64)idx * ENTRY_SIZE, pair, ENTRY_SIZE);
}

static void read_entry(uint64 arr, int idx, uint64 *cache, uint64 *obj)
{
    uint64 pair[2];
    slab_read(pair, arr + (uint64)idx * ENTRY_SIZE, ENTRY_SIZE);
    *cache = pair[0];
    *obj   = pair[1];
}

void work(void *pdata)
{
    struct data_s data = *(struct data_s *)pdata;
    int size = 0;
    int object_size = data.id + 1;

    kmem_cache_t cache = kmem_cache_create(CACHE_NAMES[data.id],
                                           object_size, 0, 0);
    if (!cache) {
        printf("FAIL: kmem_cache_create returned 0\n");
        return;
    }

    // Allocate array for (cache, obj) pairs via kmalloc in kernel
    uint64 objs = kmalloc(ENTRY_SIZE * data.iterations);
    if (!objs) {
        printf("FAIL: kmalloc returned 0\n");
        return;
    }

    for (int i = 0; i < data.iterations; i++) {
        if (i % 100 == 0) {
            uint64 obj = kmem_cache_alloc(data.shared);
            if (!obj) {
                printf("FAIL: kmem_cache_alloc(shared) returned 0\n");
                return;
            }
            write_entry(objs, size, (uint64)data.shared, obj);
            // Constructor already did memset - just verify
            if (!check(obj, SHARED_SIZE)) {
                printf("Value not correct!");
            }
        } else {
            uint64 obj = kmem_cache_alloc(cache);
            if (!obj) {
                printf("FAIL: kmem_cache_alloc(cache) returned 0\n");
                return;
            }
            write_entry(objs, size, (uint64)cache, obj);
            fill(obj, object_size);
        }
        size++;
    }

    kmem_cache_info(cache);
    kmem_cache_info(data.shared);

    for (int i = 0; i < size; i++) {
        uint64 c, obj;
        read_entry(objs, i, &c, &obj);
        int sz = (c == (uint64)cache) ? object_size : SHARED_SIZE;
        if (!check(obj, sz)) {
            printf("Value not correct!");
        }
        kmem_cache_free((kmem_cache_t)c, obj);
    }

    kfree(objs);
    kmem_cache_destroy(cache);
}

void runs(void (*fn)(void *), struct data_s *data, int num)
{
    for (int i = 0; i < num; i++) {
        struct data_s private_data;
        private_data = *data;
        private_data.id = i;
        fn(&private_data);
    }
}

int
main(int argc, char *argv[])
{
    // Initialize slab allocator (kernel reserves memory in Deo 1,
    // no-op in Deo 2 since slab is already active)
    kmem_init(0);

    // Create shared cache with constructor (MASK=0xA5, size=7)
    // The 3rd arg is ctor_mask, 4th is ctor_size - kernel trampoline
    // will do printf + memset(obj, mask, size) on each alloc.
    kmem_cache_t shared = kmem_cache_create("shared object",
                                            SHARED_SIZE, MASK, SHARED_SIZE);
    if (!shared) {
        printf("FAIL: could not create shared cache\n");
        exit(1);
    }

    struct data_s data;
    data.shared = shared;
    data.iterations = ITERATIONS;

    runs(work, &data, RUN_NUM);

    kmem_cache_destroy(shared);

    printf("Test finished.\n");
    exit(0);
}
