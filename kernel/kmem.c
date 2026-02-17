#include "types.h"
#include "spinlock.h"
#include "defs.h"
#include "slab.h"
#include "buddy.h"

#define MIN_SIZE_ORDER 5   
#define MAX_SIZE_ORDER 17   
#define SIZE_CACHE_NUM (MAX_SIZE_ORDER - MIN_SIZE_ORDER + 1)

static kmem_cache_t *size_caches[SIZE_CACHE_NUM];


void
kmem_init(void *space, int block_num)
{
    buddy_init(space, (char *)space + block_num * BLOCK_SIZE);

    for (int order = MIN_SIZE_ORDER; order <= MAX_SIZE_ORDER; order++) {
        int size = 1 << order;
        int idx  = order - MIN_SIZE_ORDER;

        size_caches[idx] =
            kmem_cache_create("size-cache",
                              size,
                              0,   
                              0); 

        if (!size_caches[idx]) {
            panic("kmem_init: size cache create failed");
        }
    }
}


void *
kmalloc(size_t size)
{
    if (size == 0)
        return 0;

    int order = MIN_SIZE_ORDER;
    while ((1UL << order) < size)
        order++;

    if (order > MAX_SIZE_ORDER)
        return 0;  

    return kmem_cache_alloc(
        size_caches[order - MIN_SIZE_ORDER]
    );
}

void
kfree(const void *objp)
{
    if (!objp)
        return;
    for (int i = 0; i < SIZE_CACHE_NUM; i++) {
        kmem_cache_t *cache = size_caches[i];
        if (!cache)
            continue;

        kmem_cache_free(cache, (void *)objp);
        return;
    }

    panic("kfree: invalid pointer");
}
