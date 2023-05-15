#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "process.h"
#include "devices/shutdown.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
//filesys_create
#include "filesys/filesys.h" 
typedef int pid_t;

static void syscall_handler (struct intr_frame *);
static void syscall_write(struct intr_frame *);
static void syscall_halt(struct intr_frame *);
static void syscall_exit(struct intr_frame *);
static void syscall_wait(struct intr_frame *);
static void syscall_exec(struct intr_frame *);
static void syscall_create(struct intr_frame *);
static void syscall_remove(struct intr_frame *);
static void syscall_open(struct intr_frame *);
static void syscall_filesize(struct intr_frame *);
static void syscall_read(struct intr_frame *);
static void syscall_seek(struct intr_frame *);
static void syscall_tell(struct intr_frame *);
static void syscall_close(struct intr_frame *);

static int get_user(const uint8_t *uaddr);
static bool put_user(uint8_t *udst, uint8_t byte);
static void* check_read_user_ptr(void *ptr, int size);
static void* check_write_user_ptr(void *ptr, int size ,uint32_t*);
static void* check_read_user_string(void *ptr);
static void terminate_process();


static struct file_descriptor * get_file_descriptor(int fd);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}


static void terminate_process(){
  thread_current()->exit_code = -1;
  printf("%s: exit(%d)\n", thread_current()->name, -1);
  thread_exit();
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  //check if the pointer is valid
  // printf("system call\n");
  if (!is_user_vaddr(f->esp)){
    terminate_process();
  }
  // printf("system call --- 2\n");
  int syscall_num = *(int *)check_read_user_ptr(f->esp, sizeof(int));
  // printf("system call --- 3\n");
  // printf("system call: syscall number %d\n", syscall_num);
  switch (syscall_num)
  {
    case SYS_HALT:  syscall_halt(f);   break;
    case SYS_EXIT:  syscall_exit(f);   break;
    case SYS_WRITE: syscall_write(f);  break;
    case SYS_WAIT:  syscall_wait(f);   break;
    case SYS_EXEC:  syscall_exec(f);   break;
    case SYS_CREATE: syscall_create(f); break;
    case SYS_REMOVE: syscall_remove(f); break;
    case SYS_OPEN: syscall_open(f); break;
    case SYS_FILESIZE: syscall_filesize(f); break;
    case SYS_READ: syscall_read(f); break;
    case SYS_SEEK: syscall_seek(f); break;
    case SYS_TELL: syscall_tell(f); break;
    case SYS_CLOSE: syscall_close(f); break;
    default:
      printf("system call: unknown syscall number %d\n");
      break;
  }
  // puts("END_SYSCALL");
}

static void syscall_halt(struct intr_frame *f UNUSED)
{
  shutdown_power_off();
}

static void syscall_exit(struct intr_frame *f UNUSED){
  int status = *(int *)check_read_user_ptr(f->esp + 4, sizeof(int));
  struct thread *cur = thread_current();
  cur->exit_code = status;
  printf("%s: exit(%d)\n", cur->name, status);
  thread_exit ();  
}

static void syscall_write(struct intr_frame *f UNUSED)
{
  int fd = *(int *)check_read_user_ptr(f->esp + 4, sizeof(int));
  const void *buffer = *(void **)check_read_user_ptr(f->esp + 8, sizeof(void **));
  unsigned size = *(unsigned *)check_read_user_ptr(f->esp + 12, sizeof(unsigned));
  check_read_user_ptr(buffer, size);

  if (fd == 1) {
    f->eax = size;
    putbuf(buffer, size);
  } else{
    struct file_descriptor *filed = get_file_descriptor(fd);
    if (filed == NULL) {
      f->eax = -1;
      return;
    }
    struct file *file = filed->file;
    // printf("fd = %d\n", fd);
    // printf("file_name = %p\n", file);
    lock_acquire(&filesys_lock);
    f -> eax = file_write(file, buffer, size);
    lock_release(&filesys_lock);
    // printf("f->eax = %d\n", f->eax);
    // printf("filesize = %d\n", file_length(file));
  }

}

static void syscall_seek(struct intr_frame *f UNUSED){
  int fd = *(int *)check_read_user_ptr(f->esp + 4, sizeof(int));
  unsigned position = *(unsigned *)check_read_user_ptr(f->esp + 8, sizeof(unsigned));
  struct file_descriptor *filed = get_file_descriptor(fd);
  if (filed == NULL) {
    return;
  }
  struct file *file = filed->file;
  lock_acquire(&filesys_lock);
  file_seek(file, position);
  lock_release(&filesys_lock);
}


static void syscall_tell(struct intr_frame *f UNUSED){
  int fd = *(int *)check_read_user_ptr(f->esp + 4, sizeof(int));
  struct file_descriptor *filed = get_file_descriptor(fd);
  if (filed == NULL) {
    f->eax = -1;
    return;
  }
  struct file *file = filed->file;
  lock_acquire(&filesys_lock);
  f->eax = file_tell(file);
  lock_release(&filesys_lock);
}


static void syscall_read(struct intr_frame *f UNUSED){
  int fd = *(int *)check_read_user_ptr(f->esp + 4, sizeof(int));
  const void *buffer = *(void **)check_read_user_ptr(f->esp + 8, sizeof(void **));
  unsigned size = *(unsigned *)check_read_user_ptr(f->esp + 12, sizeof(unsigned));
  check_write_user_ptr(buffer, size, f->esp);
  if(fd == 0){
    int i;
    for(i = 0; i < size; i++){
      *(char *)(buffer + i) = input_getc();
    }
    f->eax = size;
    return;
  }
  struct file_descriptor *filed = get_file_descriptor(fd);
  if (filed == NULL) {
    f->eax = -1;
    return;
  }
  struct file *file = filed->file;
  int total_read = 0;
  lock_acquire(&filesys_lock);
  total_read = file_read(file, buffer, size);
  lock_release(&filesys_lock);
  f->eax = total_read;
}


