#ifndef _PENG_BASE_H
#define _PENG_BASE_H

/* Assumes C99 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>

typedef uint64_t uint64;
typedef int64_t int64;
typedef uint8_t  uint8;

/* A year is 31,536,000 seconds, which fits in 25 bits
   There are 1,000,000 microseconds in a second, which fits in 20 bits
   If we count microseconds, we only have 12 bits left in 32 bits,
   enough for 4096 seconds, about 68 minutes or a little more than an hour.
   64 bits of microseconds gives 19 bits of years, over 500ky: plenty.
 */
typedef uint64_t peng_time_t;         // timestamps: assumed never to overflow
#define NO_EVENT_SCHEDULED ULONG_MAX

extern peng_time_t now;               // Name of current instant

// Thread priority types
typedef uint32_t priority_t; // 32-bit thread priority
typedef uint8_t  depth_t;   // Index of least significant priority bit
#define PRIORITY_AT_ROOT 0
#define DEPTH_AT_ROOT 32

// Routine activation records

typedef struct act act_t;      // Routine activation record "Base class"
typedef void stepf_t(act_t *); // Type of a step function

#define ACTIVATION_RECORD_FIELDS \
  stepf_t *step;            /* C function for running this continuation */ \
  struct act *caller;       /* Activation record of caller */\
  uint16_t pc;	            /* Stored "program counter" for the function */\
  uint16_t children;        /* Number of running child threads */\
  priority_t priority;	    /* Execution priority */\
  depth_t depth;            /* Index of the LSB in our priority */\
  bool scheduled            /* True when in the schedule queue */
  
struct act {  // Start of every function activation record
  ACTIVATION_RECORD_FIELDS;
};

// Enter a function: allocate the activation record, set up the
// function and program counter value, and remember the caller
inline act_t *
enter(size_t bytes, stepf_t *step, act_t *parent,
      priority_t priority, depth_t depth)
{
  assert(bytes > 0);
  assert(step);
  assert(parent);
  ++parent->children; // Add ourself as a child
  act_t *act = calloc(1,bytes);
  *act = (act_t) { .step = step,
		   .caller = parent,
		   .pc = 0,
		   .children = 0,
		   .priority = priority,
		   .depth = depth,
		   .scheduled = false };
  return act;
}

// Execute a routine immediately
inline void call(act_t *act) { (*(act->step))(act); }

// Schedule a routine for the current instant
extern void fork_routine(act_t *);

// Deallocate an activation record; return to caller if we were the last child
inline void leave(act_t *act, size_t bytes)
{
  assert(act);
  assert(act->caller);
  assert(act->caller->step);
  act_t *caller = act->caller;
  free(act);   // Free the whole activation record, not just the start
  if ((--caller->children) == 0) // Were we the last child?
    call(caller);                // If so, run our parent
}

// Deallocate an activation record, but don't return to caller
inline void leave_no_sched(act_t *act, size_t bytes) {
  assert(act);
  free(act);
}

// Scheduled variables

typedef struct sv sv_t;

#define SCHEDULED_VARIABLE_FIELDS \
  void (*update)(sv_t *);   /* Function to update this particular type       */\
  struct trigger *triggers; /* Doubly-linked list of sensitive continuations */\
  peng_time_t last_updated;  /* Time of last update, for detecting event     */\
  peng_time_t event_time;     /* Time at which the variable should be updated */\
  void (*to_string)(sv_t *, char *, size_t)

// "Base class" for scheduled variable
struct sv {
    SCHEDULED_VARIABLE_FIELDS;
};

// Was there an event on the given variable in the current instant?
inline bool event_on(sv_t *var) { return var->last_updated == now; }

extern void schedule_sensitive(sv_t *, priority_t);

// Event scheduled variables (valueless, pure events)
typedef sv_t sv_event_t;
extern void initialize_event(sv_event_t *);
extern void assign_event(sv_event_t *, priority_t);
extern void later_event(sv_t *, peng_time_t);


// A trigger: indicates that a write to a scheduled variable
// should schedule a routine to run in the current instant
// Each function needs at least one of these for every variable
// mentioned at the most complex "await" point
typedef struct trigger trigger_t;
struct trigger {
  act_t *act;             // Routine triggered by this scheduled variable

  trigger_t *next;       // Next trigger sensitive to this variable, if any
  trigger_t **prev_ptr;  // Pointer to ourself in previous list element
};


// Event queue: variable updates scheduled for the future
typedef uint16_t event_queue_index_t; // Number in the queue/highest index
extern event_queue_index_t event_queue_len;
extern sv_t *event_queue[];

extern void sensitize(sv_t *, trigger_t *); // Add a trigger to a variable
extern void desensitize(trigger_t *);      // Remove a trigger from its variable


// Continuation queue: scheduled for current instant
typedef uint16_t cont_queue_index_t;  // Number in the queue/highest index
extern cont_queue_index_t cont_queue_len;
extern act_t *cont_queue[];

extern peng_time_t next_event_time(void); // Time of the next event

extern void tick(void); // Execute the system for the current instant

#endif /* _PENG_BASE_H */
