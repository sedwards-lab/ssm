#ifndef _SSM_IO_H
#define _SSM_IO_H

#include "ssm-types.h"
#include "ssm-debug.h"

struct io_read_svt {
  u8_svt u8_sv;

  int fd;
  const char *file_name;
  struct io_read_svt *next;
  struct io_read_svt **prev_ptr;
};

struct io_read_svt *io_vars;

#endif /* _SSM_IO_H */
