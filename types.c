#include "ssm.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define QUEUE_SIZE 0
#define QUEUE_HEAD 1		/* FIXME: move to header file */

typedef uint16_t inner_queue_index_t;

static void update_unit(cv_t *cvt)
{
	CVT(unit) *v = (CVT(unit) *) cvt;
	assert(v);
	assert(v->event_time == now);
	v->last_updated = now;
	v->event_time = NO_EVENT_SCHEDULED;
}

static void assign_unit(cv_t *cvt, priority_t prio, const any_t _value, sel_t _selector)
{
	CVT(unit) *v = (CVT(unit) *) cvt;
	assert(v);
	assert(_selector == 0);
	if (v->event_time != NO_EVENT_SCHEDULED)
		unsched_event(cvt);
	v->last_updated = now;
	schedule_sensitive(cvt->triggers, prio, _selector);
}

static void later_unit(cv_t *cvt, ssm_time_t then, const any_t value, sel_t _selector)
{
	assert(cvt);
	assert(_selector == 0);
	cvt->event_time = then;
	sched_event(cvt);
}

void initialize_unit(CVT(unit) *v)
{
	assert(v);
	v->update = update_unit;
	v->assign = assign_unit;
	v->later = later_unit;
	v->triggers = NULL;
	v->last_updated = now;
	v->select = NULL; /* Nothing should ever try to access the unit value */
	v->event_time = NO_EVENT_SCHEDULED;
}

#define DEFINE_CHANNEL_VARIABLE_TYPE_VAL(payload_t) \
	static void update_##payload_t(cv_t *cvt) \
	{ \
		CVT(payload_t) *v = (CVT(payload_t) *) cvt; \
		assert(v); \
		assert(v->event_time == now); \
		v->last_updated = v->event_time; \
		v->value = v->later_value; \
		v->event_time = NO_EVENT_SCHEDULED; \
	} \
	static void assign_##payload_t(cv_t *cvt, priority_t prio, const any_t value, sel_t _selector) \
	{ \
		CVT(payload_t) *v = (CVT(payload_t) *) cvt; \
		assert(v); \
		assert(_selector == 0); \
		if (v->event_time != NO_EVENT_SCHEDULED) \
			unsched_event(cvt); \
		v->last_updated = now; \
		v->value = (payload_t) value; \
		schedule_sensitive(cvt->triggers, prio, 0); \
	} \
	static void later_##payload_t(cv_t *cvt, ssm_time_t then, const any_t value, sel_t _selector) \
	{ \
		CVT(payload_t) *v = (CVT(payload_t) *) cvt; \
		assert(v); \
		assert(_selector == 0); \
		v->later_value = (payload_t) value; \
		v->event_time = then; \
		sched_event(cvt); \
	} \
	void initialize_##payload_t(CVT(payload_t) *cvt, payload_t init_value) \
	{ \
		assert(cvt); \
		cvt->update = update_##payload_t;\
		cvt->assign = assign_##payload_t;\
		cvt->later = later_##payload_t;\
		cvt->triggers = NULL; \
		cvt->last_updated = now; \
		cvt->value = init_value; \
		cvt->select[0] = &cvt->value; \
		cvt->event_time = NO_EVENT_SCHEDULED; \
	}

DEFINE_CHANNEL_VARIABLE_TYPE_VAL(int)
DEFINE_CHANNEL_VARIABLE_TYPE_VAL(bool)

static void unsched_inner(sel_t *inner_queue, ssm_time_t *inner_time, inner_queue_index_t hole)
{
	assert(inner_queue);
	assert(inner_time);
	assert(hole);
	assert(inner_time[inner_queue[hole]] != NO_EVENT_SCHEDULED);
	inner_time[inner_queue[hole]] = NO_EVENT_SCHEDULED; /* Create the hole */

	sel_t to_insert = inner_queue[inner_queue[QUEUE_SIZE]--];
	for (;;) {
		inner_queue_index_t child = hole << 1;
		if (child > inner_queue[QUEUE_SIZE])
			break;

		if (child + 1 <= inner_queue[QUEUE_SIZE] &&
				inner_time[inner_queue[child+1]] < inner_time[inner_queue[child]])
			child++;
		 
		if (inner_time[to_insert] < inner_time[inner_queue[child]])
			break;

		inner_queue[hole] = inner_queue[child];
		hole = child;
	}
	inner_queue[hole] = to_insert;
}

