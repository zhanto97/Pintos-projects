

#include "devices/disk.h"
#include "threads/synch.h"
#include <list.h>

struct list buffer_cache;
struct lock buffer_cache_lock;
size_t buffer_cache_size;		// to avoid O(n) cache size computation

struct cache_entry{
	bool accessed;
	bool dirty;
	int64_t access_time;
	disk_sector_t sector_num;
	uint8_t payload[DISK_SECTOR_SIZE];
	struct list_elem elem;
};

void buffer_cache_init();
struct cache_entry *cache_get_sector(disk_sector_t sector_num, bool set_dirty);
struct cache_entry *cache_fetch_sector(disk_sector_t sector_num, bool set_dirty);
void free_cache();