/**
 * Type-generic implementation of priority queue logic, with type-specialized
 * instantiations for use in ssm-sched.c.
 *
 * With any luck, a good optimizing C compiler will inline the function pointers
 * and synthesize type-specific code for per-type implementations exposed by
 * this compilation unit.
 */
#include "ssm-queue.h"
#include "ssm-act.h" /* for definition of struct act */
#include "ssm-sv.h"  /* for definition of struct sv */

/*** Priority queue logic {{{ */

/**
 * NOTE: Caller is responsible for incrementing queue_len before this.
 */
static inline void enqueue(int (*compar)(void *, void *, void *), void *ctx,
                           void *(*get)(void *, idx_t),
                           void (*set)(void *, idx_t, void *), void *queue,
                           size_t queue_len, void *to_insert) {
  idx_t hole = queue_len;
  for (; hole > QUEUE_HEAD && compar(to_insert, get(queue, hole >> 1), ctx) < 0;
       hole >>= 1)
    set(queue, hole, get(queue, hole >> 1));

  set(queue, hole, to_insert);
}

/**
 * NOTE: to_insert must not already be in the queue, but queue_len should
 * already be large enough to accommodate new item
 */
static inline void fill_hole(int (*compar)(void *, void *, void *), void *ctx,
                             void *(*get)(void *, idx_t),
                             void (*set)(void *, idx_t, void *), void *queue,
                             size_t queue_len, idx_t hole, void *to_insert) {
  for (;;) {
    /* Find earlier of hole's two children */
    idx_t child = hole << 1; /* Left child */

    if (child > queue_len)
      /* Reached the bottom of minheap */
      break;

    /* Compare against right child */
    if (child + 1 <= queue_len &&
        compar(get(queue, child + 1), get(queue, child), ctx) < 0)
      child++; /* Right child is earlier */

    /* Is to_insert earlier than both children? */
    if (compar(to_insert, get(queue, child), ctx) < 0)
      break;

    /* If not, swap earlier child up (push hole down), and descend */
    set(queue, hole, get(queue, child));
    hole = child;
  }
  set(queue, hole, to_insert);
  return;
}

/*** Priority queue logic }}} */

/*** Event queue (struct sv *) {{{ */

static int compar_event(void *lp, void *rp, void *ctx) {
  struct sv *l = *(struct sv **)lp, *r = *(struct sv **)rp;

  if (l->later_time < r->later_time)
    return -1;
  else if (l->later_time == r->later_time)
    return 0;
  else
    return 1;
}

static void *get_event(void *queue, idx_t idx) {
  struct sv **q = (struct sv **)queue;
  return &q[idx];
}

static void set_event(void *queue, idx_t idx, void *val) {
  sel_t **q = (sel_t **)queue, **v = (sel_t **)val;
  q[idx] = *v;
}

void enqueue_event(struct sv **event_queue, idx_t *queue_len,
                   struct sv *to_insert) {
  enqueue(compar_event, NULL, get_event, set_event, event_queue, ++*queue_len,
          &to_insert);
}

void dequeue_event(struct sv **event_queue, idx_t *queue_len, idx_t to_dequeue) {
  /*
   * We don't need to create a separate copy of the tail of the queue because
   * we decrement the queue_size before we call fill_hole, leaving tail beyond
   * the queue.
   */
  struct sv **to_insert = &event_queue[QUEUE_HEAD + --*queue_len];
  fill_hole(compar_event, NULL, get_event, set_event, event_queue, *queue_len,
            to_dequeue, to_insert);
}

void requeue_event(struct sv **event_queue, idx_t *queue_len,
                   idx_t to_requeue) {
  struct sv *to_insert = event_queue[to_requeue];
  fill_hole(compar_event, NULL, get_event, set_event, event_queue, *queue_len,
            to_requeue, &to_insert);
}

idx_t index_of_event(struct sv **event_queue, idx_t *queue_len,
                     struct sv *to_find) {
  for (idx_t idx = QUEUE_HEAD; idx < *queue_len; idx++)
    if (event_queue[idx] == to_find)
      return idx;
  return 0;
}

/*** Event queue }}} */

/*** Act queue (struct act *) {{{ */

