// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[];  // first address after kernel.
                    // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  int ref_count[INDEXBYPA(PHYSTOP) + 1];
} kmem;

int decrease_ref_count(void *pa) {
  int ind = INDEXBYPA(pa);
  if (ind < 0 || ind > INDEXBYPA(PHYSTOP)) panic("decrease: pa");
  acquire(&kmem.lock);
  int ret = --kmem.ref_count[ind];
  release(&kmem.lock);
  return ret;
}

int increase_ref_count(void *pa) {
  int ind = INDEXBYPA(pa);
  if (ind < 0 || ind > INDEXBYPA(PHYSTOP)) panic("increase: pa");
  acquire(&kmem.lock);
  int ret = ++kmem.ref_count[ind];
  release(&kmem.lock);
  return ret;
}

void kinit() {
  initlock(&kmem.lock, "kmem");
  freerange(end, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end) {
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE) {
    kmem.ref_count[INDEXBYPA(p)] = 1;
    kfree(p);
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa) {
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  int ref_cnt = decrease_ref_count(pa);
  if (ref_cnt > 0) return;
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *kalloc(void) {
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if (r) {
    kmem.ref_count[INDEXBYPA(r)] = 1;
    kmem.freelist = r->next;
  }
  release(&kmem.lock);

  if (r) memset((char *)r, 5, PGSIZE);  // fill with junk
  return (void *)r;
}
