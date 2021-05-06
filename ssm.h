#ifndef _SSM_H
#define _SSM_H

/* Assumes C99 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>

/* A year is 31,536,000 seconds, which fits in 25 bits
   There are 1,000,000 microseconds in a second, which fits in 20 bits
   If we count microseconds, we only have 12 bits left in 32 bits,
   enough for 4096 seconds, about 68 minutes or a little more than an hour.
   64 bits of microseconds gives 19 bits of years, over 500ky: plenty.
 */
typedef uint64_t ssm_time_t;         // timestamps: assumed never to overflow
#define NO_EVENT_SCHEDULED ULONG_MAX
#define TICKS_PER_SECOND 1000000     // ssm_time_t counts microseconds

extern ssm_time_t now;               // Name of current instant

// Thread priority types
typedef uint32_t priority_t;	// 32-bit thread priority
typedef uint8_t  depth_t;	// Index of least significant priority bit
#define PRIORITY_AT_ROOT 1	// TODO: bumping this to 1 is fine, right?
#define DEPTH_AT_ROOT 32


// Routine activation records

typedef struct rar rar_t;      // Routine activation record "Base class"
typedef void stepf_t(rar_t *); // Type of a step function

#define ACTIVATION_RECORD_FIELDS \
  stepf_t *step;            /* C function for running this continuation */ \
  struct rar *caller;       /* Activation record of caller */\
  uint16_t pc;	            /* Stored "program counter" for the function */\
  uint16_t children;        /* Number of running child threads */\
  priority_t priority;	    /* Execution priority */\
  depth_t depth;  /* Index of the LSB in our priority */\
  bool scheduled            /* True when in the schedule queue */

struct rar {  // Start of every function activation record
  ACTIVATION_RECORD_FIELDS;
};

// Enter a function: allocate the activation record, set up the
// function and program counter value, and remember the caller
inline rar_t *
enter(size_t bytes, stepf_t *step, rar_t *parent,
      priority_t priority, depth_t depth)
{
  assert(bytes > 0);
  assert(step);
  assert(parent);
  ++parent->children; // Add ourself as a child
  rar_t *rar = malloc(bytes);
  *rar = (rar_t) { .step = step,
		   .caller = parent,
		   .pc = 0,
		   .children = 0,
		   .priority = priority,
		   .depth = depth,
		   .scheduled = false };
  return rar;
}

// Execute a routine immediately
inline void call(rar_t *rar) { (*(rar->step))(rar); }

// Schedule a routine for the current instant
extern void fork(rar_t *);

// Deallocate an activation record; return to caller if we were the last child
inline void leave(rar_t *rar, size_t bytes)
{
  assert(rar);
  assert(rar->caller);
  assert(rar->caller->step);
  rar_t *caller = rar->caller;
  free(rar);   // Free the whole activation record, not just the start
  if ((--caller->children) == 0) // Were we the last child?
    call(caller);                // If so, run our parent
}

// Channel variables

typedef unsigned long any_t;

typedef struct cv cv_t;			/* Forward-declared type name for generic channel variable */
#define CVT(type_name) type_name##_cvt	/* Channel variable name mangler */
typedef size_t sel_t;			/* Member selector for aggregate data types */
#define BAD_SELECTOR ULONG_MAX		/* Poison value for selector queue */
typedef struct trigger trigger_t;	/* Forward-declared type name for channel trigger list head */

/**
 * Channel variable interface, pointers to which are placed in the event queue.
 */
#define CHANNEL_VARIABLE_FIELDS \
	void (*update)(cv_t *);		/* See below */\
	void (*assign)(cv_t *, priority_t, const any_t, sel_t); /* See below */\
	void (*later)(cv_t *, ssm_time_t, const any_t, sel_t); /* See below */\
	struct trigger *triggers;	/* List of sensitive continuations */\
	sel_t selector;			/* Which element is being updated */\
	ssm_time_t event_time		/* Time at which the variable should be updated  */

/**
 * "Base class" for channel variable
 */
struct cv {
    CHANNEL_VARIABLE_FIELDS;
};

// TODO: place these in protected header file
extern void schedule_sensitive(trigger_t *, priority_t, sel_t);
extern void sched_event(cv_t *var);
extern void unsched_event(cv_t *var);

