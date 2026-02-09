    #include "slab.h"
    #include "buddy.h"
    #include <string.h>
    #include <stdio.h>

    #define MIN_OBJS_PER_SLAB 8
    #define ALIGN8(x) (((x) + 7) & ~7UL)


    static inline int bitmap_test(unsigned char *bm, int idx)
    {
        return bm[idx / 8] & (1 << (idx % 8));
    }

    static inline void bitmap_set(unsigned char *bm, int idx)
    {
        bm[idx / 8] |= (1 << (idx % 8));
    }

    static inline void bitmap_clear(unsigned char *bm, int idx)
    {
        bm[idx / 8] &= ~(1 << (idx % 8));
    }

    static void *slab_objects(slab_t *slab)
    {
        kmem_cache_t *cache = slab->cache;
        int bitmap_bytes = (cache->obj_num_per_slab + 7) / 8;

        unsigned char *base = slab->bitmap + bitmap_bytes;
        return (void *)ALIGN8((unsigned long)base);
    }

    x
    static slab_t *alloc_slab(kmem_cache_t *cache)
    {
        int order = cache->slab_order;
        void *region = buddy_alloc(order);
        if (!region) {
            cache->error = 1;
            return 0;
        }

        slab_t *slab = (slab_t *)region;
        slab->cache = cache;
        slab->order = order;
        slab->free_count = cache->obj_num_per_slab;
        slab->next = 0;

        int bitmap_bytes = (cache->obj_num_per_slab + 7) / 8;
        memset(slab->bitmap, 0, bitmap_bytes);

        cache->slab_num++;
        cache->total_obj_num += cache->obj_num_per_slab;
        cache->free_obj_num  += cache->obj_num_per_slab;

        return slab;
    }

    static void free_slab(slab_t *slab)
    {
        kmem_cache_t *cache = slab->cache;

        buddy_free((void *)slab, slab->order);

        cache->slab_num--;
        cache->total_obj_num -= cache->obj_num_per_slab;
        cache->free_obj_num  -= cache->obj_num_per_slab;
    }

    kmem_cache_t *kmem_cache_create(const char *name, size_t size,
                                    void (*ctor)(void *),
                                    void (*dtor)(void *))
    {
        kmem_cache_t *cache = (kmem_cache_t *)buddy_alloc(0);
        if (!cache)
            return 0;

        memset(cache, 0, sizeof(*cache));

        cache->name = name;
        cache->size = ALIGN8(size);
        cache->ctor = ctor;
        cache->dtor = dtor;

        int order = 0;
        while (((BLOCK_SIZE << order) / cache->size) < MIN_OBJS_PER_SLAB)
            order++;

        cache->slab_order = order;
        cache->obj_num_per_slab = (BLOCK_SIZE << order) / cache->size;
        cache->error = 0;

        return cache;
    }

    void *kmem_cache_alloc(kmem_cache_t *cache)
    {
        slab_t *slab = cache->partial_slabs;

        if (!slab) {
            slab = alloc_slab(cache);
            if (!slab)
                return 0;

            slab->next = cache->partial_slabs;
            cache->partial_slabs = slab;
        }

        int i;
        for (i = 0; i < cache->obj_num_per_slab; i++) {
            if (!bitmap_test(slab->bitmap, i))
                break;
        }

        bitmap_set(slab->bitmap, i);
        slab->free_count--;
        cache->free_obj_num--;

        void *obj = (char *)slab_objects(slab) + i * cache->size;

        if (cache->ctor)
            cache->ctor(obj);

        if (slab->free_count == 0) {
            cache->partial_slabs = slab->next;
            slab->next = cache->full_slabs;
            cache->full_slabs = slab;
        }

        return obj;
    }

    void kmem_cache_free(kmem_cache_t *cache, void *objp)
    {
        slab_t *slab;
        slab_t **lists[] = {
            &cache->partial_slabs,
            &cache->full_slabs
        };

        for (int l = 0; l < 2; l++) {
            slab_t **pp = lists[l];
            while (*pp) {
                slab = *pp;

                void *start = slab_objects(slab);
                void *end = (char *)slab + (BLOCK_SIZE << slab->order);

                if (objp >= start && objp < end) {
                    int idx = ((char *)objp - (char *)start) / cache->size;

                    if (cache->dtor)
                        cache->dtor(objp);

                    bitmap_clear(slab->bitmap, idx);
                    slab->free_count++;
                    cache->free_obj_num++;

                    if (slab->free_count == cache->obj_num_per_slab) {
                        *pp = slab->next;
                        slab->next = cache->free_slabs;
                        cache->free_slabs = slab;
                    } else if (l == 1) {
                        *pp = slab->next;
                        slab->next = cache->partial_slabs;
                        cache->partial_slabs = slab;
                    }

                    return;
                }

                pp = &(*pp)->next;
            }
        }
    }


    int kmem_cache_shrink(kmem_cache_t *cache)
    {
        int count = 0;

        while (cache->free_slabs) {
            slab_t *slab = cache->free_slabs;
            cache->free_slabs = slab->next;
            free_slab(slab);
            count++;
        }

        return count;
    }

    void kmem_cache_destroy(kmem_cache_t *cache)
    {
        kmem_cache_shrink(cache);

        while (cache->partial_slabs) {
            slab_t *s = cache->partial_slabs;
            cache->partial_slabs = s->next;
            free_slab(s);
        }

        while (cache->full_slabs) {
            slab_t *s = cache->full_slabs;
            cache->full_slabs = s->next;
            free_slab(s);
        }

        buddy_free(cache, 0);
    }

    void kmem_cache_info(kmem_cache_t *cache)
    {
        printf("CACHE %s\n", cache->name);
        printf("  object size: %lu\n", cache->size);
        printf("  slab order: %d\n", cache->slab_order);
        printf("  objects per slab: %d\n", cache->obj_num_per_slab);
        printf("  slabs: %d\n", cache->slab_num);
        printf("  total objects: %d\n", cache->total_obj_num);
        printf("  free objects: %d\n", cache->free_obj_num);
    }

    int kmem_cache_error(kmem_cache_t *cache)
    {
        return cache->error;
    }
