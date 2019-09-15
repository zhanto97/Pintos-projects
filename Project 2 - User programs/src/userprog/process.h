#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

#define ARGC_LIMIT 40

struct file_wrapper{
    struct file *fp;
    int fd;
    struct list_elem file_elem;     // Membership to open_files list of struct thread
};

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