static void syscall_filesize(struct intr_frame *f UNUSED){
  int fd = *(int *)check_read_user_ptr(f->esp + 4, sizeof(int));
  struct file_descriptor *filed = get_file_descriptor(fd);
  if (filed == NULL) {
    f->eax = -1;
    return;
  }
  struct file *file = filed->file;
  lock_acquire(&filesys_lock);
  f->eax = file_length(file);
  lock_release(&filesys_lock);
}

static void syscall_wait(struct intr_frame *f UNUSED)
{
  pid_t pid = *(pid_t *)check_read_user_ptr(f->esp + 4, sizeof(pid_t));
  f->eax = process_wait(pid);
}

static void syscall_exec(struct intr_frame *f UNUSED)
{
  const char** pp = check_read_user_ptr(f->esp + 4, sizeof(char *));
  const char* cmd_line = check_read_user_string(*pp);
  // printf("syscall_exec: cmd_line: %s\n", cmd_line);
  f->eax = process_execute(cmd_line);
}

static void syscall_create(struct intr_frame *f UNUSED)
{
  const char** pp = check_read_user_ptr(f->esp + 4, sizeof(char *));
  const char* file = check_read_user_string(*pp);
  unsigned initial_size = *(unsigned *)check_read_user_ptr(f->esp + 8, sizeof(unsigned));
  lock_acquire(&filesys_lock);
  f->eax = filesys_create(file, initial_size);
  lock_release(&filesys_lock);
}

static void syscall_open(struct intr_frame *f UNUSED)
{
  const char** pp = check_read_user_ptr(f->esp + 4, sizeof(char *));
  const char* filename = check_read_user_string(*pp);
  lock_acquire(&filesys_lock);
  struct file *myfile = filesys_open(filename);
  lock_release(&filesys_lock);

  // printf("syscall_open: filename: %s, file: %p\n", filename, myfile);

  if(myfile == NULL){
    f->eax = -1;
    return;
  }
  struct file_descriptor *fd = malloc(sizeof(struct file_descriptor));
  fd->file = myfile;
  fd->fd = thread_current()->max_fd++;
  list_push_back(&thread_current()->file_list, &fd->elem);
  f->eax = fd->fd;
}


static void syscall_remove(struct intr_frame *f UNUSED)
{
  const char** pp = check_read_user_ptr(f->esp + 4, sizeof(char *));
  const char* file = check_read_user_string(*pp);
  lock_acquire(&filesys_lock);
  f->eax = filesys_remove(file);
  lock_release(&filesys_lock);
}


static struct file_descriptor * get_file_descriptor(int fd){

  struct thread *cur = thread_current();
  struct list_elem *e;
  for (e = list_begin(&cur->file_list); e != list_end(&cur->file_list); e = list_next(e)){
    struct file_descriptor *fd_elem = list_entry(e, struct file_descriptor, elem);
    if (fd_elem->fd == fd){
      return fd_elem;
    }
  }
  return NULL;
}


static void syscall_close(struct intr_frame *f UNUSED)
{
  int fd_num = *(int *)check_read_user_ptr(f->esp + 4, sizeof(int));
  struct file_descriptor *fd = get_file_descriptor(fd_num);
  if (fd == NULL){
    return;
  }
  struct file *file = fd->file;
  lock_acquire(&filesys_lock);
  file_close(file);
  lock_release(&filesys_lock);

  list_remove(&fd->elem);
  free(fd);
}

/**
 * check if the pointer is valid to read
*/
static void* check_read_user_ptr(void *ptr, int size){
  // printf("check_read_user_ptr\n");
  if (!is_user_vaddr(ptr)){
    terminate_process();
  }
  // printf("--- 1\n");

  struct thread *cur = thread_current();
  for (int i = 0; i < size; i++){
    if (get_user(ptr + i) == -1){
#ifdef VM
      if (pagedir_get_page(cur->pagedir,ptr + i) == NULL){
        struct suppl_pte *spte;
        spte = get_spte (&cur->sup_page_table, pg_round_down (ptr+i));
        if (spte != NULL && !spte->is_loaded){
          load_page (spte);
          continue;
        }
      }
#endif
      // printf("[%d]I want to get(%p)\n", thread_current()->tid ,ptr + i);
      // printf("check_read_user_ptr: get_user failed\n");
      terminate_process();
    }
  }
  // printf("--- 2\n");
  return ptr;
}

static void* check_write_user_ptr(void *ptr, int size,uint32_t* esp){
  if (!is_user_vaddr(ptr)){
    terminate_process();
  }
  struct thread *cur = thread_current();
  for (int i = 0; i < size; i++){
    if (put_user(ptr + i, 0) == false){
      //if is_load = false ...
#ifdef VM
      if (pagedir_get_page(cur->pagedir,ptr+i) == NULL){
        struct suppl_pte *spte;
        spte = get_spte (&cur->sup_page_table, pg_round_down (ptr+i));
        if (spte != NULL && !spte->is_loaded){
          load_page (spte);
          continue;
        }
        if (spte == NULL && ptr+i >= (esp - 32)){
          grow_stack (ptr+i);
          continue;
        }
      }
#endif
      terminate_process();
    }
  }
  return ptr;
}

static void* check_read_user_string(void *ptr){
  if (!is_user_vaddr(ptr)){
    terminate_process();
  }
  void* tmp = ptr;
  for(;;){
    if (get_user(tmp) == -1){
      terminate_process();
    }
    if (get_user(tmp) == 0){
      break;
    }
    tmp++;
  }
  return ptr;
}

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}

/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}