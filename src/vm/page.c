#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "vm/load_page.h"
#include "lib/kernel/hash.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
void page_init(void){
    ;
}

unsigned spt_hash(const struct hash_elem * e, void * aux UNUSED){
    //use hash(vaddr)
    const struct suppl_pte * spte = hash_entry(e, struct suppl_pte, elem);
    // printf("hashval = %d\n", hash_bytes(&spte->vaddr, sizeof(spte->vaddr)));
    return hash_bytes(&spte->vaddr, sizeof(spte->vaddr));
}

bool spt_less(const struct hash_elem * a, const struct hash_elem * b, void * aux UNUSED){
    //compare vaddr
    const struct suppl_pte * spte_a = hash_entry(a, struct suppl_pte, elem);
    const struct suppl_pte * spte_b = hash_entry(b, struct suppl_pte, elem);
    // printf("compare(%p, %p)\n", spte_a->vaddr, spte_b->vaddr);
    return spte_a->vaddr - spte_b->vaddr < 0;
}


static void free_spt_entry(struct hash_elem * e, void * aux UNUSED){
    struct suppl_pte * spte = hash_entry(e, struct suppl_pte, elem);
    if(spte -> is_loaded == true){ //如果已经加载到内存中
        // printf("[%p]\n",thread_current()->tid, spte->vaddr);
        uint32_t *pd = thread_current()->pagedir;
        // printf("[%d]FREE_FRAME[%p]\n",thread_current()->tid, spte->vaddr);
        free_frame(pagedir_get_page(pd, spte->vaddr));
        pagedir_clear_page(pd, spte->vaddr);
    } else
    if(spte -> type & SPTE_TYPE_SWAP){
        free_swap_slot(spte->swap_index);
    } else{
        ;
        // printf("[%d]NOT_LOADED[%p]\n",thread_current()->tid, spte->vaddr);
    }
    free(spte);
}
static free_mmap_entry(struct hash_elem * e, void * aux){
    struct mmap_file * mf = hash_entry(e, struct mmap_file, elem);
    free(mf);
}

void free_spt(struct hash* spt){
    lock_acquire(&frame_evict_lock);
    hash_destroy(spt, free_spt_entry);
    lock_release(&frame_evict_lock);
}

void free_mmap(struct hash * mmap_file){
    hash_destroy(mmap_file, free_mmap_entry);
}

unsigned mmap_hash(const struct hash_elem * e, void * aux){
    const struct mmap_file * mf = hash_entry(e, struct mmap_file, elem);
    return hash_bytes(&mf->mapid, sizeof(mf->mapid));
}

bool mmap_less(const struct hash_elem * a, const struct hash_elem * b, void * aux){
    const struct mmap_file * mf_a = hash_entry(a, struct mmap_file, elem);
    const struct mmap_file * mf_b = hash_entry(b, struct mmap_file, elem);
    return mf_a->mapid < mf_b->mapid;
}

bool spt_insert_stack(void* vaddr){
    struct suppl_pte *spte;
    struct thread *t = thread_current();
    spte = malloc(sizeof(struct suppl_pte));
    if(spte == NULL) PANIC("SPTE MALLOC FAILED");
    //spt_insert_stack_page
    spte->vaddr = vaddr;
    spte->is_loaded = true;
    spte->type = SPTE_TYPE_STACK;
    // spte->swap_writable = true;
    bool result = hash_insert(&t->sup_page_table, &spte->elem);
    if(result != NULL) PANIC("HASH INSERT FAILED");
    return true;
}

bool spt_insert_file(struct file *file,off_t ofs,uint8_t *upage,
                     uint32_t read_bytes,uint32_t zero_bytes,bool writable){
    // if(thread_current()-> tid == 7){
    //     printf("[%d]ADD_SGGMENT FROM (%p to %p)\n", thread_current()->tid
        // , upage, upage + read_bytes + zero_bytes);
    // }
    struct suppl_pte *spte;
    struct hash_elem *result;
    struct thread *t = thread_current();
    spte = malloc(sizeof(struct suppl_pte));
    if(spte == NULL){
        return false;
    }
    spte->type = SPTE_TYPE_FILE;
    spte->vaddr = upage;
    spte->data.file = file;
    spte->data.ofs = ofs;
    spte->data.read_bytes = read_bytes;
    spte->data.zero_bytes = zero_bytes;
    spte->data.writable = writable;
    spte->is_loaded = false;
    // printf("INSERT(%p %p)\n", &t->sup_page_table, upage);
    result = hash_insert(&t->sup_page_table, &spte->elem);
    if(result != NULL){
        free(spte);
        return false;
    }
    return true;
}
//pintos -v -k -T 60 --qemu  --filesys-size=2 -p tests/vm/pt-grow-stk-sc
// -a pt-grow-stk-sc --swap-size=4 -- -q  -f run pt-grow-stk-sc
void grow_stack(void* ptr){
    ptr = pg_round_down(ptr);
    void *spage;
    struct thread* cur = thread_current();
    spage = alloc_frame(PAL_USER | PAL_ZERO);
    if (spage == NULL) return ;
    if (!pagedir_set_page(cur->pagedir , ptr , spage , true)){
        free_frame(spage);
        PANIC("grow_stack failed");
    }
    spt_insert_stack(ptr);
    // puts("ED_STACK");
}


//get_spte
struct suppl_pte * get_spte(struct hash * spt, void * vaddr){

    struct suppl_pte spte;
    struct hash_elem * result;
    spte.vaddr = vaddr;
    result = hash_find(spt, &spte.elem);
    if(result == NULL){
        return NULL;
    }
    struct suppl_pte * ret = hash_entry(result, struct suppl_pte, elem);
    return ret;
}
