/**
 * Implementation for the SSM runtime's scheduler, which is only aware of pure
 * events and abstract activation records.
 */

#define _POSIX_C_SOURCE 199309L
#include <time.h>

#include "ssm-act.h"
#include "ssm-queue.h"
#include "ssm-runtime.h"
#include "ssm-sv.h"

#define ACT_QUEUE_SIZE 1024
#define EVENT_QUEUE_SIZE 1024

/**
 * Event queue, used to track and schedule events between instants.
 *
 * Managed as a binary heap sorted by e->later_time, implemented in ssm-queue.c.
 */
struct sv *event_queue[EVENT_QUEUE_SIZE + QUEUE_HEAD];
size_t event_queue_len = 0;

/**
 * Activation record queue, used to track and schedule continuations at each
 * instant.
 *
 * Managed as a binary heap sorted by a->priority, implemented in ssm-queue.c.
 */
struct act *act_queue[ACT_QUEUE_SIZE + QUEUE_HEAD];
size_t act_queue_len = 0;

/**
 * Note that this starts out uninitialized. It is the responsibility of the
 * runtime to do so.
 */
ssm_time_t now;

/*** Internal helpers {{{ ***/

static void schedule_act(struct act *act) {
  if (!act->scheduled)
    enqueue_act(act_queue, &act_queue_len, act);
  act->scheduled = true;
}

static struct act *unschedule_act(idx_t idx) {
  assert(idx >= QUEUE_HEAD);
  struct act *act = act_queue[idx];
  assert(act->scheduled);

  act->scheduled = false;
  dequeue_act(act_queue, &act_queue_len, idx);
  return act;
}

/**
 * Enqueue lower priority sensitive continuations of a scheduled variable.
 */
static void schedule_sensitive_triggers(struct sv *sv, priority_t priority) {
  for (struct trigger *trigger = sv->triggers; trigger; trigger = trigger->next)
    if (trigger->act->priority > priority)
      if (!trigger->act->scheduled)
        schedule_act(trigger->act);
}

/**
 * Enqueue all the sensitive continuations of a scheduled variable.
 */
static void schedule_all_sensitive_triggers(struct sv *sv) {
  for (struct trigger *trigger = sv->triggers; trigger; trigger = trigger->next)
    if (!trigger->act->scheduled)
      schedule_act(trigger->act);
}

/**
 * Calls sv->update to perform a delayed assignment, if its update
 * method/payload exists, then sets sv->later_time to NO_EVENT_SCHEDULED if
 * sv is an atomic type. Finally, put its sensitive triggers on act_queue.
 */
static void update_event(struct sv *sv) {
  assert(sv->later_time == now);

#ifdef DEBUG
  printf("update event: %s\n", sv->var_name);
#endif
  if (sv->vtable)
  /* Non-unit type, so we need to call update method to update payload */
#ifdef DEBUG
    printf("calling update: %s\n", sv->vtable->type_name),
#endif
        sv->vtable->update(sv);

  sv->later_time = NO_EVENT_SCHEDULED;
  sv->last_updated = sv->later_time;

  schedule_all_sensitive_triggers(sv);
}

/*** Internal helpers }}} ***/

/*** Events API, exposed via ssm-event.h {{{ ***/

void initialize_event(struct sv *sv, const struct svtable *vtable) {
  sv->vtable = vtable;
  sv->triggers = NULL;
  sv->last_updated = now;
  sv->later_time = NO_EVENT_SCHEDULED;
  sv->var_name = "(no var name)";
}

void assign_event(struct sv *sv, priority_t prio) {
  if (sv->later_time != NO_EVENT_SCHEDULED) {
    /* Note that for aggregate data types, we assume that any conflicting
     * updates have been resolved by this point. If sv->later_time is not set to
     * NO_EVENT_SCHEDULED, that means we need to unschedule this sv from the
     * event queue.
     */
    idx_t idx = index_of_event(event_queue, &event_queue_len, sv);
    assert(QUEUE_HEAD <= idx);

    sv->later_time = NO_EVENT_SCHEDULED;
    dequeue_event(event_queue, &event_queue_len, idx);
  }

  sv->last_updated = now;
  schedule_sensitive_triggers(sv, prio);
}

