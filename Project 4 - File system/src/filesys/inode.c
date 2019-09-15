#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/cache.h"
#include "threads/malloc.h"
#include "threads/synch.h"



/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define DIRECT_BLOCKS 12
#define INDIRECT_BLOCKS 1
#define DOUBLE_INDIRECT_BLOCKS 1
#define NUM_DIRECT_PTRS 128   // number of direct pointers in an indirect block (512 / 4 = 128)
// max file size would be 512*(12 + 128 + 128*128) = 8,460,288 > 8MB

/* On-disk inode.
   Must be exactly DISK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                         /* File size in bytes. */
    unsigned magic;                       /* Magic number. */
    disk_sector_t blocks[14];             /* num (direct + indirect + double indirect) */
    uint32_t direct;                      /* direct block index; if 12, all directs are used up*/
    uint32_t indirect;                    /* indirect block index; if 128, all indirect blocks are used up */
    uint32_t double_indirect;             /* double indirect index; if 128*128, all are used up (never happens) */
    bool isFile;                          /* Dirctory or file */
    disk_sector_t parent_sector;          /* If directory, contains sector where its parent's inode is located */
    uint32_t unused[107];                 /* Not used. */
  };
  
/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, DISK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    disk_sector_t sector;               /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */

    off_t length;
    uint32_t direct;                      /* direct block index; if 12, all directs are used up*/
    uint32_t indirect;                    /* indirect block index; if 128, all indirect blocks are used up */
    uint32_t double_indirect;             /* double indirect index; if 128*128, all are used up (never happens) */
    bool isFile;                          /* Dirctory or file */
    disk_sector_t parent_sector;          /* If directory, contains sector where its parent's inode is located */
    disk_sector_t blocks[14];             /* num (direct + indirect + double indirect) */
    //struct inode_disk data;             /* Inode content. */
  };

static void inode_free_resources(struct inode *inode);
static bool inode_expand(struct inode *inode, off_t new_length);

