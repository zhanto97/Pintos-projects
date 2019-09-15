#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "vm/page.h"
#include <list.h>

#define ARGC_LIMIT 40

struct unmap_struct{
	int map_id;
	struct sup_page_table_entry *spte;
	struct list_elem elem;
};

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
void unmap_all(void);
void unmap(int map_id);

#endif /* userprog/process.h */
