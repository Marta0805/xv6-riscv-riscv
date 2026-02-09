#include "types.h"
#include "spinlock.h"
#include "riscv.h"
#include "buddy.h"
#include "defs.h"

static inline int idx(int order)
{
    return order - MIN_ORDER;
}

void buddy_dump(struct buddy_allocator *b)
{
    acquire(&b->lock);

    printf("\n=== BUDDY ===\n");

    for (int o = MIN_ORDER; o <= b->max_order; o++) {
        int count = 0;
        struct buddy_block *bl = b->free[idx(o)];

        while (bl) {
            count++;
            bl = bl->next;
        }

        if (count == 0)
            continue;

        uint64 size = (1UL << o) * BLOCK_SIZE;

        printf("order %d | block size %lu KB | %d blocks\n",
               o, size / 1024, count);

        bl = b->free[idx(o)];
        while (bl) {
            printf("    %p\n", bl);
            bl = bl->next;
        }
    }

    printf("=================\n\n");

    release(&b->lock);
}

void buddy_init(struct buddy_allocator *b, void *start, void *end)
{
    initlock(&b->lock, "buddy");

    b->start = PGROUNDUP((uint64)start);
    uint64 limit = (uint64)end;
    uint64 total = limit - b->start;

    for (int i = 0; i < BUDDY_ORDERS; i++)
        b->free[i] = 0;

    // Find the highest order that fits at all
    int max_ord = MAX_ORDER;
    while (max_ord >= MIN_ORDER) {
        if (((uint64)1 << max_ord) * BLOCK_SIZE <= total)
            break;
        max_ord--;
    }

    if (max_ord < MIN_ORDER) {
        printf("[BUDDY] init failed\n");
        b->max_order = MIN_ORDER - 1;
        return;
    }

    b->max_order = max_ord;
    b->total_size = total;

    // Greedily place blocks: from largest order down to smallest,
    // filling all available memory.
    uint64 addr = b->start;
    uint64 remaining = total;
    int placed = 0;

    for (int order = max_ord; order >= MIN_ORDER; order--) {
        uint64 bsize = (uint64)1 << (order + 12);
        while (remaining >= bsize) {
            struct buddy_block *bl = (struct buddy_block *)addr;
            bl->next = b->free[idx(order)];
            b->free[idx(order)] = bl;
            addr += bsize;
            remaining -= bsize;
            placed++;
        }
    }

    printf("[BUDDY] initialized: %lu KB in %d blocks\n",
           (total - remaining) / 1024, placed);
}

void *buddy_alloc(struct buddy_allocator *b, int order)
{
    if (order < MIN_ORDER || order > b->max_order)
        return 0;

    acquire(&b->lock);

    int o;
    for (o = order; o <= b->max_order; o++) {
        if (b->free[idx(o)])
            break;
    }

    if (o > b->max_order) {
        release(&b->lock);
        return 0;
    }

    struct buddy_block *bl = b->free[idx(o)];
    b->free[idx(o)] = bl->next;

    while (o > order) {
        o--;
        uint64 size = (1UL << o) * BLOCK_SIZE;
        struct buddy_block *split =
            (struct buddy_block *)((uint64)bl + size);
        split->next = b->free[idx(o)];
        b->free[idx(o)] = split;
    }

    release(&b->lock);
    return (void *)bl;
}

void buddy_free(struct buddy_allocator *b, void *addr, int order)
{
    if (!addr || order < MIN_ORDER || order > b->max_order)
        return;

    acquire(&b->lock);

    if ((uint64)addr < b->start ||
        (uint64)addr >= b->start + b->total_size) {
        printf("[BUDDY] invalid free: %p\n", addr);
        release(&b->lock);
        return;
    }

    uint64 block = (uint64)addr;

    while (order < b->max_order) {
        uint64 size = (1UL << order) * BLOCK_SIZE;
        uint64 buddy_addr =
            ((block - b->start) ^ size) + b->start;

        struct buddy_block **pp = &b->free[idx(order)];
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

    struct buddy_block *bl = (struct buddy_block *)block;
    bl->next = b->free[idx(order)];
    b->free[idx(order)] = bl;

    release(&b->lock);
}
