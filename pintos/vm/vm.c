/* vm.c: Generic interface for virtual memory objects. */

#include "vm/vm.h"

#include "include/threads/vaddr.h"
#include "lib/kernel/hash.h"
#include "threads/malloc.h"
#include "threads/mmu.h"
#include "userprog/process.h"
#include "vm/inspect.h"

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void) {
  vm_anon_init();
  vm_file_init();
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

void spt_remove_page(struct supplemental_page_table *spt, struct page *page) {
  vm_dealloc_page(page);
  return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *vm_get_victim(void) {
  struct frame *victim = NULL;
  /* TODO: The policy for eviction is up to you. */

  return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *vm_evict_frame(void) {
  struct frame *victim UNUSED = vm_get_victim();
  /* TODO: swap out the victim and return the evicted frame. */

  return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *vm_get_frame(void) {
  struct frame *frame = NULL;
  /* TODO: Fill this function. */

  int8_t *kaddr = palloc_get_page(PAL_USER);
  if (kaddr == NULL) {
    PANIC("vm_get_frame: palloc_get_page(PAL_USER) failed (todo: eviction)");
  }
  if ((frame = malloc(sizeof *frame)) == NULL) {
    PANIC("vm_get_frame:  malloc(sizeof *frame) failed");
  }

  frame->kva = kaddr;
  frame->page = NULL;

  ASSERT(frame != NULL);
  ASSERT(frame->page == NULL);
  return frame;
}

/* Growing the stack. */
static void vm_stack_growth(void *addr UNUSED) {}

/* Handle the fault on write_protected page */
static bool vm_handle_wp(struct page *page UNUSED) {}

/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED, bool user UNUSED,
                         bool write UNUSED, bool not_present UNUSED) {
  struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
  struct page *page = NULL;
  /* TODO: Validate the fault */
  /* TODO: Your code goes here */
  if (!user) return false;                                 // 유저 모드에서만 처리
  if (addr == NULL || !is_user_vaddr(addr)) return false;  // addr valid - 유저 VA가 아닐때
  if (!not_present) return false;                          // 보호 위반 여부

  void *va = pg_round_down(addr);
  page = spt_find_page(&thread_current()->spt, va);
  if (!page) return false;  // SPT에 페이지 없음

  //  스택 성장 처리

  if (write && !page->writable) return false;  // write 동작인데 페이지가 지원안할 때

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
