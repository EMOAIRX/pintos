#include "vm/swap.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "devices/block.h"
#include "lib/kernel/bitmap.h"
#include "filesys/filesys.h"
#include <stdio.h>
#include <string.h>
//define swap_disk;
static struct disk *swap_disk;
static struct bitmap *swap_table;
static struct lock swap_lock;
#define SECTORS_PER_PAGE (PGSIZE/BLOCK_SECTOR_SIZE)
void swap_init(void){
    // printf("SWAP INIT(%d)\n",block_size(swap_disk) / 8);
    swap_disk = block_get_role(BLOCK_SWAP);
    if(swap_disk == NULL) PANIC("NO SWAP DISK");
    swap_table = bitmap_create(block_size(swap_disk) / 8);
    if(swap_table == NULL) PANIC("NO SWAP TABLE");
    bitmap_set_all(swap_table,false);
    lock_init(&swap_lock);
}
void swap_in(size_t index, void *frame){
    // lock_acquire(&filesys_lock);
    lock_acquire(&swap_lock);
    // printf("[%d]ACQUIRE_SWAP_LOCK(%d)\n",thread_current()->tid,thread_current()->tid);
    // printf("[%d]kpage = %p\n",thread_current()->tid,frame);
    int i;
    for(i = 0; i < 8; i++){
        block_read(swap_disk,index*8+i,frame + i * BLOCK_SECTOR_SIZE);
    }
    bitmap_set(swap_table,index,false);
    lock_release(&swap_lock);
    // lock_release(&filesys_lock);
    // printf("[%d]swap_in , index = [%d]\n",thread_current()->tid,index);
    // printf("[%d]RELEASE_SWAP_LOCK\n",thread_current()->tid);
}
//swap out
size_t swap_out(void *frame){
    // printf("[%d]ACQUIRE_SWAP_LOCK(%d)\n",thread_current()->tid,thread_current()->tid);
    // lock_acquire(&filesys_lock);
    lock_acquire(&swap_lock);
    // printf("[%d]ACQUREING",thread_current()->tid);
    size_t index = bitmap_scan_and_flip(swap_table,0,1,false);
    if(index == BITMAP_ERROR) PANIC("NO SWAP SPACE");
    for(int i = 0; i < 8; i++){
        block_write(swap_disk,index*8+i,frame + i * BLOCK_SECTOR_SIZE);
    }
    // printf("[%d]OUT-index = %d / %d\n",0,index,block_size(swap_disk) / 8);
    lock_release(&swap_lock);
    // lock_release(&filesys_lock);
    // printf("[%d]RELEASE_SWAP_LOCK\n",thread_current()->tid);
    return index;
}

void free_swap_slot(size_t index){
    // printf("[%d]ACQUIRE_SWAP_LOCK(%d)\n",thread_current()->tid,thread_current()->tid);
    // lock_acquire(&filesys_lock);
    lock_acquire(&swap_lock);
    bitmap_set(swap_table,index,false);
    lock_release(&swap_lock);
    // lock_release(&filesys_lock);
    // printf("[%d]RELEASE_SWAP_LOCK\n",thread_current()->tid);
}