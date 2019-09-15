#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* A directory. */


/* A single directory entry. */


/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (disk_sector_t sector, size_t entry_cnt) 
{
  return inode_create (sector, entry_cnt * sizeof (struct dir_entry), false);
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 0;
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode) 
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  // TODO: lock inode of dir
  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, disk_sector_t inode_sector) 
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);
  // TODO: lock inode
  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  if (!inode_make_parent(inode_get_inumber(dir->inode), inode_sector))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

 done:
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  // TODO: lock inode
  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  /* Reject removing if directory is non-empty */
  if (!inode_is_file(inode) && !dir_is_empty(inode))
    goto done;

  /* Reject removing if used by other processes */
  if (!inode_is_file(inode) && inode_open_cnt(inode) > 1)
    goto done;
  
  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  // TODO: lock inode
  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        } 
    }
  return false;
}


bool dir_is_empty(struct inode *inode){
  struct dir_entry e;
  off_t pos = 0;
  while(inode_read_at(inode, &e, sizeof e, pos) == sizeof e){
      pos += sizeof e;
      if(e.in_use)
        return false;
  }
  return true;
}

bool dir_is_root(struct dir *dir){
  if (dir == NULL)
    return false;
  if (inode_get_inumber(dir->inode) == ROOT_DIR_SECTOR)
    return true;
  return false;
}

bool dir_get_parent(struct dir *dir, struct inode **inode){
  disk_sector_t sector = inode_parent_sector(dir->inode);
  //printf("dir_get_parent: sector %d\n", sector);
  *inode = inode_open(sector);
  //printf("dir_get_parent: inode %d\n", *inode != NULL);
  return *inode != NULL;
}



/*
  If path is "/root/home/zhanto", returns struct dir for directory "home"
*/
struct dir *dir_from_path(char *path){
  unsigned len = strlen(path);
  char copy[len + 1];
  memcpy(copy, path, len + 1);

  struct dir *dir;
  if (copy[0] == '/' || thread_current()->cur_dir == NULL)
    dir = dir_open_root();
  else
    dir = dir_reopen(thread_current()->cur_dir);
  
  char *saveptr;
  char *token = strtok_r(copy, "/", &saveptr);
  char *next = strtok_r(NULL, "/", &saveptr);
  while(next){
    if (strcmp(token, ".") != 0){
      struct inode *inode;
      if (strcmp(token, "..") == 0 && !dir_get_parent(dir, &inode))
        return NULL;
      if (!dir_lookup(dir, token, &inode))
        return NULL;

      if (!inode_is_file(inode)){
        dir_close(dir);
        dir = dir_open(inode);
      }
      else
        inode_close(inode);
    }
    token = next;
    next = strtok_r(NULL, "/", &saveptr);
  }
  return dir;
}

/*
  If path is "/root/home/zhanto", returns "zhanto"
*/
char *dir_last_dir(char *path){
  unsigned len = strlen(path);
  char copy[len + 1];
  memcpy(copy, path, len + 1);

  char *saveptr;
  char *prev = "";
  char *token = strtok_r(copy, "/", &saveptr);
  while(token != NULL){
    prev = token;
    token = strtok_r(NULL, "/", &saveptr);
  }

  len = strlen(prev);
  char *last_dir = (char *) malloc(len + 1);
  memcpy(last_dir, prev, len + 1);
  return last_dir;
}