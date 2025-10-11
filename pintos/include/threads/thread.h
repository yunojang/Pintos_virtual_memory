#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>

#include "threads/fixed-point.h"
#include "threads/interrupt.h"
#include "threads/synch.h"

#ifdef VM
#include "vm/vm.h"
#endif

/* States in a thread's life cycle. */
enum thread_status {
  THREAD_RUNNING, /* Running thread. */
  THREAD_READY,   /* Not running but ready to run. */
  THREAD_BLOCKED, /* Waiting for an event to trigger. */
  THREAD_DYING    /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t)-1) /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0      /* Lowest priority. */
#define PRI_DEFAULT 31 /* Default priority. */
#define PRI_MAX 63     /* Highest priority. */

/* Thread fd table */
#define MAX_FILES 32 /* 초기 파일 디스크립터 테이블 크기*/

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally ge this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
  /* Owned by thread.c. */
  tid_t tid;                 /* Thread identifier. */
  enum thread_status status; /* Thread state. */
  char name[16];             /* Name (for debugging purposes). */
  int priority;              /* Priority. */

  /* Shared between thread.c and synch.c. */
  struct list_elem elem;       /* List element. */
  int64_t wake_tick;           /* 쓰레드를 깨울 시간 */
  struct list_elem sleep_elem; /* sleep_list에서의 연결리스트 노드 */
  struct list_elem all_elem;   /* all_list에서의 연결리스트 노드 */

  int original_priority;         /* 원래 우선순위(기부 이전) */
  struct list acquired_locks;    /* 현재 보유(점유) 중인 락들 */
  struct lock *waiting_for_lock; /* 내가 기다리고 있는 락 */
  int is_donated;                /* 현재 우선순위가 기부받고 있는 상황인지 */

  /* mlfqs 전용*/
  int nice;           /* CPU를 양보하는 척도 (-20~20) */
  fixed_t recent_cpu; /* 최근 CPU 사용량 (fixed-point)*/

  /* process wait, exit 용 */
  struct list child_list;     // child_info 의 리스트
  struct lock children_lock;  // children list 순회할때 race condition 막기 위해
  tid_t parent_tid;           // 내 부모의 tid

  /* filesys 용 */
  struct file **fd_table;  // 파일 디스크립터 테이블
  size_t fd_max;           //현재 등록되어 있는 fd 중 가장 큰 값
  size_t fd_size;          // 현재 fd table의 크기
  struct file *running_file;

#ifdef USERPROG
  /* Owned by userprog/process.c. */
  uint64_t *pml4; /* Page map level 4 */
#endif
#ifdef VM
  /* Table for whole virtual memory owned by thread. */
  struct supplemental_page_table spt;
  uintptr_t ursp;
  struct list mmaps;
#endif

  /* Owned by thread.c. */
  struct intr_frame tf; /* Information for switching */
  unsigned magic;       /* Detects stack overflow. */
};

struct child_info {
  /* process wait, exit 용 */
  tid_t child_tid;              //자식의 tid
  int exit_status;              //자식의 exit status
  bool has_exited;              //종료 여부
  bool fork_success;            // fork 성공여부
  struct semaphore wait_sema;   //이 자식만을 위한 semaphore
  struct list_elem child_elem;  // child_list의 노드
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init(void);
void thread_start(void);

void thread_tick(void);
void thread_print_stats(void);

typedef void thread_func(void *aux);
tid_t thread_create(const char *name, int priority, thread_func *, void *);

void thread_block(void);
void thread_unblock(struct thread *);

struct thread *thread_current(void);
tid_t thread_tid(void);
const char *thread_name(void);

void thread_exit(void) NO_RETURN;
void thread_yield(void);

int thread_get_priority(void);
void thread_set_priority(int);

int thread_get_nice(void);
void thread_set_nice(int);
int thread_get_recent_cpu(void);
void thread_update_all_recent_cpu(void);
int thread_get_load_avg(void);
void thread_update_load_avg(void);
void do_iret(struct intr_frame *tf);

struct list *get_ready_list(void);
struct list *get_sleep_list(void);

void thread_update_all_priority(void);
void mlfqs_update_priority(struct thread *t);
bool thread_priority_less(const struct list_elem *, const struct list_elem *, void *);
bool is_not_idle(struct thread *);
int max_priority_mlfqs_queue(void);

struct thread *thread_get_by_tid(tid_t tid);  // userprog 추가

struct file *init_std();
struct file *get_std_in();   // userprog 추가
struct file *get_std_out();  // userprog 추가

#endif /* threads/thread.h */
