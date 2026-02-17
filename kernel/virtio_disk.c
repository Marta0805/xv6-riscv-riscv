//
// driver for qemu's virtio disk device.
// uses qemu's mmio interface to virtio.
//
// qemu ... -drive file=fs.img,if=none,format=raw,id=x0 -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "virtio.h"
#ifdef SLAB_KERNEL
#include "slab.h"
#endif

// the address of virtio mmio register r.
#define R(r) ((volatile uint32 *)(VIRTIO0 + (r)))

struct disk_s {
  // a set (not a ring) of DMA descriptors, with which the
  // driver tells the device where to read and write individual
  // disk operations. there are NUM descriptors.
  // most commands consist of a "chain" (a linked list) of a couple of
  // these descriptors.
  struct virtq_desc *desc;

  // a ring in which the driver writes descriptor numbers
  // that the driver would like the device to process.  it only
  // includes the head descriptor of each chain. the ring has
  // NUM elements.
  struct virtq_avail *avail;

  // a ring in which the device writes descriptor numbers that
  // the device has finished processing (just the head of each chain).
  // there are NUM used ring entries.
  struct virtq_used *used;

  // our own book-keeping.
  char free[NUM];  // is a descriptor free?
  uint16 used_idx; // we've looked this far in used[2..NUM].

  // track info about in-flight operations,
  // for use when completion interrupt arrives.
  // indexed by first descriptor index of chain.
  struct {
    struct buf *b;
    char status;
  } info[NUM];

  // disk command headers.
  // one-for-one with descriptors, for convenience.
  struct virtio_blk_req ops[NUM];
  
  struct spinlock vdisk_lock;
};

#ifdef SLAB_KERNEL
static struct disk_s *disk_ptr;  // dynamically allocated via kmalloc
#define DISK (*(disk_ptr))
#else
static struct disk_s disk;
#define DISK disk
#endif

void
virtio_disk_init(void)
{
  uint32 status = 0;

#ifdef SLAB_KERNEL
  disk_ptr = (struct disk_s*)kmalloc(sizeof(struct disk_s));
  if(!disk_ptr)
    panic("virtio_disk_init: kmalloc");
  memset(disk_ptr, 0, sizeof(struct disk_s));
#endif
  initlock(&DISK.vdisk_lock, "virtio_disk");

  if(*R(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
     *R(VIRTIO_MMIO_VERSION) != 2 ||
     *R(VIRTIO_MMIO_DEVICE_ID) != 2 ||
     *R(VIRTIO_MMIO_VENDOR_ID) != 0x554d4551){
    panic("could not find virtio disk");
  }
  
  // reset device
  *R(VIRTIO_MMIO_STATUS) = status;

  // set ACKNOWLEDGE status bit
  status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
  *R(VIRTIO_MMIO_STATUS) = status;

  // set DRIVER status bit
  status |= VIRTIO_CONFIG_S_DRIVER;
  *R(VIRTIO_MMIO_STATUS) = status;

  // negotiate features
  uint64 features = *R(VIRTIO_MMIO_DEVICE_FEATURES);
  features &= ~(1 << VIRTIO_BLK_F_RO);
  features &= ~(1 << VIRTIO_BLK_F_SCSI);
  features &= ~(1 << VIRTIO_BLK_F_CONFIG_WCE);
  features &= ~(1 << VIRTIO_BLK_F_MQ);
  features &= ~(1 << VIRTIO_F_ANY_LAYOUT);
  features &= ~(1 << VIRTIO_RING_F_EVENT_IDX);
  features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC);
  *R(VIRTIO_MMIO_DRIVER_FEATURES) = features;

  // tell device that feature negotiation is complete.
  status |= VIRTIO_CONFIG_S_FEATURES_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  // re-read status to ensure FEATURES_OK is set.
  status = *R(VIRTIO_MMIO_STATUS);
  if(!(status & VIRTIO_CONFIG_S_FEATURES_OK))
    panic("virtio disk FEATURES_OK unset");

  // initialize queue 0.
  *R(VIRTIO_MMIO_QUEUE_SEL) = 0;

  // ensure queue 0 is not in use.
  if(*R(VIRTIO_MMIO_QUEUE_READY))
    panic("virtio disk should not be ready");

  // check maximum queue size.
  uint32 max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
  if(max == 0)
    panic("virtio disk has no queue 0");
  if(max < NUM)
    panic("virtio disk max queue too short");

  // allocate and zero queue memory.
  DISK.desc = kalloc();
  DISK.avail = kalloc();
  DISK.used = kalloc();
  if(!DISK.desc || !DISK.avail || !DISK.used)
    panic("virtio disk kalloc");
  memset(DISK.desc, 0, PGSIZE);
  memset(DISK.avail, 0, PGSIZE);
  memset(DISK.used, 0, PGSIZE);

  // set queue size.
  *R(VIRTIO_MMIO_QUEUE_NUM) = NUM;

  // write physical addresses.
  *R(VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint64)DISK.desc;
  *R(VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint64)DISK.desc >> 32;
  *R(VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint64)DISK.avail;
  *R(VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint64)DISK.avail >> 32;
  *R(VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint64)DISK.used;
  *R(VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint64)DISK.used >> 32;

  // queue is ready.
  *R(VIRTIO_MMIO_QUEUE_READY) = 0x1;

  // all NUM descriptors start out unused.
  for(int i = 0; i < NUM; i++)
    DISK.free[i] = 1;

  // tell device we're completely ready.
  status |= VIRTIO_CONFIG_S_DRIVER_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  // plic.c and trap.c arrange for interrupts from VIRTIO0_IRQ.
}

