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
#include "include/filesys/off_t.h"
struct page *check_address(void *addr);
void check_buffer(void *buffer, size_t size, bool writable);
void* mmap(void *addr, size_t length, int writable, int fd, off_t offset);
void munmap(void *addr);
extern struct lock filesys_lock;
#endif