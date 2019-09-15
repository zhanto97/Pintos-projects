#ifndef FILESYS_DIRECTORY_H
#define FILESYS_DIRECTORY_H
#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include "devices/disk.h"
#include "filesys/inode.h"

/* Maximum length of a file name component.
   This is the traditional UNIX maximum length.
   After directories are implemented, this maximum length may be
   retained, but much longer full path names must be allowed. */
#define NAME_MAX 14
struct inode;

struct dir 
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };
 
struct dir_entry 
  {
    disk_sector_t inode_sector;         /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };
/* Opening and closing directories. */
bool dir_create (disk_sector_t sector, size_t entry_cnt);
struct dir *dir_open (struct inode *);
struct dir *dir_open_root (void);
struct dir *dir_reopen (struct dir *);
void dir_close (struct dir *);
struct inode *dir_get_inode (struct dir *);

/* Reading and writing. */
bool dir_lookup (const struct dir *, const char *name, struct inode **);
bool dir_add (struct dir *, const char *name, disk_sector_t);
bool dir_remove (struct dir *, const char *name);
bool dir_readdir (struct dir *, char name[NAME_MAX + 1]);

bool dir_get_parent(struct dir *dir, struct inode **inode);
bool dir_is_root(struct dir *dir);
bool dir_is_empty(struct inode *inode);
struct dir *dir_from_path(char *path);
char *dir_last_dir(char *path);
#endif /* filesys/directory.h */
