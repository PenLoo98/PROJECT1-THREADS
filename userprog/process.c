#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef VM
#include "vm/vm.h"
#endif

#define WORD 8
#define ROUND_TO_WORD(x) (((x/WORD)*WORD)+WORD)

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);
static void parse_command(char* file_name,char** parse,int* count);
static void argument_stack(char** parse, int count,struct intr_frame* _if);
/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	//로드와 경쟁상태가 발생함.
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);
	//file_name으로 parsing한 것으로 넘기자.
	char* save_ptr;
	char* file_name_not_arg = strtok_r (file_name, " ", &save_ptr);
	/* Create a new thread to execute FILE_NAME. */
	//fn_copy는 aux로 매개변수 
	int b= 0;
	tid = thread_create (file_name_not_arg, PRI_DEFAULT, initd, fn_copy);

	//자식 생성에 문제가 생기지 않은 경우 자식의 로드를 기다린다.
	if(tid!=TID_ERROR){
		struct thread* child = get_child_process(tid);
		// sema_down(&child->load_sema);
	}

	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);
	return tid;
}

//process_create_initD과 다른 점은 무조건 기다리지 않는 것이다. 

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();
	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}



/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	/* Clone current thread to new thread.*/
	struct thread *parent = thread_current();
	tid_t child_tid;
    
	/* parent_if에 유저스택 정보 담기*/
	memcpy(&parent->parent_if,if_,sizeof(struct intr_frame));//if_는 유저스택, 이 정보를(userland context)를 Parent_if에 넘겨준다
    
	/* 자식 스레드를 생성 */
	child_tid=thread_create (name,	// function함수를 실행하는 스레드 생성
			PRI_DEFAULT, __do_fork, thread_current ()); //부모스레드는 현재 실행중인 유저 스레드
            
	if (child_tid==TID_ERROR)
		return TID_ERROR;
        
	/* Project 2 fork()*/
	struct thread *child = get_child_process(child_tid);
	sema_down(&child->load_sema);

    if (child->exit_stat== -1)
    {
        return TID_ERROR;
    }
    
	return child_tid;
} 

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	//pte 부모의 페이지 테이블 엔트리 포인터, va 가상주소, aux 부모 스레드
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	//va가 유저 공간이 아닌 경우 parent page가 kernel page가 아닐까
	if(is_kern_pte(pte)){
		return true;
	}
	if(is_user_pte(pte)){
		// printf("zzz\n");
	}

	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page (parent->pml4, va);

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	newpage = palloc_get_page(PAL_USER|PAL_ZERO);

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	memcpy (newpage,parent_page,PGSIZE);
	writable = is_writable(pte);

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if;
	parent_if = &parent->parent_if;
	bool succ = true;

	/* 1. Read the cpu context to local stack. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));
	//set return value
	// printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!%d\n",if_.R.rax);

	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate (current);
	// printf("%d\n",PGSIZE / sizeof(uint64_t *));
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	//page table이 잘못된 것인가???...
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/

	struct file** par_fdt = parent->file_discriptor_table;

	for(int i=2;i<parent->next_fd;i++){
		struct file *file = *(par_fdt+i);
		if(file==NULL){
			continue;
		}
		process_add_file(file_duplicate(*(par_fdt+i)));
	}

	current->next_fd = parent->next_fd;
	// printf("%d!!!!!!!!!\n",current->next_fd);

	if_.R.rax = 0;

	process_init ();

	sema_up(&current->load_sema);
	/* Finally, switch to the newly created process. */
	if (succ){
		do_iret (&if_);
	}
error:
	sema_up(&current->load_sema);
	thread_exit ();
}

struct thread* 
get_child_process(int pid){
	struct list* child_list = &thread_current()->child_list;
	struct list_elem* e;

	for (e = list_begin (child_list); e != list_end (child_list); e = list_next (e)){
		struct thread* child = list_entry(e,struct thread,child_elem);
		if(child->tid == pid){
			return child;
		}
	}
	return NULL;
}

//자식 프로세스 제거 
void 
remove_child_process(struct thread* cp){
	list_remove(&cp->child_elem);
	palloc_free_page(cp);
}

//인자를 나누고 인자의 개수를 센다.
static void 
parse_command(char* file_name,char** parse,int* count){
	*count = 0;
	char* arg,* save_ptr;
	for (arg = strtok_r (file_name, " ", &save_ptr); arg != NULL;
   		arg = strtok_r (NULL, " ", &save_ptr)){
		*parse = arg;
		parse++;
		(*count)++;
   	}
}

