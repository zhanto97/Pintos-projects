#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "lib/kernel/list.h"
#include "process.h"


static void syscall_handler (struct intr_frame *);
static int get_free_fd(struct thread *curr);
static int validate_addr(const uint8_t *addr);
void exit_process(int status);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int *sys_stack = f->esp;
  validate_addr((uint8_t *) sys_stack);
  int sys_call = sys_stack[0];
  struct file *fp;
  struct open_file *file;

  switch(sys_call){
    case SYS_HALT:
      power_off();
      break;
    
    case SYS_EXIT:
      validate_addr((uint8_t *) (sys_stack+1));
      exit_process(sys_stack[1]);
      break;

    case SYS_EXEC:
      validate_addr((uint8_t *) (sys_stack + 1));
      validate_addr((uint8_t *) sys_stack[1]);
      f->eax = process_execute((char *) sys_stack[1]);
      break;

    case SYS_WAIT:
      validate_addr((uint8_t *) (sys_stack+1));
      f->eax = process_wait(sys_stack[1]);
      break;

    case SYS_CREATE:
      validate_addr((uint8_t *) (sys_stack+1));
      validate_addr((uint8_t *) (sys_stack+2));
      validate_addr((uint8_t *) sys_stack[1]);
      acquire_filesys();
      f->eax = filesys_create((char *) sys_stack[1], sys_stack[2], true);
      release_filesys();
      break;

    case SYS_REMOVE:
      validate_addr((uint8_t *) sys_stack[1]);
      acquire_filesys();
      f->eax = filesys_remove((char *) sys_stack[1]);
      release_filesys();
      break;

    case SYS_OPEN:
      validate_addr((uint8_t *) (sys_stack+1));
      validate_addr((uint8_t *) sys_stack[1]);

      acquire_filesys();
      fp = filesys_open((char *) sys_stack[1]);
      release_filesys();

      if (fp == NULL)
        f->eax = -1;
      else{
        if (!inode_is_file(file_get_inode(fp)))
          file_deny_write(fp);
        int free = get_free_fd(thread_current());
        if (free != -1){
          file = (struct open_file *) malloc(sizeof(struct open_file));
          if (file == NULL)
            f->eax = -1;
          else{
            if (inode_is_file(file_get_inode(fp)))
              file->isFile = true;
            else
              file->isFile = false;
            file->file = fp;
            file->dir = fp;
            thread_current()->open_files[free] = file;
            f->eax = free;
          }
        }
        else
          f->eax = -1;
      }
      break;

    case SYS_FILESIZE:
      validate_addr((uint8_t *) (sys_stack+1));
      if (2 <= sys_stack[1] && sys_stack[1] < 128){
        file = thread_current()->open_files[sys_stack[1]];
        if (file == NULL)
          f->eax = 0;
        else if (!file->isFile)
          f->eax = 0;
        else{
          acquire_filesys();
          f->eax = file_length(file->file);
          release_filesys();
        }
      }
      else
        f->eax = 0;
      break;

    case SYS_READ:
      validate_addr((uint8_t *) (sys_stack+3));
      validate_addr((uint8_t *) sys_stack[2]);

      if (sys_stack[1] == 0){
        // Reading from STDIN
        uint8_t *buf = (uint8_t *) sys_stack[2];
        int i;
        for (i = 0; i < sys_stack[3]; i++)
          buf[i] = input_getc();
        f->eax = sys_stack[3];
      }
      else if (2 <= sys_stack[1] && sys_stack[1] < 128){
        file = thread_current()->open_files[sys_stack[1]];
        if (file == NULL)
          f->eax = 0;
        else if(!file->isFile)
          f->eax = 0;
        else{
          acquire_filesys();
          f->eax = file_read(file->file, sys_stack[2], sys_stack[3]);
          release_filesys();
        }
      }
      else
        f->eax = 0;
      break;
    
    case SYS_WRITE:
      validate_addr((uint8_t *) (sys_stack+3));
      validate_addr((uint8_t *) sys_stack[2]);
      int fd = sys_stack[1];
      char *buf = (char *) sys_stack[2];
      int size = sys_stack[3];
      if (fd == 1){
        putbuf(buf, size);
        f->eax = size;
      }
      else if (2 <= fd && fd < 128){
        file = thread_current()->open_files[fd];
        if (file == NULL)
          f->eax = 0;
        else if(!file->isFile)
          f->eax = -1;
        else{
          acquire_filesys();
          f->eax = file_write(file->file, buf, size);
          release_filesys();
        }
      }
      else
        f->eax = 0;
      break;


    case SYS_SEEK:
      validate_addr((uint8_t *) (sys_stack+2));
      if (2 <= sys_stack[1] && sys_stack[1] < 128){
        file = thread_current()->open_files[sys_stack[1]];
        if (file != NULL && file->isFile){
          acquire_filesys();
          file_seek(file->file, sys_stack[2]);
          release_filesys();
        }
      }
      break;

    case SYS_TELL:
      validate_addr((uint8_t *) (sys_stack+1));
      if (2 <= sys_stack[1] && sys_stack[1] < 128){
        file = thread_current()->open_files[sys_stack[1]];
        if (file == NULL)
          f->eax = -1;
        else if (!file->isFile)
          f->eax = -1;
        else{
          acquire_filesys();
          f->eax = file_tell(file->file);
          release_filesys();
        }
      }
      else
        f->eax = -1;
      break;

    case SYS_CLOSE:
      validate_addr((uint8_t *) (sys_stack+1));
      if (2 <= sys_stack[1] && sys_stack[1] < 128){
        file = thread_current()->open_files[sys_stack[1]];
        if (file == NULL){
          return;
        }
        else{
          acquire_filesys();
          if (file->isFile)
            file_close(file->file);
          else
            dir_close(file->dir);
          free(file);
          thread_current()->open_files[sys_stack[1]] = NULL;
          release_filesys();
        }
      }
      break;

    case SYS_CHDIR:
      validate_addr((uint8_t *) (sys_stack+1));
      validate_addr((uint8_t *) sys_stack[1]);
      acquire_filesys();
      f->eax = filesys_chdir((char *) sys_stack[1]);
      release_filesys();
      break;

    case SYS_MKDIR:
      validate_addr((uint8_t *) (sys_stack+1));
      validate_addr((uint8_t *) sys_stack[1]);
      acquire_filesys();
      f->eax = filesys_create(sys_stack[1], 0, false);
      release_filesys();
      break;

    case SYS_READDIR:
      validate_addr((uint8_t *) (sys_stack+1));
      validate_addr((uint8_t *) (sys_stack+2));
      validate_addr((uint8_t *) sys_stack[2]);
      if (2 <= sys_stack[1] && sys_stack[1] < 128){
        file = thread_current()->open_files[sys_stack[1]];
        if(file == NULL)
          f->eax = false;
        else if (file->isFile)
          f->eax = false;
        else
          f->eax = file_readdir(file->dir, sys_stack[2]);
      }
      else
        f->eax = false;
      break;

    case SYS_ISDIR:
      validate_addr((uint8_t *) (sys_stack + 1));
      if (2 <= sys_stack[1] && sys_stack[1] < 128){
        file = thread_current()->open_files[sys_stack[1]];
        if(file != NULL)
          f->eax = !file->isFile;
        else
          f->eax = false;
      }
      else
        f->eax = false;
      break;

    case SYS_INUMBER:
      validate_addr((uint8_t *) (sys_stack + 1));
      if (2 <= sys_stack[1] && sys_stack[1] < 128){
        file = thread_current()->open_files[sys_stack[1]];
        if(file != NULL)
          f->eax = inode_get_inumber(file_get_inode(file->file));
        else
          f->eax = -1;
      }
      else
        f->eax = -1;
      break;

    default:
      return;
  }
}

static int get_free_fd(struct thread *curr){
  int i;
  for (i = 2; i < 128; i++){
    if (curr->open_files[i] == NULL)
      return i;
  }
  return -1;
}

static int validate_addr(const uint8_t *addr)
{
  if (!is_user_vaddr(addr)){
    exit_process(-1);
    return -1;
  }
  
	if (!pagedir_get_page(thread_current()->pagedir, addr))
	{
		exit_process(-1);
		return -1;
	}
  return 0;
}

void exit_process(int status)
{ 
  struct thread *curr = thread_current();


  // TODO: Below code should not commented actually. However, it fails multi-oom.
  /*
  while (!list_empty(&curr->child_processes)){
    struct list_elem *e = list_pop_front(&curr->child_processes);
    struct child *ch = list_entry(e, struct child, child_elem);
    free(ch);
  }
  */

  if (curr->parent){
    curr->cp->exit_status = status;
    if (curr->cp->waited_by_parent)
      sema_up(&curr->cp->wait_lock);
  }
  
  printf("%s: exit(%d)\n", curr->name, status);
  thread_exit();
}