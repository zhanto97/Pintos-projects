#include <stdbool.h>
#include <hash.h>
#include "filesys/file.h"

#ifndef VM_PAGE_H
#define VM_PAGE_H
#define MAX_STACK_SIZE (1 << 20) // 1MB

enum page_type{
	FILE,
	STACK,
	SWAPPED,
	MMAP
};

struct sup_page_table_entry 
{
	uint32_t* user_vaddr;
	enum page_type type;
	uint64_t access_time;			// last access time?

	bool read_only;					// true if read_only
	bool loaded;					// true if loaded to memory
	bool dirty;
	bool accessed;

	// for files
	struct file *file;
  	size_t offset;
  	size_t read_bytes;
  	size_t zero_bytes;

  	// for swapped out pages
  	size_t index;

  	// for MMAP
  	int map_id;
  	
	struct hash_elem h_elem;
};

void page_init (struct hash *spt);
struct sup_page_table_entry *
allocate_page_file(void *addr, struct file *f, bool read_only, 
			  off_t ofs, size_t read_bytes, size_t zero_bytes);
struct sup_page_table_entry *
allocate_page_mmap(void *addr, struct file *f, int map_id, 
			  off_t ofs, size_t read_bytes, size_t zero_bytes);
struct sup_page_table_entry *
allocate_page_stack(void *addr);

struct sup_page_table_entry *get_page(void *user_vaddr);
bool load_page(struct sup_page_table_entry *spte);
void destroy_spt(struct hash *spt);

#endif /* vm/page.h */
