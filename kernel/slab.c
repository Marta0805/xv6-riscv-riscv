#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"
#include "slab.h"
#include "buddy.h"

#ifndef SLAB_KERNEL
static struct buddy_allocator slab_buddy;
#endif

#define ALIGN8(x) (((x) + 7) & ~7UL)

static inline int bitmap_test(unsigned char *bm, int i)
{
    return bm[i / 8] & (1 << (i % 8));
}

static inline void bitmap_set(unsigned char *bm, int i)
{
    bm[i / 8] |= (1 << (i % 8));
}

static inline void bitmap_clear(unsigned char *bm, int i)
{
    bm[i / 8] &= ~(1 << (i % 8));
}

static struct {
    struct spinlock lock;     
    kmem_cache_t *caches; 
} slab_state;

static kmem_cache_t *small_buf_caches[NUM_SMALL_BUF_SIZES];

static void *slab_obj_start(slab_t *slab)
{
    kmem_cache_t *c = slab->cache;
    int bitmap_bytes = (c->obj_per_slab + 7) / 8;
    uint64 base = (uint64)slab + sizeof(slab_t) + bitmap_bytes;
    return (void *)ALIGN8(base);
}

static int compute_obj_per_slab(uint64 obj_size, int order)
{
    uint64 total = (uint64)BLOCK_SIZE << order;
    uint64 hdr = ALIGN8(sizeof(slab_t));
    int n = (int)((total - hdr) / obj_size);
    while (n > 0) {
        uint64 bitmap_bytes = (n + 7) / 8;
        uint64 overhead = (sizeof(slab_t) + bitmap_bytes);
        if (ALIGN8(overhead) + (uint64)n * obj_size <= total)
            break;
        n--;
    }
    return n;
}

#define MIN_OBJS_PER_SLAB 8

static int choose_slab_order(uint64 obj_size)
{
    for (int order = 0; order <= 10; order++) {
        int n = compute_obj_per_slab(obj_size, order);
        if (n >= MIN_OBJS_PER_SLAB)
            return order;
    }
    for (int order = 0; order <= 14; order++) {
        if (compute_obj_per_slab(obj_size, order) >= 1)
            return order;
    }
    return 0;
}


static slab_t *alloc_slab(kmem_cache_t *cache){
#ifdef SLAB_KERNEL
    void *region = kalloc_order(cache->slab_order);
#else
    void *region = buddy_alloc(&slab_buddy, cache->slab_order);
    if (!region) {
        buddy_dump(&slab_buddy);
    }
#endif
    if (!region) {
        cache->error = 1;
        return 0;
    }

    slab_t *slab = (slab_t *)region;
    slab->cache = cache;
    slab->order = cache->slab_order;
    slab->free_count = cache->obj_per_slab;
    slab->next_free = 0;
    slab->next = 0;

    slab->bitmap = (unsigned char *)((uint64)slab + sizeof(slab_t));
    int bitmap_bytes = (cache->obj_per_slab + 7) / 8;
    memset(slab->bitmap, 0, bitmap_bytes);

    if (cache->ctor) {
        void *obj_base = slab_obj_start(slab);
        for (int i = 0; i < cache->obj_per_slab; i++) {
            void *obj = (char *)obj_base + (uint64)i * cache->obj_size;
            cache->ctor(obj);
        }
    }

    cache->slab_count++;
    cache->total_objs += cache->obj_per_slab;
    cache->free_objs += cache->obj_per_slab;
    cache->grown_since_shrink = 1;

    return slab;
}

static void destroy_slab(kmem_cache_t *cache, slab_t *slab)
{
    if (cache->dtor) {
        void *obj_base = slab_obj_start(slab);
        for (int i = 0; i < cache->obj_per_slab; i++) {
            if (bitmap_test(slab->bitmap, i)) {
                void *obj = (char *)obj_base + i * cache->obj_size;
                cache->dtor(obj);
            }
        }
    }

    cache->slab_count--;
    cache->total_objs -= cache->obj_per_slab;
    cache->free_objs -= slab->free_count;

#ifdef SLAB_KERNEL
    pgfree_order((void *)slab, slab->order);
#else
    buddy_free(&slab_buddy, (void *)slab, slab->order);
#endif
}

