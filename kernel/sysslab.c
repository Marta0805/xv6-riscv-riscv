// System call wrappers for slab allocator testing.
//
// Per-function syscalls (kmem_cache_create, kmem_cache_alloc, etc.) for
// user-space testing.  Objects are returned as opaque uint64 handles;
// slab_write/slab_read copy data between user and kernel space.

#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "slab.h"
#include "memlayout.h"

#ifndef SLAB_KERNEL
// Deo 1: reserve a contiguous region for slab's buddy.
// We take the top 32 MB of physical memory (8192 pages).
#define SLAB_TEST_BLOCKS   8192
#define SLAB_TEST_SIZE     ((uint64)SLAB_TEST_BLOCKS * 4096)
#define SLAB_TEST_START    ((void *)(PHYSTOP - SLAB_TEST_SIZE))
#endif

// ================================================================
//  Per-function syscalls for user-space testing
// ================================================================

// ---------- built-in constructor support ----------

#define MAX_CTORS 16

struct builtin_ctor {
    int in_use;
    unsigned char mask;
    int size;
};

static struct builtin_ctor ctors[MAX_CTORS];
static struct spinlock ctor_lock;
static int ctor_lock_inited = 0;

static void init_ctor_lock(void)
{
    if (!ctor_lock_inited) {
        initlock(&ctor_lock, "ctorlock");
        ctor_lock_inited = 1;
    }
}

static int ctor_counter = 0;

static void ctor_fn_0(void *p)  { printf("%d Shared object constructed.\n", ++ctor_counter); memset(p, ctors[0].mask,  ctors[0].size);  }
static void ctor_fn_1(void *p)  { printf("%d Shared object constructed.\n", ++ctor_counter); memset(p, ctors[1].mask,  ctors[1].size);  }
static void ctor_fn_2(void *p)  { printf("%d Shared object constructed.\n", ++ctor_counter); memset(p, ctors[2].mask,  ctors[2].size);  }
static void ctor_fn_3(void *p)  { printf("%d Shared object constructed.\n", ++ctor_counter); memset(p, ctors[3].mask,  ctors[3].size);  }
static void ctor_fn_4(void *p)  { printf("%d Shared object constructed.\n", ++ctor_counter); memset(p, ctors[4].mask,  ctors[4].size);  }
static void ctor_fn_5(void *p)  { printf("%d Shared object constructed.\n", ++ctor_counter); memset(p, ctors[5].mask,  ctors[5].size);  }
static void ctor_fn_6(void *p)  { printf("%d Shared object constructed.\n", ++ctor_counter); memset(p, ctors[6].mask,  ctors[6].size);  }
static void ctor_fn_7(void *p)  { printf("%d Shared object constructed.\n", ++ctor_counter); memset(p, ctors[7].mask,  ctors[7].size);  }
static void ctor_fn_8(void *p)  { printf("%d Shared object constructed.\n", ++ctor_counter); memset(p, ctors[8].mask,  ctors[8].size);  }
static void ctor_fn_9(void *p)  { printf("%d Shared object constructed.\n", ++ctor_counter); memset(p, ctors[9].mask,  ctors[9].size);  }
static void ctor_fn_10(void *p) { printf("%d Shared object constructed.\n", ++ctor_counter); memset(p, ctors[10].mask, ctors[10].size); }
static void ctor_fn_11(void *p) { printf("%d Shared object constructed.\n", ++ctor_counter); memset(p, ctors[11].mask, ctors[11].size); }
static void ctor_fn_12(void *p) { printf("%d Shared object constructed.\n", ++ctor_counter); memset(p, ctors[12].mask, ctors[12].size); }
static void ctor_fn_13(void *p) { printf("%d Shared object constructed.\n", ++ctor_counter); memset(p, ctors[13].mask, ctors[13].size); }
static void ctor_fn_14(void *p) { printf("%d Shared object constructed.\n", ++ctor_counter); memset(p, ctors[14].mask, ctors[14].size); }
static void ctor_fn_15(void *p) { printf("%d Shared object constructed.\n", ++ctor_counter); memset(p, ctors[15].mask, ctors[15].size); }

static void (*ctor_table[MAX_CTORS])(void *) = {
    ctor_fn_0,  ctor_fn_1,  ctor_fn_2,  ctor_fn_3,
    ctor_fn_4,  ctor_fn_5,  ctor_fn_6,  ctor_fn_7,
    ctor_fn_8,  ctor_fn_9,  ctor_fn_10, ctor_fn_11,
    ctor_fn_12, ctor_fn_13, ctor_fn_14, ctor_fn_15,
};

