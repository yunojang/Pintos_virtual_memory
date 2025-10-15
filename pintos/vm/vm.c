/* vm.c: Generic interface for virtual memory objects. */

#include "vm/vm.h"

#include "include/threads/vaddr.h"
#include "lib/kernel/hash.h"
#include "threads/malloc.h"
#include "threads/mmu.h"
#include "userprog/process.h"
#include "vm/inspect.h"

static struct list frame_table;
static struct list_elem *clock_hand;
static struct lock frame_lock;

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void) {
  vm_anon_init();
  vm_file_init();

  list_init(&frame_table);
  lock_init(&frame_lock);
  clock_hand = list_begin(&frame_table);
#ifdef EFILESYS /* For project 4 */
  pagecache_init();
#endif
  register_inspect_intr();
  /* DO NOT MODIFY UPPER LINES. */
  /* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type page_get_type(struct page *page) {
  int ty = VM_TYPE(page->operations->type);
  switch (ty) {
    case VM_UNINIT:
      return VM_TYPE(page->uninit.type);
    default:
      return ty;
  }
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
                                    vm_initializer *init, void *aux) {
  ASSERT(VM_TYPE(type) != VM_UNINIT)

  struct supplemental_page_table *spt = &thread_current()->spt;

  /* Check wheter the upage is already occupied or not. */
  if (spt_find_page(spt, upage) == NULL) {
    /* TODO: Create the page, fetch the initialier according to the VM type,
     * TODO: and then create "uninit" page struct by calling uninit_new. You
     * TODO: should modify the field after calling the uninit_new. */

    struct page *page = malloc(sizeof *page);
    /* TODO: Insert the page into the spt. */
    if (VM_TYPE(type) == VM_ANON) {
      uninit_new(page, upage, init, type, aux, anon_initializer);
    } else if (VM_TYPE(type) == VM_FILE) {
      uninit_new(page, upage, init, type, aux, file_backed_initializer);
    } else {
      return false;
    }

    page->pml4 = thread_current()->pml4;
    page->writable = writable;
    spt_insert_page(spt, page);

    return true;
  }

err:
  return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *spt_find_page(struct supplemental_page_table *spt, void *va) {
  /* TODO: Fill this function. */
  if (!is_user_vaddr(va)) return NULL;

  struct page key;

  key.va = pg_round_down(va);
  struct hash_elem *he = hash_find(&spt->hash_table, &key.hash_elem);
  return (he != NULL) ? hash_entry(he, struct page, hash_elem) : NULL;
}

/* Insert PAGE into spt with validation. */
bool spt_insert_page(struct supplemental_page_table *spt UNUSED, struct page *page UNUSED) {
  bool succ = false;
  /* TODO: Fill this function. */
  ASSERT(spt != NULL && page != NULL);
  ASSERT(page->va == pg_round_down(page->va));

  succ = (hash_insert(&spt->hash_table, &page->hash_elem) == NULL);
  return succ;
}

static void frame_release(struct page *page) {
  pml4_clear_page(thread_current()->pml4, page->va);  // pml4 매핑 해제 (by va)
  palloc_free_page(page->frame->kva);                 // frame의 kva의 page_alloc 해제
  free(page->frame);                                  // frame 객체 free
  page->frame = NULL;                                 // frame 포인터 지우기
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page) {
  if (page->frame != NULL) {
    frame_release(page);  // frame release
  }
  hash_delete(&spt->hash_table, &page->hash_elem);  // spt table hash 에서 제거 - 중요
  vm_dealloc_page(page);                            // dealloc_page flow

  return true;
}

static struct frame *clock_next(void) {
  if (list_end(&frame_table) != clock_hand) {
    clock_hand = list_next(clock_hand);
  } else {
    clock_hand = list_begin(&frame_table);
  }

  struct frame *next = list_entry(clock_hand, struct frame, elem);
  return next;
}

/* Get the struct frame, that will be evicted. */
static struct frame *vm_get_victim(void) {
  struct frame *victim = NULL;
  /* TODO: The policy for eviction is up to you. */
  while (true) {
    victim = clock_next();
    if (victim->pinned) continue;

    void *va = victim->page->va;
    if (pml4_is_accessed(victim->page->pml4, va)) {
      pml4_set_accessed(victim->page->pml4, va, false);  // 접근 비트 끄기
      continue;                                          // second-chance
    }
    return victim;
  }
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *vm_evict_frame(void) {
  /* TODO: swap out the victim and return the evicted frame. */
  struct frame *victim = vm_get_victim();
  bool succ;

  victim->pinned = true;
  succ = swap_out(victim->page);

  pml4_clear_page(victim->page->pml4, victim->page->va);
  victim->page->frame = NULL;
  victim->page = NULL;

  ASSERT(victim->page == NULL)

  if (!succ) return NULL;
  return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/

static struct frame *vm_get_frame(void) {
  struct frame *frame = NULL;
  /* TODO: Fill this function. */

  int8_t *kaddr = palloc_get_page(PAL_USER);
  if (kaddr == NULL) {  // palloc 실패 시 evict로 프레임 사용
    if ((frame = vm_evict_frame()) == NULL) {
      PANIC("vm_get_frame: vm_evict_frame() failed");
    }
  } else {  // 성공 시 새 프레임 구조체 할당 후 초기화
    if ((frame = malloc(sizeof *frame)) == NULL) {
      palloc_free_page(kaddr);
      PANIC("vm_get_frame:  malloc(sizeof *frame) failed");
    }

    frame->kva = kaddr;
    frame->page = NULL;
    frame->pinned = true;
    list_push_back(&frame_table, &frame->elem);  // 새 프레임은 테이블에
  }

  ASSERT(frame != NULL);
  ASSERT(frame->page == NULL);

  return frame;
}

/* Growing the stack. */
static bool vm_stack_growth(void *addr UNUSED) {
  if (!vm_alloc_page(VM_ANON | VM_MARKER_0, addr, true)) return false;
  return vm_claim_page(addr);
}

/* Handle the fault on write_protected page */
static bool vm_handle_wp(struct page *page UNUSED) {}

bool valid_stack_growth(void *addr, struct intr_frame *f, bool user) {
  // u -> k 때만 프레임 저장, 커널 발생 fault는 rsp 별도 처리
  uintptr_t rsp = user ? f->rsp : thread_current()->ursp;
  bool near_rsp = addr == rsp - 8 || addr >= rsp;
  return (addr < USER_STACK) && near_rsp && (addr >= MIN_STACK_ADDR);
}

/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED, bool user UNUSED,
                         bool write UNUSED, bool not_present UNUSED) {
  struct supplemental_page_table *spt = &thread_current()->spt;
  struct page *page = NULL;
  /* TODO: Validate the fault */
  /* TODO: Your code goes here */
  if (addr == NULL || !is_user_vaddr(addr)) return false;  // addr valid
  if (!not_present) return false;                          // 보호 위반 여부

  void *va = pg_round_down(addr);
  page = spt_find_page(&thread_current()->spt, va);

  if (!page) {
    if (!valid_stack_growth(addr, f, user)) return false;  //  스택 성장 가능 체크
    return vm_stack_growth(va);                            // stack growth
  }
  if (write && !page->writable) return false;  // write 동작에, 페이지가 지원안할 때
  return vm_do_claim_page(page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page) {
  destroy(page);
  free(page);
}

/* Claim the page that allocate on VA. */
bool vm_claim_page(void *va UNUSED) {
  struct page *page = NULL;
  /* TODO: Fill this function */
  if ((page = spt_find_page(&thread_current()->spt, va)) == NULL) {
    return false;
  }

  return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
static bool vm_do_claim_page(struct page *page) {
  struct frame *frame = vm_get_frame();

  /* Set links */
  frame->page = page;
  page->frame = frame;

  /* TODO: Insert page table entry to map page's VA to frame's PA. */
  bool succ = pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable);
  frame->pinned = false;

  if (!succ) {
    frame->page = NULL;
    page->frame = NULL;
    palloc_free_page(frame->kva);
    free(frame);
    return false;
  }

  return swap_in(page, frame->kva);
}

static uint64_t hash_func(const struct hash_elem *e, void *aux) {
  struct page *p = hash_entry(e, struct page, hash_elem);
  void *key = p->va;
  return hash_bytes(&key, sizeof key);
}

static bool less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux) {
  const struct page *pa = hash_entry(a, struct page, hash_elem);
  const struct page *pb = hash_entry(b, struct page, hash_elem);
  return pa->va < pb->va;  // less
}

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED) {
  ASSERT(spt != NULL);

  // spt->hash_table = malloc(sizeof *spt->hash_table);
  hash_init(&spt->hash_table, hash_func, less_func, NULL);
}

// void uninit_page_copy() {}
// void anon_page_copy() {}

/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
                                  struct supplemental_page_table *src UNUSED) {
  bool succ = false;
  ASSERT(&thread_current()->spt == dst);  // dst가 현재 쓰레드여야함

  struct hash_iterator i;
  hash_first(&i, &src->hash_table);

  while (hash_next(&i)) {
    struct page *src_page = hash_entry(hash_cur(&i), struct page, hash_elem);
    enum vm_type type = VM_TYPE(src_page->operations->type);
    bool writable = src_page->writable;
    struct uninit_page uninit = src_page->uninit;
    struct load_aux *old_aux = src_page->uninit.aux;
    void *va = src_page->va;
    ASSERT(va == pg_round_down(va));

    switch (type) {
      case VM_UNINIT:
        struct load_aux *new_aux = malloc(sizeof *new_aux);
        *new_aux = *old_aux;
        new_aux->file = file_reopen(old_aux->file);  // reopen(pos 복사안함) <-> duplicate
        if (!vm_alloc_page_with_initializer(uninit.type, va, writable, uninit.init, new_aux)) {
          return false;
        }
        succ = vm_claim_page(va);  // 즉시 클레임
        break;
      case VM_ANON:
        bool is_in = src_page->frame != NULL;

        if (!vm_alloc_page(VM_ANON, va, writable)) {
          return false;
        }
        succ = vm_claim_page(va);
        memcpy(spt_find_page(dst, va)->frame->kva, src_page->frame->kva, PGSIZE);
        break;
      default:
        break;
    }
  }

  return succ;
}

static void page_destory(struct hash_elem *e, void *aux) {
  struct page *p = hash_entry(e, struct page, hash_elem);
  destroy(p);
}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED) {
  /* TODO: Destroy all the supplemental_page_table hold by thread and
   * TODO: writeback all the modified contents to the storage. */
  hash_clear(spt, page_destory);
}
