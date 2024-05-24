#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include <filesys/filesys.h>

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void check_address(void *addr);
void get_argument(void *rsp, int *arg, int count);
void halt(void);
void exit(int status);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);

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

/* The main system call interface */
/* 유저 스택에 저장된 시스템콜 번호에 따라 시스템콜을 호출하는 함수 */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	printf ("system call!\n");
	thread_exit ();
}

/* 주소유효성 검사: 포인터가 가리키는 주소가 사용자 영역에 속해있는지 확인*/
void check_address(void *addr){
	if(addr == NULL || addr < (void *)0x08048000 || addr > (void *)0xc0000000){
		exit(-1);
	}
}

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
	printf("%s: exit(%d)\n", curr_thread->name, THREAD_DYING);
	thread_exit();
}

/* 파일을 생성하는 시스템 콜 */
bool create(const char *file, unsigned initial_size){
	check_address((void*)file); // void*로 묵시적 형변환을 하면 const속성이 사라질 수 있다.
	return filesys_create(file, initial_size);
}

/* 파일을 삭제하는 시스템 콜 */
bool remove(const char *file){
	check_address((void*)file); // void*로 묵시적 형변환을 하면 const속성이 사라질 수 있다.
	return filesys_remove(file);
}