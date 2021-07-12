/**
 * Linux implementation of SSM's IO interface -- a doubly linked list
 * representing the system's file descriptor table.
 */

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include "ssm-io.h"

struct io_read_svt io_vars[MAX_IO_VARS];

int ssm_max_fd = -1;
fd_set ssm_read_fds;

void initialize_io() {
  struct io_read_svt *stdin_v = &io_vars[STDIN_FILENO];
  initialize_event(&stdin_v->u8_sv.sv, &u8_vtable);
  stdin_v->fd = STDIN_FILENO;
  stdin_v->file_name = "stdio";
  stdin_v->is_open = true;

  FD_SET(STDIN_FILENO, &ssm_read_fds);
  ssm_max_fd = 0;
}

void deinitialize_io() {
  for (int i = 0; i < MAX_IO_VARS; i++) {
    if (io_vars[i].is_open) {
      close(io_vars[i].fd);
      io_vars[i].file_name = "";
      io_vars[i].is_open = false;
    }
  }

  FD_ZERO(&ssm_read_fds);
  ssm_max_fd = -1;
}

struct io_read_svt *open_io_var(const char *file_name) {
  int fd = open(file_name, O_RDONLY);
  assert(fd != -1);
  struct io_read_svt *v = io_vars + fd;

  initialize_event(&v->u8_sv.sv, &u8_vtable);
  v->fd = fd;
  v->file_name = file_name;
  v->is_open = true;

  FD_SET(fd, &ssm_read_fds);
  ssm_max_fd = (fd > ssm_max_fd) ? fd : ssm_max_fd;
  return v;
}

u8_svt *get_stdin_var() {
  return &io_vars[STDIN_FILENO].u8_sv;
}

void close_io_var(struct io_read_svt *v) {
  v->is_open = false;
  v->file_name = "";
  close(v->fd);

  FD_CLR(v->fd, &ssm_read_fds);
  ssm_max_fd = -1;
  if (v->fd == ssm_max_fd) {
    for (int fd = ssm_max_fd - 1; fd >= 0; fd--) {
      if (io_vars[fd].is_open) {
        ssm_max_fd = fd;
        break;
      }
    }
  }
}