/**
 * Here are some methods, functions, and variables that should be defined
 * alongside any variable:
 *
 * ----
 *
 * static void update(cv_t *cvt);
 *
 * Callback to "update" a channel variable. Called by tick().
 *
 * Responsible for:
 * - Updating value (if there is one) to later_value, and according to selector
 * - Setting last_updated time to now, according to selector; for aggregate
 *   values, last_updated fields corresponding to all updated members should be
 *   updated.
 * - Setting event_time to when the channel variable should be next scheduled,
 *   or NO_EVENT_SCHEDULED if it shouldn't.
 * - Setting selector and later_value if channel variable should be rescheduled.
 *
 * Not responsible for:
 * - Resetting selector or later_value if unscheduling event;
 *   these will get overwritten later anyway
 *
 * ----
 *
 * static void assign(cv_t *v, priority_t prio, any_t value, sel_t selector);
 *
 * "Assign" to the unit type in the current instant, and schedule all sensitive
 * processes to run. Called by user process.
 *
 * Responsible for:
 * - Unscheduling queued update (if any) from global queue using unsched_event
 * - Unscheduling inner queued update (if any) from inner queue, and setting
 *   inner_time to NO_EVENT_SCHEDULED
 * - Updating value (if there is one) to given value, and according to selector
 * - Setting last_updated to now, according to selector
 * - Scheduling sensitive processes with schedule_sensitive
 *
 * Not responsible for:
 * - Directly resetting event_time to NO_EVENT_SCHEDULED. If this is needed to
 *   resolve a sched conflicts, this should be done using the unsched_event
 *   helper (where it will also be taken off of the queue); otherwise, this
 *   should remain what it was to ensure that previously scheduled,
 *   non-conflicting updates are still scheduled.
 * - Resetting selector or later_value; if the now assignment did not conflict
 *   with the already scheduled event, these should also stick around so that
 *   the scheduled event can take place. Otherwise, if these are unneeded, they
 *   will later be overwritten.
 *
 * ----
 *
 * static void later(cv_t *v, ssm_time_t then, any_t value, sel_t selector);
 *
 * Schedule a delayed assignment at time then. Called by user process.
 *
 * Responsible for:
 * - Unscheduling queued update (if any) from global queue using unsched_event
 * - Unscheduling inner queued update (if any) from inner queue, and setting
 *   inner_time to NO_EVENT_SCHEDULED
 * - Setting later_value (if any) to the assigned value, according to selector
 * - Schedule the assignment event using sched_event
 * - Setting event_time to then
 * - Setting selector
 *
 * Not responsible for:
 * - Directly resetting event_time to NO_EVENT_SCHEDULED (this is done in
 *   unsched_event)
 *
 * ----
 *
 * extern void initialize_##payload_t(CVT(payload_t) *, ...);
 *
 * Initialize a channel variable at time now. This is exported/visible to users,
 * to use to initialize freshly declared channel variables. If payload_t
 * contains a value, then this function should probably take some kind of value
 * to initialize that value to. Called by user process (specifically, the
 * process that declares the variable).
 *
 * Responsible for:
 * - Initializing triggers list to NULL (empty), and event_time to NO_EVENT_SCHEDULED
 *   (^identical code for all implementations)
 * - Initializing update, assign, and later to the appropriate callbacks
 *   (^generic across all implementations (i.e., probably can be templated))
 * - Initializing last_updated to now
 *   (^common across all implementations, but requires some knowledge about
 *   structure of payload_t)
 * - Setting initial value of channel variable payload, if any
 *   (^specific to channel variables with valued payloads)
 * - Initializing inner_queue and inner_time to BAD_SELECTOR and NO_EVENT_SCHEDULED
 *   (^specific to channel variables with aggregate payloads)
 *
 * Not reponsible for:
 * - Initializing later_value or selector; these will get overwritten later anyway
 *
 * ----
 *
 * static const sel_t payload_t##_sel_range;
 *
 * The maximum range of selectors for payload_t. For atomic (non-aggregate)
 * types, this should be 1.
 *
 * NOTE/TODO:
 * This is declared static because it should really be treated as a constant.
 * But it's also not clear whether users actually need this value beyond within
 * this header file.
 */

/**
 * Define the channel variable struct for the unit type (singleton, pure events)
 *
 * Since the unit type is a singleton (only one value), we don't need to declare
 * additional fields to represent the different values.
 */
typedef struct {
	CHANNEL_VARIABLE_FIELDS;
	ssm_time_t last_updated;
} CVT(unit);
static const sel_t unit_sel_range = 1;
extern void initialize_unit(CVT(unit) *v);

/**
 * Channel variable class declaration factory macros (wherein we rediscover C++ templates)
 */

/**
 * Declare a generic channel variable type for payload type payload_t,
 * which has sel_range distinct valid selector values.
 *
 * The payload_param is used to indicate what the initialization function
 * parameter type should be. If the top-level type of payload_t is a struct,
 * payload_param be payload_t *. If the top-level type of payload_t is an array
 * of T (e.g., [3]int), then payload_param should just be T * (e.g., int *),
 * to conform with C's parameter-passing conventions.
 *
 * For sel_range of 1, consider DECLARE_CHANNEL_VARIABLE_TYPE_VAL, which is for
 * payload types that can be assigned by value. However, for atomic payload
 * types (greater than sizeof(long long), but only meant to be assigned
 * atomically), use this macro with sel_range of 1.
 *
 * Note that the size sel_range+1 of the inner_queue is an upper bound on the
 * number of inner elements can be scheduled at one time. The true number will
 * be strictly smaller due to overlapping selectors. But that's fussy to
 * calculate.
 */
