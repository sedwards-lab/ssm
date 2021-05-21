#ifndef _SSM_TYPES_H
#define _SSM_TYPES_H

/**
 * Shared between type stubs and activation records; the scheduler should be
 * agonistic to anything declared here.
 */

#include "ssm-core.h"
#include "ssm-sv.h"

/**
 * SSM's pointer type is generic under the hood, but we can synthesize a fake
 * (typedef'd) pointer type for each user-defined type.
 */
struct svt_ptr {
  struct sv *ptr;
  sel_t selector;
};

#define PTR_ASSIGN(ptr_sv, prio, val)                                          \
  (ptr_sv).ptr->vtable->assign((ptr_sv).ptr, prio, val, (ptr_sv).selector)

#define PTR_LATER(ptr_sv, then, val)                                           \
  (ptr_sv).ptr->vtable->later((ptr_sv).ptr, then, val, (ptr_sv).selector)

#define PTR_OF_SV(sv)                                                          \
  (struct svt_ptr) { .ptr = &(sv), .selector = 0 }

#define DEREF(type, ptr_sv)                                                    \
  (type *)(((char *)(ptr_sv).ptr) +                                            \
           (ptr_sv).ptr->vtable->sel_info[(ptr_sv).selector].offset)

/*** Unit type {{{
 *
 * Unit types are just "pure" events without payloads, and are all that the
 * scheduler is aware of. For the sake of consistency, we "declare" them as
 * a distinct type, except their implementation will entirely alias that of the
 * sched-event API.
 */
typedef struct sv unit_svt;
typedef struct svt_ptr ptr_unit_svt;
extern const struct svtable *unit_vtable;

/* Unit type }}} */

/*** Declaration helpers {{{ */

/**
 * Declare a scheduled variable type whose payload can be directly assigned by
 * value (in C).
 *
 * Note that since C supports pass-by-value for structs, we can use this for
 * any atomic (non-aggregate) data type.
 */
#define DECLARE_SCHED_VARIABLE_SCALAR(payload_t)                               \
  typedef struct {                                                             \
    struct sv sv;                                                              \
    payload_t value;       /* Current value */                                 \
    payload_t later_value; /* Buffered value */                                \
  } payload_t##_svt;                                                           \
  extern const struct svtable payload_t##_vtable;                              \
  typedef struct svt_ptr ptr_##payload_t##_svt

/**
 * Declare an aggregate scheduled variable type for payload type payload_t,
 * whose max selector value is sel_max.
 *
 * The payload_param is used to indicate what the initialization function
 * parameter type should be. If the top-level type of payload_t is a struct,
 * payload_param be payload_t *. If the top-level type of payload_t is an array
 * of T (e.g., [3]int), then payload_param should just be T * (e.g., int *),
 * to conform with C's parameter-passing conventions.
 */
#define DECLARE_SCHED_VARIABLE_AGGREGATE(payload_t, payload_param, sel_max)    \
  typedef struct {                                                             \
    struct sv sv;                                                              \
    payload_t value;                    /* Current value */                    \
    payload_t later_value;              /* Buffered value */                   \
    ssm_time_t inner_time[sel_max + 1]; /* event_time per selector */          \
    ssm_time_t last_updated[sel_max + 1];                                      \
    sel_t inner_queue[sel_max + 1 + QUEUE_HEAD];                               \
  } payload_t##_svt;                                                           \
  extern const struct svtable payload_t##_vtable;                              \
  typedef struct svt_ptr ptr_##payload_t##_svt

/* Declaration helpers }}} */

/*** Scalar types {{{ */

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

DECLARE_SCHED_VARIABLE_SCALAR(bool);
DECLARE_SCHED_VARIABLE_SCALAR(i8);
DECLARE_SCHED_VARIABLE_SCALAR(i16);
DECLARE_SCHED_VARIABLE_SCALAR(i32);
DECLARE_SCHED_VARIABLE_SCALAR(i64);
DECLARE_SCHED_VARIABLE_SCALAR(u8);
DECLARE_SCHED_VARIABLE_SCALAR(u16);
DECLARE_SCHED_VARIABLE_SCALAR(u32);
DECLARE_SCHED_VARIABLE_SCALAR(u64);

/* Scalar types }}} */

/*** Aggregate types {{{ */

/* TODO: write these xD */

typedef struct {
  i32 left;
  i32 right;
} tup2_i32;

/* Aggregate types }}} */

#endif /* _SSM_TYPES_H */

/* vim: set ts=2 sw=2 tw=80 et foldmethod=marker :*/