/* Returns the disk sector that contains byte offset POS within
   INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static disk_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);

  if (pos < inode->length){
    uint32_t idx = pos/DISK_SECTOR_SIZE;
    if (idx < 12)
      return inode->blocks[idx];
    
    idx-=DIRECT_BLOCKS;
    if (idx < 128){
      disk_sector_t block[NUM_DIRECT_PTRS];
      disk_read(filesys_disk, inode->blocks[12], &block);
      return block[idx];
    }
    idx-=NUM_DIRECT_PTRS;
    if (idx < 128*128){
      disk_sector_t block_ptrs[NUM_DIRECT_PTRS];
      disk_read(filesys_disk, inode->blocks[13], &block_ptrs);
      int block_index = idx/128;
      disk_sector_t block[NUM_DIRECT_PTRS];
      disk_read(filesys_disk, block_ptrs[block_index], &block);
      return block[idx%128];
    }
    return -1;
  }
  else
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   disk.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (disk_sector_t sector, off_t length, bool isFile)
{
  //printf("create: sector, length, isfile %d %d %d\n", sector, length, isFile);
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = 0;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->direct = 0;
      disk_inode->indirect = 0;
      disk_inode->double_indirect = 0;
      disk_inode->isFile = isFile;
      disk_inode->parent_sector = ROOT_DIR_SECTOR;

      struct inode inode;
      inode.length = 0;
      inode.direct = 0;
      inode.indirect = 0;
      inode.double_indirect = 0;

      success = inode_expand(&inode, length);
      //printf("create: success,d,i,di %d %d %d %d\n", success, disk_inode->direct, disk_inode->indirect, disk_inode->double_indirect);
      if (success){
        disk_inode->length = length;
        disk_inode->direct = inode.direct;
        disk_inode->indirect = inode.indirect;
        disk_inode->double_indirect = inode.double_indirect;
        memcpy(disk_inode->blocks, inode.blocks, 14*sizeof(disk_sector_t));
        disk_write(filesys_disk, sector, disk_inode);
      }
      /*
      if (free_map_allocate (sectors, &disk_inode->start))
        {
          disk_write (filesys_disk, sector, disk_inode);
          if (sectors > 0) 
            {
              static char zeros[DISK_SECTOR_SIZE];
              size_t i;
              
              for (i = 0; i < sectors; i++) 
                disk_write (filesys_disk, disk_inode->start + i, zeros); 
            }
          success = true; 
        } 
      */
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (disk_sector_t sector) 
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;

  struct inode_disk disk_inode;
  disk_read (filesys_disk, inode->sector, &disk_inode);

  inode->length = disk_inode.length;
  inode->direct = disk_inode.direct;
  inode->indirect = disk_inode.indirect;
  inode->double_indirect = disk_inode.double_indirect;
  inode->isFile = disk_inode.isFile;
  inode->parent_sector = disk_inode.parent_sector;
  memcpy(inode->blocks, disk_inode.blocks, 14*sizeof(disk_sector_t));
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
disk_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
      {
        inode_free_resources(inode);
        free_map_release (inode->sector, 1);
        /*
        free_map_release (inode->data.start,
                          bytes_to_sectors (inode->data.length)); 
        */
      }
      else{
        struct inode_disk disk_inode;
        disk_inode.length = inode->length;
        disk_inode.magic = INODE_MAGIC;
        memcpy(disk_inode.blocks, inode->blocks, 14*sizeof(disk_sector_t));
        disk_inode.direct = inode->direct;
        disk_inode.indirect = inode->indirect;
        disk_inode.double_indirect = inode->double_indirect;
        disk_inode.isFile = inode->isFile;
        disk_inode.parent_sector = inode->parent_sector;
        disk_write(filesys_disk, inode->sector, &disk_inode);
      }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  //uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      disk_sector_t sector_idx = byte_to_sector (inode, offset);
      if (sector_idx == -1)
        break;
      int sector_ofs = offset % DISK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      struct cache_entry *entry = cache_get_sector(sector_idx, false);
      if (entry == NULL)
        entry = cache_fetch_sector(sector_idx, false);
      ASSERT(entry != NULL);
      memcpy(buffer + bytes_read, entry->payload + sector_ofs, chunk_size);
      /*
      if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) 
        {
          // Read full sector directly into caller's buffer.
          disk_read (filesys_disk, sector_idx, buffer + bytes_read); 
        }
      else 
        {
          // Read sector into bounce buffer, then partially copy
          //   into caller's buffer. 
          if (bounce == NULL) 
            {
              bounce = malloc (DISK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          disk_read (filesys_disk, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      */
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  //free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  //uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;
  //printf("write: inode, inode length, size, offset %p %d %d %d \n", inode, inode->data.length, size, offset);
  if (offset + size > inode_length(inode)){
    //printf("write: calling expand\n");
    if (inode_expand(inode, offset+size))
      inode->length = offset+size;
  }

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      disk_sector_t sector_idx = byte_to_sector (inode, offset);
      //printf("write: inode length, offset, sector_idx %d %d %d \n", inode->data.length, offset, sector_idx);
      int sector_ofs = offset % DISK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      struct cache_entry *entry = cache_get_sector(sector_idx, true);
      if (entry == NULL)
        entry = cache_fetch_sector(sector_idx, true);
      ASSERT(entry != NULL);
      memcpy (entry->payload + sector_ofs, buffer + bytes_written, chunk_size);
      /*
      if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) 
        {
          // Write full sector directly to disk. 
          disk_write (filesys_disk, sector_idx, buffer + bytes_written); 
        }
      else 
        {
          // We need a bounce buffer. 
          if (bounce == NULL) 
            {
              bounce = malloc (DISK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          // If the sector contains data before or after the chunk
          // we're writing, then we need to read in the sector
          // first.  Otherwise we start with a sector of all zeros. 
          if (sector_ofs > 0 || chunk_size < sector_left) 
            disk_read (filesys_disk, sector_idx, bounce);
          else
            memset (bounce, 0, DISK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          disk_write (filesys_disk, sector_idx, bounce); 
        }
      */

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  //free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->length;
}

