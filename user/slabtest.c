#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define RUN_NUM      5
#define ITERATIONS   1000
#define SHARED_SIZE  7
#define BLOCK_SIZE   4096
#define MASK         0xA5

struct data_s {
    int id;
    kmem_cache_t shared;   
    int iterations;
};

const char * const CACHE_NAMES[] = {
    "tc_0",
    "tc_1",
    "tc_2",
    "tc_3",
    "tc_4"
};


int check(void *data, int size) {
    for (int i = 0; i < size; i++) {
        if (((unsigned char *)data)[i] != MASK) {
            return 0;
        }
    }
    return 1;
}

void construct(void *data) {
    memset(data, MASK, SHARED_SIZE);
}

void construct_copy(void *data) {
    memset(data, MASK, SHARED_SIZE);
}

struct objects_s {
    kmem_cache_t cache;   // HANDLE
    void *data;
};

void work(void* pdata) {
    struct data_s data = *(struct data_s*) pdata;
    int size = 0;
    int object_size = data.id + 1;

    kmem_cache_t cache =
        kmem_cache_create(CACHE_NAMES[data.id], object_size, 0, 0);


    volatile struct objects_s *objs =
        (volatile struct objects_s*) kmalloc(sizeof(struct objects_s) * data.iterations);
        

    for (int i = 0; i < data.iterations; i++) {
        if (i % 100 == 0) {
            objs[size].data = (void*) kmem_cache_alloc(data.shared);
            printf("Value ");

            objs[size].cache = data.shared;

            if (!check(objs[size].data, SHARED_SIZE)) {
                printf("Value not correct!\n");
            }
        } else {
            objs[size].data = (void*) kmem_cache_alloc(cache);
            objs[size].cache = cache;

            memset(objs[size].data, MASK, object_size);
        }
        size++;
    }

    kmem_cache_info(cache);
    kmem_cache_info(data.shared);

    for (int i = 0; i < size; i++) {
        int sz = (objs[i].cache == cache) ?
                 object_size : SHARED_SIZE;

        if (!check(objs[i].data, sz)) {
            printf("Value not correct!\n");
        }

        kmem_cache_free(objs[i].cache,
                        (uint64)objs[i].data);
        
    }

    kfree((uint64)objs);
    kmem_cache_destroy(cache);
}

void runs(void(*work)(void*), struct data_s* data, int num) {
    for (int i = 0; i < num; i++) {
        struct data_s private_data = *data;
        private_data.id = i;
        work(&private_data);
    }
}

int main() {
    int num_of_blocks = 1024;

    void* space = malloc(num_of_blocks * BLOCK_SIZE);    

    kmem_init((uint64)space, num_of_blocks);    
    
    kmem_cache_t shared =
        kmem_cache_create("shared object",
                          SHARED_SIZE,
                          ((uint64)construct) > (uint64)construct_copy ? (uint64)construct : (uint64)construct_copy,
                          0);

    struct data_s data;
    data.shared = shared;
    data.iterations = ITERATIONS;

    runs(work, &data, RUN_NUM);

    kmem_cache_destroy(shared);
    free(space);

    exit(0);
}
