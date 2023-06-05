#ifndef VM_SWAP_H
#define VM_SWAP_H
#define SWAP_ERROR SIZE_MAX
#include <stddef.h>
void swap_init(void);
void swap_in(size_t used_index, void *kpage);
void free_swap_slot(size_t index);
size_t swap_out(void *kpage);
//swaptable
#endif 