//인자의 길이의 합을 4의 배수로 만들고, stack에 넣는다.
static void
argument_stack(char** parse, int count, struct intr_frame* _if){
	//char들을 stack에 삽입
	int total_length = 0;
	// int arg_length;
	//arg를 가르킬 포인터
	char* arg_ptr[count];
	for(int i = count-1;i>-1;i--){
		for(int j= strlen(parse[i]);j>-1;j--){
			//스택 주소 감소
			_if->rsp = _if->rsp-sizeof(char);
			//스택에 char 추가
			*(char*)(_if->rsp) = parse[i][j];
			total_length ++;
		}
		//stack에 추가해줄 인수 추가
		arg_ptr[i] = _if->rsp;
	}
	
	// word-align
	int pad = (_if->rsp)%WORD;
	for(int i=0;i<pad;i++){
		_if->rsp -= sizeof(char);
		*(uint8_t *)_if->rsp = 0;
	}

	//push address of arg to stack
	_if->rsp = _if->rsp - sizeof(char *);
	*(char**)_if->rsp = NULL;
	for(int i=0;i<count;i++){
		_if->rsp -= sizeof(char *);
		//stack에 char*를 저장
		*(char**)_if->rsp  = arg_ptr[count-i-1];
	}
	// for(int i=0;i<count;i++){
	// 	_if->rsp -= sizeof(char *);
	// 	//stack에 char*를 저장
	// 	*(char**)_if->rsp  = arg_ptr[i];
	// }

	//char** argv에 넣어줄 매개변수
	_if->R.rsi = _if->rsp;
	//int argc에 넣어줄 변수
	_if->R.rdi = count;

	//fake return address
	_if->rsp = _if->rsp - sizeof(void*);
	*(void**)_if->rsp = NULL;
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int
process_exec (void *f_name) {
	char *file_name = f_name;
	bool success;
	int count = 0;
	char* parse[128];
	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	//initialize intrupt stack
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	//현재 프로세스의 resource를 반납한다.
	process_cleanup ();

	//태스크를 쪼갠 토큰들을 parse에 담고 개수를 count에 저장한다.
	parse_command(f_name,parse,&count);

	/* And then load the binary */
	success = load (parse[0], &_if);

	if(success){
		//stack에 전달할 argument 삽입 
		argument_stack(parse,count,&_if);
	}

	/* If load failed, quit. */
	palloc_free_page (file_name);
	if (!success){
		thread_current()->is_memory_loaded = false;
		return -1;
	}
	thread_current()->is_memory_loaded = false;
	/* Start switched process. */
	//로드한 내용들을 실행
	//getting out of kernel
	do_iret (&_if);
	NOT_REACHED ();
}

//스레드가 죽기를 기다리고 죽으면 exit status를 return한다.
/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
// 직진 자식이 아니거나 다른 자식을 wait하고 있는 도중에는 wait함수를 호출할 수 없다.
int
process_wait (tid_t child_tid UNUSED) {
	// printf("wait for %d\n",child_tid);
	struct thread* child = get_child_process(child_tid);

	//잘못된 자식을 호출할때는 오류를 잡는 것을 구현했으나 wait함수 구현x 해야됨 나중에...
	if(child==NULL){
		return -1;
	}
	// printf("wait에서의 자식 : %s\n",child->name);
	// printf("wait에서 자식 sema 값 %d\n",child->exit_sema.value);
	//자식이 exit를 반환하면 sema에서 탈출
	sema_down(&child->exit_sema);
	//scheduler가 자식 프로세스의 상태를 가져오기 전에 free 되면 어캄?
	int child_exit_stat = child->exit_stat;
	//자식 종료했으니 자식 리스트에서 제거
	list_remove(&child->child_elem);
	sema_up(&child->wait_dying_sema);
	//자식 프로세스 디스크립터는 부모가 지워줄 때 까지 삭제되면 안됨.
	// palloc_free_page(child);
	return child_exit_stat;
}

/* Exit the process. This function is called by thread_exit (). */
//proecss가 exit할때 부모를 꺼내고 부모의 리스트에서 자신을 제거하고, 부모의 스레드 구조체에
//자신의 argument 값을 전달한다.
void
process_exit (void) {
	struct thread* current = thread_current();
	//파일과 관련된 메모리 해제 
	int last_fd = current -> next_fd - 1;
	struct file** files_to_eleminate = current->file_discriptor_table;

	for(int i=2;i<=last_fd;i++){
		if(*(files_to_eleminate+i)!=NULL){
			file_close(*(files_to_eleminate+i));
		}
	}

	free(current->file_discriptor_table);
	// palloc_free_multiple(current->file_discriptor_table, 2);
	
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */
	process_cleanup ();
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR
static bool setup_stack (struct intr_frame *if_) ;
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	/* Open executable file. */
	// printf("%s\n",file_name);
	// printf("%d\n",strcmp(file_name,"child-simple"));
	file = filesys_open (file_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	/* Read and verify executable header. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}
	/* Set up stack. */
	//rsp에 스택포인터
	if (!setup_stack (if_))
		goto done;

	/* Start address. */
	//function entry point
	if_->rip = ehdr.e_entry;
	file_deny_write(file);

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */

	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	// file_close (file);
	thread_current()->executable = file;
	return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

int process_add_file(struct file *f){
	struct thread* current = thread_current();

	int current_fd = current->next_fd;
	*(current-> file_discriptor_table + current_fd) = f;

	current->next_fd = (current->next_fd)+1;
	return current_fd;
}

struct file* process_get_file(int fd){
	if(fd>100){
		return NULL;
	}
	return *((thread_current()->file_discriptor_table)+fd);
}

void process_close_file(int fd){
	struct file* file_to_close = process_get_file(fd);

	file_close(file_to_close);

	*(thread_current()->file_discriptor_table+fd) = NULL;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
		//kpage(kernel page를 채우는 것인가 보다.)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

// 사용자 가상 메모리 주소인 UPAGE를 KERNEL의 주소공간인 KPAGE로 mapping한 것을
//page table에 추가한다.
/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	//user page는 없어야 하고 kernel page는 있어야 함.
	//아마 kernel page의 page table로 mapping한다는 것인가?
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		void *aux = NULL;
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */

	return success;
}
#endif /* VM */
