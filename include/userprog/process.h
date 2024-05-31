#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
//pid에 해당하는 자식 프로세스 찾기
struct thread* get_child_process(int pid);
void remove_child_process(struct thread* cp);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

//파일 디스크립터 다루기
int process_add_file(struct file* f);
struct file* process_get_file(int fd);
void process_close_file(int fd);

#endif /* userprog/process.h */
