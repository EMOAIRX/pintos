
#include "vm/load_page.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "threads/vaddr.h"
#include "filesys/file.h"


static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}


bool load_file(struct suppl_pte *spte){
    struct thread *t = thread_current();
    struct file *file = spte->data.file;
    off_t ofs = spte->data.ofs;
    void *upage = spte->vaddr;
    uint32_t read_bytes = spte->data.read_bytes;
    uint32_t zero_bytes = spte->data.zero_bytes;
    bool writable = spte->data.writable;
    // printf("TRY to ALLOC");
    void *kpage = alloc_frame(PAL_USER);
    // printf("ALLOCED, kpage = %p\n", kpage);
    if(kpage == NULL){
        PANIC("FULL");
    }
    if(file_read_at(file, kpage, read_bytes, ofs) != (int) read_bytes){
        free_frame(kpage);
        PANIC("READ FAILED");
    }
    memset(kpage + read_bytes, 0, zero_bytes);
    // puts("TRY TO INSTALL");
    if(!install_page(upage, kpage, writable)){
        free_frame(kpage);
        PANIC("INSTALL FAILED");
    }
    // puts("INSTALLED\n");
    spte -> is_loaded = true;
    return true;
}

bool load_swap(struct suppl_pte *spte){
    // printf("~~~");
    void *upage = spte->vaddr;
    void *kpage = alloc_frame(PAL_USER);
    if(kpage == NULL){
        PANIC("FULL");
    }
//8048 ~ 8248
    if(!install_page(upage, kpage, spte->swap_writable)){
        //一开始打成了data.writeable
        //然后调了两天！！！！！！！！！！！！！！！！
        //FUCK
        free_frame(kpage);
        PANIC("INSTALL FAILED");
    }
    // printf("LOAD_SWAP");
    // printf("[%p -> %p]\n", upage, kpage);
    // printf("~~~~");
    swap_in(spte->swap_index, kpage);
    spte -> is_loaded = true;
    return true;
}

bool load_page(struct suppl_pte * spte){
    // printf("LOADING");
    // printf("[LOAD_PAGE] vaddr = %p\n", spte->vaddr);
    // printf("[LOAD_PAGE] type = %d\n", spte->type);
    // printf("IN_SWAP = %d\n", spte->is_swapped);
    
    bool success = false;
    switch (spte->type){
        case SPTE_TYPE_FILE:
        // puts("LOADFILE");
            load_file(spte);
            break;
        case SPTE_TYPE_SWAP:  //perhaps STACK
        case SPTE_TYPE_FILE | SPTE_TYPE_SWAP:   //some file that comes into stacks
        // puts("LOAD_SWAP");
            load_swap(spte);
            break;
        default:
            puts("???");
            printf("spte->type = %d\n",spte->type);
    }
    // puts("ED");
        //from file to memory
    return success;
}