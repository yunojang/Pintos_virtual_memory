/* file.c: Implementation of memory backed file object (mmaped object). */

#include "include/threads/vaddr.h"
#include "threads/malloc.h"
#include "vm/vm.h"

static bool file_backed_swap_in(struct page *page, void *kva);
static bool file_backed_swap_out(struct page *page);
static void file_backed_destroy(struct page *page);

#define MIN(A, B) (A) > (B) ? (B) : (A);
#define MAX(A, B) (A) > (B) ? (A) : (B);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
    .swap_in = file_backed_swap_in,
    .swap_out = file_backed_swap_out,
    .destroy = file_backed_destroy,
    .type = VM_FILE,
};

/* The initializer of file vm */
void vm_file_init(void) {}

/* Initialize the file backed page */
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva) {
  /* Set up the handler */
  page->operations = &file_ops;

  struct file_page *file_page = &page->file;

  return true;
}

/* Swap in the page by read contents from the file. */
static bool file_backed_swap_in(struct page *page, void *kva) {
  struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool file_backed_swap_out(struct page *page) {
  struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void file_backed_destroy(struct page *page) {
  struct file_page *file_page UNUSED = &page->file;
}

static bool lazy_load_file(struct page *page, void *_aux) {
  struct file_load_aux *aux = _aux;

  if (file_read_at(aux->file, page->frame->kva, aux->read_bytes, aux->file_ofs) !=
      (int)aux->read_bytes) {
    free(aux);
    return false;
  }
  memset(page->frame->kva + aux->read_bytes, 0, aux->zero_bytes);

  // hex_dump((intptr_t)page->frame->kva, page->frame->kva, aux->read_bytes, true);
  // printf("ofs=%lld read=%zu zero=%zu\n", (long long)aux->file_ofs, aux->read_bytes,
  //        aux->zero_bytes);

  free(aux);
  return true;
}

static void clean_pages(struct list *aux_list) {
  struct list_elem *cur, *next;
  for (cur = list_begin(aux_list); cur != list_end(aux_list); cur = next) {
    next = list_next(cur);
    struct file_load_aux *aux = list_entry(cur, struct file_load_aux, elem);
    spt_remove_page(&thread_current()->spt, spt_find_page(&thread_current()->spt, aux->va));
    free(aux);
  }
}

/* Do the mmap */
void *do_mmap(void *addr, size_t length, int writable, struct file *file, off_t offset) {
  struct list aux_list;
  list_init(&aux_list);
  struct file *new_file = file_reopen(file);
  size_t f_len = file_length(new_file);

  off_t done = 0;
  while (done < length) {
    struct file_load_aux *aux = malloc(sizeof *aux);
    if (aux == NULL) goto fail;

    off_t remain = length - done;
    size_t page_bytes = MIN(remain, PGSIZE);
    off_t cur_ofs = offset + done;
    size_t file_left = MAX(f_len - cur_ofs, 0);

    aux->file = new_file;
    aux->file_ofs = cur_ofs;
    aux->read_bytes = MIN(page_bytes, file_left);
    aux->zero_bytes = PGSIZE - aux->read_bytes;
    aux->va = addr;
    list_push_back(&aux_list, &aux->elem);

    if (!vm_alloc_page_with_initializer(VM_FILE, addr + done, writable, lazy_load_file, aux)) {
      printf("vm_alloc_page_with_initializer failed\n");
      goto fail;
    }

    done += PGSIZE;
  }

  struct mmap_desc *desc = malloc(sizeof *desc);
  if (desc == NULL) goto fail;
  desc->start = addr;
  desc->length = pg_round_down(length);
  desc->file = new_file;

  list_push_back(&thread_current()->mmaps, &desc->elem);
  return addr;

fail:
  clean_pages(&aux_list);
  file_close(new_file);
  return NULL;
}

static bool find_start_va(const struct list_elem *elem, void *aux) {
  struct mmap_desc *desc = list_entry(elem, struct mmap_desc, elem);
  return desc->start == aux;
}

struct mmap_desc *mmap_lookup(struct thread *t, void *addr) {
  return list_find(&t->mmaps, find_start_va, addr);
}

/* Do the munmap */
void do_munmap(struct mmap_desc *desc) {
  struct thread *t = thread_current();
  void *va = desc->start;

  // list_remove(list_find(&t->mmaps, find_start_va, va));
  // spt_remove_page(&t->spt, spt_find_page(&t->spt, va));

  file_close(desc->file);
}
