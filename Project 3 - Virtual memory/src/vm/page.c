#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include <hash.h>
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"


static unsigned hash_func(const struct hash_elem *e, void *aux UNUSED);
static bool less_func(const struct hash_elem *e1, const struct hash_elem *e2, void *aux UNUSED);
static void action_func(struct hash_elem *e, void *aux UNUSED);
static bool load_page_file(struct sup_page_table_entry *spte);
static bool load_page_stack(struct sup_page_table_entry *spte);
static bool load_page_swap(struct sup_page_table_entry *spte);
static bool load_page_mmap(struct sup_page_table_entry *spte);

/*
 * Initialize supplementary page table
 */
void page_init(struct hash *spt)
{
	hash_init(spt, hash_func, less_func, NULL);
}

/*
 * Make new supplementary page table entry for addr 
 */
struct sup_page_table_entry *
allocate_page_file(void *addr, struct file *f, bool read_only, 
			  off_t ofs, size_t read_bytes, size_t zero_bytes)
{
	ASSERT(pg_ofs(addr) == 0);

	struct sup_page_table_entry *spte = (struct sub_page_table_entry *) malloc(sizeof(struct sup_page_table_entry));
	if (spte == NULL)
		return NULL;

	spte->user_vaddr = addr;
	spte->type = FILE;
	spte->map_id = -1;
	spte->read_only = read_only;
	spte->loaded = false;
	spte->file = f;
	spte->offset = ofs;
	spte->read_bytes = read_bytes;
	spte->zero_bytes = zero_bytes;
	return spte;
}

struct sup_page_table_entry *
allocate_page_mmap(void *addr, struct file *f, int map_id, 
			  off_t ofs, size_t read_bytes, size_t zero_bytes)
{
	ASSERT(pg_ofs(addr) == 0);

	struct sup_page_table_entry *spte = (struct sub_page_table_entry *) malloc(sizeof(struct sup_page_table_entry));
	if (spte == NULL)
		return NULL;

	spte->user_vaddr = addr;
	spte->type = MMAP;
	spte->map_id = map_id;
	spte->read_only = false;
	spte->loaded = false;
	spte->file = f;
	spte->offset = ofs;
	spte->read_bytes = read_bytes;
	spte->zero_bytes = zero_bytes;
	return spte;
}

struct sup_page_table_entry *
allocate_page_stack(void *addr){
	struct sup_page_table_entry *spte = (struct sup_page_table_entry *) malloc(sizeof(struct sup_page_table_entry));
	if (spte == NULL)
	    return NULL;
	spte->user_vaddr = pg_round_down(addr);
	spte->type = STACK;
	spte->read_only = false;
	spte->loaded = false;
	return spte;
}

struct sup_page_table_entry *get_page(void *user_vaddr){
	struct sup_page_table_entry spte;
  	spte.user_vaddr = pg_round_down(user_vaddr);

  	struct hash_elem *e = hash_find(&thread_current()->spt, &spte.h_elem);
  	if (e == NULL)
    	return NULL;
  	return hash_entry(e, struct sup_page_table_entry, h_elem);
}

bool load_page(struct sup_page_table_entry *spte){
  	if (spte->loaded)
  		return true;
  	switch (spte->type){
    	case FILE:
      		return load_page_file(spte);
    	case STACK:
      		return load_page_stack(spte);
      	case SWAPPED:
      		return load_page_swap(spte);
      	case MMAP:
      		return load_page_mmap(spte);
      	default:
      		return false;
    }
}

static bool load_page_file(struct sup_page_table_entry *spte){
	ASSERT(spte != NULL && spte->type == FILE);
	if (spte->loaded)
		return true;
	
	uint32_t *frame = allocate_frame(spte, spte->zero_bytes == PGSIZE);
	if (frame == NULL)
		return false;
	
	if (spte->read_bytes > 0)
    {
      acquire_filesys();
      off_t actual_read = file_read_at(spte->file, frame, spte->read_bytes, spte->offset);
      release_filesys();
      //printf("actual_read and read_bytes: %d and %d\n", actual_read, spte->read_bytes);
      if (actual_read != spte->read_bytes){
      	free_frame(frame);
      	printf("Did not read as much as expected\n");
      	return false;
      }
      //memset(frame + spte->read_bytes, 0, spte->zero_bytes);
    }
    if (!(pagedir_get_page (thread_current()->pagedir, spte->user_vaddr) == NULL
          && pagedir_set_page (thread_current()->pagedir, spte->user_vaddr, frame, !spte->read_only))){
    	free_frame(frame);
    	return false;
    }
    spte->loaded = true;
    return true;
}