void later_event(struct sv *sv, ssm_time_t then) {
  assert(then != NO_EVENT_SCHEDULED);
  assert(now < then);

  if (sv->later_time == NO_EVENT_SCHEDULED) {
    /* This event isn't already scheduled, so add it to the event queue. */
    sv->later_time = then;
    enqueue_event(event_queue, &event_queue_len, sv);
  } else {
    /* This event is already scheduled, so we need to reschedule it. */
    idx_t idx = index_of_event(event_queue, &event_queue_len, sv);
    assert(QUEUE_HEAD <= idx);
    sv->later_time = then;
    requeue_event(event_queue, &event_queue_len, idx);
  }
}

ssm_time_t last_updated_event(struct sv *sv) { return sv->last_updated; }

/*** Events API }}} ***/

/*** Activation records API, exposed via ssm-act.h {{{ ***/

void act_fork(struct act *act) {
  assert(act);
  assert(act->caller);
  schedule_act(act);
}

void sensitize(struct sv *var, struct trigger *trigger) {
  assert(var);
  assert(trigger);

  /* Point us to the first element */
  trigger->next = var->triggers;

  if (var->triggers)
    /* Make first element point to us */
    var->triggers->prev_ptr = &trigger->next;

  /* Insert us at the beginning */
  var->triggers = trigger;

  /* Our previous is the variable */
  trigger->prev_ptr = &var->triggers;
}

void desensitize(struct trigger *trigger) {
  assert(trigger);
  assert(trigger->prev_ptr);

  /* Tell predecessor to skip us */
  *trigger->prev_ptr = trigger->next;

  if (trigger->next)
    /* Tell successor its predecessor is our predecessor */
    trigger->next->prev_ptr = trigger->prev_ptr;
}

/*** Activation records API }}} ***/

/*** Runtime API, exposed via ssm-runtime.h ***/

void initialize_ssm(ssm_time_t start) { now = start; }

ssm_time_t tick() {
#ifdef DEBUG
  printf("tick called. event_queue_len: %lu\n", event_queue_len);
#endif
  /*
   * For each queued event scheduled for the current time, remove the event from
   * the queue, update its variable, and schedule everything sensitive to it.
   */
  while (event_queue_len > 0 && event_queue[QUEUE_HEAD]->later_time == now) {
    struct sv *sv = event_queue[QUEUE_HEAD];
#ifdef DEBUG
    printf("Enacting event: \n");
#endif
    update_event(sv);

    if (sv->later_time == NO_EVENT_SCHEDULED)
      /* Unschedule */
      dequeue_event(event_queue, &event_queue_len, QUEUE_HEAD);
    else
      /* Reschedule */
      requeue_event(event_queue, &event_queue_len, QUEUE_HEAD);
  }

  /*
   * Until the queue is empty, take the lowest-numbered continuation from the
   * activation record queue and run it, which might insert additional
   * continuations in the queue.
   *
   * Note that we remove it from the queue first before running it in case it
   * tries to schedule itself.
   */
#ifdef DEBUG
  printf("running activation records now. act_queue_len: %lu.\n",
         act_queue_len);
  for (int i = 0; i < act_queue_len; i++)
    printf("\tact: %s\n", act_queue[QUEUE_HEAD + i]->act_name);
#endif

  while (act_queue_len > 0) {
    struct act *to_run = unschedule_act(QUEUE_HEAD);
#ifdef DEBUG
    printf("Running routine: %s (act_queue_len: %ld)\n", to_run->act_name,
           act_queue_len);
#endif
    to_run->step(to_run);
  }

  /*
   * FIXME: this interface isn't really usable. We want the runtime driver to be
   * able to interrupt sooner than now, so that it can respond to I/O etc.
   */

  ssm_time_t next = event_queue_len > 0 ? event_queue[QUEUE_HEAD]->later_time
                                        : NO_EVENT_SCHEDULED;

  if (next != NO_EVENT_SCHEDULED) {
    time_t secs = (next - now) / 1000000;
    long ns = ((next - now) % 1000000) * 1000;

    struct timespec dur, rem = { secs, ns };
    while (1) {
      if (!nanosleep(&dur, &rem)) {
        break;
      }

      // Assuming nanosleep got interrupted...
      dur = rem;
      continue;
    }
  }

  now = next;
  return now;
}

/*** Runtime API }}} ***/