static void update_arr3_int(cv_t *cvt)
{
	CVT(arr3_int) *v = (CVT(arr3_int) *) cvt;
	assert(v);
	assert(v->selector < arr3_int_sel_range);

	if (v->selector == 0) {
		/* magic number bound comes from size of array */
		for (int i = 0; i < 3; i++)
			v->value[i] = v->later_value[i];
		for (int i = 0; i < arr3_int_sel_range; i++)
			v->last_updated[i] = now;

		/* Since we just performed a full value update, it's not
		 * possible for another update to be already queued up;
		 * the inner_queue must contain exactly one selector (0).
		 */
		assert(v->inner_queue[QUEUE_SIZE] == 1);
	} else {
		v->value[v->selector-1] = v->later_value[v->selector-1];
		v->last_updated[v->selector] = now;
	}

	/* The head of the inner queue should have been v->selector */
	assert(v->inner_queue[QUEUE_HEAD] == v->selector);

	/* Unschedule the inner event queue head */
	unsched_inner(v->inner_queue, v->inner_time, QUEUE_HEAD);

	/* Propagate inner_queue state to outer queue via v->event_time and friends*/
	if (v->inner_queue[QUEUE_SIZE] > 0) {
		/* Inner event scheduled; we need to reschedule in global queue */
		v->selector = v->inner_queue[QUEUE_HEAD];
		v->event_time = v->inner_time[v->selector];
		/* Note that if there was another inner event scheduled, then
		 * v->later_value at the corresponding selector should remain
		 * unclobbered at this point.
		 */
	} else {
		/* Nothing scheduled in inner_queue */
		v->event_time = NO_EVENT_SCHEDULED;
	}
}

/**
 * Resolve any pre-existing scheduling conflicts.
 *
 * Commonly used between the assign and later methods.
 *
 * NOTE: this probably can't be made generic (in C anyway), because we can't
 * tell which two selectors constitutes a conflict without reflection. This is
 * not clearly demonstrated by a flat array of 3 integers, but a conflict occurs
 * when there are updates scheduled for both the selector and any of its
 * ancestors or descendants; inversely, siblings and other extended family do
 * not constitute conflicts. So in particular, a generic implementation would
 * need information about this family tree, whose structure is derived from the
 * definition of the payload type.
 *
 * That being said, there is some logic here that can likely be reused.
 *
 * Update: not going to implement this just yet, but there is a generic way to
 * do this. Pseudocode:
 *
 * def parent(s: sel_t) -> sel_t:
 *   """returns parent selector of s"""
 *   ...
 *
 * def children(s: sel_t) -> size_t:
 *   """returns the number of descendent nodes until sibling"""
 *   ...
 *
 * def check_inner_conflict(v: cv_t, s: sel_t) -> bool:
 *
 *   # recursively check s and all its ancestors
 *   def check_parent(v: cv_t, s: sel_t) -> bool:
 *     if v.inner_time[s].is_scheduled:
 *       return True
 *     return check_parent(v, get_parent(s))
 *
 *   if check_parent(v, s):
 *     return True
 *
 *   # check all of s's children
 *   for i in range(s+1, s+children(s)):
 *     if v.inner_time[s].is_scheduled:
 *       return True
 *
 *   return False
 *
 * Since aggregate data structures are organized like trees, it's easy to chase
 * each parent recursively to check for ancestor conflicts (using the parent
 * lookup table). The tricky part is figuring out which children to check
 * without scanning the whole tree. The key is to define a per-type lookup table
 * that tells us how many children s has. As long as we use a dense selector
 * mapping scheme, this will give us the range of children. We can just scan
 * through them linearly.
 */
