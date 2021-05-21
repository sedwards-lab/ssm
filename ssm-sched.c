/**
 * Implementation for the SSM runtime's scheduler, which is only aware of pure
 * events and abstract activation records.
 */

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
idx_t event_queue_len = 0;

/**
 * Activation record queue, used to track and schedule continuations at each
 * instant.
 *
 * Managed as a binary heap sorted by a->priority, implemented in ssm-queue.c.
 */
struct act *act_queue[ACT_QUEUE_SIZE + QUEUE_HEAD];
idx_t act_queue_len = 0;

ssm_time_t now; /* FIXME: Should we initialize? */

/*** Internal helpers {{{ ***/

/**
 * Enqueue all the sensitive continuations of a scheduled variable.
 */
static void schedule_sensitive_triggers(struct sv *sv, priority_t priority,
                                        sel_t selector) {
  for (struct trigger *trigger = sv->triggers; trigger; trigger = trigger->next)
    if (trigger->act->priority > priority) {
      /* TODO: check predicate */
      /* && trigger->start <= selector && selector < trigger->span) */
      /* if (!trigger->predicate || trigger->predicate(cvt)) */
      enqueue_act(act_queue, &act_queue_len, trigger->act);
    }
}

/**
 * Calls sv->update to perform a delayed assignment, if its update
 * method/payload exists, then sets sv->later_time to NO_EVENT_SCHEDULED if
 * sv is an atomic type. Finally, put its sensitive triggers on act_queue.
 */
static void update_event(struct sv *sv) {
  assert(sv->later_time == now);

  sel_t selector = 0;

  if (sv->vtable)
    /* Non-unit type, so we need to call update method to update payload */
    selector = sv->vtable->update(sv);

  if (!selector && sv->vtable->sel_max == SELECTOR_ROOT) {
    /* Atomic or unit type; update its last_updated and later_time fields */
    sv->later_time = NO_EVENT_SCHEDULED;
    sv->u.last_updated = sv->later_time;
  }

  /* FIXME: don't use dummy values for priority and selector params */
  schedule_sensitive_triggers(sv, 0, selector);
}

/*** Internal helpers }}} ***/

/*** Events API, exposed via ssm-event.h {{{ ***/

void initialize_event(struct sv *sv) {
  /* For non-unit types, the caller should set sv->vtable to something else
   * after calling this function.
   */
  sv->vtable = NULL;
  sv->triggers = NULL;
  sv->u.last_updated = now;
  sv->later_time = NO_EVENT_SCHEDULED;
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

  if (sv->vtable && sv->vtable->sel_max > SELECTOR_ROOT) {
    /* Aggregate type; the caller is expected to set sv->u.later_selector to
     * that of the current assign event, so that we can use it to schedule
     * sensitive triggers.
     */
    schedule_sensitive_triggers(sv, prio, sv->u.later_selector);
    /* If there are subsequent non-conflicting updates that should remain
     * scheduled, sv->later_time and sv->u.later_selector should be restored by
     * the caller after this function returns.
     */

  } else {
    sv->u.last_updated = now;
    schedule_sensitive_triggers(sv, prio, 0);
  }
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

/*** Events API }}} ***/

/*** Activation records API, exposed via ssm-act.h {{{ ***/

void act_fork(struct act *act) {
  assert(act);
  assert(act->caller);
  enqueue_act(act_queue, &act_queue_len, act);
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

void initialize_ssm(ssm_time_t start) {
  now = start;
}

ssm_time_t tick() {
  /*
   * For each queued event scheduled for the current time, remove the event from
   * the queue, update its variable, and schedule everything sensitive to it.
   */
  while (event_queue_len > 0 && event_queue[QUEUE_HEAD]->later_time == now) {
    struct sv *sv = event_queue[QUEUE_HEAD];
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
  while (act_queue_len > 0) {
    struct act *to_run = act_queue[QUEUE_HEAD];
    to_run->scheduled = false;
    dequeue_act(act_queue, &act_queue_len, QUEUE_HEAD);
    to_run->step(to_run);
  }

  /* FIXME: this interface isn't really usable. We want the runtime driver to be
   * able to interrupt sooner than now, so that it can respond to I/O etc.
   */
  now = event_queue_len > 0 ? event_queue[QUEUE_HEAD]->later_time
                             : NO_EVENT_SCHEDULED;
  return now;
}

/*** Runtime API }}} ***/
