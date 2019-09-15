#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "lib/kernel/list.h"
#include "process.h"


static void syscall_handler (struct intr_frame *);
static struct file_wrapper * search_fd(struct list *file_list, int fd);
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
  struct file_wrapper *f_wrapper;

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
      f->eax = filesys_create((char *) sys_stack[1], sys_stack[2]);
      release_filesys();
      break;

    case SYS_REMOVE:
      validate_addr((uint8_t *) sys_stack[1]);
      acquire_filesys();
      f->eax = filesys_remove((char *) sys_stack[1]);
      release_filesys();
      break;

    case SYS_OPEN:
      // TODO: impose limit on number of open files. It is optional tho
      validate_addr((uint8_t *) (sys_stack+1));
      validate_addr((uint8_t *) sys_stack[1]);

      // We need to be careful if we are opening our own or parent's exec file
      bool match_current = strcmp(sys_stack[1], thread_current()->name);
      bool match_parent = false;
      if (thread_current()->parent)
        match_parent = strcmp(sys_stack[1], thread_current()->parent->name);

      acquire_filesys();
      struct file *fp = filesys_open((char *) sys_stack[1]);
      release_filesys();
      if (fp == NULL)
        f->eax = -1;
      else{
        f_wrapper = (struct file_wrapper *) malloc(sizeof(struct file_wrapper));
        f_wrapper->fp = fp;
        f_wrapper->fd = thread_current()->num_open_files++;
        list_push_back(&thread_current()->open_files, &f_wrapper->file_elem);
        if (!match_current){
          thread_current()->curr_open_fd = f_wrapper->fd;
        }
        else if (!match_parent){
          thread_current()->parent_open_fd = f_wrapper->fd;
        }
        f->eax = f_wrapper->fd;
      }
      break;

    case SYS_FILESIZE:
      validate_addr((uint8_t *) (sys_stack+1));
      f_wrapper = search_fd(&thread_current()->open_files, sys_stack[1]);
      if (f_wrapper == NULL)
        f->eax = 0;
      else{
        acquire_filesys();
        f->eax = file_length(f_wrapper->fp);
        release_filesys();
      }
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
      else{
        f_wrapper = search_fd(&thread_current()->open_files, sys_stack[1]);
        if (f_wrapper == NULL)
          f->eax = -1;
        else{
          acquire_filesys();
          f->eax = file_read(f_wrapper->fp, sys_stack[2], sys_stack[3]);
          release_filesys();
        }
      }
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
      } else {
      
        f_wrapper = search_fd(&thread_current()->open_files, fd);
        if (f_wrapper == NULL)
          f->eax = 0;
        else if (thread_current()->curr_open_fd != -1 && f_wrapper->fd == thread_current()->curr_open_fd){
          //printf("Trying to modilfy currently running executable");
          f->eax = 0;
        }
        else if (thread_current()->parent_open_fd != -1 && f_wrapper->fd == thread_current()->parent_open_fd){
          //printf("Trying to modilfy parent's executable");
          f->eax = 0;
        }
        else{
          // TODO: implement write until size permits using tell and file size
          acquire_filesys();
          f->eax = file_write(f_wrapper->fp, buf, size);
          release_filesys();
        }
      }
      break;


    case SYS_SEEK:
      validate_addr((uint8_t *) (sys_stack+2));
      f_wrapper = search_fd(&thread_current()->open_files, sys_stack[1]);
      if (f_wrapper != NULL){
        acquire_filesys();
        file_seek(f_wrapper->fp, sys_stack[2]);
        release_filesys();
      }
      break;

    case SYS_TELL:
      validate_addr((uint8_t *) (sys_stack+1));
      f_wrapper = search_fd(&thread_current()->open_files, sys_stack[1]);
      if (f_wrapper == NULL)
        f->eax = -1;
      else{
        acquire_filesys();
        f->eax = file_tell(f_wrapper->fp);
        release_filesys();
      }
      break;

    case SYS_CLOSE:
      validate_addr((uint8_t *) (sys_stack+1));
      f_wrapper = NULL;
      struct list_elem *e;
      for (e = list_begin(&thread_current()->open_files);
          e != list_end(&thread_current()->open_files);
          e = list_next(e))
      {
        f_wrapper = list_entry(e, struct file_wrapper, file_elem);
        if(f_wrapper->fd == sys_stack[1]){
          list_remove(e);
          thread_current()->num_open_files--;
          break;
        }
      }
      if (f_wrapper != NULL && f_wrapper->fd == sys_stack[1]){
        acquire_filesys();
        file_close(f_wrapper->fp);
        release_filesys();
        if (f_wrapper->fd == thread_current()->curr_open_fd)
          thread_current()->curr_open_fd = -1;
        else if (f_wrapper->fd == thread_current()->parent_open_fd)
          thread_current()->parent_open_fd = -1;
        free(f_wrapper);
      }
      break;

    default:
      return;
  }
}

static struct file_wrapper * search_fd(struct list *file_list, int fd){
  struct file_wrapper *f_wrapper = NULL;
  struct list_elem *e;
  for (e = list_begin(file_list);
      e != list_end(file_list);
      e = list_next(e))
  {
    f_wrapper = list_entry(e, struct file_wrapper, file_elem);
    if(f_wrapper->fd == fd)
      return f_wrapper;
  }
  return NULL;
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
    struct child *ch;
    struct list_elem *e;
    for (e = list_begin(&curr->parent->child_processes);
          e != list_end(&curr->parent->child_processes);
          e = list_next(e))
    {
      ch = list_entry(e, struct child, child_elem);
      if(ch->tid == curr->tid){
        ch->exit_status = status;
        if (ch->waited_by_parent)
          sema_up(&curr->parent->child_lock);
        break;
      }
    }
  }
  
  printf("%s: exit(%d)\n", curr->name, status);
	thread_exit();
}