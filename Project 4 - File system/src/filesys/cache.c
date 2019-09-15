

#include "devices/disk.h"
#include "devices/timer.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include <list.h>

static struct cache_entry *victim_sector();

void buffer_cache_init(){
	list_init(&buffer_cache);
	lock_init(&buffer_cache_lock);
	buffer_cache_size = 0;
}

struct cache_entry *cache_get_sector(disk_sector_t sector_num, bool set_dirty){
	struct cache_entry *ans;
	struct list_elem *e;
	//printf("get_sector: sector is %d \n", sector_num);
	for (e = list_begin(&buffer_cache); e != list_end(&buffer_cache); e = list_next(e)){
		ans = list_entry(e, struct cache_entry, elem);
		if (ans->sector_num == sector_num){
			ans->accessed = true;
			ans->access_time = timer_ticks();
			ans->dirty|=set_dirty;
			return ans;
		}
	}
	return NULL;
}

struct cache_entry *cache_fetch_sector(disk_sector_t sector_num, bool set_dirty){
	lock_acquire(&buffer_cache_lock);
	//printf("fetch_sector: sector is %d \n", sector_num);
	struct cache_entry *entry;
	if (buffer_cache_size < 64){
		//printf("fetch_sector: cache size is less than 64\n");
		entry = (struct cache_entry *) malloc(sizeof(struct cache_entry));
		if (entry == NULL){
			printf("fetch_sector: malloc returned NULL\n");
			return NULL;
		}
		list_push_back(&buffer_cache, &entry->elem);
		buffer_cache_size++;
	}
	else{
		entry = victim_sector();
		if (entry->dirty)
			disk_write(filesys_disk, entry->sector_num, entry->payload);
	}
	entry->accessed = true;
	entry->dirty = set_dirty;
	entry->access_time = timer_ticks();
	entry->sector_num = sector_num;
	disk_read(filesys_disk, sector_num, entry->payload);

	lock_release(&buffer_cache_lock);
	return entry;
}

void free_cache(){
	lock_acquire(&buffer_cache_lock);
	struct cache_entry *temp;
	struct list_elem *e = list_begin(&buffer_cache);
	struct list_elem *next;
	while (e != list_end(&buffer_cache)){
		temp = list_entry(e, struct cache_entry, elem);
		next = list_remove(e);
		if (temp->dirty){
			disk_write(filesys_disk, temp->sector_num, temp->payload);
			temp->dirty = false;
		}
		free(temp);
		e = next;
	}
	lock_release(&buffer_cache_lock);
}

// First unaccessed entry in cache (if exists) or 
// least recently accessed entry
static struct cache_entry *victim_sector(){
	struct cache_entry *ans;
	struct cache_entry *temp;
	struct list_elem *e;
	int64_t min_time = 9223372036854775807; // int64_t max
	for (e = list_begin(&buffer_cache); e != list_end(&buffer_cache); e = list_next(e)){
		temp = list_entry(e, struct cache_entry, elem);
		if (!temp->accessed)
			return temp;
		else{
			if (temp->access_time < min_time){
				ans = temp;
				min_time = temp->access_time;
			}
		}
	}
	return ans;
}