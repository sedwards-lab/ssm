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

void sift_down(event_queue_index_t i, sv_t* to_sift);
void sift_up(event_queue_index_t i, sv_t* to_sift);

#define CONT_QUEUE_SIZE 8192
cont_queue_index_t cont_queue_len = 0;
act_t *cont_queue[CONT_QUEUE_SIZE+1];

#define EVENT_QUEUE_SIZE 8192
event_queue_index_t event_queue_len = 0;
sv_t *event_queue[EVENT_QUEUE_SIZE+1];

/** Only used for debugging */
int can_schedule(sv_t *var) {
  return event_queue_len + 1 <= EVENT_QUEUE_SIZE;
}
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

/*
 * Verify that the heap property of the event queue holds. As far as I remember, the
 * heap property only states that the priority of any node should be (for a min heap),
 * equal to or greater than the priority of its parent.
 */
bool event_heap_property(event_queue_index_t i) {
    /* 
     * if we've passed the end of the event queue without finding any errors,
     * the heap property held up.
     */
    if(i > event_queue_len) {
        return 1;
    }

    /*
     * Calculate indexes for the left and right child
     */ 
    event_queue_index_t left_child  = i << 1;
    event_queue_index_t right_child = left_child + 1;

    /*
     * If this node has a parent, we need to calculate the index of the parent so that we
     * can verify that this node has an event_time that is bigger than or equal to
     * the parents time. Aside from this, we also need to verify the heap property of
     * this node's children.
     */
    if (i > 1) {
      event_queue_index_t parent      = i >> 1;
      bool res = event_queue[parent]->event_time <= event_queue[i]->event_time &&
             event_heap_property(left_child) &&
             event_heap_property(right_child);
     // printf("node:  %lu\nparent: %lu\n", event_queue[i]->event_time
     //                                   , event_queue[parent]->event_time);
      return res;
    }

    /*
     * If this node does not have a parent we just need to verify the heap property of
     * the children.
     */
    return event_heap_property(left_child) && event_heap_property(right_child);
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
  //event_queue[i] = var;
  sift_up(i, var);
}

sv_t* dequeue_minimum() {
  sv_t *earliest = event_queue[1];
  sift_down(1, event_queue[event_queue_len--]);
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
  while(i <= event_queue_len && event_queue[i] != var) {
    i++;
  }

  sv_t* to_insert = event_queue[event_queue_len--];
  event_queue_index_t parent = i >> 1;
  if (parent == 0) {
    event_queue[1] = to_insert;
  } else if(to_insert->event_time > event_queue[parent]->event_time) {
    sift_down(i, to_insert);
  } else {
    sift_up(i, to_insert);
  }
}

// sift the element at index i down to its proper place.
void sift_down(event_queue_index_t i, sv_t* to_sift) {
    peng_time_t then           = to_sift->event_time;
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
      event_queue[parent] = event_queue[child];
      parent              = child;
    }
    event_queue[parent] = to_sift;

    assert(event_heap_property(1));
}

// sift the element at index i up to its right place
void sift_up(event_queue_index_t i, sv_t* to_sift) {

  event_queue_index_t hole = i;
  for(; hole > 1 && event_queue[hole >> 1]->event_time > to_sift->event_time; hole >>= 1) {
    event_queue[hole] = event_queue[hole >> 1];
  }
  event_queue[hole] = to_sift;
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
  DEBUG_ASSERT(((int8_t) cont->depth) >= 0, "negative depth\n");
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
    if (trigger->act->priority > priority) {
      DEBUG_ASSERT(cont_queue_len + 1 <= CONT_QUEUE_SIZE, "contqueue full\n");
      enqueue( trigger->act );
    }
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
  printf("[");
  while(i <= event_queue_len) {
    if(i == event_queue_len) {
      printf("{index: %d, time: %lu}", i, ((sv_t *)event_queue[i])->event_time);
      break;
    }
    printf("{index: %d, time: %lu}, ", i, ((sv_t *)event_queue[i])->event_time);
    i++;
  }
  printf("]\n");
}

void tick()
{
  // For each queued event scheduled for the current time,
  // remove the event from the queue, update its variable, and schedule
  // everything sensitive to it

  while ( event_queue_len > 0 && event_queue[1]->event_time == now ) {
    assert(event_heap_property(1));
    sv_t *var = dequeue_minimum();
    assert(event_heap_property(1));
    
    (*var->update)(var);     // Update the value

#ifdef DEBUG
    char buffer[50];
    var->to_string(var, buffer, sizeof(buffer));
    DEBUG_PRINT("event %lu value %s\n", now, buffer);
#endif

    var->last_updated = now; // Remember that it was updated
    // Schedule all sensitive continuations
    for (trigger_t *trigger = var->triggers ; trigger ; trigger = trigger->next) {
      DEBUG_ASSERT(cont_queue_len + 1 <= CONT_QUEUE_SIZE, "contqueue full\n");
      enqueue(trigger->act);
    }

    // Remove the earliest event from the queue
    var->event_time = NO_EVENT_SCHEDULED;
    assert(event_heap_property(1));
  }

    DEBUG_PRINT("numconts %d\n", cont_queue_len);

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