// find a free descriptor, mark it non-free, return its index.
static int
alloc_desc()
{
  for(int i = 0; i < NUM; i++){
    if(DISK.free[i]){
      DISK.free[i] = 0;
      return i;
    }
  }
  return -1;
}

// mark a descriptor as free.
static void
free_desc(int i)
{
  if(i >= NUM)
    panic("free_desc 1");
  if(DISK.free[i])
    panic("free_desc 2");
  DISK.desc[i].addr = 0;
  DISK.desc[i].len = 0;
  DISK.desc[i].flags = 0;
  DISK.desc[i].next = 0;
  DISK.free[i] = 1;
  wakeup(&DISK.free[0]);
}

// free a chain of descriptors.
static void
free_chain(int i)
{
  while(1){
    int flag = DISK.desc[i].flags;
    int nxt = DISK.desc[i].next;
    free_desc(i);
    if(flag & VRING_DESC_F_NEXT)
      i = nxt;
    else
      break;
  }
}

// allocate three descriptors (they need not be contiguous).
// disk transfers always use three descriptors.
static int
alloc3_desc(int *idx)
{
  for(int i = 0; i < 3; i++){
    idx[i] = alloc_desc();
    if(idx[i] < 0){
      for(int j = 0; j < i; j++)
        free_desc(idx[j]);
      return -1;
    }
  }
  return 0;
}

void
virtio_disk_rw(struct buf *b, int write)
{
  uint64 sector = b->blockno * (BSIZE / 512);

  acquire(&DISK.vdisk_lock);

  // the spec's Section 5.2 says that legacy block operations use
  // three descriptors: one for type/reserved/sector, one for the
  // data, one for a 1-byte status result.

  // allocate the three descriptors.
  int idx[3];
  while(1){
    if(alloc3_desc(idx) == 0) {
      break;
    }
    sleep(&DISK.free[0], &DISK.vdisk_lock);
  }

  // format the three descriptors.
  // qemu's virtio-blk.c reads them.

  struct virtio_blk_req *buf0 = &DISK.ops[idx[0]];

  if(write)
    buf0->type = VIRTIO_BLK_T_OUT; // write the disk
  else
    buf0->type = VIRTIO_BLK_T_IN; // read the disk
  buf0->reserved = 0;
  buf0->sector = sector;

  DISK.desc[idx[0]].addr = (uint64) buf0;
  DISK.desc[idx[0]].len = sizeof(struct virtio_blk_req);
  DISK.desc[idx[0]].flags = VRING_DESC_F_NEXT;
  DISK.desc[idx[0]].next = idx[1];

  DISK.desc[idx[1]].addr = (uint64) b->data;
  DISK.desc[idx[1]].len = BSIZE;
  if(write)
    DISK.desc[idx[1]].flags = 0; // device reads b->data
  else
    DISK.desc[idx[1]].flags = VRING_DESC_F_WRITE; // device writes b->data
  DISK.desc[idx[1]].flags |= VRING_DESC_F_NEXT;
  DISK.desc[idx[1]].next = idx[2];

  DISK.info[idx[0]].status = 0xff; // device writes 0 on success
  DISK.desc[idx[2]].addr = (uint64) &DISK.info[idx[0]].status;
  DISK.desc[idx[2]].len = 1;
  DISK.desc[idx[2]].flags = VRING_DESC_F_WRITE; // device writes the status
  DISK.desc[idx[2]].next = 0;

  // record struct buf for virtio_disk_intr().
  b->disk = 1;
  DISK.info[idx[0]].b = b;

  // tell the device the first index in our chain of descriptors.
  DISK.avail->ring[DISK.avail->idx % NUM] = idx[0];

  __sync_synchronize();

  // tell the device another avail ring entry is available.
  DISK.avail->idx += 1; // not % NUM ...

  __sync_synchronize();

  *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0; // value is queue number

  // Wait for virtio_disk_intr() to say request has finished.
  while(b->disk == 1) {
    sleep(b, &DISK.vdisk_lock);
  }

  DISK.info[idx[0]].b = 0;
  free_chain(idx[0]);

  release(&DISK.vdisk_lock);
}

void
virtio_disk_intr()
{
  acquire(&DISK.vdisk_lock);

  // the device won't raise another interrupt until we tell it
  // we've seen this interrupt, which the following line does.
  // this may race with the device writing new entries to
  // the "used" ring, in which case we may process the new
  // completion entries in this interrupt, and have nothing to do
  // in the next interrupt, which is harmless.
  *R(VIRTIO_MMIO_INTERRUPT_ACK) = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;

  __sync_synchronize();

  // the device increments DISK.used->idx when it
  // adds an entry to the used ring.

  while(DISK.used_idx != DISK.used->idx){
    __sync_synchronize();
    int id = DISK.used->ring[DISK.used_idx % NUM].id;

    if(DISK.info[id].status != 0)
      panic("virtio_disk_intr status");

    struct buf *b = DISK.info[id].b;
    b->disk = 0;   // disk is done with buf
    wakeup(b);

    DISK.used_idx += 1;
  }

  release(&DISK.vdisk_lock);
}
