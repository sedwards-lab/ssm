/**
 * Implementations of type-specific scheduled variables, whose vtable methods
 * are aware of the size and layout of their respective payloads.
 */

#include "ssm-types.h"

/**
 * Scalar type definition helper macro.
 */
#define DEFINE_SCHED_VARIABLE_SCALAR(payload_t)                                \
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
  void initialize_##payload_t(payload_t##_svt *v) {                            \
    initialize_event(&v->sv);                                                  \
    v->sv.update = update_##payload_t;                                         \
  }

/**
 * Define implementation for scalar types
 */
DEFINE_SCHED_VARIABLE_SCALAR(bool)
DEFINE_SCHED_VARIABLE_SCALAR(i8)
DEFINE_SCHED_VARIABLE_SCALAR(i16)
DEFINE_SCHED_VARIABLE_SCALAR(i32)
DEFINE_SCHED_VARIABLE_SCALAR(i64)
DEFINE_SCHED_VARIABLE_SCALAR(u8)
DEFINE_SCHED_VARIABLE_SCALAR(u16)
DEFINE_SCHED_VARIABLE_SCALAR(u32)
DEFINE_SCHED_VARIABLE_SCALAR(u64)