static void resolve_conflict_arr3_int(CVT(arr3_int) *v, sel_t selector)
{
	/* Unnecessary defensive programming xD */
	assert(v);
	assert(selector < arr3_int_sel_range);

	if (selector == 0) {
		/* Whole value update */

		/* Are we scheduled in the global queue? */
		if (v->event_time != NO_EVENT_SCHEDULED) {
			unsched_event((cv_t *) v);

			/* Were we scheduled for partial updates? */
			if (v->selector != 0) {
				/* Yes; remove all updates from inner queue */
				for (inner_queue_index_t i = QUEUE_HEAD; i < v->inner_queue[QUEUE_SIZE]; i++)
					v->inner_time[v->inner_queue[i]] = NO_EVENT_SCHEDULED;
				v->inner_queue[QUEUE_SIZE] = 0;
			}
		}

		/* Note that if v->event_time == NO_EVENT_SCHEDULED, there can
		 * be no inner updates scheduled. So we needn't check that here.
		 */
	} else {
		/* Partial value update */

		/* Note that v->event_time != NO_EVENT_SCHEDULED is fine as long
		 * as the scheduled update does not conflict with the one we're
		 * doing here. But we do still need to check if there is an
		 * inner update queued up too.
		 */

		/* Was the inner selector scheduled? */
		if (v->inner_time[selector] != NO_EVENT_SCHEDULED) {
			if (v->event_time != NO_EVENT_SCHEDULED && v->selector == selector) {
				/* If we were scheduled globally, remove from global queue */
				unsched_event((cv_t *) v);
			}

			inner_queue_index_t hole = 0;
			for (inner_queue_index_t i = QUEUE_HEAD; i <= v->inner_queue[QUEUE_SIZE]; i++) {
				if (v->inner_queue[i] == selector) {
				}
			}
			/* if v->inner_time[selector] != NO_EVENT_SCHEDULED, then we must be on the inner_queue. */
			assert(hole);
			unsched_inner(v->inner_queue, v->inner_time, hole);
		}
	}
}

static void assign_arr3_int(cv_t *cvt, priority_t prio, const any_t value, sel_t selector)
{
	CVT(arr3_int) *v = (CVT(arr3_int) *) cvt;
	assert(v);
	assert(selector < arr3_int_sel_range);
	resolve_conflict_arr3_int(v, selector);
	
	if (selector == 0) {
		const int *val = (const int *) value;
		for (int i = 0; i < 3; i++)
			v->value[i] = val[i];
		for (int i = 0; i < arr3_int_sel_range; i++)
			v->last_updated[i] = now;
	} else {
		const int val = (const int) value;
		v->value[selector-1] = val;
		v->last_updated[selector] = now;
	}
	schedule_sensitive(cvt->triggers, prio, selector);
}

static void later_arr3_int(cv_t *cvt, ssm_time_t then, const any_t value, sel_t selector)
{
	CVT(arr3_int) *v = (CVT(arr3_int) *) cvt;
	assert(v);
	assert(selector < arr3_int_sel_range);
	resolve_conflict_arr3_int(v, selector);

	if (selector == 0) {
		const int *val = (const int *) value;
		for (int i = 0; i < 3; i++)
			v->later_value[i] = val[i];
	} else {
		const int val = (const int) value;
		v->later_value[selector-1] = val;
	}
	/* Schedule within inner_queue */
	v->inner_time[selector] = then;

	inner_queue_index_t hole = ++v->inner_queue[QUEUE_SIZE];
	assert(hole <= arr3_int_sel_range); /* Don't overflow the queue */
	for (; hole > QUEUE_HEAD && v->inner_time[selector] < v->inner_time[v->inner_queue[hole >> 1]]; hole >>= 1)
		v->inner_queue[hole] = v->inner_queue[hole >> 1];
	v->inner_queue[hole] = selector;

	/* Propagate inner_queue state to outer queue via v->event_time and friends*/
	v->selector = v->inner_queue[QUEUE_HEAD];
	v->event_time = v->inner_time[v->selector];
	/* Reorganize queue */
	sched_event(cvt);
}

