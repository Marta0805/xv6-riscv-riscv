#include "types.h"
#include "spinlock.h"
#include "riscv.h"
#include "buddy.h"
#include "defs.h"

#define BLOCK_SIZE 4096

static struct {
    struct spinlock lock;
    struct buddy_block *free[BUDDY_ORDERS];
    uint64 start;
    int max_order;
} buddy;

static inline int idx(int order)
{
    return order - MIN_ORDER;
}

void buddy_dump(void)
{
    acquire(&buddy.lock);

    printf("\n=== BUDDY ===\n");

    for (int o = MIN_ORDER; o <= buddy.max_order; o++) {
        int count = 0;
        struct buddy_block *b = buddy.free[idx(o)];

        while (b) {
            count++;
            b = b->next;
        }

        if (count == 0)
            continue;

        uint64 size = (1UL << o) * BLOCK_SIZE;

        printf("order %d | block size %lu KB | %d blocks\n",
               o, size / 1024, count);

        b = buddy.free[idx(o)];
        while (b) {
            printf("    %p\n", b);
            b = b->next;
        }
    }

    printf("=================\n\n");

    release(&buddy.lock);
}

void buddy_init(void *start, void *end)
{
    initlock(&buddy.lock, "buddy");

    buddy.start = PGROUNDUP((uint64)start);
    uint64 limit = (uint64)end;
    uint64 total = limit - buddy.start;

    for (int i = 0; i < BUDDY_ORDERS; i++)
        buddy.free[i] = 0;

    int order = MAX_ORDER;
    while (order >= MIN_ORDER) {
        uint64 size = (1UL << order) * BLOCK_SIZE;
        if (size <= total)
            break;
        order--;
    }

    if (order < MIN_ORDER) {
        printf("[BUDDY] init failed\n");
        buddy.max_order = MIN_ORDER - 1;
        return;
    }

    buddy.max_order = order;

    struct buddy_block *b = (struct buddy_block *)buddy.start;
    b->next = 0;
    buddy.free[idx(order)] = b;
}

void *buddy_alloc(int order)
{
    if (order < MIN_ORDER || order > buddy.max_order)
        return 0;

    acquire(&buddy.lock);

    int o;
    for (o = order; o <= buddy.max_order; o++) {
        if (buddy.free[idx(o)])
            break;
    }

    if (o > buddy.max_order) {
        release(&buddy.lock);
        return 0;
    }

    struct buddy_block *b = buddy.free[idx(o)];
    buddy.free[idx(o)] = b->next;

    while (o > order) {
        o--;
        uint64 size = (1UL << o) * BLOCK_SIZE;
        struct buddy_block *split =
            (struct buddy_block *)((uint64)b + size);
        split->next = buddy.free[idx(o)];
        buddy.free[idx(o)] = split;
    }

    release(&buddy.lock);
    return (void *)b;
}

void buddy_free(void *addr, int order)
{
    if (!addr || order < MIN_ORDER || order > buddy.max_order)
        return;

    acquire(&buddy.lock);

    if ((uint64)addr < buddy.start ||
        (uint64)addr >= buddy.start +
        ((uint64)1 << buddy.max_order) * BLOCK_SIZE) {
        printf("[BUDDY] invalid free: %p\n", addr);
        release(&buddy.lock);
        return;
    }

    uint64 block = (uint64)addr;

    while (order < buddy.max_order) {
        uint64 size = (1UL << order) * BLOCK_SIZE;
        uint64 buddy_addr =
            ((block - buddy.start) ^ size) + buddy.start;

        struct buddy_block **pp = &buddy.free[idx(order)];
        struct buddy_block *curr = *pp;

        while (curr) {
            if ((uint64)curr == buddy_addr)
                break;
            pp = &curr->next;
            curr = curr->next;
        }

        if (!curr)
            break;

        *pp = curr->next;

        if (buddy_addr < block)
            block = buddy_addr;

        order++;
    }

    struct buddy_block *b = (struct buddy_block *)block;
    b->next = buddy.free[idx(order)];
    buddy.free[idx(order)] = b;

    release(&buddy.lock);
}
