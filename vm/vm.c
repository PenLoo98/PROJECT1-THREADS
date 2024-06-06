/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "include/lib/kernel/hash.h"
#include "include/threads/vaddr.h"

struct list frame_table; // 프레임 테이블

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	list_init(&frame_table); // 프레임 테이블 초기화
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

// 페이지의 가상 주소를 해시 값으로 변환
page_hash (const struct hash_elem *elem, void *aux UNUSED) {
	const struct page *page = hash_entry (elem, struct page, hash_elem); // hash_elem을 가진 부모 구조체인 page를 반환
	return hash_bytes (&page->va, sizeof page->va); // page의 va를 해싱하여 반환
}

// 두 페이지를 비교해서 더 이전 주소의 페이지를 반환
page_less (const struct hash_elem *hash_a, const struct hash_elem *hash_b, void *aux UNUSED) {
	const struct page *a = hash_entry (hash_a, struct page, hash_elem); // hash_elem을 가진 부모 구조체인 page를 반환
	const struct page *b = hash_entry (hash_b, struct page, hash_elem); // hash_elem을 가진 부모 구조체인 page를 반환
	return a->va < b->va; // a의 va가 b의 va보다 작으면 true 반환
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

		/* TODO: Insert the page into the spt. */
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
/* va와 대응되는 spt속에 있는 페이지 구조체를 찾아서 반환 */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function. */
	struct page *p = malloc(sizeof(struct page));
	p->va = pg_round_down(va); // va를 가장 가까운 페이지로 내림 -> 페이지 크기로 정렬된 주소를 얻게된다.
	struct hash_elem *e = hash_find(&spt->hash_spt, &p->hash_elem); // 해시테이블에서 va에 해당하는 페이지를 찾음
	free(p);
	p=NULL;

	if (e != NULL) { // 페이지를 찾았다면
		page = hash_entry(e, struct page, hash_elem); // 페이지 구조체를 반환
	}

	return page;
}

/* Insert PAGE into spt with validation. */
/* SPT에 페이지를 삽입*/
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	/* TODO: Fill this function. */
	// 이미 같은 가상 주소를 가진 페이지가 spt에 있는지 확인
	struct page *existing_page = spt_find_page(spt, page->va);
	if (existing_page ==NULL) { // 같은 가상 주소를 가진 페이지가 없다면
		// 페이지를 해시테이블에 삽입 -> 성공하면 NULL을 반환
		struct hash_elem *inserted_elem = hash_insert(&spt->hash_spt, &page->hash_elem); 
		if(inserted_elem == NULL){ // 삽입에 성공했다면
			succ = true;
		}
	}	

	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
/* 유저 풀에서 새로운 프레임을 가져온다. 필요하면 기존 페이지를 eviction하고 프레임을 반환한다. */
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
	/* TODO: Fill this function. */

	struct frame *frame = (struct frame *)malloc(sizeof(struct frame)); // 프레임 크기의 메모리 할당
	ASSERT (frame != NULL);

	frame->kva = palloc_get_page(PAL_USER); // 유저 풀에서 페이지 할당
	// frame -> kva = palloc_get_page(PAL_USER | PAL_ZERO); // 유저 풀에서 페이지 할당

	if (frame->kva == NULL) { // 페이지 할당에 실패했다면
		frame = vm_evict_frame(); // 페이지를 교체
	}
	else{
		list_push_back(&frame_table, &frame->frame_elem); // 프레임 테이블에 프레임 추가
	}

	frame->page = NULL; // 프레임의 페이지를 NULL로 초기화

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

	return vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
/* 가상 주소에 할당된 페이지를 요청한다.*/
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function */

	page = spt_find_page(&thread_current()->spt, va); // 가상 주소에 해당하는 페이지를 찾음
	if (page == NULL) { // 페이지를 찾지 못했다면
		return false;
	}
	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
/* 페이지를 요청하고 mmu를 설정한다. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	// 페이지 테이블 엔트리를 설정하여 페이지의 VA를 프레임의 PA에 매핑한다.
	if (!pml4_set_page (thread_current()->pml4, page->va, frame->kva, page->writable)){
		return false;
	}

	return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table */
/* 보조 페이지 테이블을 초기화한다. */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	// 해시테이블 초기화
	hash_init (&spt->hash_spt, page_hash, page_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
}
