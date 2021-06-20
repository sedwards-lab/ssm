#ifndef _SSM_IO_H
#define _SSM_IO_H

#include "ssm-types.h"
#include "ssm-debug.h"

/*
 * An IO "sv" -- a wrapper around u8_svt that contains bookkeeping info for IO.
 */
struct io_read_svt {
  u8_svt u8_sv;

  int fd;
  const char *file_name;
  struct io_read_svt *next;
  struct io_read_svt **prev_ptr;
};

/* Doubly linked list of all open files in the program -- our representation of
 * the file descriptor table.
 */
struct io_read_svt *io_vars;

/*
 * Open a file and return an io_read_svt.
 */
struct io_read_svt *open_io_var(const char *file_name);

/*
 * Return the sv representing stdin. Assumed to be open by all programs.
 */
struct sv *get_stdin_var();

/*
 * Close the file represented by v.
 */
void close_io_var(struct io_read_svt *v);

#endif /* ifndef _SSM_IO_H */
