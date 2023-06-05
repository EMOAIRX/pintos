#ifndef VM_LOAD_PAGE_H
#define VM_LOAD_PAGE_H
#include "vm/page.h"
bool load_file(struct suppl_pte *spte);
bool load_swap(struct suppl_pte *spte);
bool load_page(struct suppl_pte *spte);
#endif