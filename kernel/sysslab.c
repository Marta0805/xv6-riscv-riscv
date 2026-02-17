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

extern pagetable_t kernel_pagetable;

uint64
sys_kmem_init(void)
{
    struct proc *p = myproc();
    uint64 space;
    int block_num;

    argaddr(0, &space);
    argint(1, &block_num);

    if (space == 0 || block_num <= 0)
        return -1;

    if (mirror_user_pagetable(p->pagetable,
                              kernel_pagetable,
                              p->sz) < 0)
        return -1;

    kmem_init((void*)space, block_num);

    return 0;
}
uint64
sys_kmem_cache_create(void)
{
    char name[32];
    int size;
    uint64 ctor_addr;
    uint64 dtor_addr;

    if (argstr(0, name, sizeof(name)) < 0)
        return 0;

    argint(1, &size);
    argaddr(2, &ctor_addr);
    argaddr(3, &dtor_addr);

    void (*ctor)(void*) = 0;
    void (*dtor)(void*) = 0;

    if (ctor_addr != 0)
        ctor = (void (*)(void*)) ctor_addr;

    if (dtor_addr != 0)
        dtor = (void (*)(void*)) dtor_addr;

    kmem_cache_t *cache =
        kmem_cache_create(name, size, ctor, dtor);

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
