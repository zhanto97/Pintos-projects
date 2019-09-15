#include <stdbool.h>
#include "threads/synch.h"
#include "vm/page.h"
#include <list.h>

#ifndef VM_FRAME_H
#define VM_FRAME_H

struct list frame_table;			// frame table itself
struct lock frame_table_lock;		// lock for using frame table

struct frame_table_entry
{
	uint32_t* frame;
	struct thread* owner;
	struct sup_page_table_entry* spte;
	struct list_elem elem;
};

void frame_init(void);
uint32_t *allocate_frame (struct sup_page_table_entry *spte, bool pal_zero);
void insert_frame(uint32_t *frame, struct sup_page_table_entry *spte);
void free_frame(uint32_t *frame);
#endif /* vm/frame.h */
