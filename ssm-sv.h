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

/**
 * Represents the whole value for aggregate data types, and the only selector
 * value for atomic and unit types.
 */
#define SELECTOR_ROOT 0

typedef unsigned long offset_t;
/** Information about each selector. */
struct sel_info {
  offset_t offset;       /* Bytes from sv to payload at selector */
  offset_t later_offset; /* Bytes from sv to buffered payload at selector */
  sel_t range;           /* Number of children for selector */
};

/**
 * Used as untyped parameter value for data type-generic parameters.
 * FIXME: this is quite hacky.
 */
typedef uint64_t any_t;

/** The virtual table for each scheduled variable type. */
struct svtable {
#ifdef DEBUG
  const char *type_name;
#endif

  /**
   * The maximum selector value for the type. sel_max should be 0 if and only if
   * the scheduled variable type is atomic (its inner components cannot be
   * scheduled separately).
   */
  sel_t sel_max;

  /**
   * Callback to update a channel variable. Called in tick().
   *
   * Reponsible for:
   * - Updating value (if there is one), according to later_value and selector.
   * - Setting last_updated time(s) to now, according to selector.
   * - Setting event_time to when the channel variable should be next scheduled,
   *   or NO_EVENT_SCHEDULED if it shouldn't.
   * - Setting selector and later_value if sv should be rescheduled.
   *
   * Not responsible for:
   * - Restting selector or later_value if sv isn't being rescheduled; these
   *   will get overwritten later anyway.
   */
  sel_t (*update)(struct sv *);

  /**
   * Assign to the unit type in the current instant, and schedule all sensitive
   * processes to run. Called by user-defined routine.
   *
   * Responsible for:
   * - Updating value (if there is one) to the given value, and according to the
   *   given selector.
   * - Aggregate types: resolving any inner update conflicts. This means
   *   internally unscheduling any conflicting updates. If the globally queued
   *   update (exposed by later_time and later_selector) is conflicting, leave
   *   later_time set, so that assign_event will unschedule it. Otherwise, set
   *   later_time to NO_EVENT_SCHEDULED, to leave event in the queue. Set
   *   later_selector to the given selector.
   * - Calling assign_event, which will unschedule the event if later_time is
   *   not NO_EVENT_SCHEDULED, set last_updated to now for atomic types, and
   *   schedule sensitive triggers.
   * - Aggregate types: restore later_time and later_selector for subsequent,
   *   non-conflicting updates.
   *
   * Not responsible for:
   * - Atomic types: unsetting later_time; this is done by assign_event, which
   *   will also unschedule the event from the event queue.
   */
  void (*assign)(struct sv *, priority_t, const any_t, sel_t);

  /**
   * Schedule a delayed assignment at the given time. Called by user-defined
   * routine.
   *
   * Responsible for:
   * - Setting later_value to the given value, according to the given selector.
   * - Aggregate types: adding update to inner queue, and resolving any inner
   *   update conflicts. If the globally queued update is conflicting, or later
   *   than the given time, leave later_time as is so the event can be
   *   rescheduled, and set later_selector according to the given selector.
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
  void (*later)(struct sv *, ssm_time_t, const any_t, sel_t);

  /**
   * Points to table of size sel_max, containing information about each member.
   *
   * TODO: this table can be inlined into the vtable to reduce one level of
   * indirection, but that involves some pointer casting I don't want to do just
   * yet.
   */
  const struct sel_info *sel_info;
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
  union {
    ssm_time_t last_updated; /* When vtable->sel_max == 0 */
    sel_t later_selector;    /* Otherwise */
  } u;
#ifdef DEBUG
  const char *var_name;
#endif
};

/**
 * Scheduling interface for scheduled variables. Defined in ssm-sched.c, and
 * used by type-specific implementations.
 */
extern void initialize_event(struct sv *);
extern void assign_event(struct sv *, priority_t);
extern void later_event(struct sv *, ssm_time_t);

#endif /* _SSM_SV_H */
