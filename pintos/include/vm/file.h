#ifndef VM_FILE_H
#define VM_FILE_H
#include "filesys/file.h"
#include "vm/vm_types.h"
// #include "vm/vm.h"

// struct page;
// enum vm_type;
struct thread;

struct file_page {
  struct file *file;
  off_t ofs;
  size_t read_bytes;
};

struct mmap_desc {
  void *start;
  size_t length;
  struct file *file;
  off_t ofs;
  bool writable;
  struct list_elem elem;
};

struct file_load_aux {
  struct file *file;
  off_t file_ofs;
  size_t read_bytes;
  size_t zero_bytes;
  struct list_elem elem;
  void *va;
};

void vm_file_init(void);
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva);
void *do_mmap(void *addr, size_t length, int writable, struct file *file, off_t offset);
void do_munmap(struct mmap_desc *desc);
struct mmap_desc *mmap_lookup(struct thread *t, void *addr);

#endif
