#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include "ssm-io.h"

struct io_read_svt *open_io_var(const char *file_name) {
  struct io_read_svt *v = malloc(sizeof(struct io_read_svt));
  assert(v);
  initialize_event(&v->u8_sv.sv, &u8_vtable);
  if (!strcmp(file_name, "stdin")) {
    DEBUG_SV_NAME(&v->u8_sv.sv, "stdin");
    v->file_name = v->u8_sv.sv.var_name = "stdin";
    v->fd = STDIN_FILENO;
  } else {
    v->fd = open(file_name, O_RDONLY);
    assert(v->fd != -1);
  }

  v->next = io_vars;
  if (io_vars)
    io_vars->prev_ptr = &v->next;

  io_vars = v;

  v->prev_ptr = &io_vars;

  return v;
}

struct sv *get_stdin_var() {
  for (struct io_read_svt *io_sv = io_vars; io_sv; io_sv = io_sv->next) {
    if (!strcmp(io_sv->file_name, "stdin"))
      return &io_sv->u8_sv.sv;
  }

  return NULL;
}

void close_io_var(struct io_read_svt *v) {
  *v->prev_ptr = v->next;

  if (v->next)
    v->next->prev_ptr = v->prev_ptr;

  close(v->fd);
  free(v);
}
