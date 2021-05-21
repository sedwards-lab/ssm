/* C99 */

/* Implementing a priority queue with a binary heap:
   https://www.cs.cmu.edu/~adamchik/15-121/lectures/Binary%20Heaps/heaps.html
   http://www.algolist.net/Data_structures/Binary_heap/Remove_minimum */

#include "peng.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

peng_time_t now;

#ifdef DEBUG
uint64_t debug_count = 0;
#endif

extern inline void leave(act_t *, size_t);
extern inline void leave_no_sched(act_t *, size_t);
extern inline act_t *enter(size_t, stepf_t *,
				    act_t *, priority_t, depth_t);
extern inline void call(act_t *);
extern inline bool event_on(sv_t *);

void sift_down(event_queue_index_t i);
void sift_up(event_queue_index_t i);

#define CONT_QUEUE_SIZE 8192
cont_queue_index_t cont_queue_len = 0;
act_t *cont_queue[CONT_QUEUE_SIZE+1];

#define EVENT_QUEUE_SIZE 8192
event_queue_index_t event_queue_len = 0;
sv_t *event_queue[EVENT_QUEUE_SIZE+1];

#ifdef DEBUG
int can_schedule(sv_t *var) {
  if(var->event_time == NO_EVENT_SCHEDULED) {
    return event_queue_len + 1 <= EVENT_QUEUE_SIZE;
  }
}

int can_fork() {
  return (cont_queue_len + 1 <= CONT_QUEUE_SIZE);
}
#endif

void sensitize(sv_t *var, trigger_t *trigger)
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

/* Enqueue an event in the event queue. This function assumes the variable we
 * are inserting is not already present in the queue.
 */
void enqueue_event(sv_t *var, peng_time_t then) {
  assert(var);
  assert(now < then);

  // Initially insert new node at end, which breaks invariant
  event_queue_index_t i = ++event_queue_len;
  var->event_time = then;
  assert (i <= EVENT_QUEUE_SIZE);
  event_queue[i] = var;
  sift_up(i);
}

sv_t* dequeue_minimum() {
  sv_t *earliest = event_queue[1];
  event_queue[1] = event_queue[event_queue_len--];
  sift_down(1);
  return earliest;
}

// Dequeue an event from the event queue. Assumes the element is present in the queue.
void dequeue_event(sv_t *var) {
  assert(var);
  if(var->event_time == NO_EVENT_SCHEDULED) {
    return;
  }

  event_queue_index_t i = 1;
  // this search can be made better, but it was not clear to me if the children
  // were ordered in any way. I am not sure which child to 'follow'.
  while(i < event_queue_len && event_queue[i] != var) {
    i++;
  }

  event_queue[i] = event_queue[event_queue_len--];
  sift_down(i);
}

// sift the element at index i down to its proper place.
void sift_down(event_queue_index_t i) {

    peng_time_t then           = event_queue[i]->event_time;
    event_queue_index_t parent = i;

    for (;;) {
      event_queue_index_t child = parent << 1; // Left child
      // if the child is 'outside' of the event queue we can not sift down more
      if ( child > event_queue_len) {
          break;
      }
      // if the right child is earlier than the right one, we want to sift with the right now
      if ( child + 1 <= event_queue_len && event_queue[child+1]->event_time < event_queue[child]->event_time ) {
	        child++;
      }
      // if then is already earlier than the child we don't want to sift
      if (then < event_queue[child]->event_time) {
          break;
      }
      // otherwise the element we are looking at should be sifted down one step
      sv_t *elem          = event_queue[parent];
      event_queue[parent] = event_queue[child];
      event_queue[child]  = elem;
      parent              = child;
    }
}

// sift the element at index i up to its right place
void sift_up(event_queue_index_t i) {
  event_queue_index_t child = i;
  while(child > 1) {
    sv_t *elem = event_queue[child];
    event_queue_index_t parent = child >> 1;
    // if the event time of the child is smaller than that of the parent,
    // replace the child with the parent and sift again
    if(elem->event_time < event_queue[parent]->event_time) {
      event_queue[child] = event_queue[parent];
      event_queue[parent] = elem;
      child = parent;
    // otherwise the child is in the right place and we should not sift anymore
    } else {
      return;
    }
  }
}

