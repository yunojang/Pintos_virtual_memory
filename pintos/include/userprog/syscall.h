#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

/* Process identifier. */
typedef int pid_t;
#define PID_ERROR ((pid_t)-1)

extern struct lock filesys_lock; /* filesys 함수 접근 시 동기화 용 */

void syscall_init(void);
void system_exit(int status);
void system_close(int fd);

#endif /* userprog/syscall.h */
