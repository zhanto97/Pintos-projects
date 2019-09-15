#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stdlib.h>

/* The swap device */
static struct disk *swap_device;

/* Tracks in-use and free swap slots */
static struct bitmap *swap_table;

/* Protects swap_table */
static struct lock swap_lock;

#define SECTORS_PER_PAGE (PGSIZE / DISK_SECTOR_SIZE) // 8

void swap_init (void);
void swap_in(size_t index, void *frame);
size_t swap_out (void *frame);

#endif /* vm/swap.h */
