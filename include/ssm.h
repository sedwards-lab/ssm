#ifndef _SSM_H
#define _SSM_H

/**
 *
 * Main SSM API: user-visible types and functions
 *
 */

/* For uint16_t, etc. */
#include <stdint.h>
/* For bool, true, false */
#include <stdbool.h>
/* For size_t */
#include <stdlib.h>
/* For ULONG_MAX */
#include <limits.h>
#include <assert.h>

#define SSM_NANOSECOND(x)  (x)
#define SSM_MICROSECOND(x) ((x) *          1000L)
#define SSM_MILLISECOND(x) ((x) *       1000000L)
#define SSM_SECOND(x)      ((x) *    1000000000L)
#define SSM_MINUTE(x)      ((x) *   60000000000L)
#define SSM_HOUR(x)        ((x) * 3600000000000L)

/** \brief Absolute time; never to overflow. */
typedef uint64_t ssm_time_t;

/** \brief Time used to indicate something will never happen */
#define SSM_NEVER ULONG_MAX

/** \brief Thread priority.  Lower numbers execute first */
typedef uint32_t ssm_priority_t;

/** \brief The priority for the entry point of an SSM program. */
#define SSM_ROOT_PRIORITY 0

/** \brief Index of least significant bit in a group of priorities */
typedef uint8_t ssm_depth_t;

/** \brief The depth at the entry point of an SSM program. */
#define SSM_ROOT_DEPTH 32

struct ssm_sv;
struct ssm_trigger;
struct ssm_act;

/** \brief The function that does an instant's work for a routine */
typedef void ssm_stepf_t(struct ssm_act *);

/** \brief Activation record for an SSM routine

    Routine activation record "base class." A struct for a particular
    routine must start with this type but then may be followed by
    routine-specific fields 
*/
struct ssm_act {
  ssm_stepf_t *step;       /**< C function for running this continuation */
  struct ssm_act *caller;  /**< Activation record of caller */
  uint16_t pc;             /**< Stored "program counter" for the function */
  uint16_t children;       /**< Number of running child threads */
  ssm_priority_t priority; /**< Execution priority */
  ssm_depth_t depth;       /**< Index of the LSB in our priority */
  bool scheduled;          /**< True when in the schedule queue */
};

/** \brief  Indicates a routine should run when a scheduled variable is written
 *
 * Node in linked list of activation records, maintained by each scheduled
 * variable to determine which continuations should be scheduled when the
 * variable is updated.
 */
struct ssm_trigger {
  struct ssm_trigger *next;      /**< Next sensitive trigger, if any */
  struct ssm_trigger **prev_ptr; /**< Pointer to ourself in previous list element */
  struct ssm_act *act;           /**< Routine triggered by this channel variable */
};

/** \brief Indicate writing to a variable should trigger a routine
 *
 * Add a trigger to a variable's list of triggers.
 * When the scheduled variable is written, the scheduler
 * will run the trigger's routine routine
 */
extern void ssm_sensitize(struct ssm_sv *, struct ssm_trigger *);

/** \brief Disable a sensitized routine
 *
 * Remove the trigger from its variable.  Only call this on
 * a previously-sensitized trigger.
 */
extern void ssm_desensitize(struct ssm_trigger *);

/** \brief Schedule a routine to run in the current instant
 *
 * Enter the given activation record into the queue of activation
 * records.  This may be called multiple times on the same activation
 * record within an instant; only the first call has any effect.
 */
extern void ssm_activate(struct ssm_act *);

/**
 * \brief Execute a routine immediately.
 */
static inline void ssm_call(struct ssm_act *act) { (*(act->step))(act); }

/** \brief Enter a routine
 *
 * Enter a function: allocate the activation record, set up the function and
 * program counter value, and remember the caller.
 *
 */
static inline struct ssm_act *ssm_enter(size_t bytes,
					ssm_stepf_t *step,
					struct ssm_act *parent,
					ssm_priority_t priority,
					ssm_depth_t depth) {
  assert(bytes > 0);
  assert(step);
  assert(parent);
  ++parent->children;
  struct ssm_act *act = (struct ssm_act *)malloc(bytes);
  *act = (struct ssm_act){
      .step = step,
      .caller = parent,
      .pc = 0,
      .children = 0,
      .priority = priority,
      .depth = depth,
      .scheduled = false,
  };
  return act;
}

/**
 * \brief Deallocate an activation record; return to caller if we were the last child.
 */
static inline void ssm_leave(struct ssm_act *act, size_t bytes) {
  assert(act);
  assert(act->caller);
  assert(act->caller->step);
  struct ssm_act *caller = act->caller;
  free(act); /* Free the whole activation record, not just the start */
  if ((--caller->children) == 0)
    ssm_call(caller); /* If we were the last child, run our parent */
}

/** \brief A variable that may have scheduled updates and triggers
 *
 * This is the "base class" for other scheduled variable types.
 *
 * On its own, this represents a pure event variable, i.e., a scheduled variable
 * with no data/payload.
 *
 * The update field must point to code that copies the new value of
 * the scheduled variable into its current value.  For pure events,
 * this function may do nothing, but the pointer must be non-zero.
 *
 * This can also be embedded in a wrapper struct/class to implement a scheduled
 * variable with a payload. In this case, the payload should also be embedded
 * in that wrapper class, and the vtable should have update/assign/later
 * methods specialized to be aware of the size and layout of the wrapper class.
 */
struct ssm_sv {
  void (*update)(struct ssm_sv *); /**< Update virtual method */
  struct ssm_trigger *triggers;    /**< List of sensitive continuations */
  ssm_time_t later_time;       /**< When the variable should be next updated */
  ssm_time_t last_updated;     /**< When the variable was last updated */
};

/** \brief Return the time of the next event in the queue or SSM_NEVER */
ssm_time_t ssm_next_event_time(void);

/** \brief Update variables of pending events; run every routine so triggered
 */
void ssm_tick();

#endif