#ifndef _KERNEL_SLAB_H
#define _KERNEL_SLAB_H

#include "types.h"
#include "spinlock.h"

#define BLOCK_SIZE (4096)

typedef uint64 size_t;

// Small buffer sizes: 2^5=32  to  2^17=131072
#define SMALL_BUF_MIN_ORDER 5
#define SMALL_BUF_MAX_ORDER 17
#define NUM_SMALL_BUF_SIZES (SMALL_BUF_MAX_ORDER - SMALL_BUF_MIN_ORDER + 1)

typedef struct slab_s slab_t;
typedef struct kmem_cache_s kmem_cache_t;

struct slab_s {
    kmem_cache_t *cache;        // owning cache
    unsigned char *bitmap;      // inuse bitmap (right after slab_t header)
    int free_count;             // free objects in this slab
    int order;                  // buddy order of this slab's allocation
    int next_free;              // index of next free slot (-1 = none)
    slab_t *next;               // next slab in list
};

struct kmem_cache_s {
    char name[32];              // cache name
    uint64 obj_size;            // size of one object (aligned to 8)
    void (*ctor)(void *);       // constructor (may be NULL)
    void (*dtor)(void *);       // destructor (may be NULL)

    struct spinlock lock;       // per-cache lock for thread safety

    slab_t *partial_slabs;      // slabs with some free objects
    slab_t *full_slabs;         // completely full slabs
    slab_t *free_slabs;         // completely empty slabs

    int obj_per_slab;           // max objects per slab
    int slab_order;             // buddy order for each slab

    int slab_count;             // total number of slabs in cache
    int total_objs;             // total objects across all slabs
    int free_objs;              // free objects across all slabs

    int grown_since_shrink;     // 1 if cache grew since last shrink
    int error;                  // last error code (0 = no error)

    // Performance: slab coloring
    int color_max;              // max color offset (in ALIGN8 units)
    int color_next;             // next color to assign to a new slab

    // Performance stats
    uint64 alloc_count;         // total allocations
    uint64 free_count_total;    // total frees

    kmem_cache_t *next;         // next cache in global cache list
};

// ---- Slab allocator API ----

void kmem_init(void *space, int block_num);

kmem_cache_t *kmem_cache_create(const char *name, size_t size,
                                void (*ctor)(void *),
                                void (*dtor)(void *));

int kmem_cache_shrink(kmem_cache_t *cachep);

void *kmem_cache_alloc(kmem_cache_t *cachep);

void kmem_cache_free(kmem_cache_t *cachep, void *objp);

void *kmalloc(size_t size);

void kfree(const void *objp);

void kmem_cache_destroy(kmem_cache_t *cachep);

void kmem_cache_info(kmem_cache_t *cachep);

int kmem_cache_error(kmem_cache_t *cachep);

#endif // _KERNEL_SLAB_H
