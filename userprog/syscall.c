#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

// USERPROG 추가
#include "threads/palloc.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

// Project 3 - Anonymous Page
#ifndef VM
void check_address(void *addr);
#else
/** #Project 3: Anonymous Page */
struct page *check_address(void *addr);
#endif

void get_argument(void *rsp, int *arg, int count);
void halt(void);
void exit(int status);
int fork (const char *thread_name, struct intr_frame *f);
int exec (const char *cmd_line);
int wait (int pid);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
	lock_init(&filesys_lock);
}

/* The main system call interface */
/* 유저 스택에 저장된 시스템콜 번호에 따라 시스템콜을 호출하는 함수 */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	// printf("system call!\n");
	// thread_exit();
	int syscall_num = f->R.rax;
	switch (syscall_num) {
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			exit(f->R.rdi);
			break;
		case SYS_FORK:
			f->R.rax = fork(f->R.rdi, f);
			break;
		case SYS_EXEC:
			f->R.rax = exec(f->R.rdi);
			break;
		case SYS_WAIT:
			f->R.rax = wait(f->R.rdi);
			break; 
		case SYS_CREATE:
			f->R.rax = create(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			f->R.rax = remove(f->R.rdi);
			break;
		case SYS_OPEN:
			f->R.rax = open(f->R.rdi);
			break;
		case SYS_FILESIZE:
			f->R.rax = filesize(f->R.rdi);
			break;
		case SYS_READ:
			f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;  
		case SYS_WRITE:      
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:
			seek(f->R.rdi, f->R.rsi);
			break;
		case SYS_TELL:
			f->R.rax = tell(f->R.rdi);
			break;
		case SYS_CLOSE:
			close(f->R.rdi);
			break;
		default:
			exit(-1);
			break;
  	}
}

/* 주소유효성 검사: 포인터가 가리키는 주소가 사용자 영역에 속해있는지 확인*/
#ifndef VM
/* Project 2 - System Call */
void check_address(void *addr){
	if(addr == NULL || is_kernel_vaddr(addr) || !is_user_vaddr(addr)){
		exit(-1);
	}
	if(pml4_get_page(thread_current()->pml4, addr) == NULL){
		exit(-1);
	}
}
#else
/** Project 3-Anonymous Page */
struct page *check_address(void *addr) {
    struct thread *curr = thread_current();

    if (is_kernel_vaddr(addr) || addr == NULL){
        exit(-1);
	}
	if (!spt_find_page(&curr->spt, addr)){
		exit(-1);
	}

    return spt_find_page(&curr->spt, addr);
}
#endif

/* 유저 스택에 있는 인자들을 커널에 저장 
   64비트 환경에서는 86-64 Call Convention(ABI)에 따라 인자들이 스택에 저장된다.
   rsp는 스택 포인터를 가리키는 레지스터로, 유저 스택의 주소를 가리킨다. */
void get_argument(void *rsp, int *arg, int count){
	int i;
	for(i = 0; i < count; i++){
		check_address(rsp);
		arg[i] = *(int *)rsp;
		rsp += 8;
	}
}

/* pintos를 종료시키는 시스템 콜 */
void halt(void){
	power_off();
}

/* 현재 프로세스를 종료시키는 시스템 콜 */
void exit(int status){
	struct thread *curr_thread = thread_current();
	curr_thread->exit_status = status;
	printf("%s: exit(%d)\n", curr_thread->name, status);
	thread_exit();
}

int fork (const char *thread_name, struct intr_frame *f){
	check_address(thread_name); // void*로 묵시적 형변환을 하면 const속성이 사라질 수 있다.
	return process_fork(thread_name, f);
}

/* 새로운 프로그램을 실행시키는 시스템 콜 */
int exec (const char *cmd_line){
	check_address(cmd_line); // void*로 묵시적 형변환을 하면 const속성이 사라질 수 있다.

	char *cmd_line_copy = palloc_get_page(0);
	if(cmd_line_copy == NULL){
		exit(-1);
	}
	strlcpy(cmd_line_copy, cmd_line, PGSIZE);
	if(process_exec(cmd_line_copy) == -1){
		exit(-1);
	}
}

/* 자식 프로세스가 종료될 때까지 부모 프로세스를 대기시키는 시스템 콜 */
int wait (int pid){
	return process_wait(pid);
}

/* 파일을 생성하는 시스템 콜 */
bool create(const char *file, unsigned initial_size){
	check_address((void*)file); // void*로 묵시적 형변환을 하면 const속성이 사라질 수 있다.
	return filesys_create(file, initial_size);
}

/* 파일을 삭제하는 시스템 콜 */
bool remove(const char *file){
	check_address(file); // void*로 묵시적 형변환을 하면 const속성이 사라질 수 있다.
	return filesys_remove(file);
}

int open(const char *file){
	check_address(file); // void*로 묵시적 형변환을 하면 const속성이 사라질 수 있다.
	struct thread *cur = thread_current();
	struct file *f = filesys_open(file);
	if (f){
		for (int i=2 ; i<128; i++){
			if (!cur->fd_table[i]){
				cur->fd_table[i] = f;
				cur->next_fd = i+1;
				return i;
			}
		}
		file_close(f);
	}
	return -1;
}

int filesize(int fd){
	struct file *file = thread_current()->fd_table[fd];
	if (file){
		return file_length(file);
	}
	return -1;
}

int read(int fd, void *buffer, unsigned size){
	check_address(buffer);
	if (fd == 1){
		return -1;
	}

	if (fd == 0){
		lock_acquire(&filesys_lock);
		int bytes = input_getc();
		lock_release(&filesys_lock);
		return bytes;
	}
	struct file *file = thread_current()->fd_table[fd];
	if (file){
		lock_acquire(&filesys_lock);
		int read_bytes = file_read(file, buffer, size);
		lock_release(&filesys_lock);
		return read_bytes;
	}
	return -1;
}

int write(int fd, const void *buffer, unsigned size){
	check_address(buffer);

	if (fd == 0){
		return -1;
	}

	if (fd == 1){
		lock_acquire(&filesys_lock);
		putbuf(buffer, size);
		lock_release(&filesys_lock);
		return size;
	}

	struct file *file = thread_current()->fd_table[fd];
	if (file){
		lock_acquire(&filesys_lock);
		int write_bytes = file_write(file, buffer, size);
		lock_release(&filesys_lock);
		return write_bytes;
	}
}

void seek(int fd, unsigned position){
	struct file *find_file = thread_current()->fd_table[fd];
	if (find_file){
		file_seek(find_file, position);
	}
}

unsigned tell(int fd){
	struct file *find_file = thread_current()->fd_table[fd];
	if (find_file){
		return file_tell(find_file);
	}
}

void close(int fd){
	struct file *file = thread_current()->fd_table[fd];
	if (file){
		lock_acquire(&filesys_lock);
		thread_current()->fd_table[fd] = NULL;
		file_close(file);
		lock_release(&filesys_lock);
	}
}