static int compar_act(void *lp, void *rp, void *ctx) {
  struct act *l = *(struct act **)lp, *r = *(struct act **)rp;

  if (l->priority < r->priority)
    return -1;
  else if (l->priority == r->priority)
    return 0;
  else
    return 1;
}

static void *get_act(void *queue, idx_t idx) {
  struct act **q = (struct act **)queue;
  return &q[idx];
}

static void set_act(void *queue, idx_t idx, void *val) {
  sel_t **q = (sel_t **)queue, **v = (sel_t **)val;
  q[idx] = *v;
}

void enqueue_act(struct act **act_queue, idx_t *queue_len,
                 struct act *to_insert) {
  enqueue(compar_act, NULL, get_act, set_act, act_queue, ++*queue_len,
          &to_insert);
}

void dequeue_act(struct act **act_queue, idx_t *queue_len, idx_t to_dequeue) {
  /*
   * We don't need to create a separate copy of the tail of the queue because
   * we decrement the queue_size before we call fill_hole, leaving tail beyond
   * the queue.
   */
  struct act **to_insert = &act_queue[QUEUE_HEAD + --*queue_len];
  fill_hole(compar_act, NULL, get_act, set_act, act_queue, *queue_len,
            to_dequeue, to_insert);
}

void requeue_act(struct act **act_queue, idx_t *queue_len, idx_t to_requeue) {
  struct act *to_insert = act_queue[to_requeue];
  fill_hole(compar_act, NULL, get_act, set_act, act_queue, *queue_len,
            to_requeue, &to_insert);
}

idx_t index_of_act(struct act **act_queue, idx_t *queue_len,
                   struct act *to_find) {
  for (idx_t idx = QUEUE_HEAD; idx < *queue_len; idx++)
    if (act_queue[idx] == to_find)
      return idx;
  return 0;
}

/*** Act queue }}} */

/*** Inner queue (sel_t) {{{ */

static int compar_inner(void *lp, void *rp, void *ctx) {
  ssm_time_t *inner_time = ctx;
  sel_t l = *(sel_t *)lp, r = *(sel_t *)rp;

  if (inner_time[l] < inner_time[r])
    return -1;
  else if (inner_time[l] == inner_time[r])
    return 0;
  else
    return 1;
}

static void *get_inner(void *queue, idx_t idx) {
  sel_t *q = (sel_t *)queue;
  return &q[idx];
}

static void set_inner(void *queue, idx_t idx, void *val) {
  sel_t *q = (sel_t *)queue, *v = (sel_t *)val;
  q[idx] = *v;
}

void enqueue_inner(ssm_time_t *inner_time, sel_t *inner_queue,
                   sel_t to_insert) {
  enqueue(compar_inner, inner_time, get_inner, set_inner, inner_queue,
          ++inner_queue[QUEUE_LEN], &to_insert);
}

void dequeue_inner(ssm_time_t *inner_time, sel_t *inner_queue, idx_t to_dequeue) {
  /*
   * We don't need to create a separate copy of the tail of the queue because
   * we decrement the queue_size before we call fill_hole, leaving tail beyond
   * the queue.
   */
  sel_t *to_insert = &inner_queue[QUEUE_HEAD + --inner_queue[QUEUE_LEN]];
  fill_hole(compar_inner, inner_time, get_inner, set_inner, inner_queue,
            inner_queue[QUEUE_LEN], to_dequeue, to_insert);
}

void requeue_inner(ssm_time_t *inner_time, sel_t *inner_queue,
                   idx_t to_requeue) {
  sel_t to_insert = inner_queue[to_requeue];
  fill_hole(compar_inner, inner_time, get_inner, set_inner, inner_queue,
            inner_queue[QUEUE_LEN], to_requeue, &to_insert);
}

idx_t index_of_inner(sel_t *inner_queue, sel_t to_find) {
  for (idx_t idx = QUEUE_HEAD; idx < inner_queue[QUEUE_LEN]; idx++)
    if (inner_queue[idx] == to_find)
      return idx;
  return 0;
}

/*** Inner queue }}} */

/* vim: set ts=2 sw=2 tw=80 et foldmethod=marker :*/
