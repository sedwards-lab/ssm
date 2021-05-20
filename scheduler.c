/* C99 */

/* Implementing a priority queue with a binary heap:
   https://www.cs.cmu.edu/~adamchik/15-121/lectures/Binary%20Heaps/heaps.html
   http://www.algolist.net/Data_structures/Binary_heap/Remove_minimum */

#include "ssm.h"
#include <stdio.h>

ssm_time_t now;

extern inline void leave(rar_t *, size_t);
extern inline rar_t *enter(size_t, stepf_t *,
				    rar_t *, priority_t, depth_t);
extern inline void call(rar_t *);
/* extern inline bool event_on(cv_t *); */

#define QUEUE_HEAD 1		/* NOTE: 1-indexed event queue indicies */
// TODO: we can probably store event_queue_len in the first index of event_queue
// if we were being stingy about space..

#define CONT_QUEUE_SIZE 1024
cont_queue_index_t cont_queue_len = 0;
rar_t *cont_queue[CONT_QUEUE_SIZE+QUEUE_HEAD];

#define EVENT_QUEUE_SIZE 1024
event_queue_index_t event_queue_len = 0;
cv_t *event_queue[EVENT_QUEUE_SIZE+QUEUE_HEAD];

void sensitize(cv_t *var, trigger_t *trigger)
{
  assert(var);
  assert(trigger);
  trigger->next = var->triggers;              // Point us to the first element
  if (var->triggers)
    var->triggers->prev_ptr = &trigger->next; // Make first element point to us
  var->triggers = trigger;                    // Insert us at the beginning
  trigger->prev_ptr = &var->triggers;         // Our previous is the variable
}

void desensitize(trigger_t *trigger)
{
  assert(trigger);
  assert(trigger->prev_ptr);
  *trigger->prev_ptr = trigger->next;         // Tell predecessor to skip us
  if (trigger->next)
    trigger->next->prev_ptr = trigger->prev_ptr; // Tell successor its predecessor is our predecessor
}

/**
 * Insert var into the global event_queue according to var->event_time.
 *
 * FIXME: move me to header file.
 */
void sched_event(cv_t *var)
{
	assert(var);
	assert(now < var->event_time);  /* No scheduling in this instant */
	assert(var->event_time != NO_EVENT_SCHEDULED); /* Not trying to schedule for bogus time */

	/* To insert this event in the global event queue, we start at some leaf
	 * node, and copy to parent until we find where we can put the new event.
	 */
	event_queue_index_t i = ++event_queue_len;
	assert(i <= EVENT_QUEUE_SIZE); // FIXME: should handle this better

	for (; i > QUEUE_HEAD && var->event_time < event_queue[i >> 1]->event_time; i >>= 1)
		event_queue[i] = event_queue[i >> 1];
	event_queue[i] = var;
}

/**
 * Remove var from the global event_queue, and set var->event_time to NO_EVENT_SCHEDULED.
 *
 * FIXME: move me to header file.
 *
 * For now, we do a linear scan of the queue. If this becomes a bottle neck,
 * there are bigger-brained ways to do this (though likely at the cost of an
 * additional field in the cv_t struct).
 */
void unsched_event(cv_t *var)
{
	assert(var);
	event_queue_index_t hole = 0;

	/* Find the hole that removing var creates. <= because queue is 1-indexed. */
	for (event_queue_index_t i = QUEUE_HEAD; i <= event_queue_len; i++) {
		if (event_queue[i] == var) {
			hole = i;
			break;
		}
	}
	assert(hole); /* Fails if we try to unsched an event that wasn't already scheduled (please don't). */
	var->event_time = NO_EVENT_SCHEDULED;

	/* Fill the hole with the last event */
	cv_t *to_insert = event_queue[event_queue_len--];
	for (;;) {
		/* Find earlier of hole's two children */
		event_queue_index_t child = hole << 1; /* Left child */

		if (child > event_queue_len)
			/* Reached the bottom of minheap */
			break;

		/* Compare against right child */
		if (child + 1 <= event_queue_len &&
				event_queue[child+1]->event_time < event_queue[child]->event_time)
			child++; /* Right child is earlier */

		/* Is to_insert earlier than both children? */
		if (to_insert->event_time < event_queue[child]->event_time)
			break;

		/* If not, swap earlier child up (push hole down), and descend */
		event_queue[hole] = event_queue[child];
		hole = child;
	}
	event_queue[hole] = to_insert;
}

