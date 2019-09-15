#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include <list.h>
//#include <stdlib.h>
//#include <time.h>

static struct frame_table_entry *victim_frame(void);
/*
 * Initialize frame table
 */
void frame_init (void){
	//srand(time(NULL));
	list_init(&frame_table);
	lock_init(&frame_table_lock);
}


/* 
 * Make a new frame table entry for spte.
 */
uint32_t *allocate_frame (struct sup_page_table_entry *spte, bool pal_zero){
	enum palloc_flags flags = PAL_USER;
	if (pal_zero)
		flags|=PAL_ZERO;
	
	uint32_t *frame = (uint32_t *) palloc_get_page(flags);
	if (frame == NULL){
		// PANIC("Frame couldn't be allocated\n");
		// 
		
		lock_acquire(&frame_table_lock);
		struct frame_table_entry *victim = victim_frame();
		//if (victim->spte->type == MMAP)
		size_t index = swap_out(victim->frame);
		victim->spte->loaded = false;
		victim->spte->type = SWAPPED;
		victim->spte->index = index;
		pagedir_clear_page(victim->owner->pagedir, victim->spte->user_vaddr);
		
		victim->owner = thread_current();
		victim->spte = spte;
		lock_release(&frame_table_lock);
		
		frame = victim->frame;
		if (pal_zero)
			memset(frame, 0, PGSIZE);
		return frame;
	}
	insert_frame(frame, spte);
	return frame;
}

void insert_frame(uint32_t *frame, struct sup_page_table_entry *spte){
	struct frame_table_entry *fte = malloc(sizeof(struct frame_table_entry));
	fte->frame = frame;
	fte->owner = thread_current();
	fte->spte = spte;
	
	lock_acquire(&frame_table_lock);
	list_push_back(&frame_table, &fte->elem);
	lock_release(&frame_table_lock);
}

void free_frame(uint32_t *frame){
	lock_acquire(&frame_table_lock);

	struct frame_table_entry *temp;
	struct list_elem *e = list_begin(&frame_table);
	while (e != list_end(&frame_table)){
		temp = list_entry(e, struct frame_table_entry, elem);
		if (temp->frame == frame){
			list_remove(e);
			palloc_free_page(frame);
			free(temp);
			break;
		}
		e = list_next(e);
	}
	
	lock_release(&frame_table_lock);
}

static struct frame_table_entry *victim_frame(void){
	/*
	lock_acquire(&frame_table_lock);
	
	int size = list_size(&frame_table);
	int r = rand()%size;
	struct list_elem *e;
	int i = 0;
	for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e)){
		if (i >= r){
			struct frame_table_entry *temp = list_entry(e, struct frame_table_entry, elem);
			list_remove(e);
			lock_release(&frame_table_lock);
			return temp;
		}
		i++;
	}

	printf("victim_frame function should never reach here\n");
	lock_release(&frame_table_lock);
	return NULL;
	*/

	/*
	//lock_acquire(&frame_table_lock);
	struct list_elem *e = list_back(&frame_table);
	struct frame_table_entry *fte = list_entry(e, struct frame_table_entry, elem);
	//lock_release(&frame_table_lock);
	*/

	struct frame_table_entry *fte0 = NULL;
	struct frame_table_entry *fte1 = NULL;
	struct frame_table_entry *fte2 = NULL;
	struct frame_table_entry *fte3 = NULL;

	struct thread *curr = thread_current();
	struct list_elem *e;
	struct frame_table_entry *fte;
	for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e)){
		fte = list_entry(e, struct frame_table_entry, elem);
		if (pagedir_is_accessed(curr->pagedir, fte->spte->user_vaddr)
			&& pagedir_is_dirty(curr->pagedir, fte->spte->user_vaddr)
			&& fte3 == NULL)
			fte3 = fte;
		else if (pagedir_is_accessed(curr->pagedir, fte->spte->user_vaddr)
			&& (!pagedir_is_dirty(curr->pagedir, fte->spte->user_vaddr))
			&& fte2 == NULL)
			fte2 = fte;
		else if ((!pagedir_is_accessed(curr->pagedir, fte->spte->user_vaddr))
			&& pagedir_is_dirty(curr->pagedir, fte->spte->user_vaddr)
			&& fte1 == NULL)
			fte1 = fte;
		else if (fte0 == NULL)
			fte0 = fte;
	}

	if (fte0 != NULL)
		return fte0;
	else if (fte1 != NULL)
		return fte1;
	else if (fte2 != NULL)
		return fte2;
	return fte3;
}