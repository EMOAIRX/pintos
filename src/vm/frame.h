#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stdbool.h>
#include <stdint.h>
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"

struct frame_table_entry{
  struct thread *owner;
  uint32_t *pte; //page_table_entry
  void *kaddr; // kernel address
  void *vaddr; // virtual address
  struct list_elem elem;
};
struct lock frame_lock;//lock for frame table
struct lock frame_evict_lock;//lock for frame eviction(and thread_exit)
void output_all_frame();
void frame_init (void);
void *alloc_frame (enum palloc_flags flags);

void free_frame (void *frame);
void frame_set_usr(void *,uint32_t *,void *);

#endif /* vm/frame.h */