#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/disk.h"
#include <list.h>
#include "threads/synch.h"

struct bitmap;

void inode_init (void);
bool inode_create (disk_sector_t sector, off_t length, bool isFile);
struct inode *inode_open (disk_sector_t);
struct inode *inode_reopen (struct inode *);
disk_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

bool inode_make_parent(disk_sector_t parent_sector, disk_sector_t child_sector);
bool inode_is_file(struct inode *inode);
int inode_open_cnt(struct inode *inode);
disk_sector_t inode_parent_sector(struct inode *inode);
//void inode_free_resources(struct inode_disk *disk_inode);
//bool inode_expand(struct inode_disk *disk_inode, off_t new_length);

#endif /* filesys/inode.h */
