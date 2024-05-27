#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
//자식 프로세스를 검색하기 위해 추가한 헤더
#include "userprog/process.h"

//pid_t 내가 임의로 만든건데 틀릴 수 도 있음
typedef int pid_t;

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

//주소 유효성 검사 vaddr에 있는 함수 사용하기.
void check_address(void *addr);

//시스템 콜 넘버에 따른 로직 함수
void halt(void);
void exit(int status);
pid_t fork(const char *thread_name);
int exec(const char *cmd_line);
int wait(pid_t pid);
bool create(const char* file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer,unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);

void halt(void){
	power_off();
}

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
}

//자식 프로세스를 생성하고 프로그램을 실행시키는 시스템 콜
int exec(const char *cmd_line){
	tid_t tid = process_create_initd (cmd_line);
	//이미 자식 프로세스 생성된 상태 그러나 로드는 안됨
	struct thread* child = get_child_process(tid);
	if(child != NULL){
		return -1;
	}
	//로드가 완료되면 계속 실행
	sema_down(&child->load_sema);
	return tid;
}

int wait(pid_t pid){
	process_wait(pid);
}

void exit(int status){
	struct thread *cur = thread_current();
	cur->exit_stat = status;
	printf("%s: exit(%d)\n",cur->name,status);
	thread_exit();
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
		case SYS_EXEC:
			exec((char*)first_arg);
			break;
		case SYS_EXIT:
			exit(*(int*)first_arg);
			break;
	}

	printf ("system call!\n");
	thread_exit (); //syscall 호출 이후 다시 자기 코드 실행할 텐데 왜 exit?
}
