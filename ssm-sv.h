#ifndef _SSM_SV_H
#define _SSM_SV_H

/**
 * Generic definitions for scheduled variables, without considering payload
 * type-specific specializations. This is the generic interface that ssm-sched.c
 * uses to perform internal scheduling.
 *
 * For type-specific specifications, see ssm-types.h.
 */

#include "ssm-core.h"


typedef unsigned long offset_t;

/** Information about each sv payload. */
struct payload_info {
  offset_t offset;       /* Bytes from sv to payload */
  offset_t later_offset; /* Bytes from sv to buffered payload */
};

/**
 * Used as untyped parameter value for data type-generic parameters.
 * FIXME: this is quite hacky.
 */
typedef uint64_t any_t;

/** The virtual table for each scheduled variable type. */
struct svtable {
  /**
   * Callback to update a channel variable. Called in tick().
   *
   * Reponsible for:
   * - Updating value (if there is one), according to later_value.
   * - Setting later_time to when the channel variable should be next scheduled,
   *   or NO_EVENT_SCHEDULED if it shouldn't.
   * - Setting later_value if sv should be rescheduled.
   *
   * Not responsible for:
   * - Atomic types: setting last_updated time to now; this will be done by
   *   the caller, in tick().
   */
  void (*update)(struct sv *);

  /**
   * Assign to the unit type in the current instant, and schedule all sensitive
   * processes to run. Called by user-defined routine.
   *
   * Responsible for:
   * - Updating value (if there is one) to the given value.
   * - Calling assign_event, which will unschedule the event if later_time is
   *   not NO_EVENT_SCHEDULED, set last_updated to now for atomic types, and
   *   schedule sensitive triggers.
   *
   * Not responsible for:
   * - Atomic types: unsetting later_time; this is done by assign_event, which
   *   will also unschedule the event from the event queue.
   */
  void (*assign)(struct sv *, priority_t, const any_t);

  /**
   * Schedule a delayed assignment at the given time. Called by user-defined
   * routine.
   *
   * Responsible for:
   * - Setting later_value to the given value.
   * - Calling later_event if the event needs to be rescheduled, which will set
   *   later_time to the given time and add or reschedule the event in the event
   *   queue. Note that this does not need to be done if the scheduled event is
   *   later than the globally queued update and does not conflict with it.
   *
   * Not responsible for:
   * - Atomic types: unsetting later_time, or setting it to the given time; this
   *   is done by later_event, which will also unschedule/reschedule the event
   *   from the event queue.
   */
  void (*later)(struct sv *, ssm_time_t, const any_t);

  /**
   * Points to information about each member.
   *
   * TODO: this table can be inlined into the vtable to reduce one level of
   * indirection, but that involves some pointer casting I don't want to do just
   * yet.
   */
  const struct payload_info *payload_info;

  const char *type_name;
};

/**
 * The "base class" for other scheduled variable types.
 *
 * On its own, this represents a pure event variable, i.e., a scheduled variable
 * with no data/payload. In this case, the vtable pointer should be NULL.
 *
 * This can also be embedded in a wrapper struct/class to implement a scheduled
 * variable with a payload. In this case, the payload should also be embedded
 * in that wrapper class, and the vtable should have update/assign/later
 * methods specialized to be aware of the size and layout of the wrapper class.
 */
struct sv {
  const struct svtable *vtable; /* Pointer to the virtual table */
  struct trigger *triggers;     /* List of sensitive continuations */
  ssm_time_t later_time;        /* When the variable should be next updated */
  ssm_time_t last_updated;      /* When the variable was last updated */
  const char *var_name;
};

/**
 * Scheduling interface for scheduled variables. Defined in ssm-sched.c, and
 * used by type-specific implementations.
 */
extern void initialize_event(struct sv *, const struct svtable *);
extern void assign_event(struct sv *, priority_t);
extern void later_event(struct sv *, ssm_time_t);
extern ssm_time_t last_updated_event(struct sv *);

#endif /* _SSM_SV_H */
