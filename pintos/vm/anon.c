/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "devices/disk.h"
#include "include/threads/vaddr.h"
#include "kernel/bitmap.h"
#include "vm/vm.h"

#define SEC_PER_PAGE (PGSIZE / DISK_SECTOR_SIZE)
#define SEC_NO(SLOT_NO) ((SLOT_NO)*SEC_PER_PAGE)

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static struct bitmap *swap_bitmap;
static bool anon_swap_in(struct page *page, void *kva);
static bool anon_swap_out(struct page *page);
static void anon_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
    .swap_in = anon_swap_in,
    .swap_out = anon_swap_out,
    .destroy = anon_destroy,
    .type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void vm_anon_init(void) {
  /* TODO: Set up the swap_disk. */
  swap_disk = disk_get(1, 1);  // 실패시 panic

  size_t slot_len = disk_size(swap_disk) / SEC_PER_PAGE;
  swap_bitmap = bitmap_create(slot_len);
}

/* Initialize the file mapping */
bool anon_initializer(struct page *page, enum vm_type type, void *kva) {
  /* Set up the handler */
  page->operations = &anon_ops;
  struct anon_page *anon_page = &page->anon;

  anon_page->slot = BITMAP_ERROR;  // slot_no 초기화

  return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool anon_swap_in(struct page *page, void *kva) {
  struct anon_page *anon_page = &page->anon;

  vm_claim_page(page);
  // disk to memory
  disk_sector_t sec_base = SEC_NO(anon_page->slot);
  uint8_t *va = page->frame->kva;
  for (size_t i = 0; i < SEC_PER_PAGE; i++) {
    size_t done = DISK_SECTOR_SIZE * i;
    disk_read(swap_disk, sec_base + i, va + done);
  }
  bitmap_set(swap_bitmap, anon_page->slot, false);
  anon_page->slot = BITMAP_ERROR;
}

/* Swap out the page by writing contents to the swap disk. */
static bool anon_swap_out(struct page *page) {
  struct anon_page *anon_page = &page->anon;

  if (anon_page->slot == BITMAP_ERROR) {
    size_t slot = bitmap_scan_and_flip(swap_bitmap, 0, 1, false);
    if (slot == BITMAP_ERROR) {
      return false;
    }

    anon_page->slot = slot;
  }

  // memory to disk
  disk_sector_t sec_base = SEC_NO(anon_page->slot);
  uint8_t *va = page->frame->kva;
  for (size_t i = 0; i < SEC_PER_PAGE; i++) {
    size_t done = i * DISK_SECTOR_SIZE;
    disk_write(swap_disk, sec_base + i, va + done);
  }

  // page frame clear -> 프레임 지우면 안되고 재사용해야함

  return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void anon_destroy(struct page *page) {
  struct anon_page *anon_page = &page->anon;
  // anon_page - 클린업 해야할때
}
