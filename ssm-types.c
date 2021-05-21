/**
 * Implementations of type-specific scheduled variables, whose vtable methods
 * are aware of the size and layout of their respective payloads.
 */

#include "ssm-types.h"
#include "ssm-queue.h" /* For managing inner queues */

/**
 * Unit type "implementation", which uses underlying the pure event
 * implementation and nothing more.
 */
void (*const initialize_unit)(unit_svt *) = &initialize_event;

/**
 * Scalar definition helper macro
 */
#define DEFINE_SCHED_VARIABLE_SCALAR(payload_t)                                \
  static sel_t update_##payload_t(struct sv *sv) {                             \
    payload_t##_svt *v = container_of(sv, payload_t##_svt, sv);                \
    v->value = v->later_value;                                                 \
    return SELECTOR_ROOT;                                                      \
  }                                                                            \
  static void assign_##payload_t(struct sv *sv, priority_t prio,               \
                                 const any_t value, sel_t _selector) {         \
    payload_t##_svt *v = container_of(sv, payload_t##_svt, sv);                \
    v->value = (payload_t)value;                                               \
    assign_event(sv, prio);                                                    \
  }                                                                            \
  static void later_##payload_t(struct sv *sv, ssm_time_t then,                \
                                const any_t value, sel_t _selector) {          \
    payload_t##_svt *v = container_of(sv, payload_t##_svt, sv);                \
    v->later_value = (payload_t)value;                                         \
    later_event(sv, then);                                                     \
  }                                                                            \
  static const struct sel_info sel_info_##payload_t[1] = {                     \
      {.offset = offsetof(payload_t##_svt, value),                             \
       .later_offset = offsetof(payload_t##_svt, later_value),                 \
       .span = 1}};                                                            \
  static const struct svtable vtable_##payload_t = {                           \
      .sel_max = 0,                                                            \
      .update = update_##payload_t,                                            \
      .assign = assign_##payload_t,                                            \
      .later = later_##payload_t,                                              \
      .sel_info = sel_info_##payload_t,                                        \
      .type_name = #payload_t,                                                 \
  };                                                                           \
  void initialize_##payload_t(payload_t##_svt *v, payload_t init_value) {      \
    initialize_event(&v->sv);                                                  \
    v->value = init_value;                                                     \
    v->sv.vtable = &vtable_##payload_t;                                        \
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
