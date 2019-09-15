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
#include "vm/page.h"


static void syscall_handler (struct intr_frame *);
static int get_free_fd(struct thread *curr);
static int validate_addr(const void *addr, void *esp, bool to_grow);
static int validate_string(char *str, void *esp);
static int validate_buffer(char *buffer, int size, void *esp, bool writable);
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
  validate_addr((void *) sys_stack, f->esp, false);
  int sys_call = sys_stack[0];
  struct file *fp;
  switch(sys_call){
    case SYS_HALT:
      power_off();
      break;
    
    case SYS_EXIT:
      validate_addr((void *) (sys_stack+1), f->esp, false);
      //printf("exit syscall\n");
      exit_process(sys_stack[1]);
      break;

    case SYS_EXEC:
      validate_addr((void *) (sys_stack + 1), f->esp, false);
      validate_string((char *) sys_stack[1], f->esp);
      f->eax = process_execute((char *) sys_stack[1]);
      break;

    case SYS_WAIT:
      validate_addr((void *) (sys_stack+1), f->esp, false);
      f->eax = process_wait(sys_stack[1]);
      break;

    case SYS_CREATE:
      validate_addr((void *) (sys_stack+1), f->esp, false);
      validate_addr((uint8_t *) (sys_stack+2), f->esp, false);
      validate_string((char *) sys_stack[1], f->esp);
      acquire_filesys();
      f->eax = filesys_create((char *) sys_stack[1], sys_stack[2]);
      release_filesys();
      break;

    case SYS_REMOVE:
      validate_addr((void *) (sys_stack+1), f->esp, false);
      validate_string((char *) sys_stack[1], f->esp);
      acquire_filesys();
      f->eax = filesys_remove((char *) sys_stack[1]);
      release_filesys();
      break;

    case SYS_OPEN:
      validate_addr((void *) (sys_stack+1), f->esp, false);
      validate_string((char *) sys_stack[1], f->esp);

      acquire_filesys();
      fp = filesys_open((char *) sys_stack[1]);
      release_filesys();
      if (fp == NULL)
        f->eax = -1;
      else{
        int free = get_free_fd(thread_current());
        if (free != -1){
          thread_current()->open_files[free] = fp;
          f->eax = free;
        }
        else
          f->eax = -1;
      }
      break;

    case SYS_FILESIZE:
      validate_addr((void *) (sys_stack+1), f->esp, false);
      fp = thread_current()->open_files[sys_stack[1]];
      if (fp == NULL)
        f->eax = 0;
      else{
        acquire_filesys();
        f->eax = file_length(fp);
        release_filesys();
      }
      break;

    case SYS_READ:
      validate_addr((void *) (sys_stack+1), f->esp, false);
      validate_addr((void *) (sys_stack+2), f->esp, false);
      validate_addr((void *) (sys_stack+3), f->esp, false);
      validate_buffer((char *) sys_stack[2], sys_stack[3], f->esp, true);

      if (sys_stack[1] == 0){
        // Reading from STDIN
        uint8_t *buf = (uint8_t *) sys_stack[2];
        int i;
        for (i = 0; i < sys_stack[3]; i++)
          buf[i] = input_getc();
        f->eax = sys_stack[3];
      }
      else if (2 <= sys_stack[1] && sys_stack[1] < 128){
        fp = thread_current()->open_files[sys_stack[1]];
        if (fp == NULL)
          f->eax = 0;
        else{
          acquire_filesys();
          f->eax = file_read(fp, sys_stack[2], sys_stack[3]);
          release_filesys();
        }
      }
      else
        f->eax = 0;
      break;
    
    case SYS_WRITE:
      //printf("WRITE SYSCALL!\n");
      validate_addr((void *) (sys_stack+1), f->esp, false);
      validate_addr((void *) (sys_stack+2), f->esp, false);
      validate_addr((void *) (sys_stack+3), f->esp, false);
      validate_buffer((char *) sys_stack[2], sys_stack[3], f->esp, false);
      int fd = sys_stack[1];
      char *buf = (char *) sys_stack[2];
      int size = sys_stack[3];
      if (fd == 1){
        putbuf(buf, size);
        f->eax = size;
      }
      else if (2 <= fd && fd < 128){
        fp = thread_current()->open_files[fd];
        if (fp == NULL)
          f->eax = 0;
        else{
          // TODO: implement write until size permits using tell and file size
          acquire_filesys();
          f->eax = file_write(fp, buf, size);
          release_filesys();
        }
      }
      else
        f->eax = 0;
      break;


    case SYS_SEEK:
      validate_addr((void *) (sys_stack+1), f->esp, false);
      validate_addr((void *) (sys_stack+2), f->esp, false);
      fp = thread_current()->open_files[sys_stack[1]];
      if (fp != NULL){
        acquire_filesys();
        file_seek(fp, sys_stack[2]);
        release_filesys();
      }
      break;

    case SYS_TELL:
      validate_addr((void *) (sys_stack+1), f->esp, false);
      fp = thread_current()->open_files[sys_stack[1]];
      if (fp == NULL)
        f->eax = -1;
      else{
        acquire_filesys();
        f->eax = file_tell(fp);
        release_filesys();
      }
      break;

    case SYS_MMAP:
      validate_addr((void *) (sys_stack+1), f->esp, false);
      validate_addr((void *) (sys_stack+2), f->esp, false);
      //validate_addr((void *) sys_stack[2], f->esp, false);
      f->eax = mmap(sys_stack[1], (void *) sys_stack[2]);
      break;

    case SYS_MUNMAP:
      validate_addr((void *) (sys_stack+1), f->esp, false);
      munmap(sys_stack[1]);
      break;

    case SYS_CLOSE:
      validate_addr((void *) (sys_stack+1), f->esp, false);
      fp = thread_current()->open_files[sys_stack[1]];
      if (fp == NULL){
        return;
      }
      else{
        acquire_filesys();
        file_close(fp);
        thread_current()->open_files[sys_stack[1]] = NULL;
        release_filesys();
      }
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

int mmap(int fd, void* vaddr) {
  //printf("MMAP call\n\n\n");
  struct thread *curr = thread_current();
  struct file *oldf = curr->open_files[fd];
  if (oldf == NULL)
    return -1;
  
  if (!is_user_vaddr(vaddr) || pg_ofs(vaddr) != 0 || vaddr == NULL)
    return -1;

  struct file *f = file_reopen(oldf);
  if (f == NULL || file_length(f) == 0)
    return -1;
  
  curr->map_id++;
  off_t ofs = 0;
  size_t read_bytes = file_length(f);
  //printf("mapping %d bytes\n", read_bytes);
  while(read_bytes > 0){
    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;

    //printf("allocating mmap page: vaddr %p, ofs %d\n");
    struct sup_page_table_entry *spte = allocate_page_mmap(vaddr, f, 
                      curr->map_id, ofs, page_read_bytes, page_zero_bytes);
    if (spte == NULL){
      munmap(curr->map_id);
      //printf("MMAP: spte is NULL\n");
      return -1;
    }
    if (hash_insert(&curr->spt, &spte->h_elem) != NULL){
      munmap(curr->map_id);
      //printf("MMAP: could not insert to hash\n");
      return -1;
    }
    read_bytes -= page_read_bytes;
    ofs += page_read_bytes;
    vaddr += PGSIZE;
  }
  return curr->map_id;
}

void munmap(int map_id){
  unmap(map_id);
}

static int validate_addr(const void *addr, void *esp, bool to_grow){
  //printf("validate_addr: addr %p esp %p to_grow %d\n", addr, esp, to_grow);
  if (!is_user_vaddr(addr)){
    //printf("validate_addr: !is_user_vaddr\n");
    exit_process(-1);
    return -1;
  }
  
  struct sup_page_table_entry *spte = get_page(addr);
  if (spte != NULL){
    //printf("validate_addr: got page\n");
    if (load_page(spte))
      return 0;
    else{
      exit_process(-1);
      return -1;
    }
    //printf("validate_addr: couldn't load\n");
  }
	else if (to_grow && addr >= (char *) esp - 32){
    //printf("validate_addr: trying to grow stack\n");
		struct sup_page_table_entry *spte = allocate_page_stack(addr);
    if (spte != NULL){
      if (load_page(spte))
        return 0;
      else{
        free(spte);
        exit_process(-1);
        return -1;
      }
      //printf("validate_addr: couldn't grow stack\n");
    }
    else{
      exit_process(-1);
      return -1;
      //printf("validate_addr: couldn't allocate page for stack\n");
    }
	}
  //printf("Reached here\n");
  exit_process(-1);
  //printf("validate_addr: reached here\n");
  return -1;
}

static int validate_string(char *str, void *esp){
  //printf("validate_string\n");
  if(validate_addr(str, esp, false) == -1){
    exit_process(-1);
    return -1;
  }
  int i = 0;
  while(str[i] != '\0'){
    if (validate_addr(str + i, esp, true) == -1){
      //printf("validate_string: bad string\n");
      exit_process(-1);
      return -1;
    }
    i++;
  }
  return 0;
}

static int validate_buffer(char *buffer, int size, void *esp, bool writable){
  //printf("validate_buffer\n");
  int i;
  for (i = 0; i < size; i++){
    if (validate_addr(buffer + i, esp, true) == -1){
      //printf("validate_buffer: bad buffer address\n");
      exit_process(-1);
      return -1;
    }
    struct sup_page_table_entry *spte = get_page(buffer + i);
    if (spte != NULL && writable){
      if (spte->read_only){
        //printf("validate_buffer: trying to write to read_only buffer\n");
        exit_process(-1);
        return -1;
      }
    }
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
    /*
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
    */
  }
  
  printf("%s: exit(%d)\n", curr->name, status);
	thread_exit();
}