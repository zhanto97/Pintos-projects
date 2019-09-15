#include "vm/swap.h"
#include "devices/disk.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include <list.h>
#include <bitmap.h>

/* 
 * Initialize swap_device, swap_table, and swap_lock.
 */
void 
swap_init (void)
{
	//disk_init();
	swap_device = disk_get(1, 1);
	if (swap_device == NULL)
		PANIC("Disk is not initialized\n");
	swap_table = bitmap_create(disk_size(swap_device));
	if (swap_table == NULL)
		PANIC("Swap table is not initialized\n");
	lock_init(&swap_lock);
}

/*
 * Reclaim a frame from swap device.
 * 1. Check that the page has been already evicted. 
 * 2. You will want to evict an already existing frame
 * to make space to read from the disk to cache. 
 * 3. Re-link the new frame with the corresponding supplementary
 * page table entry. 
 * 4. Do NOT create a new supplementray page table entry. Use the 
 * already existing one. 
 * 5. Use helper function read_from_disk in order to read the contents
 * of the disk into the frame. 
 */ 
void 
swap_in(size_t index, void *frame)
{
	size_t i;

	lock_acquire(&swap_lock);
	for (i = 0; i < SECTORS_PER_PAGE; i++){
		bitmap_flip(swap_table, index+i);
		disk_read(swap_device, index + i, (char *) frame + i*DISK_SECTOR_SIZE);
	}
	lock_release(&swap_lock);
}

/* 
 * Evict a frame to swap device. 
 * 1. Choose the frame you want to evict. 
 * (Ex. Least Recently Used policy -> Compare the timestamps when each 
 * frame is last accessed)
 * 2. Evict the frame. Unlink the frame from the supplementray page table entry
 * Remove the frame from the frame table after freeing the frame with
 * pagedir_clear_page. 
 * 3. Do NOT delete the supplementary page table entry. The process
 * should have the illusion that they still have the page allocated to
 * them. 
 * 4. Find a free block to write you data. Use swap table to get track
 * of in-use and free swap slots.
 */
size_t
swap_out (void *frame)
{
	lock_acquire(&swap_lock);
	size_t free_index = bitmap_scan_and_flip(swap_table, 0, SECTORS_PER_PAGE, false);
	
  	if (free_index == BITMAP_ERROR){
  		lock_release(&swap_lock);
    	PANIC("Swap device is full");
    	return free_index;
  	}
  	
  	size_t i;
  	for (i = 0; i < SECTORS_PER_PAGE; i++)
      	disk_write(swap_device, free_index + i, (char *) frame + i*DISK_SECTOR_SIZE);
	lock_release(&swap_lock);
  	return free_index;
}