void later_event(sv_t *var, peng_time_t then)
{
  assert(var);
  assert(now < then);  // No scheduling in this instant
//  printf("schedule at %lu\n", then);
  if ( var->event_time == NO_EVENT_SCHEDULED ) {
    enqueue_event(var, then);
  } else {
    //assert(0);
    dequeue_event(var);
    enqueue_event(var, then);
  }

  event_queue_index_t i = 1;
}

void enqueue(act_t *cont)
{
  assert(cont);
  if (cont->scheduled) return; // Don't add a continuation twice

  priority_t priority = cont->priority;

  cont_queue_index_t i = ++cont_queue_len;
  assert( i <= CONT_QUEUE_SIZE ); // FIXME: should handle this better

  // Copy parent to child until we find where we can put the new one
  for ( ; i > 1 && priority < cont_queue[i >> 1]->priority ; i >>= 1 )
    cont_queue[i] = cont_queue[i >> 1];
  cont_queue[i] = cont;
  cont->scheduled = true;
}

void schedule_sensitive(sv_t *var, priority_t priority)
{
  assert(var);
  for (trigger_t *trigger = var->triggers ; trigger ; trigger = trigger->next)
    if (trigger->act->priority > priority)
      enqueue( trigger->act );
}

void fork_routine(act_t *act)
{
  assert(act);
  assert(act->caller);
  enqueue(act);
}


// Nothing to do
void update_event(sv_t *var)
{
  assert(var);
  assert(var->event_time == now);
}

void initialize_event(sv_event_t *v)
{
  assert(v);
  *v = (sv_event_t) { .update = update_event,
		      .triggers = NULL,
		      .last_updated = now,
		      .event_time = NO_EVENT_SCHEDULED };
}

void assign_event(sv_event_t *iv, priority_t priority)
{
  iv->last_updated = now;
  schedule_sensitive((sv_t *) iv, priority);
}

peng_time_t next_event_time()
{
  if (event_queue_len)
    return event_queue[1]->event_time;
  else
    return NO_EVENT_SCHEDULED;
}

void printdeadlines() {
  event_queue_index_t i = 1;
  while(i <= event_queue_len) {
//    printf("deadline of event %d is %lu\n", i, ((sv_t *)event_queue[i])->event_time);
    i++;
  }
}

void tick()
{
  // For each queued event scheduled for the current time,
  // remove the event from the queue, update its variable, and schedule
  // everything sensitive to it

#ifdef DEBUG
  char buffer[50];
#endif

  while ( event_queue_len > 0 && event_queue[1]->event_time == now ) {
    sv_t *var = dequeue_minimum();
    
    (*var->update)(var);     // Update the value
#ifdef DEBUG
    var->to_string(var, buffer, 50);
    DEBUG_PRINT("event %lu value %s\n", now, buffer);
#endif
    var->last_updated = now; // Remember that it was updated
    // Schedule all sensitive continuations
    for (trigger_t *trigger = var->triggers ; trigger ; trigger = trigger->next) {
#ifdef DEBUG
      if(!can_fork()) {
        printf("contqueue full\n");
        exit(1);
      }
#endif
      enqueue(trigger->act);
    }

    // Remove the earliest event from the queue
    var->event_time = NO_EVENT_SCHEDULED;

  }

  // Until the queue is empty, take the lowest-numbered continuation from
  // the queue and run it, which might insert additional continuations
  // in the queue

  while ( cont_queue_len > 0 ) {
    // Get minimum element
    act_t *to_run = cont_queue[1];
    to_run->scheduled = false;
    
    act_t *to_insert = cont_queue[cont_queue_len--];
    priority_t priority = to_insert->priority;

    cont_queue_index_t parent = 1;
    // Invariant: there's a hole at parent where we'd like to put the
    // "to_insert" continuation.  If all children are greater, we're done,
    // otherwise, move the smaller child into the hole and go to that child
    for (;;) {      
      cont_queue_index_t child = parent << 1;
      if (child > cont_queue_len) break;
      if (child + 1 <= cont_queue_len &&
	  cont_queue[child+1]->priority < cont_queue[child]->priority)
	child++;
      if (priority < cont_queue[child]->priority) break;
      cont_queue[parent] = cont_queue[child];
      parent = child;
    }
    cont_queue[parent] = to_insert;

    call(to_run);
  }
  
}