static bool load_page_mmap(struct sup_page_table_entry *spte){
	ASSERT(spte != NULL && spte->type == MMAP);
	if (spte->loaded)
		return true;
	
	uint32_t *frame = allocate_frame(spte, spte->zero_bytes == PGSIZE);
	if (frame == NULL)
		return false;
	
	if (spte->read_bytes > 0)
    {
      acquire_filesys();
      off_t actual_read = file_read_at(spte->file, frame, spte->read_bytes, spte->offset);
      release_filesys();
      //printf("actual_read and read_bytes: %d and %d\n", actual_read, spte->read_bytes);
      if (actual_read != spte->read_bytes){
      	free_frame(frame);
      	printf("Did not read as much as expected\n");
      	return false;
      }
      //memset(frame + spte->read_bytes, 0, spte->zero_bytes);
    }
    if (!(pagedir_get_page (thread_current()->pagedir, spte->user_vaddr) == NULL
          && pagedir_set_page (thread_current()->pagedir, spte->user_vaddr, frame, !spte->read_only))){
    	free_frame(frame);
    	return false;
    }
    spte->loaded = true;
    return true;
}

static bool load_page_stack(struct sup_page_table_entry *spte){
	ASSERT(spte != NULL && spte->type == STACK);
	if (spte->loaded)
		return true;

	uint32_t *frame = allocate_frame(spte, true);
	if (frame == NULL)
		return false;

	if (hash_insert(&thread_current()->spt, &spte->h_elem) != NULL){
	    free_frame(frame);
	    return false;
	}
	
	if (!(pagedir_get_page (thread_current()->pagedir, spte->user_vaddr) == NULL
          && pagedir_set_page (thread_current()->pagedir, spte->user_vaddr, frame, true))){
    	free_frame(frame);
    	return false;
    }
	spte->loaded = true;
	return true;
}

static bool load_page_swap(struct sup_page_table_entry *spte){
	ASSERT(spte != NULL && spte->type == SWAPPED && !spte->loaded);

	uint32_t *frame = allocate_frame(spte, true);
	if (frame == NULL)
		return false;
	
	swap_in(spte->index, frame);
	if (!(pagedir_get_page (thread_current()->pagedir, spte->user_vaddr) == NULL
          && pagedir_set_page (thread_current()->pagedir, spte->user_vaddr, frame, true))){
    	free_frame(frame);
    	return false;
    }
    if(spte->file != NULL && spte->map_id == -1)
    	spte->type = FILE;
    else if (spte->file != NULL)
    	spte->type = MMAP;
    else
    	spte->type = STACK;
	spte->loaded = true;
	return true;
}

void destroy_spt(struct hash *spt){
 	hash_destroy(spt, action_func);
}

static unsigned hash_func(const struct hash_elem *e, void *aux UNUSED)
{
 	struct sup_page_table_entry *spte = hash_entry(e, struct sup_page_table_entry, h_elem);
 	return hash_int((int) spte->user_vaddr);
}

static bool less_func(const struct hash_elem *e1, const struct hash_elem *e2, void *aux UNUSED)
{
 	struct sup_page_table_entry *spte1 = hash_entry(e1, struct sup_page_table_entry, h_elem);
 	struct sup_page_table_entry *spte2 = hash_entry(e2, struct sup_page_table_entry, h_elem);
 	return (spte1->user_vaddr < spte2->user_vaddr)? true: false;
}

static void action_func(struct hash_elem *e, void *aux UNUSED)
{
 	struct sup_page_table_entry *spte = hash_entry(e, struct sup_page_table_entry, h_elem);
 	if (spte->loaded){
		free_frame(pagedir_get_page(thread_current()->pagedir, spte->user_vaddr));
		pagedir_clear_page(thread_current()->pagedir, spte->user_vaddr);
 	}
  	free(spte);
}