static void free_empty_slab(kmem_cache_t *cache, slab_t *slab)
{
    if (cache->dtor) {
        void *obj_base = slab_obj_start(slab);
        for (int i = 0; i < cache->obj_per_slab; i++) {
            void *obj = (char *)obj_base + i * cache->obj_size;
            cache->dtor(obj);
        }
    }

    cache->slab_count--;
    cache->total_objs -= cache->obj_per_slab;
    cache->free_objs -= slab->free_count;

#ifdef SLAB_KERNEL
    pgfree_order((void *)slab, slab->order);
#else
    buddy_free(&slab_buddy, (void *)slab, slab->order);
#endif
}

void kmem_init(void *space, int block_num)
{
    initlock(&slab_state.lock, "slab");
    slab_state.caches = 0;

    for (int i = 0; i < NUM_SMALL_BUF_SIZES; i++) {
        small_buf_caches[i] = 0;
    }

#ifndef SLAB_KERNEL
    if (space && block_num > 0) {
        void *mem_end = (void *)((uint64)space + (uint64)block_num * BLOCK_SIZE);
        buddy_init(&slab_buddy, space, mem_end);
    }
#endif
}

static void str_copy(char *dst, const char *src, int max){
    int i;
    for (i = 0; i < max - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

kmem_cache_t *kmem_cache_create(const char *name, size_t size,
                                void (*ctor)(void *),
                                void (*dtor)(void *))
{
    if (size == 0)
        return 0;

    //uint64 aligned_size = ALIGN8(size);

#ifdef SLAB_KERNEL
    kmem_cache_t *cache = (kmem_cache_t *)kalloc_order(0);
#else
    kmem_cache_t *cache = (kmem_cache_t *)buddy_alloc(&slab_buddy, 0);
#endif
    if (!cache)
        return 0;

    memset(cache, 0, sizeof(*cache));
    str_copy(cache->name, name, sizeof(cache->name));
    cache->obj_size = size;
    cache->ctor = ctor;
    cache->dtor = dtor;

    initlock(&cache->lock, "cache");

    cache->slab_order = choose_slab_order(size);
    cache->obj_per_slab = compute_obj_per_slab(size, cache->slab_order);

    if (cache->obj_per_slab <= 0) {
#ifdef SLAB_KERNEL
        pgfree_order(cache, 0);
#else
        buddy_free(&slab_buddy, cache, 0);
#endif
        return 0;
    }

    cache->error = 0;
    cache->grown_since_shrink = 0;

    acquire(&slab_state.lock);
    cache->next = slab_state.caches;
    slab_state.caches = cache;
    release(&slab_state.lock);

    return cache;
}

void *kmem_cache_alloc(kmem_cache_t *cachep)
{
    if (!cachep)
        return 0;

    acquire(&cachep->lock);

    slab_t *slab = cachep->partial_slabs;

    if (!slab) {
        slab = cachep->free_slabs;
        if (slab) {
            cachep->free_slabs = slab->next;
            slab->next = cachep->partial_slabs;
            cachep->partial_slabs = slab;
        }
    }

    if (!slab) {
        slab = alloc_slab(cachep);
        
        if (!slab) {
            release(&cachep->lock);
            return 0;
        }
        slab->next = cachep->partial_slabs;
        cachep->partial_slabs = slab;
    }

    int i = slab->next_free;
    if (i < 0 || i >= cachep->obj_per_slab) {
        cachep->error = 2;
        release(&cachep->lock);
        return 0;
    }

    void *obj = (char *)slab_obj_start(slab) + (uint64)i * cachep->obj_size;

    bitmap_set(slab->bitmap, i);
    slab->free_count--;
    cachep->free_objs--;

    slab->next_free = -1;
    for (int j = i + 1; j < cachep->obj_per_slab; j++) {
        if (!bitmap_test(slab->bitmap, j)) {
            slab->next_free = j;
            break;
        }
    }
    if (slab->next_free < 0) {
        for (int j = 0; j < i; j++) {
            if (!bitmap_test(slab->bitmap, j)) {
                slab->next_free = j;
                break;
            }
        }
    }

    if (slab->free_count == 0) {
        cachep->partial_slabs = slab->next;
        slab->next = cachep->full_slabs;
        cachep->full_slabs = slab;
    }
    release(&cachep->lock);
    return obj;
}


static inline slab_t *obj_to_slab(kmem_cache_t *cachep, void *objp)
{
    uint64 slab_size = (uint64)BLOCK_SIZE << cachep->slab_order;
    uint64 slab_addr = (uint64)objp & ~(slab_size - 1);
    return (slab_t *)slab_addr;
}

void kmem_cache_free(kmem_cache_t *cachep, void *objp)
{
    if (!cachep || !objp)
        return;

    acquire(&cachep->lock);

    slab_t *slab = obj_to_slab(cachep, objp);

    if (slab->cache != cachep) {
        cachep->error = 3;
        release(&cachep->lock);
        return;
    }

    void *obj_base = slab_obj_start(slab);
    int idx = (int)(((uint64)objp - (uint64)obj_base) / cachep->obj_size);

    if (idx < 0 || idx >= cachep->obj_per_slab || !bitmap_test(slab->bitmap, idx)) {
        cachep->error = 4;
        release(&cachep->lock);
        return;
    }

    int was_full = (slab->free_count == 0);

    bitmap_clear(slab->bitmap, idx);
    slab->free_count++;
    cachep->free_objs++;

    if (slab->next_free < 0 || idx < slab->next_free)
        slab->next_free = idx;

    if (slab->free_count == cachep->obj_per_slab) {

        slab_t **pp = &cachep->partial_slabs;
        while (*pp && *pp != slab)
            pp = &(*pp)->next;
        if (*pp == slab)
            *pp = slab->next;

        slab->next = cachep->free_slabs;
        cachep->free_slabs = slab;
    } else if (was_full) {
        slab_t **pp = &cachep->full_slabs;
        while (*pp && *pp != slab)
            pp = &(*pp)->next;
        if (*pp == slab)
            *pp = slab->next;

        slab->next = cachep->partial_slabs;
        cachep->partial_slabs = slab;
    }

    release(&cachep->lock);
}

int kmem_cache_shrink(kmem_cache_t *cachep)
{
    if (!cachep)
        return 0;

    acquire(&cachep->lock);

    if (cachep->grown_since_shrink) {
        cachep->grown_since_shrink = 0;
        release(&cachep->lock);
        return 0;
    }

    int freed_blocks = 0;

    while (cachep->free_slabs) {
        slab_t *slab = cachep->free_slabs;
        cachep->free_slabs = slab->next;
        freed_blocks += (1 << slab->order);
        free_empty_slab(cachep, slab);
    }

    release(&cachep->lock);
    return freed_blocks;
}

int kmem_cache_shrink_all(void)
{
    int total_freed = 0;

    acquire(&slab_state.lock);
    kmem_cache_t *c = slab_state.caches;
    release(&slab_state.lock);

    while (c) {
        total_freed += kmem_cache_shrink(c);
        acquire(&slab_state.lock);
        c = c->next;
        release(&slab_state.lock);
    }

    return total_freed;
}

void kmem_cache_destroy(kmem_cache_t *cachep)
{
    if (!cachep)
        return;

    acquire(&cachep->lock);

    while (cachep->free_slabs) {
        slab_t *s = cachep->free_slabs;
        cachep->free_slabs = s->next;
        free_empty_slab(cachep, s);
    }

    while (cachep->partial_slabs) {
        slab_t *s = cachep->partial_slabs;
        cachep->partial_slabs = s->next;
        destroy_slab(cachep, s);
    }

    while (cachep->full_slabs) {
        slab_t *s = cachep->full_slabs;
        cachep->full_slabs = s->next;
        destroy_slab(cachep, s);
    }

    release(&cachep->lock);

    acquire(&slab_state.lock);
    kmem_cache_t **pp = &slab_state.caches;
    while (*pp) {
        if (*pp == cachep) {
            *pp = cachep->next;
            break;
        }
        pp = &(*pp)->next;
    }
    release(&slab_state.lock);

#ifdef SLAB_KERNEL
    pgfree_order(cachep, 0);
#else
    buddy_free(&slab_buddy, cachep, 0);
#endif
}

void kmem_cache_info(kmem_cache_t *cachep)
{
    if (!cachep)
        return;

    acquire(&cachep->lock);

    int used = cachep->total_objs - cachep->free_objs;
    int pct = 0;
    if (cachep->total_objs > 0)
        pct = (used * 100) / cachep->total_objs;

    int cache_blocks = cachep->slab_count * (1 << cachep->slab_order);


    printf("CACHE: %s\n", cachep->name);
    printf("  obj size:   %lu B\n", cachep->obj_size);
    printf("  cache size: %d blocks\n", cache_blocks);
    printf("  slabs:      %d\n", cachep->slab_count);
    printf("  objs/slab:  %d\n", cachep->obj_per_slab);
    printf("  total objs: %d\n", cachep->total_objs - cachep->free_objs);
    printf("  usage:      %d%%\n", pct);

    // printf("Partial slabs:\n");
    // slab_t *par = cachep->partial_slabs;
    // while(par){
    //     printf("free %p\n",par);
    //     par = par->next;
    // }
    // printf("Full slabs:\n");
    // slab_t *full = cachep->full_slabs;
    // while(full){
    //     printf("free %p\n", full);
    //     full = full->next;
    // }

    release(&cachep->lock);
}

int kmem_cache_error(kmem_cache_t *cachep)
{
    if (!cachep)
        return -1;

    acquire(&cachep->lock);
    int err = cachep->error;
    if (err) {
        printf("[SLAB ERROR] cache '%s': error code %d\n", cachep->name, err);
    }
    release(&cachep->lock);
    return err;
}


static int size_to_index(size_t size)
{
    for (int i = 0; i < NUM_SMALL_BUF_SIZES; i++) {
        if (size <= (1UL << (SMALL_BUF_MIN_ORDER + i)))
            return i;
    }

    return -1;
}

void *kmalloc(size_t size)
{
    if (size == 0)
        return 0;

    int idx = size_to_index(size);
    if (idx < 0)
        return 0;  

    if (!small_buf_caches[idx]) {
        acquire(&slab_state.lock);
        if (!small_buf_caches[idx]) {
            uint64 buf_size = 1UL << (SMALL_BUF_MIN_ORDER + idx);
            char name[32];
            char *p = name;
            *p++ = 's'; *p++ = 'i'; *p++ = 'z'; *p++ = 'e'; *p++ = '-';
            uint64 tmp = buf_size;
            char digits[20];
            int d = 0;
            do {
                digits[d++] = '0' + (tmp % 10);
                tmp /= 10;
            } while (tmp > 0);
            for (int i = d - 1; i >= 0; i--)
                *p++ = digits[i];
            *p = '\0';

            release(&slab_state.lock);
            small_buf_caches[idx] = kmem_cache_create(name, buf_size, 0, 0);
            if (!small_buf_caches[idx])
                return 0;
        } else {
            release(&slab_state.lock);
        }
    }
    return kmem_cache_alloc(small_buf_caches[idx]);
}

void kfree(const void *objp)
{
    if (!objp)
        return;

    for (int i = 0; i < NUM_SMALL_BUF_SIZES; i++) {
        kmem_cache_t *cache = small_buf_caches[i];

        if (!cache)
            continue;

        acquire(&cache->lock);

        slab_t *slab = cache->full_slabs;
        while (slab) {
            if((uint64)objp >= (uint64)slab && (uint64)objp < (uint64)slab + ((uint64)BLOCK_SIZE << slab->order)){
                break;
            }
            slab = slab->next;
        }
        if(!slab) {
            slab = cache->partial_slabs;
            while (slab) {
                if((uint64)objp >= (uint64)slab && (uint64)objp < (uint64)slab + ((uint64)BLOCK_SIZE << slab->order)){
                    break;
                }
                slab = slab->next;
            }
        }
        if(!slab) {
            release(&cache->lock);
            continue;
        }
    
        void *obj_base = slab_obj_start(slab);

        uint64 offset = (uint64)objp - (uint64)obj_base;
        int idx = (int)(offset / cache->obj_size);

        if (idx >= 0 && idx < cache->obj_per_slab && (uint64)objp == (uint64)obj_base + (uint64)idx * cache->obj_size) {
            release(&cache->lock);
            kmem_cache_free(cache, (void *)objp);
            return;
        }
        release(&cache->lock);
    }
    printf("[SLAB] kfree: could not find object %p\n", objp);
}