void initialize_arr3_int(CVT(arr3_int) *v, const int *init_value)
{
	assert(v);
	v->triggers = NULL;
	v->event_time = NO_EVENT_SCHEDULED;
	v->update = update_arr3_int;
	v->assign = assign_arr3_int;
	v->later = later_arr3_int;

	v->select[0] = v->value;

	/* Magic number 3 from size of array */
	for (int i = 0; i < 3; i++) {
		v->value[i] = init_value[i];
		v->select[i+1] = &v->value[i];
	}

	v->inner_queue[QUEUE_SIZE] = 0;
	for (int i = 0; i < arr3_int_sel_range; i++) {
		v->last_updated[i] = now;
		v->inner_queue[i+QUEUE_HEAD] = BAD_SELECTOR; /* FIXME: Technically not necessary */
		v->inner_time[i] = NO_EVENT_SCHEDULED;
	}
}

static void update_tup2_int(cv_t *cvt)
{
	CVT(tup2_int) *v = (CVT(tup2_int) *) cvt;
	assert(v);
	assert(v->selector < tup2_int_sel_range);

	switch (v->selector) {
	case 0:
		v->value.left = v->later_value.left;
		v->value.right = v->later_value.right;
		for (int i = 0; i < tup2_int_sel_range; i++)
			v->last_updated[i] = now;

		/* Since we just performed a full value update, it's not
		 * possible for another update to be already queued up;
		 * the inner_queue must contain exactly one selector (0).
		 */
		assert(v->inner_queue[QUEUE_SIZE] == 1);
		break;
	case 1:
		v->value.left = v->later_value.left;
		v->last_updated[v->selector] = now;
		break;
	case 2:
		v->value.right = v->later_value.right;
		v->last_updated[v->selector] = now;
		break;
	default:
		assert(0);
	}

	/* The head of the inner queue should have been v->selector */
	assert(v->inner_queue[QUEUE_HEAD] == v->selector);

	/* Unschedule the inner event queue head */
	unsched_inner(v->inner_queue, v->inner_time, QUEUE_HEAD);

	/* Propagate inner_queue state to outer queue via v->event_time and friends*/
	if (v->inner_queue[QUEUE_SIZE] > 0) {
		/* Inner event scheduled; we need to reschedule in global queue */
		v->selector = v->inner_queue[QUEUE_HEAD];
		v->event_time = v->inner_time[v->selector];
		/* Note that if there was another inner event scheduled, then
		 * v->later_value at the corresponding selector should remain
		 * unclobbered at this point.
		 */
	} else {
		/* Nothing scheduled in inner_queue */
		v->event_time = NO_EVENT_SCHEDULED;
	}
}

static void resolve_conflict_tup2_int(CVT(tup2_int) *v, sel_t selector)
{
	/* Same implementation as arr3, both flat data structures */
	assert(v);
	assert(selector < tup2_int_sel_range);

	if (selector == 0) {
		/* Whole value update */

		/* Are we scheduled in the global queue? */
		if (v->event_time != NO_EVENT_SCHEDULED) {
			unsched_event((cv_t *) v);

			/* Were we scheduled for partial updates? */
			if (v->selector != 0) {
				/* Yes; remove all updates from inner queue */
				for (inner_queue_index_t i = QUEUE_HEAD; i < v->inner_queue[QUEUE_SIZE]; i++)
					v->inner_time[v->inner_queue[i]] = NO_EVENT_SCHEDULED;
				v->inner_queue[QUEUE_SIZE] = 0;
			}
		}

		/* Note that if v->event_time == NO_EVENT_SCHEDULED, there can
		 * be no inner updates scheduled. So we needn't check that here.
		 */
	} else {
		/* Partial value update */

		/* Note that v->event_time != NO_EVENT_SCHEDULED is fine as long
		 * as the scheduled update does not conflict with the one we're
		 * doing here. But we do still need to check if there is an
		 * inner update queued up too.
		 */

		/* Was the inner selector scheduled? */
		if (v->inner_time[selector] != NO_EVENT_SCHEDULED) {
			if (v->event_time != NO_EVENT_SCHEDULED && v->selector == selector) {
				/* If we were scheduled globally, remove from global queue */
				unsched_event((cv_t *) v);
			}

			inner_queue_index_t hole = 0;
			for (inner_queue_index_t i = QUEUE_HEAD; i <= v->inner_queue[QUEUE_SIZE]; i++) {
				if (v->inner_queue[i] == selector) {
				}
			}
			/* if v->inner_time[selector] != NO_EVENT_SCHEDULED, then we must be on the inner_queue. */
			assert(hole);
			unsched_inner(v->inner_queue, v->inner_time, hole);
		}
	}
}

