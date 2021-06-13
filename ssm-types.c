/**
 * Implementations of type-specific scheduled variables, whose vtable methods
 * are aware of the size and layout of their respective payloads.
 */
#include <unistd.h>

#include "ssm-types.h"
#include "ssm-queue.h" /* For managing inner queues */
#include "ssm-runtime.h" /* For accessing current time */

const struct svtable *unit_vtable = NULL;

/**
 * Scalar definition helper macro
 */
#define DEFINE_SCHED_VARIABLE_SCALAR(payload_t)                                \
  static void update_##payload_t(struct sv *sv) {                              \
    payload_t##_svt *v = container_of(sv, payload_t##_svt, sv);                \
    v->value = v->later_value;                                                 \
  }                                                                            \
  static void assign_##payload_t(struct sv *sv, priority_t prio,               \
                                 const any_t value) {                          \
    payload_t##_svt *v = container_of(sv, payload_t##_svt, sv);                \
    v->value = (payload_t)value;                                               \
    assign_event(sv, prio);                                                    \
  }                                                                            \
  static void later_##payload_t(struct sv *sv, ssm_time_t then,                \
                                const any_t value) {                           \
    payload_t##_svt *v = container_of(sv, payload_t##_svt, sv);                \
    v->later_value = (payload_t)value;                                         \
    later_event(sv, then);                                                     \
  }                                                                            \
  static const struct payload_info payload_t##_payload_info[1] = {{            \
      .offset = offsetof(payload_t##_svt, value),                              \
      .later_offset = offsetof(payload_t##_svt, later_value),                  \
  }};                                                                          \
  const struct svtable payload_t##_vtable = {                                  \
      .update = update_##payload_t,                                            \
      .assign = assign_##payload_t,                                            \
      .later = later_##payload_t,                                              \
      .payload_info = payload_t##_payload_info,                                \
      .type_name = #payload_t,                                                 \
  }

/**
 * Define implementation for scalar types
 */
DEFINE_SCHED_VARIABLE_SCALAR(bool);
DEFINE_SCHED_VARIABLE_SCALAR(i8);
DEFINE_SCHED_VARIABLE_SCALAR(i16);
DEFINE_SCHED_VARIABLE_SCALAR(i32);
DEFINE_SCHED_VARIABLE_SCALAR(i64);
DEFINE_SCHED_VARIABLE_SCALAR(u8);
DEFINE_SCHED_VARIABLE_SCALAR(u16);
DEFINE_SCHED_VARIABLE_SCALAR(u32);
DEFINE_SCHED_VARIABLE_SCALAR(u64);


static void update_io_read(struct sv *sv) {
  io_read_svt *v = container_of(sv, io_read_svt, sv);
  if (sv->later_time == get_now()) // Didn't read in time, use default value.
    v->value = v->later_value;
  else // Detected value sooner, read from fd.
    read(v->fd, &v->value, 1);
}
static void assign_io_read(struct sv *sv, priority_t prio,
                               const any_t value) {
  io_read_svt *v = container_of(sv, io_read_svt, sv);
  v->value = (u8) value;
  assign_event(sv, prio);
}
static void later_io_read(struct sv *sv, ssm_time_t timeout,
                              const any_t default_value) {
  io_read_svt *v = container_of(sv, io_read_svt, sv);
  v->later_value = (u8) default_value;
  later_event(sv, timeout);
}
static const struct payload_info io_read_payload_info[1] = {{
    .offset = offsetof(io_read_svt, value),
    .later_offset = offsetof(io_read_svt, later_value),
}};
const struct svtable io_read_vtable = {
    .update = update_io_read,
    .assign = assign_io_read,
    .later = later_io_read,
    .payload_info = io_read_payload_info,
    .type_name = "io_read (u8)",
};