static void inode_free_resources(struct inode *inode){
  size_t sectors = bytes_to_sectors(inode->length);
  if (sectors){
    int i = 0;
    while (i < DIRECT_BLOCKS && sectors > 0){
      free_map_release(inode->blocks[i], 1);
      i++;
      sectors--;
    }
  }

  if (sectors){
    int i = 0;
    disk_sector_t block[NUM_DIRECT_PTRS];
    disk_read(filesys_disk, inode->blocks[12], &block);
    while(i < 128 && sectors > 0){
      free_map_release(block[i], 1);
      i++;
      sectors--;
    }
  }

  if (sectors){
    int block_index = 0;
    disk_sector_t block_ptrs[NUM_DIRECT_PTRS];
    disk_read(filesys_disk, inode->blocks[13], &block_ptrs);
    while (block_index < 128 && sectors > 0){
      disk_sector_t block[NUM_DIRECT_PTRS];
      disk_read(filesys_disk, block_ptrs[block_index], &block);
      int i = 0;
      while (i < 128 && sectors > 0){
        free_map_release(block[i], 1);
        i++;
        sectors--;
      }
      block_index++;
    }
  }
}

static bool inode_expand(struct inode *inode, off_t new_length){
  static char zeros[DISK_SECTOR_SIZE];
  size_t extra_sectors = bytes_to_sectors(new_length) - bytes_to_sectors(inode->length);
  if (extra_sectors == 0)
    return true;
  
  while (inode->direct < 12){
    // Use all direct blocks first
    if (!free_map_allocate(1, &inode->blocks[inode->direct]))
      return false;
    //printf("expand: sector num given %d\n", inode->blocks[inode->direct]);
    disk_write(filesys_disk, inode->blocks[inode->direct], zeros);
    inode->direct++;
    extra_sectors--;
    if (extra_sectors == 0)
      return true;
  }

  if(inode->indirect < 128){
    // If no direct blocks, start using indirect blocks
    disk_sector_t block[NUM_DIRECT_PTRS];
    if (inode->indirect > 0)
      disk_read(filesys_disk, inode->blocks[12], &block); // blocks[12] holds array of direct pointers
    else if (!free_map_allocate(1, &inode->blocks[12]))
      return false;
    
    while(inode->indirect < 128){
      if (!free_map_allocate(1, &block[inode->indirect]))
        return false;
      disk_write(filesys_disk, block[inode->indirect], zeros);
      inode->indirect++;
      extra_sectors--;
      if (extra_sectors == 0)
        break;
    }
    disk_write(filesys_disk, inode->blocks[12], &block);
    if (extra_sectors == 0)
      return true;
  }

  if (inode->double_indirect < 128*128){
    // If no indirect blocks, start using double indirect blocks

    disk_sector_t block_ptrs[NUM_DIRECT_PTRS];
    if (inode->double_indirect > 0)
      disk_read(filesys_disk, inode->blocks[13], &block_ptrs);
    else if (!free_map_allocate(1, &inode->blocks[13]))
      return false;

    while(inode->double_indirect < 128*128){
      uint32_t block_index = inode->double_indirect/128;
      disk_sector_t block[NUM_DIRECT_PTRS];
      if (inode->double_indirect%128 == 0){
        if (!free_map_allocate(1, &block_ptrs[block_index]))
          return false;
      }
      else
        disk_read(filesys_disk, block_ptrs[block_index], &block);

      uint32_t index = inode->double_indirect%128;
      while (index < 128){
        if (!free_map_allocate(1, &block[index]))
          return false;
        disk_write(filesys_disk, block[index], zeros);
        inode->double_indirect++;
        extra_sectors--;
        index++;
        if (extra_sectors == 0)
          break;
      }
      disk_write(filesys_disk, block_ptrs[block_index], &block);
      if (extra_sectors == 0)
        break;
    }

    disk_write(filesys_disk, inode->blocks[13], &block_ptrs);
    if (extra_sectors == 0)
      return true;
  }
  return false;
}

bool inode_is_file(struct inode *inode){
  return inode->isFile;
}

int inode_open_cnt(struct inode *inode){
  return inode->open_cnt;
}

disk_sector_t inode_parent_sector(struct inode *inode){
  return inode->parent_sector;
}

bool inode_make_parent(disk_sector_t parent_sector, disk_sector_t child_sector){
  struct inode* inode = inode_open(child_sector);
  if (inode == NULL)
    return false;
  inode->parent_sector = parent_sector;
  inode_close(inode);
  return true;
}