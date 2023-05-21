#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include <list.h>
#include "threads/pte.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "lib/string.h"
#include "threads/malloc.h"
#include "lib/kernel/list.h"
#include "filesys/filesys.h"


static struct list frame_table;
int frame_table_size;
struct list_elem *global_current_elem;


// static void remove_frame(void*);
static struct frame_table_entry* get_frame_from_kaddr (void *frame);

void frame_init (void){
    list_init (&frame_table);
    lock_init (&frame_lock);
    lock_init (&frame_evict_lock);
    void *kaddr = palloc_get_page(PAL_USER | PAL_ZERO);
    while(kaddr != NULL){
        struct frame_table_entry *vf;
        vf = malloc(sizeof(struct frame_table_entry));
        if(vf == NULL){
            PANIC("MALLOC FAILED");
        }
        vf -> owner = NULL;
        vf -> kaddr = kaddr;
        vf -> vaddr = NULL;
        list_push_back(&frame_table, &vf->elem);
        kaddr = palloc_get_page(PAL_USER | PAL_ZERO);
    }
    global_current_elem = list_begin(&frame_table);
}

// void output_all_frame(){
//     struct list_elem *e;
//     for(e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e)){
//         struct frame_table_entry *f = list_entry(e, struct frame_table_entry, elem);
//         if(f -> owner != NULL)
//         printf("FRAME: %p -> %p\n", f->vaddr,f->kaddr);
//     }
// }

static struct frame_table_entry* find_frame_by_clock(){
    // printf("START_FINDING\n");
    struct frame_table_entry *vf;
    struct list_elem *e;
    for(;;){
        for(e = global_current_elem; e != list_end(&frame_table); e = list_next(e)){
            vf = list_entry(e, struct frame_table_entry, elem);
            if(vf->owner == NULL ){//逆天，pagedir清空表示整个线程已经结束了
                global_current_elem = list_next(e);
                return vf;
            }
            if(vf -> owner != NULL && vf -> vaddr == NULL){
                continue;
            }//this is the block that was loading (alloc_frame but not frame_set_usr)
            if(!pagedir_is_accessed(vf->owner->pagedir, vf->vaddr)){
                global_current_elem = list_next(e);
                return vf;
            }
            pagedir_set_accessed(vf->owner->pagedir, vf->vaddr, false);
        }global_current_elem = list_begin(&frame_table);
    }
    return NULL;//IMPOSSIBLE
}

static void write_back_to_file(struct file *file, off_t offset, void *kpage, uint32_t size){
    printf("WRITE_BACK_TO_FILE\n");
    lock_acquire(&filesys_lock);
    file_seek(file, offset);
    file_write(file, kpage, size);
    lock_release(&filesys_lock);
}

static bool evict_frame(struct frame_table_entry *vf){
    struct thread *t = vf->owner;
    struct suppl_pte *spte = get_spte (&t->sup_page_table, vf->vaddr);
    ASSERT(spte != NULL);
    // printf("[%d %d]\n",pagedir_is_dirty(t->pagedir, spte->vaddr), spte->type);
    if(pagedir_is_dirty(t->pagedir, spte->vaddr) && spte->type == SPTE_TYPE_MMF){
        write_back_to_file(spte->data.mmf_page.file, spte->data.mmf_page.ofs, 
                                        vf->kaddr, spte->data.mmf_page.read_bytes);
    } else
    if(pagedir_is_dirty(t->pagedir, spte->vaddr) || spte->type != SPTE_TYPE_FILE){
        size_t swap_index = swap_out(vf -> kaddr);
        if(swap_index == SWAP_ERROR) PANIC("SWAP OUT FAILED");
        spte->type |= SPTE_TYPE_SWAP; //只要写过一次，永远都是SWAP了
        spte->swap_index = swap_index;
        spte->swap_writable = *(vf->pte) & PTE_W;
    }
    // memset(vf->kaddr, 0, PGSIZE);
    spte->is_loaded = false;
    pagedir_clear_page(t->pagedir, spte->vaddr);
    return true;
}

void frame_set_usr (void *kpage, uint32_t* pte, void* upage){
    // printf("[%d]ACQURE FRAME LOCK\n",thread_current()->tid);
    lock_acquire(&frame_lock);
    struct frame_table_entry *vf = get_frame_from_kaddr(kpage);
    //可以优化
    if(vf != NULL){
        vf->pte = pte;
        vf->vaddr = upage;
    }
    lock_release(&frame_lock);
    // printf("[%d]frame_set_usr   [%p -> %p]\n",thread_current()->tid, vf->vaddr, vf->kaddr);
    // printf("[%d]RELEASE FRAME LOCK\n",thread_current()->tid);
}

static struct frame_table_entry*get_frame_from_kaddr (void *kaddr){
    struct list_elem *e;
    for(e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e)){
        struct frame_table_entry *f = list_entry(e, struct frame_table_entry, elem);
        if(f->kaddr == kaddr) return f;
    }
    return NULL;    
}



void* alloc_frame (enum palloc_flags flags){
    lock_acquire(&frame_evict_lock);
    lock_acquire(&frame_lock);
    struct frame_table_entry *vf;
    vf = find_frame_by_clock();
    if(vf -> owner != NULL && vf -> owner -> pagedir != NULL){
        bool result = evict_frame(vf);
        if(!result){
            lock_release(&frame_evict_lock);
            PANIC("EVICT FAILED");
        }
    }
    
            // printf("vf -> owner = %p\n",vf->owner);
    lock_release(&frame_evict_lock);
    if(flags & PAL_ZERO)
        memset(vf->kaddr, 0, PGSIZE);
    vf->owner = thread_current();
    vf->vaddr = NULL; //注意同步问题，可能这里还是NULL，然后这个块就被替换了，这是我们不想要的.
    vf->pte = NULL;
    lock_release(&frame_lock);
    return vf->kaddr;
}

void free_frame (void *kaddr){
    lock_acquire(&frame_lock);
    struct frame_table_entry *vf = get_frame_from_kaddr(kaddr);
    if(vf == NULL) PANIC("FREE FRAME FAILED");
    vf -> owner = NULL;
    vf -> vaddr = NULL;
    vf -> pte = NULL;
    lock_release(&frame_lock);
    // printf("[%d]RELEASE FRAME LOCK\n",thread_current()->tid);
}
/*
pintos -v -k -T 300 --qemu  --filesys-size=2 -p tests/vm/page-linear
 -a page-linear --swap-size=4 -- -q  -f run page-linear

pintos -v -k -T 60 --qemu  --filesys-size=2 -p tests/vm/page-parallel 
-a page-parallel -p tests/vm/child-linear -a child-linear --swap-size=4 -- -q  -f run page-parallel

pintos -v -k -T 600 --qemu  --filesys-size=2 -p tests/vm/page-merge-seq 
-a page-merge-seq -p tests/vm/child-sort -a child-sort --swap-size=4 -- -q  -f run page-merge-seq
*/

// static void 
// remove_frame(void* kaddr){
//     lock_acquire(&frame_lock);
//     struct list_elem *e;
//     struct frame_table_entry *vf = get_frame_from_kaddr(kaddr);
//     ASSERT(vf != NULL);
//     vf -> owner = NULL;
//     vf -> vaddr = NULL;
//     vf -> pte = NULL;
//     lock_release(&frame_lock);
// }
