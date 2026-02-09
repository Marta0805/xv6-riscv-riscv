// Performance test for slab allocator.
// Measures allocation/deallocation speed and fragmentation.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// ---- timing helpers ----
static int timer_start(void) { return uptime(); }
static int timer_elapsed(int start) { return uptime() - start; }

// ---- Test 1: Sequential alloc/free throughput ----
static void test_sequential(void)
{
    printf("\n=== Test 1: Sequential alloc/free ===\n");

    int sizes[] = {8, 32, 64, 128, 256, 512, 1024};
    int nsizes = 7;

    for (int s = 0; s < nsizes; s++) {
        int N = 2000;
        int t0 = timer_start();
        for (int i = 0; i < N; i++) {
            uint64 p = kmalloc(sizes[s]);
            if (!p) { printf("  FAIL at %d\n", i); break; }
            kfree(p);
        }
        int dt = timer_elapsed(t0);
        printf("  size=%4d  N=%d  ticks=%d\n", sizes[s], N, dt);
    }
}

// ---- Test 2: Bulk alloc then bulk free ----
static void test_bulk(void)
{
    printf("\n=== Test 2: Bulk alloc + bulk free ===\n");

    // We'll store handles in kernel memory via kmalloc
    int N = 1000;
    uint64 arr = kmalloc(N * 8);  // array of uint64 handles
    if (!arr) { printf("  FAIL: cant alloc arr\n"); return; }

    int t0 = timer_start();

    // Alloc N objects of size 64
    for (int i = 0; i < N; i++) {
        uint64 p = kmalloc(64);
        if (!p) { printf("  FAIL alloc at %d\n", i); break; }
        // Write handle into kernel array
        uint64 val = p;
        slab_write(arr + (uint64)i * 8, &val, 8);
    }

    int t_alloc = timer_elapsed(t0);

    // Free all
    int t1 = timer_start();
    for (int i = 0; i < N; i++) {
        uint64 val;
        slab_read(&val, arr + (uint64)i * 8, 8);
        kfree(val);
    }
    int t_free = timer_elapsed(t1);

    kfree(arr);

    printf("  N=%d  size=64  alloc_ticks=%d  free_ticks=%d  total=%d\n",
           N, t_alloc, t_free, t_alloc + t_free);
}

// ---- Test 3: Cache create/destroy overhead ----
static void test_cache_lifecycle(void)
{
    printf("\n=== Test 3: Cache create/destroy ===\n");

    int N = 100;
    int t0 = timer_start();

    for (int i = 0; i < N; i++) {
        kmem_cache_t c = kmem_cache_create("perf_cache", 48, 0, 0);
        if (!c) { printf("  FAIL create at %d\n", i); break; }

        // Alloc a few objects
        uint64 objs[10];
        for (int j = 0; j < 10; j++) {
            objs[j] = kmem_cache_alloc(c);
        }
        for (int j = 0; j < 10; j++) {
            kmem_cache_free(c, objs[j]);
        }

        kmem_cache_destroy(c);
    }

    int dt = timer_elapsed(t0);
    printf("  N=%d  (create + 10 alloc + 10 free + destroy)  ticks=%d\n", N, dt);
}

// ---- Test 4: Mixed sizes (realistic workload) ----
static void test_mixed(void)
{
    printf("\n=== Test 4: Mixed size workload ===\n");

    int N = 500;
    uint64 arr = kmalloc(N * 8);
    if (!arr) { printf("  FAIL: cant alloc arr\n"); return; }

    int t0 = timer_start();

    for (int i = 0; i < N; i++) {
        // Alternate between different sizes
        int sz;
        switch (i % 5) {
            case 0: sz = 16;   break;
            case 1: sz = 64;   break;
            case 2: sz = 256;  break;
            case 3: sz = 128;  break;
            default: sz = 32;  break;
        }
        uint64 p = kmalloc(sz);
        if (!p) { printf("  FAIL alloc at %d\n", i); break; }
        uint64 val = p;
        slab_write(arr + (uint64)i * 8, &val, 8);
    }

    // Free in reverse order (worst case for some allocators)
    for (int i = N - 1; i >= 0; i--) {
        uint64 val;
        slab_read(&val, arr + (uint64)i * 8, 8);
        kfree(val);
    }

    int dt = timer_elapsed(t0);
    kfree(arr);

    printf("  N=%d  mixed sizes  ticks=%d\n", N, dt);
}

// ---- Test 5: Fragmentation stress ----
static void test_fragmentation(void)
{
    printf("\n=== Test 5: Fragmentation stress ===\n");

    int N = 400;
    uint64 arr = kmalloc(N * 8);
    if (!arr) { printf("  FAIL: cant alloc arr\n"); return; }

    // Allocate all
    for (int i = 0; i < N; i++) {
        uint64 p = kmalloc(64);
        if (!p) { printf("  FAIL alloc at %d\n", i); return; }
        uint64 val = p;
        slab_write(arr + (uint64)i * 8, &val, 8);
    }

    // Free every other one (fragment)
    int t0 = timer_start();
    for (int i = 0; i < N; i += 2) {
        uint64 val;
        slab_read(&val, arr + (uint64)i * 8, 8);
        kfree(val);
    }

    // Re-allocate into the holes
    for (int i = 0; i < N; i += 2) {
        uint64 p = kmalloc(64);
        if (!p) { printf("  FAIL realloc at %d\n", i); break; }
        uint64 val = p;
        slab_write(arr + (uint64)i * 8, &val, 8);
    }

    int dt = timer_elapsed(t0);

    // Cleanup
    for (int i = 0; i < N; i++) {
        uint64 val;
        slab_read(&val, arr + (uint64)i * 8, 8);
        kfree(val);
    }
    kfree(arr);

    printf("  N=%d  fragment+realloc  ticks=%d\n", N, dt);
}

int
main(int argc, char *argv[])
{
    // Initialize slab
    kmem_init(0);

    printf("===== SLAB PERFORMANCE TESTS =====\n");

    test_sequential();
    test_bulk();
    test_cache_lifecycle();
    test_mixed();
    test_fragmentation();

    printf("\n===== ALL PERFORMANCE TESTS DONE =====\n");
    exit(0);
}
