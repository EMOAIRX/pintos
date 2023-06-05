#ifndef VM_PAGE_H
#define VM_PAGE_H

#define STACK_SIZE (8 * (1 << 20))

#include <stdio.h>
#include "threads/thread.h"
#include "threads/palloc.h"
#include "lib/kernel/hash.h"
#include "filesys/file.h"

/* Data Definition */

/* The lowest bit indicate whether the page has been swapped out */
struct FILE_PAGE
{
  struct file *file;
  off_t ofs;
  uint32_t read_bytes;
  uint32_t zero_bytes;
  bool writable;
};
struct MMF_PAGE
{
  struct file *file;
  off_t ofs;
  uint32_t read_bytes;
};
union suppl_pte_data{
  struct FILE_PAGE file_page;
  struct MMF_PAGE mmf_page;
};

#define SPTE_TYPE_STACK 000
#define SPTE_TYPE_FILE 001
#define SPTE_TYPE_MMF 002
#define SPTE_TYPE_SWAP 004

#define IS_SWAP(type) (type & SPTE_TYPE_SWAP)
#define IS_FILE(type) (type & SPTE_TYPE_FILE)
#define IS_MMF(type) (type & SPTE_TYPE_MMF)

/* supplemental page table entry */
struct suppl_pte
{
  int type;
  void *vaddr;   //user virtual address as the unique identifier of a page
  union suppl_pte_data data;
  bool is_loaded;

  /* reserved for possible swapping */
  size_t swap_index;
  bool swap_writable;

  struct hash_elem elem;
};

unsigned spt_hash(const struct hash_elem *, void *);
bool spt_less(const struct hash_elem *, const struct hash_elem *, void *);
unsigned mmap_hash(const struct hash_elem *, void *);
bool mmap_less(const struct hash_elem *, const struct hash_elem *, void *);

/* Initialization of the supplemental page table management provided */
void page_init(void);

void free_spt(struct hash *);

void free_mmap(struct hash *);


/* Add a file supplemental page table entry to the current thread's
 * supplemental page table */
bool spt_insert_file ( struct file *, off_t, uint8_t *, 
			    uint32_t, uint32_t, bool);


void free_spt_entry(struct suppl_pte *spte);

bool spt_insert_stack (void *);

/* Add a memory-mapped-file supplemental page table entry to the current
 * thread's supplemental page table */
bool suppl_pt_insert_mmf (struct file *, off_t, uint8_t *, uint32_t);

/* Given hash table and its key which is a user virtual address, find the
 * corresponding hash element*/
struct suppl_pte *get_spte(struct hash *, void *);

/* Free the given supplimental page table, which is a hash table */
void free_suppl_pt (struct hash *);

/* Load page data to the page defined in struct suppl_pte. */
bool load_page (struct suppl_pte *);

/* Grow stack by one page where the given address points to */
void grow_stack (void *);

#endif /* vm/page.h */
