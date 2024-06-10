#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/synch.h"
struct lock filesys_lock;

void syscall_init (void);

#endif /* userprog/syscall.h */

#ifndef VM
void check_address(void *addr);
#else
/** #Project 3: Anonymous Page */
struct page *check_address(void *addr);
#endif