#ifndef VM_MMFILE_H
#define VM_MMFILE_H
#include "page.h"
struct mmfile {
    mapid_t mapid;
    struct file *file;
    void* start_addr;
    unsigned pg_cnt;
    struct hash_elem elem;
};

void load_mmfile(struct mmfile *mmfile, void *addr);
mapid_t mmfile_insert(struct thread *t, struct file *file, void *addr, unsigned file_len);
struct mmfile* mmfile_find(struct hash *mmfile_table, mapid_t id);
void mmfile_remove(struct hash *mmfile_table, struct mmfile *mmfile);
#endif

