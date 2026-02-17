#ifndef _KERNEL_SLAB_H
#define _KERNEL_SLAB_H

#include "types.h"
#include "spinlock.h"

#define BLOCK_SIZE (4096)

typedef uint64 size_t;

#define SMALL_BUF_MIN_ORDER 5
#define SMALL_BUF_MAX_ORDER 17
#define NUM_SMALL_BUF_SIZES (SMALL_BUF_MAX_ORDER - SMALL_BUF_MIN_ORDER + 1)

typedef struct slab_s slab_t;
typedef struct kmem_cache_s kmem_cache_t;

struct slab_s {
    kmem_cache_t *cache;       
    unsigned char *bitmap;  
    int free_count;            
    int order;                
    int next_free;             
    slab_t *next;              
};

struct kmem_cache_s {
    char name[32];             
    uint64 obj_size;            
    void (*ctor)(void *);       
    void (*dtor)(void *);    

    struct spinlock lock;      

    slab_t *partial_slabs;      
    slab_t *full_slabs;       
    slab_t *free_slabs;       

    int obj_per_slab;          
    int slab_order;           

    int slab_count;           
    int total_objs;          
    int free_objs;            

    int grown_since_shrink;    
    int error;                 

    kmem_cache_t *next;        
};


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
