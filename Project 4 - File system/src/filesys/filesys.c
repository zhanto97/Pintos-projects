#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/cache.h"
#include "filesys/directory.h"
#include "devices/disk.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/malloc.h"

/* The disk that contains the file system. */
struct disk *filesys_disk;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  filesys_disk = disk_get (0, 1);
  if (filesys_disk == NULL)
    PANIC ("hd0:1 (hdb) not present, file system initialization failed");
  
  free_map_init ();
  inode_init ();
  buffer_cache_init();
  
  if (format) 
    do_format ();

  free_map_open ();

  lock_init(&filesys_lock);
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_cache();
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size, bool isFile) 
{
  disk_sector_t inode_sector = 0;
  bool success = false;
  //struct dir *dir = dir_open_root ();
  struct dir *dir = dir_from_path(name);
  char *filename = dir_last_dir(name);
  //printf("create: dir, is root and filename %p %d %s\n", dir, dir_is_root(dir), filename);
  if (strcmp(filename, ".") != 0 && strcmp(filename, "..") != 0)
    success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, isFile)
                  && dir_add (dir, filename, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);

  struct inode *inode;
  //if (dir_lookup(dir, filename, &inode))
  //  printf("create: normally added I guess\n");
  free(filename);
  dir_close (dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  if (strlen(name) == 0)
    return NULL;
  //struct dir *dir = dir_open_root ();
  struct dir *dir = dir_from_path(name);
  char *filename = dir_last_dir(name);
  //printf("open: dir, is root and filename %p %d %s\n", dir, dir_is_root(dir), filename);
  struct inode *inode = NULL;

  if (dir != NULL){
    if (strcmp(filename, "..") == 0){
      if (!dir_get_parent(dir, &inode)){
        free(filename);
        return NULL;
      }
    }
    else if ((dir_is_root(dir) && strlen(filename) == 0) || strcmp(filename, ".") == 0){
      free(filename);
      return (struct file *) dir;
    }
    else{
      //printf("open: doing lookup\n");
      dir_lookup(dir, filename, &inode);
    }
  }
  free(filename);
  dir_close(dir);

  if (inode == NULL){
    //printf("open: inode is null\n");
    return NULL;
  }
  else if (!inode_is_file(inode)){
    //printf("open: inode is dir");
    return (struct file *) dir_open(inode);
  }
  return file_open(inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  //struct dir *dir = dir_open_root ();
  struct dir *dir = dir_from_path(name);
  char *filename = dir_last_dir(name);
  
  bool success = dir != NULL && dir_remove (dir, filename);
  free(filename);
  dir_close (dir); 
  return success;
}

bool filesys_chdir(char *path){
  struct dir *dir = dir_from_path(path);
  char *filename = dir_last_dir(path);
  struct inode *inode = NULL;
  if (dir != NULL){
    if (strcmp(filename, "..") == 0){
      if (!dir_get_parent(dir, &inode)){
        free(filename);
        return false;
      }
    }
    else if((dir_is_root(dir) && strlen(filename) == 0) || strcmp(filename, ".") == 0){
      thread_current()->cur_dir = dir;
      free(filename);
      return true;
    }
    else
      dir_lookup(dir, filename, &inode);
  }
  free(filename);
  dir_close(dir);
  
  if (inode == NULL){
    //printf("chdir: inode NULL\n");
    return false;
  }

  dir = dir_open(inode);
  if (dir != NULL){
    dir_close(thread_current()->cur_dir);
    thread_current()->cur_dir = dir;
    return true;
  }
  return false;
}


/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

void acquire_filesys(){
  lock_acquire(&filesys_lock);
}
void release_filesys(){
  lock_release(&filesys_lock);
}