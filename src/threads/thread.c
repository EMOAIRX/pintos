#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/fixed-point.h"
#include "devices/timer.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif
#include "filesys/filesys.h"
#include "filesys/file.h"

/** Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define TRIGGER_TIME_MAX INT64_MAX
#define THREAD_MAGIC 0xcd6abf4b

/** List of threads wating for trigger time.
 */
static struct list trigger_wating_list;

#define MAX_NICE 20
#define MIN_NICE -20
#define NICE_DEFAULT 0
#define RECENT_CPU_DEFAULT INT_TO_FIXED(0)
#define LOAD_AVG_DEFAULT INT_TO_FIXED(0)

/** List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/** List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/** Idle thread. */
static struct thread *idle_thread;

/** Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/** Lock used by allocate_tid(). */
static struct lock tid_lock;

/** Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /**< Return address. */
    thread_func *function;      /**< Function to call. */
    void *aux;                  /**< Auxiliary data for function. */
  };

/** Statistics. */
static long long idle_ticks;    /**< # of timer ticks spent idle. */
static long long kernel_ticks;  /**< # of timer ticks in kernel threads. */
static long long user_ticks;    /**< # of timer ticks in user programs. */

/** Scheduling. */
#define TIME_SLICE 4            /**< # of timer ticks to give each thread. */
static unsigned thread_ticks;   /**< # of timer ticks since last yield. */

/** If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

fixed_t load_avg;               /**< load_avg for mlfqs */

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *thread_alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);
inline static void thread_check_wakeup(int64_t);

// some functions for mlfqs

static void update_recent_cpu(struct thread *t, void *aux UNUSED);
static void calculate_advanced_priority(struct thread*t, void *aux UNUSED);
static void update_load_avg(void);



/**---------------------COMPARE FUNCTION---------------------*/
/**
 * trigger_time_less - compare the trigger_time of two threads
*/
static bool trigger_time_less(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED){
  struct thread *thread_a = list_entry(a, struct thread, trigger_wating_elem);
  struct thread *thread_b = list_entry(b, struct thread, trigger_wating_elem);
  return thread_a->trigger_time < thread_b->trigger_time;
}

/**
 * priority_greater - compare the priority of two threads
*/
static bool priority_greater(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED){
  struct thread *thread_a = list_entry(a, struct thread, elem);
  struct thread *thread_b = list_entry(b, struct thread, elem);
  return thread_a->priority > thread_b->priority;
}

/**----------------------------------------------------------*/

/** Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);
  list_init (&trigger_wating_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/** Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  // At system boot, it is initialized to 0.
  load_avg = LOAD_AVG_DEFAULT;

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/** Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (int64_t ticks)
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  thread_check_wakeup(ticks);


  bool changed = false;
  if(thread_mlfqs){
    if(t != idle_thread){
      t->recent_cpu = ADD_INT(t->recent_cpu, 1);
    }
    if(ticks % TIMER_FREQ == 0){
      update_load_avg();
      thread_foreach(update_recent_cpu, NULL);
      thread_foreach(calculate_advanced_priority, NULL);
      list_sort(&ready_list, priority_greater, NULL);
    }
    if(ticks % 4 == 0){
      calculate_advanced_priority(t, NULL);
      list_sort(&ready_list, priority_greater, NULL);
      if(!list_empty(&ready_list)){
        struct thread *max_priority_thread = list_entry(list_begin(&ready_list), struct thread, elem);
        if(max_priority_thread->priority > t->priority)
          changed = true;
      }
    }
  }

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE || changed)
    intr_yield_on_return ();
}

/** Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/** Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Stack frame for kernel_thread(). */
  kf = thread_alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = thread_alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = thread_alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  t->as_child = (struct child_entry *)malloc(sizeof(struct child_entry));
  t->as_child->tid = tid;
  t->as_child->t = t;
  t->as_child->exit_code = -1;
  t->as_child->is_alive = true;
  t->as_child->is_waiting_on = false;
  sema_init(&t->as_child->sema, 0);
  t->parent = thread_current();
  list_push_back(&t->parent->child_list, &t->as_child->elem);


  /* Add to run queue. */
  thread_unblock (t);
  
  //disable
  enum intr_level old_level = intr_disable();
  ASSERT(!intr_context());
  if( t->priority > thread_current()->priority && thread_current() != idle_thread){
    thread_yield();
  }
  intr_set_level(old_level);

  return tid;
}

/** Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

// {
  // printf("\nthread_BLOCK(%s)\n" , thread_current() -> name);
//   printf("\ncurrent thread %s priority %d\n", thread_current()->name, thread_current()->priority);
//   struct list_elem *e = list_begin(&all_list);
//   while(e != list_end(&all_list)){
//     struct thread *t = list_entry(e, struct thread, allelem);
//     printf("check [%p] %s %lld [PRI = %d %d]\n", t,t->name, t->trigger_time, t->priority,t->original_priority);
//     //e , next(e)
//     e = list_next(e);
//   }
// }
  thread_current ()->status = THREAD_BLOCKED;

  schedule ();
}

/** Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;
  old_level = intr_disable ();

  ASSERT( intr_get_level() == INTR_OFF );

  ASSERT (is_thread (t));

  ASSERT (t->status == THREAD_BLOCKED);

  // list_push_back (&ready_list, &t->elem);
  list_insert_ordered(&ready_list, &t->elem, priority_greater, NULL);

  t->status = THREAD_READY;

  intr_set_level (old_level);
}

/** Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/** Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/** Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}


static void output_all_threads_with_their_child(){
  struct list_elem *e;
  struct thread *t;
  struct child_entry *child;
  for(e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e)){
    t = list_entry(e, struct thread, allelem);
    if(t->parent != NULL)
      printf("thread-[%d] -> %d ]] ", t->tid,t->parent->tid);
    else printf("thread-[%d] -> ]]", t->tid);
    
    for(struct list_elem *e = list_begin(&t->child_list); 
                    e != list_end(&t->child_list); e = list_next(e)){
      child = list_entry(e, struct child_entry, elem);
      printf("child[%d] ", child->tid);
    }puts("");
  }
  puts("END");
}

/** Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{

  // puts("thread_exit");

  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif
  intr_disable ();

  // puts("DISABLE");

  // output_all_threads_with_their_child();
  struct thread *cur = thread_current ();
  struct thread *parent = cur->parent;
  struct list_elem *e;
  struct child_entry *child;
  for(e = list_begin(&cur->child_list); e != list_end(&cur->child_list); ){
    child = list_entry(e, struct child_entry, elem);
    if(child->is_alive){
      child->t->parent = NULL;
      e = list_next(e);
    } else{
      e = list_remove(e); //can delete this
      free(child);
    }
  }
  if(parent == NULL){
    free(cur->as_child); //如果没爹，就free
  } else{
    cur->as_child->is_alive = false;//如果有爹，让爹来free
    cur->as_child->exit_code = cur->exit_code;
    cur->as_child->t = NULL;
    if(cur->as_child->is_waiting_on){
      sema_up(&cur->as_child->sema);
    }
  }
  //relase all files

  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/** Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  //struct r = list_entry(list_begin(&ready_list), struct thread, elem)->name
  // struct thread *nxt = list_entry(list_begin(&ready_list), struct thread, elem);
  if (cur != idle_thread) 
    list_insert_ordered(&ready_list, &cur->elem, priority_greater, NULL);
  
  // printf("cur = %s, priority = %d\n", cur->name, cur->priority);
  // printf("nxt = %s, priority = %d\n", list_entry(list_begin(&ready_list), struct thread, elem)->name, list_entry(list_begin(&ready_list), struct thread, elem)->priority);
  //output all the threads in the ready_list with their priority
  

  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}


/** thread sleep until the given time , the thread will be blocked
   and will be unblocked when the time is reached */
void thread_sleep_until(int64_t wakeup_time){
  ASSERT(intr_get_level() == INTR_OFF);
  struct thread *cur = thread_current();
  ASSERT (cur -> trigger_time == TRIGGER_TIME_MAX);
  cur->trigger_time = wakeup_time;
  // list_insert(&trigger_wating_list, &cur->trigger_wating_elem);
  list_insert_ordered(&trigger_wating_list, &cur->trigger_wating_elem, trigger_time_less, NULL);
  //output the list
  thread_block();
}

/**
 * thread_check_wakeup - check the trigger_wating_list and unblock the threads
*/
inline static void thread_check_wakeup(int64_t ticks){
  enum intr_level old_level;
  old_level = intr_disable ();
  struct list_elem *e = list_begin(&all_list);
  e = list_begin(&trigger_wating_list);
  while(e != list_end(&trigger_wating_list)){
    struct thread *t = list_entry(e, struct thread, trigger_wating_elem);
    if(t->trigger_time > ticks){
      break;
    }

    ASSERT(is_thread(t));
    e = list_remove(e);
    t->trigger_time = TRIGGER_TIME_MAX;
    
    ASSERT(t->status == THREAD_BLOCKED);
    list_insert_ordered(&ready_list, &t->elem, priority_greater, NULL);
    t->status = THREAD_READY;
  }


  intr_set_level (old_level);
}

/** Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/** Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  //close intr
  ASSERT(thread_mlfqs == false);
  enum intr_level old_level;
  old_level = intr_disable ();
  struct thread * cur = thread_current();
  cur -> original_priority = new_priority;
  cur -> priority = new_priority;
  donate_re_calc_priority(cur);

  // {
  // printf("\ncurrent thread %s priority %d\n", thread_current()->name, thread_current()->priority);
  // struct list_elem *e = list_begin(&all_list);
  // while(e != list_end(&all_list)){
  //   struct thread *t = list_entry(e, struct thread, allelem);
  //   printf("check [%p] %s %lld [PRI = %d %d]\n", t,t->name, t->trigger_time, t->priority,t->original_priority);
  //   //e , next(e)
  //   e = list_next(e);
  // }
  // }

  if(!list_empty(&ready_list)){
    struct thread *t = list_entry(list_front(&ready_list), struct thread, elem);
    if(t->priority > cur->priority){
      // printf("thread_set_priority: %s yield to %s\n", cur->name, t->name);
      thread_yield();
    }
  }
  intr_set_level (old_level);
}


static int count_ready_threads(void){
  ASSERT(intr_get_level() == INTR_OFF);
  int count = list_size(&ready_list);
  if(thread_current() != idle_thread)
    count++;
  return count;
}
//Once per second thereafter, it is updated according to the following formula:
//59/60 * old + 1/60 * new
//where old is the old value of load_avg and new is the number of ready threads
//divided by 60.

/**
 * calculate_advanced_priority - calculate the advanced priority
*/
static void calculate_advanced_priority(struct thread *t, void *aux UNUSED){
  // ASSERT(intr_get_level() == INTR_OFF);
  if(t == idle_thread){
    return;
  }
  int recent_cpu = t->recent_cpu;
  int nice = t->nice;
  int priority = PRI_MAX - FIXED_TO_INT(DIV_INT(recent_cpu, 4)) - nice * 2;
  if(priority > PRI_MAX) priority = PRI_MAX;
  if(priority < PRI_MIN) priority = PRI_MIN;
  t->priority = priority;
}

/**
 * update_load_avg - update the load_avg
*/
static void update_load_avg(void){
  // ASSERT(intr_get_level() == INTR_OFF);
  int ready_threads = count_ready_threads();
  //use fixed_point to calculate
  load_avg = ADD_FIXED(DIV_INT(MUL_INT(load_avg, 59), 60), DIV_INT(INT_TO_FIXED(ready_threads), 60));
  // load_avg = (int)(load_avg * 59 / 60 + ready_threads * 1 / 60);
}


/**
 * thread_update_recent_cpu - update the recent_cpu
*/
static void update_recent_cpu(struct thread *t, void *aux UNUSED){
  // ASSERT(intr_get_level() == INTR_OFF);
  if(t == idle_thread){
    return;
  }
  fixed_t recent_cpu = t->recent_cpu;
  //use fixed_point to calculate
  recent_cpu = ADD_FIXED(
      MUL_FIXED(DIV_FIXED(MUL_INT(load_avg, 2), ADD_INT(MUL_INT(load_avg, 2), 1)), recent_cpu),
      INT_TO_FIXED(t->nice)
    );
  if(recent_cpu < 0){
    recent_cpu = 0;
  }
  t->recent_cpu = recent_cpu;
}


/** Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/** Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) 
{
  
  //disable
  enum intr_level old_level;
  old_level = intr_disable ();

  struct thread *cur = thread_current();
  cur->nice = nice;
  calculate_advanced_priority(cur, NULL);
  if(cur != idle_thread && !list_empty(&ready_list)){
    struct thread *t = list_entry(list_front(&ready_list), struct thread, elem);
    if(t->priority > cur->priority){
      thread_yield();
    }
  }
  intr_set_level (old_level);
  /* Not yet implemented. */
}

/** Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  /* Not yet implemented. */
  return 0;
}

/** Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  //disable
  enum intr_level old_level;
  old_level = intr_disable ();
  int value = FIXED_TO_INT(MUL_INT(load_avg, 100));
  intr_set_level (old_level);
  return value;
}

/** Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  enum intr_level old_level;
  old_level = intr_disable ();
  int value = FIXED_TO_INT(MUL_INT(thread_current()->recent_cpu, 100));
  intr_set_level (old_level);
  return value;
}

/** Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/** Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /**< The scheduler runs with interrupts off. */
  function (aux);       /**< Execute the thread function. */
  thread_exit ();       /**< If function() returns, kill the thread. */
}

/** Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/** Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/** Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->exit_code = -1; 
  t->max_mapid = 0;
    //-1 means not called exit(...) but terminated by other reason
  t->magic = THREAD_MAGIC;
  sema_init (&t->sema_exec, 0);
  list_init (&t->child_list);
  list_init (&t->file_list);
  t->exec_file = NULL;
  t->max_fd = 2;


  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}

/** Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
thread_alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/** Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/** Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/** Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  // printf("SCHEDULE FROM(%s) TO(%s)\n", cur->name, next->name);
  // printf("YIELD FROM(%s) TO(%s)\n", cur->name, next->name);

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));
  // static int cnt = 0;
  // cnt++;
  // ASSERT(cnt <= 100);
  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/** Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/** Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);
