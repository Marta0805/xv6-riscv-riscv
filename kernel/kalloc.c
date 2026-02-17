#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#ifdef SLAB_KERNEL
#include "slab.h"
#include "buddy.h"
#endif

void
kfree_order(void *pa, int order)
{
  pgfree_order(pa, order);
}

extern char end[]; 

#ifdef SLAB_KERNEL

static struct buddy_allocator global_buddy;

void
kinit()
{
  void *mem_start = (void*)PGROUNDUP((uint64)end);
  buddy_init(&global_buddy, mem_start, (void*)PHYSTOP);
  kmem_init(0, 0);
}

void
pgfree(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("pgfree");
  memset(pa, 1, PGSIZE);
  buddy_free(&global_buddy, pa, 0);
}

void *
kalloc(void)
{
  void *pa = buddy_alloc(&global_buddy, 0);
  if(pa)
    memset(pa, 5, PGSIZE);
  return pa;
}

void *
kalloc_order(int order)
{
  return buddy_alloc(&global_buddy, order);
}

void
pgfree_order(void *pa, int order)
{
  buddy_free(&global_buddy, pa, order);
}

#else

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void freerange(void *pa_start, void *pa_end);

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    pgfree(p);
}

void
pgfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("pgfree");

  memset(pa, 1, PGSIZE);

  acquire(&kmem.lock);
  r = (struct run*)pa;
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE);
  return (void*)r;
}

void *
kalloc_order(int order)
{
  (void)order;
  panic("kalloc_order: not available in Deo 1");
  return 0;
}

void
pgfree_order(void *pa, int order)
{
  (void)pa;
  (void)order;
  panic("pgfree_order: not available in Deo 1");
}

#endif