static void (*alloc_ctor(unsigned char mask, int size))(void *)
{
    init_ctor_lock();
    acquire(&ctor_lock);
    for (int i = 0; i < MAX_CTORS; i++) {
        if (!ctors[i].in_use) {
            ctors[i].in_use = 1;
            ctors[i].mask = mask;
            ctors[i].size = size;
            release(&ctor_lock);
            return ctor_table[i];
        }
    }
    release(&ctor_lock);
    return 0;
}

static void free_ctor(void (*fn)(void *))
{
    if (!fn) return;
    init_ctor_lock();
    acquire(&ctor_lock);
    for (int i = 0; i < MAX_CTORS; i++) {
        if (ctor_table[i] == fn) {
            ctors[i].in_use = 0;
            break;
        }
    }
    release(&ctor_lock);
}

// ---------- per-function syscalls ----------

uint64
sys_kmem_init(void)
{
#ifndef SLAB_KERNEL
    // Deo 1: initialize slab with reserved memory region (top 32 MB)
    kmem_init(SLAB_TEST_START, SLAB_TEST_BLOCKS);
#endif
    // In Deo 2 mode (SLAB_KERNEL): already initialized at boot, no-op
    return 0;
}

uint64
sys_kmem_cache_create(void)
{
    char name[32];
    int size;
    int ctor_mask;
    int ctor_size;

    if (argstr(0, name, sizeof(name)) < 0)
        return 0;
    argint(1, &size);
    argint(2, &ctor_mask);
    argint(3, &ctor_size);

    void (*ctor)(void *) = 0;
    if (ctor_mask != 0) {
        ctor = alloc_ctor((unsigned char)ctor_mask, ctor_size);
        if (!ctor)
            return 0;
    }

    kmem_cache_t *cache = kmem_cache_create(name, (size_t)size, ctor, 0);
    if (!cache && ctor)
        free_ctor(ctor);

    return (uint64)cache;
}

uint64
sys_kmem_cache_alloc(void)
{
    uint64 handle;
    argaddr(0, &handle);
    if (!handle)
        return 0;
    void *obj = kmem_cache_alloc((kmem_cache_t *)handle);
    return (uint64)obj;
}

uint64
sys_kmem_cache_free(void)
{
    uint64 cache_handle, obj_ptr;
    argaddr(0, &cache_handle);
    argaddr(1, &obj_ptr);
    if (!cache_handle || !obj_ptr)
        return -1;
    kmem_cache_free((kmem_cache_t *)cache_handle, (void *)obj_ptr);
    return 0;
}

uint64
sys_kmem_cache_destroy(void)
{
    uint64 handle;
    argaddr(0, &handle);
    if (!handle)
        return -1;
    kmem_cache_t *cache = (kmem_cache_t *)handle;
    free_ctor(cache->ctor);
    kmem_cache_destroy(cache);
    return 0;
}

uint64
sys_kmem_cache_shrink(void)
{
    uint64 handle;
    argaddr(0, &handle);
    if (!handle)
        return -1;
    return kmem_cache_shrink((kmem_cache_t *)handle);
}

uint64
sys_kmem_cache_info(void)
{
    uint64 handle;
    argaddr(0, &handle);
    if (!handle)
        return -1;
    kmem_cache_info((kmem_cache_t *)handle);
    return 0;
}

uint64
sys_kmem_cache_error(void)
{
    uint64 handle;
    argaddr(0, &handle);
    if (!handle)
        return -1;
    return kmem_cache_error((kmem_cache_t *)handle);
}

uint64
sys_kmalloc(void)
{
    int size;
    argint(0, &size);
    if (size <= 0)
        return 0;
    void *ptr = kmalloc((size_t)size);
    return (uint64)ptr;
}

uint64
sys_kfree(void)
{
    uint64 ptr;
    argaddr(0, &ptr);
    if (!ptr)
        return -1;
    kfree((const void *)ptr);
    return 0;
}

uint64
sys_slab_write(void)
{
    uint64 kdst, usrc;
    int len;
    argaddr(0, &kdst);
    argaddr(1, &usrc);
    argint(2, &len);
    if (!kdst || len <= 0)
        return -1;
    struct proc *p = myproc();
    if (copyin(p->pagetable, (char *)kdst, usrc, len) < 0)
        return -1;
    return 0;
}

uint64
sys_slab_read(void)
{
    uint64 udst, ksrc;
    int len;
    argaddr(0, &udst);
    argaddr(1, &ksrc);
    argint(2, &len);
    if (!ksrc || len <= 0)
        return -1;
    struct proc *p = myproc();
    if (copyout(p->pagetable, udst, (char *)ksrc, len) < 0)
        return -1;
    return 0;
}
