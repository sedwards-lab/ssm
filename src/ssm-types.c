/**
 * Implementations of type-specific scheduled variables, whose vtable methods
 * are aware of the size and layout of their respective payloads.
 */

#include <ssm-types.h>

/**
 * Scalar type definition helper macro.
 */
#define DEFINE_SCHED_VARIABLE_SCALAR(payload_t, fmt)                           \
  static void update_##payload_t(struct sv *sv) {                              \
    payload_t##_svt *v = container_of(sv, payload_t##_svt, sv);                \
    v->value = v->later_value;                                                 \
  }                                                                            \
  void assign_##payload_t(struct sv *sv, priority_t prio,                      \
                          const payload_t value) {                             \
    payload_t##_svt *v = container_of(sv, payload_t##_svt, sv);                \
    v->value = (payload_t)value;                                               \
    assign_event(sv, prio);                                                    \
  }                                                                            \
  void later_##payload_t(struct sv *sv, ssm_time_t then,                       \
                         const payload_t value) {                              \
    payload_t##_svt *v = container_of(sv, payload_t##_svt, sv);                \
    v->later_value = (payload_t)value;                                         \
    later_event(sv, then);                                                     \
  }                                                                            \
  struct debug_buffer value_repr_##payload_t(struct sv *sv) {                  \
    struct debug_buffer buf;                                                   \
    payload_t##_svt *v = container_of(sv, payload_t##_svt, sv);                \
    snprintf(buf.buf, sizeof(buf.buf), fmt, v->value);                         \
    return buf;                                                                \
  }                                                                            \
  void initialize_##payload_t(payload_t##_svt *v) {                            \
    initialize_event(&v->sv);                                                  \
    v->sv.update = update_##payload_t;                                         \
    v->sv.debug.value_repr = value_repr_##payload_t;                           \
  }

/**
 * Define implementation for scalar types.
 */
DEFINE_SCHED_VARIABLE_SCALAR(bool, "%d")
DEFINE_SCHED_VARIABLE_SCALAR(i8, "%d")
DEFINE_SCHED_VARIABLE_SCALAR(i16, "%d")
DEFINE_SCHED_VARIABLE_SCALAR(i32, "%d")
DEFINE_SCHED_VARIABLE_SCALAR(i64, "%ld")
DEFINE_SCHED_VARIABLE_SCALAR(u8, "%u")
DEFINE_SCHED_VARIABLE_SCALAR(u16, "%u")
DEFINE_SCHED_VARIABLE_SCALAR(u32, "%u")
DEFINE_SCHED_VARIABLE_SCALAR(u64, "%lu")