static void assign_tup2_int(cv_t *cvt, priority_t prio, const any_t value, sel_t selector)
{
	CVT(tup2_int) *v = (CVT(tup2_int) *) cvt;
	assert(v);
	assert(selector < tup2_int_sel_range);
	resolve_conflict_tup2_int(v, selector);
	
	switch (v->selector) {
	case 0: {
		const tup2_int *val = (const tup2_int *) value;
		v->value.left = val->left;
		v->value.right = val->right;
		for (int i = 0; i < tup2_int_sel_range; i++)
			v->last_updated[i] = now;
		break;
	}
	case 1: {
		const int val = (const int) value;
		v->value.left = val;
		v->last_updated[selector] = now;
		break;
	}
	case 2: {
		const int val = (const int) value;
		v->value.right = val;
		v->last_updated[selector] = now;
		break;
	}
	default:
		assert(0);
		break;
	}
	schedule_sensitive(cvt->triggers, prio, selector);
}

static void later_tup2_int(cv_t *cvt, ssm_time_t then, const any_t value, sel_t selector)
{
	CVT(tup2_int) *v = (CVT(tup2_int) *) cvt;
	assert(v);
	assert(selector < tup2_int_sel_range);
	resolve_conflict_tup2_int(v, selector);

	switch (v->selector) {
	case 0: {
		const tup2_int *val = (const tup2_int *) value;
		v->later_value.left = val->left;
		v->later_value.right = val->right;
		break;
	}
	case 1: {
		const int val = (const int) value;
		v->later_value.left = val;
		break;
	}
	case 2: {
		const int val = (const int) value;
		v->later_value.right = val;
		break;
	}
	default:
		assert(0);
		break;
	}
	/* Schedule within inner_queue */
	v->inner_time[selector] = then;

	inner_queue_index_t hole = ++v->inner_queue[QUEUE_SIZE];
	assert(hole <= arr3_int_sel_range); /* Don't overflow the queue */
	for (; hole > QUEUE_HEAD && v->inner_time[selector] < v->inner_time[v->inner_queue[hole >> 1]]; hole >>= 1)
		v->inner_queue[hole] = v->inner_queue[hole >> 1];
	v->inner_queue[hole] = selector;

	/* Propagate inner_queue state to outer queue via v->event_time and friends*/
	v->selector = v->inner_queue[QUEUE_HEAD];
	v->event_time = v->inner_time[v->selector];
	/* Reorganize queue */
	sched_event(cvt);
}

void initialize_tup2_int(CVT(tup2_int) *v, const tup2_int *init_value)
{
	assert(v);
	v->triggers = NULL;
	v->event_time = NO_EVENT_SCHEDULED;
	v->update = update_tup2_int;
	v->assign = assign_tup2_int;
	v->later = later_tup2_int;

	v->value.left = init_value->left;
	v->value.right = init_value->right;

	v->select[0] = &v->value;
	v->select[1] = &v->value.left;
	v->select[2] = &v->value.right;

	v->inner_queue[QUEUE_SIZE] = 0;
	for (int i = 0; i < tup2_int_sel_range; i++) {
		v->last_updated[i] = now;
		v->inner_queue[i+QUEUE_HEAD] = BAD_SELECTOR; /* FIXME: Technically not necessary */
		v->inner_time[i] = NO_EVENT_SCHEDULED;
	}
}