#define DECLARE_CHANNEL_VARIABLE_TYPE(payload_t, payload_param, sel_range) \
	typedef struct { \
		CHANNEL_VARIABLE_FIELDS; \
		ssm_time_t last_updated[sel_range];	/* When each internal component was last updated */\
		payload_t value;			/* Current value */\
		payload_t later_value;			/* Buffered value */\
		sel_t inner_queue[sel_range+1];		/* Prio queue for internal partial updates */\
		ssm_time_t inner_time[sel_range];	/* When each internal component will be next updated */\
	} CVT(payload_t); \
	static const sel_t payload_t##_sel_range = sel_range; \
	extern void initialize_##payload_t(CVT(payload_t) *, const payload_param) \

/**
 * Declare a generic channel variable type whose payload can be directly
 * assigned by value (in C).
 *
 * We can even directly synthesize the definition for such types.
 */
#define DECLARE_CHANNEL_VARIABLE_TYPE_VAL(payload_t) \
	typedef struct { \
		CHANNEL_VARIABLE_FIELDS; \
		ssm_time_t last_updated;		/* When value was last updated */\
		payload_t value;			/* Current value */\
		payload_t later_value;			/* Buffered value */\
	} CVT(payload_t); \
	static const sel_t payload_t##_sel_range = 1; \
	extern void initialize_##payload_t(CVT(payload_t) *, payload_t) \

/**
 * Declare a pointer to a generic channel variable.
 *
 * Note that even if a pointer points to an atomic data type (e.g.,
 * ptr_int_cvt), we still maintain a sel_t, because it is entirely possible that
 * the pointee resides in an aggregate data structure (with non-zero offset).
 */
#define PTR(pointee_cvt) ptr_##pointee_cvt
#define DECLARE_CHANNEL_VARIABLE_TYPE_PTR(pointee_cvt) \
	typedef struct { \
		cv_t *ptr;		/* Pointer to channel variable */\
		sel_t offset;		/* Offset within ptr */\
	} PTR(pointee_cvt)

/** Declare some basic data types */
DECLARE_CHANNEL_VARIABLE_TYPE_VAL(int);
DECLARE_CHANNEL_VARIABLE_TYPE_PTR(int_cvt);
DECLARE_CHANNEL_VARIABLE_TYPE_VAL(bool);
DECLARE_CHANNEL_VARIABLE_TYPE_PTR(bool_cvt);

/**
 * [3]Int data type
 *
 * Layout:	selector	member		type
 *		0		v		[3]Int
 *		1		v[0]		Int
 *		2		v[1]		Int
 *		3		v[2]		Int
 */
typedef int arr3_int[3];
DECLARE_CHANNEL_VARIABLE_TYPE(arr3_int, int *, 4);
DECLARE_CHANNEL_VARIABLE_TYPE_PTR(arr3_int);

/**
 * (Int, Int) data type
 *
 * Layout:	selector	member		type
 *		0		v		(Int, Int)
 *		1		v.left		Int
 *		2		v.right		Int
 */
typedef struct { int left; int right; } tup2_int;
DECLARE_CHANNEL_VARIABLE_TYPE(tup2_int, tup2_int *, 3);
DECLARE_CHANNEL_VARIABLE_TYPE_PTR(tup2_int);

/**
 * Triggers: indicates that a write to a channel variable should schedule a
 * routine to run in the current instant.
 *
 * Each function needs at least one of these for every variable mentioned at
 * the most complex "await" point.
 */
struct trigger {
  rar_t *rar;             /* Routine triggered by this channel variable */

  trigger_t *next;       /* Next trigger sensitive to this variable, if any */
  trigger_t **prev_ptr;  /* Pointer to ourself in previous list element */

  // TODO: range or predicate
};

/* FIXME: move to a queue header file */
// Event queue: variable updates scheduled for the future
typedef uint16_t event_queue_index_t; // Number in the queue/highest index
extern event_queue_index_t event_queue_len;
extern cv_t *event_queue[];

extern void sensitize(cv_t *, trigger_t *); // Add a trigger to a variable
extern void desensitize(trigger_t *);      // Remove a trigger from its variable


// Continuation queue: scheduled for current instant
typedef uint16_t cont_queue_index_t;  // Number in the queue/highest index
extern cont_queue_index_t cont_queue_len;
extern rar_t *cont_queue[];

extern void tick(); // Execute the system for the current instant

#endif /* _SSM_H */
