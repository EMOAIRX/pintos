#include "mmfile.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "filesys/filesys.h"
#include "vm/frame.h"

mapid_t mmfile_insert(struct thread *t, struct file *file, void *addr, unsigned file_len){
    struct mmfile * mf = malloc(sizeof(struct mmfile));
    mf->mapid = t->max_mapid++;
    mf->file = file;
    mf->pg_cnt = file_len / PGSIZE + (file_len % PGSIZE == 0 ? 0 : 1);
    mf->start_addr = addr;
    struct hash_elem * e = hash_insert(&t->mmap_file_table, &mf->elem);
    if(e != NULL){
        free(mf);
        PANIC("HASH INSERT FAILED");
    }
    for(int i = 0; i < file_len; i += PGSIZE){
        struct suppl_pte * spte = malloc(sizeof(struct suppl_pte));
        spte->type = SPTE_TYPE_MMF;
        spte->vaddr = addr + i;
        spte->data.mmf_page.file = file;
        spte->data.mmf_page.ofs = i;
        spte->data.mmf_page.read_bytes = (i + PGSIZE < file_len) ? PGSIZE : file_len - i;
        // printf("read_bytes: %d\n", spte->data.mmf_page.read_bytes);
        // printf("from[%p] to[%p]\n", spte->vaddr, spte->vaddr + spte->data.mmf_page.read_bytes);
        spte->is_loaded = false;
        spte->swap_index = 0;
        spte->swap_writable = false;
        hash_insert(&t->sup_page_table, &spte->elem);
    }

    return mf->mapid;
}

struct mmfile* mmfile_find(struct hash *mmfile_table, mapid_t id){
    struct mmfile mf;
    mf.mapid = id;
    struct hash_elem *e = hash_find(mmfile_table, &mf.elem);
    if(e == NULL) return NULL;
    return hash_entry(e, struct mmfile, elem);
}

static void mmfile_clear_page(struct mmfile *mmfile){
    lock_acquire(&frame_evict_lock);
    struct thread *t = thread_current();
    void* addr = mmfile->start_addr;
    int pg_cnt = mmfile->pg_cnt;
    // printf("addr = %p, pg_cnt = %d\n", addr, pg_cnt);
    for(int i = 0; i < pg_cnt; i++){
        struct suppl_pte *spte = get_spte(&t->sup_page_table, addr);
        if(spte == NULL) PANIC("SPT NOT FOUND");
        struct hash_elem *he = hash_delete(&t->sup_page_table, &spte->elem);
        if(pagedir_is_dirty(t->pagedir, addr)){
            file_seek(spte->data.mmf_page.file, spte->data.mmf_page.ofs);
            file_write(spte->data.mmf_page.file, addr, spte->data.mmf_page.read_bytes);
        }
        free_spt_entry(spte);
        addr += PGSIZE;
    }
    lock_release(&frame_evict_lock);
}

void mmfile_remove(struct hash *mmfile_table, struct mmfile *mmfile){
    // lock_acquire(&filesys_lock);
    hash_delete(mmfile_table, &mmfile->elem);
    mmfile_clear_page(mmfile);
    free(mmfile);
    // lock_release(&filesys_lock);
}

/*
FAIL tests/vm/page-merge-mm
FAIL tests/vm/mmap-write
FAIL tests/vm/mmap-exit

pintos -v -k -T 60 --qemu  --filesys-size=2 -p tests/vm/page-merge-mm -a page-merge-mm -p tests/vm/child-qsort-mm -a child-qsort-mm --swap-size=4 -- -q  -f run page-merge-mm
pintos -v -k -T 60 --qemu  --filesys-size=2 -p tests/vm/mmap-exit -a mmap-exit -p tests/vm/child-mm-wrt -a child-mm-wrt --swap-size=4 -- -q  -f run mmap-exit 
pintos -v -k -T 60 --qemu  --filesys-size=2 -p tests/vm/mmap-write -a mmap-write --swap-size=4 -- -q  -f run mmap-write
*/