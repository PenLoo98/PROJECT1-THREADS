#include "userprog/syscall.h"
#include "threads/thread.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
//자식 프로세스를 검색하기 위해 추가한 헤더
#include "userprog/process.h"

#include <string.h>

//pid_t 내가 임의로 만든건데 틀릴 수 도 있음
typedef int pid_t;

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

//주소 유효성 검사 vaddr에 있는 함수 사용하기.
void check_address(void *addr);

//시스템 콜 넘버에 따른 로직 함수
void halt(void);
void exit(int status);
fork(const char *thread_name,struct intr_frame *if_);
int exec(const char *cmd_line);
int wait(pid_t pid);
bool create(const char* file, unsigned initial_size);
bool remove(const char* file);
int open(const char* file);
int filesize(int fd);
int read(int fd, void *buffer,unsigned size);
int write(int fd, void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);

static int read_by_size(void *buffer, int size);
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


void halt(void){
	power_off();
}

void exit(int status){
	struct thread *cur = thread_current();
	cur->exit_stat = status;
	printf("%s: exit(%d)\n",thread_name(),status);
	thread_exit();
}

pid_t fork(const char *thread_name,struct intr_frame *if_){
	return process_fork(thread_name, if_);
}

//자식 프로세스를 생성하고 프로그램을 실행시키는 시스템 콜
int exec(const char *cmd_line){
	char* temp_cmd = palloc_get_page(PAL_ZERO);
	if(temp_cmd == NULL){
		exit(-1);
	}
	strlcpy (temp_cmd, cmd_line, PGSIZE);
	int status = process_exec (temp_cmd);
	//이미 자식 프로세스 생성된 상태 그러나 로드는 안됨
	// printf("실행안됨???\n");
	//로드가 완료되면 계속 실행
	if(status == -1){
		// palloc_free_page(temp_cmd);
		return -1;
	}
	return 0;
}

int wait(pid_t pid){
	return process_wait(pid);
}

bool create(const char* file, unsigned initial_size){
	return filesys_create(file,initial_size);
}

bool remove(const char* file){
	return filesys_remove(file);
}

int open(const char* file){
	struct file* file_to_open = filesys_open(file);
	if(file_to_open==NULL){
		return -1;
	}
	int fd = process_add_file(file_to_open);
	return fd;
}

int filesize(int fd){
	struct file* file_to_get_size = process_get_file(fd);
	if(file_to_get_size==NULL){
		return -1;
	}
	return file_length(file_to_get_size);
}

static int read_by_size(void *buffer, int size){
	for(int i=0;i<size ;i++){
		*((char*)buffer+i) = input_getc();
	}
}
//size 만큼 파일에서 읽고 buffer에 채우는 함수, 실패 시 -1 return
//buffer의 실체는 언제 할당해놓지?
int read(int fd, void *buffer, unsigned size){
	struct file* file_to_read = process_get_file(fd);
	if(fd == 0){
		lock_acquire(&filesys_lock);
		read_by_size(buffer,size);
		lock_release(&filesys_lock);
		return size;
	}
	//허용되지 않는 파일에 접근하고자 할 때 
	else if(file_to_read == NULL){
		exit(-1);
	}
	lock_acquire(&filesys_lock);
	size = file_read (file_to_read, buffer, size);
	lock_release(&filesys_lock);
	return size;
}

//size 만큼 buffer에 있는 내용을 파일에 쓰는 함수, 실패 시 -1 return
int write(int fd, void *buffer, unsigned size){
	struct file* file_to_write = process_get_file(fd);

	if(fd == 1){
		lock_acquire(&filesys_lock);
		putbuf(buffer,size);
		lock_release(&filesys_lock);
		return size;
	}
	if(fd==0){
		exit(-1);
	}
	//허용되지 않는 파일에 접근하고자 할 때 
	else if(file_to_write == NULL){
		exit(-1);
	}

	
	lock_acquire(&filesys_lock);
	size = file_write(file_to_write, buffer, size);
	lock_release(&filesys_lock);
	return size;
}

//NULL이면 어떡하지?
void seek(int fd, unsigned position){
	struct file* file_to_seek = process_get_file(fd);
	file_seek(file_to_seek,position);
}

//NULL이면 어떡하지?
unsigned tell(int fd){
	struct file* file_to_tell = process_get_file(fd);
	return file_tell(file_to_tell);
}

void close(int fd){
	// printf("%p!!!!!!!!\n",process_get_file(fd));
	if(process_get_file(fd)==NULL){
		exit(-1);
	}
	process_close_file(fd);
}

void check_address(void *addr){
	if(!is_user_vaddr(*(void**)addr) || *(void**)addr==NULL){
		exit(-1);
	}

	//유저의 가상 메모리에 존재하지 않는 주소에 접근할 경우
	else if(pml4_get_page(thread_current()->pml4,*(void**)addr)==NULL){
		exit(-1);
	}
}

/* The main system call interface */
//시스템 콜 인스트럭션이 시스템 콜 핸들러를 호출한다. 
//인터럽트로 커널 모드로 들어오는 경우 유저 모드에 대한 정보를 저장해야 하기 때문에
//intr_frame에 저장 즉 intr_frame으로 user stack에 접근할 수 있고 number와 
void
syscall_handler (struct intr_frame *f UNUSED) {
	//각 시스템 콜 넘버에 대한 처리를 하는 함수를 따로 만들어야 될듯...
	
	int sys_call_number = f->R.rax;
	void *first_arg = &f->R.rdi;
	void *second_arg = &f->R.rsi;
	void *third_arg =&f->R.rdx;
	void *fourth_arg = &f->R.r10;
	void *fifth_arg = &f->R.r8;
	void *sixth_arg = &f->R.r9;
	void *return_val = NULL;
	//!!!!!!주소 체크해야됨!!
	switch(sys_call_number){
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			exit(*(int*)first_arg);
			break;
		case SYS_FORK:
			check_address(first_arg);
			f->R.rax = fork(*(char**)first_arg,f);
			break;
		case SYS_EXEC:
			check_address(first_arg);
			f->R.rax = exec(*(char**)first_arg);
			break;
		case SYS_WAIT:
			f->R.rax = wait(*(int*)first_arg);
			break;
		case SYS_CREATE:
			check_address(first_arg);
			f->R.rax = create(*(char**)first_arg, *(int*)second_arg);
			break;
		case SYS_REMOVE:
			check_address(first_arg);
			f->R.rax = remove(*(char**)first_arg);
			break;
		case SYS_OPEN:
			check_address(first_arg);
			f->R.rax = open(*(char**)first_arg);
			break;
		case SYS_FILESIZE:
			f->R.rax = filesize(*(int*)first_arg);
			break;
		case SYS_READ:
			check_address(second_arg);
			f->R.rax = read(*(int*)first_arg,*(char**)second_arg,*(unsigned int*)third_arg);
			break;
		case SYS_WRITE:
			check_address(second_arg);
			f->R.rax = write(*(int*)first_arg,*(char**)second_arg,*(unsigned int*)third_arg);
			break;
		case SYS_SEEK:
			seek(*(int*)first_arg,*(int*)second_arg);
			break;
		case SYS_TELL:
			f->R.rax = tell(*(int*)first_arg);
			break;
		case SYS_CLOSE:
			close(*(int*)first_arg);
			break;
	}

	// printf ("system call!\n");
	// thread_exit (); //syscall 호출 이후 다시 자기 코드 실행할 텐데 왜 exit?
}