void enqueue(rar_t *cont)
{
	assert(cont);
	if (cont->scheduled)
		return; // Don't add a continuation twice

	priority_t priority = cont->priority;

	cont_queue_index_t i = ++cont_queue_len;
	assert(i <= CONT_QUEUE_SIZE); // FIXME: should handle this better

	// Copy parent to child until we find where we can put the new one
	for (; i > QUEUE_HEAD && priority < cont_queue[i >> 1]->priority; i >>= 1)
		cont_queue[i] = cont_queue[i >> 1];
	cont_queue[i] = cont;
	cont->scheduled = true;

	/*
	printf("Scheduling %08x: ", cont->priority);
	for ( int i = 1 ; i <= cont_queue_len ; i++ )
	printf("%08x ", cont_queue[i]->priority );
	printf("\n");
	*/
}

/**
 * Schedule all sensitive continuations.
 *
 * FIXME: move docstring to declaration in header file, and actually document more.
 */
void schedule_sensitive(cv_t *cvt, priority_t priority, sel_t selector)
{
	trigger_t *trigger = cvt->triggers;
	for (; trigger; trigger = trigger->next)
		if (trigger->rar->priority > priority && trigger->start <= selector && selector < trigger->span)
					/* if (!trigger->predicate || trigger->predicate(cvt)) */
						enqueue(trigger->rar);
}

void fork(rar_t *rar)
{
  assert(rar);
  assert(rar->caller);
  enqueue(rar);
}

void tick(void)
{
	/* For each queued event scheduled for the current time, remove the event from the queue,
	 * update its variable, and schedule everything sensitive to it.
	 */
	while (event_queue_len > 0 && event_queue[QUEUE_HEAD]->event_time == now) {
		cv_t *var = event_queue[QUEUE_HEAD];

		// printf("Updating %p\n", (void *) var);

		sel_t selector = var->selector; /* Save this because ->update() may clobber it */
		var->update(var);
		schedule_sensitive(var, 0, selector);
		// ^TODO: using priority of 0 is ok for waking up, right?

		/* Remove the earliest event (var) from the queue, using it as our hole */
		event_queue_index_t hole = QUEUE_HEAD;

		/* If var no longer needs to be rescheduled.. */
		cv_t *to_insert = var->event_time == NO_EVENT_SCHEDULED ?
			event_queue[event_queue_len--] : /* reschedule the last event; */
			var; /* otherwise, reschedular var */

		for (;;) {
			/* Find earlier of hole's two children */
			event_queue_index_t child = hole << 1; /* Left child */

			if (child > event_queue_len)
				/* Reached the bottom of minheap */
				break;

			/* Compare against right child */
			if (child + 1 <= event_queue_len &&
					event_queue[child+1]->event_time < event_queue[child]->event_time)
				child++; /* Right child is earlier */

			/* Is to_insert earlier than both children? */
			if (to_insert->event_time < event_queue[child]->event_time)
				break;

			/* If not, swap earlier child up (push hole down), and descend */
			event_queue[hole] = event_queue[child];
			hole = child;
		}
		event_queue[hole] = to_insert;
	}

	/* Until the queue is empty, take the lowest-numbered continuation from the queue and run it,
	 * which might insert additional continuations in the queue.
	 *
	 * Note that we remove it from the queue first before running it in case it tries to schedule itself.
	 */
	while (cont_queue_len > 0) {
		rar_t *to_run = cont_queue[QUEUE_HEAD];
		to_run->scheduled = false;

		cont_queue_index_t hole = QUEUE_HEAD;
		rar_t *to_insert = cont_queue[cont_queue_len--];
		for (;;) {
			/* Same logic as before */
			cont_queue_index_t child = hole << 1;
			if (child > cont_queue_len)
				break;
			if (child + 1 <= cont_queue_len &&
					cont_queue[child+1]->priority < cont_queue[child]->priority)
				child++;
			if (to_insert->priority < cont_queue[child]->priority)
				break;
			cont_queue[hole] = cont_queue[child];
			hole = child;
		}
		cont_queue[hole] = to_insert;
		call(to_run);
	}
}
