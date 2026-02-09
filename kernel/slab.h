#ifndef _KERNEL_SLAB_H
#define _KERNEL_SLAB_H


typedef struct kmem_cache_s {
    const char *name;             
    size_t size;                    
    void (*ctor)(void *);         
    void (*dtor)(void *);          

    
    
    struct kmem_cache_s *next;    
    
    struct slab_s *partial_slabs;   
    struct slab_s *full_slabs;    
    struct slab_s *free_slabs;      

    int obj_num_per_slab;       
    int total_obj_num;            
    int free_obj_num;            

    int slab_num;                 

    int error;                  
}kmem_cache_t;


typedef struct slab_s {
    struct kmem_cache_s *cache;
    void *mem;
    unsigned char *inuse_bitmap;
    int free_count;
    int order;            
    struct slab_s *next;
} slab_t;


#define BLOCK_SIZE (4096)

#define MIN_OBJS_PER_SLAB 8

typedef unsigned long size_t;

void kmem_init(void *space, int block_num);

kmem_cache_t *kmem_cache_create(const char *name, size_t size, void (*ctor)(void *), void (*dtor)(void *)); // A    llocate cache

int kmem_cache_shrink(kmem_cache_t *cachep); // Shrink cache

void *kmem_cache_alloc(kmem_cache_t *cachep); // Allocate one object from cache

void kmem_cache_free(kmem_cache_t *cachep, void *objp); // Deallocate one object from cache

void *kmalloc(size_t size); // Alloacate one small memory buffer

void kfree(const void *objp); // Deallocate one small memory buffer

void kmem_cache_destroy(kmem_cache_t *cachep); // Deallocate cache

void kmem_cache_info(kmem_cache_t *cachep); // Print cache info

int kmem_cache_error(kmem_cache_t *cachep); // Print error message

#endif // _KERNEL_SLAB_H