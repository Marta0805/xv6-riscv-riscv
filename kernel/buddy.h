#ifndef BUDDY_H
#define BUDDY_H

#define BLOCK_SIZE 4096

#define MIN_ORDER 1    
#define MAX_ORDER 14   // 64MB

#define BUDDY_ORDERS (MAX_ORDER - MIN_ORDER + 1)

struct buddy_block {
    struct buddy_block *next;
};

void buddy_init(void *start, void *end);
void *buddy_alloc(int order);
void buddy_free(void *addr, int order);
void buddy_dump(void);


#endif
