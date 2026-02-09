#ifndef BUDDY_H
#define BUDDY_H

#include "spinlock.h"

#ifndef BLOCK_SIZE
#define BLOCK_SIZE 4096
#endif

#define MIN_ORDER 0
#define MAX_ORDER 15   // up to 128MB (2^15 * 4KB)

#define BUDDY_ORDERS (MAX_ORDER - MIN_ORDER + 1)

struct buddy_block {
    struct buddy_block *next;
};

struct buddy_allocator {
    struct spinlock lock;
    struct buddy_block *free[BUDDY_ORDERS];
    uint64 start;
    uint64 total_size;
    int max_order;
};

void buddy_init(struct buddy_allocator *b, void *start, void *end);
void *buddy_alloc(struct buddy_allocator *b, int order);
void buddy_free(struct buddy_allocator *b, void *addr, int order);
void buddy_dump(struct buddy_allocator *b);

#endif
