/**
 * Synchronous counter example using pass-by-reference variables
 *
 *   def clock clk : int ref =
 *     var timer : event
 *     loop
 *       after 100 timer = Event       await 100ms
 *       await @timer
 *       clk = True
 *       after 100 timer = Event
 *       await @timer
 *       clk = False
 *
 *   def dff clk d q : int ref =
 *     loop
 *       await
 *          clk: q = d
 *
 *   def inc a y : int ref =
 *     loop
 *       await
 *         @a: y = a + 1
 *
 *   def adder (a b y : int ref) : void =
 *      loop
 *        await
 *          @a or @b: y = a + b
 *
 *   def main : void =         // priority 0000....
 *     var clk : bool
 *     var q1 d1 q2 d2 : int
 *
 *     clock(clk)              // priority 0000....
 *     || dff(clk, d1, q1)     //          0010....
 *                             //          0010....  first child
 *                             //          0011....  second child
 *     || dff(clk, d2, q2 + 1) //          0100....
 *     || inc(q2, d2)          //          0110....
 *     || adder(q1, d2, d1)    //          1000....
 *
 * Note that this long-running example will only run for a finite number of
 * ticks, and doesn't bother freeing allocated memory before exiting, so it will
 * leak memory.
 */

#include <stdio.h>
#include <stdlib.h>

#include "ssm-act.h"
#include "ssm-debug.h"
#include "ssm-runtime.h"
#include "ssm-types.h"

typedef struct {
  struct act act;
  bool_svt clk;
  i32_svt q1, d1, q2, d2;
} act_main_t;

typedef struct {
  struct act act;
  ptr_bool_svt clk;
  unit_svt timer;
  struct trigger trigger1;
} act_clock_t;

typedef struct {
  struct act act;
  ptr_bool_svt clk;
  ptr_i32_svt d, q;
  struct trigger trigger1;
} act_dff_t;

typedef struct {
  struct act act;
  ptr_i32_svt a, y;
  struct trigger trigger1;
} act_inc_t;

typedef struct {
  struct act act;
  ptr_i32_svt a, b, y;
  struct trigger trigger1, trigger2;
} act_adder_t;

stepf_t step_clock;

struct act *enter_clock(struct act *parent, priority_t priority, depth_t depth,
                        ptr_bool_svt clk) {
  assert(parent);

  struct act *act =
      act_enter(sizeof(act_clock_t), step_clock, parent, priority, depth);
  DEBUG_ACT_NAME(act, "clock");
  act_clock_t *a = container_of(act, act_clock_t, act);

  a->clk = clk;

  initialize_event(&a->timer, unit_vtable);
  DEBUG_SV_NAME(&a->timer, "timer");

  return act;
}

void step_clock(struct act *act) {
  act_clock_t *a = container_of(act, act_clock_t, act);

  printf("clk = %d\n", *DEREF(bool, a->clk));

  switch (act->pc) {
  case 0:
    for (;;) { /* loop */
      later_event(&a->timer, get_now() + 100);

      a->trigger1.act = act;
      sensitize(&a->timer, &a->trigger1); /* await @timer */
      act->pc = 1;
      return;
    case 1:
      if (last_updated_event(&a->timer)) /* @timer */
        desensitize(&a->trigger1);
      else
        return;

      PTR_ASSIGN(a->clk, act->priority, true);

      later_event(&a->timer, get_now() + 100);

      a->trigger1.act = act;
      sensitize(&a->timer, &a->trigger1); /* await @timer */
      act->pc = 2;
      return;
    case 2:
      if (last_updated_event(&a->timer)) /* @timer */
        desensitize(&a->trigger1);
      else
        return;

      PTR_ASSIGN(a->clk, act->priority, false);
    }
  }
  act_leave(act, sizeof(act_clock_t));
}

stepf_t step_dff;

struct act *enter_dff(struct act *parent, priority_t priority, depth_t depth,
                      ptr_bool_svt clk, ptr_i32_svt d, ptr_i32_svt q) {
  assert(parent);

  struct act *act =
      act_enter(sizeof(act_dff_t), step_dff, parent, priority, depth);
  DEBUG_ACT_NAME(act, "dff");
  act_dff_t *a = container_of(act, act_dff_t, act);

  a->clk = clk;
  a->d = d;
  a->q = q;

  return act;
}

void step_dff(struct act *act) {
  act_dff_t *a = container_of(act, act_dff_t, act);

  printf("q = %d d = %d\n", *DEREF(int, a->q), *DEREF(int, a->d));

  switch (act->pc) {
  case 0:
    for (;;) { /* loop */
      /* await clk == True */
      a->trigger1.act = act;
      sensitize(a->clk.ptr, &a->trigger1);
      act->pc = 1;
      return;
    case 1:
      if (last_updated_event(a->clk.ptr) &&
          *DEREF(bool, a->clk)) /* clk == true */
        desensitize(&a->trigger1);
      else
        return;

      PTR_ASSIGN(a->q, act->priority, *DEREF(int, a->d));
    }
  }
  act_leave(act, sizeof(act_dff_t));
}

stepf_t step_inc;

struct act *enter_inc(struct act *parent, priority_t priority, depth_t depth,
                      ptr_i32_svt a, ptr_i32_svt y) {
  assert(parent);

  struct act *act =
      act_enter(sizeof(act_inc_t), step_inc, parent, priority, depth);
  DEBUG_ACT_NAME(act, "inc");
  act_inc_t *ac = container_of(act, act_inc_t, act);

  ac->a = a;
  ac->y = y;

  return act;
}

void step_inc(struct act *act) {
  act_inc_t *a = container_of(act, act_inc_t, act);

  switch (act->pc) {
  case 0:
    for (;;) { /* loop */
      /* await @a */
      a->trigger1.act = act;
      sensitize(a->a.ptr, &a->trigger1);
      act->pc = 1;
      return;
    case 1:
      if (last_updated_event(a->a.ptr)) { /* @a */
        desensitize(&a->trigger1);
        PTR_ASSIGN(a->y, act->priority, *DEREF(int, a->a) + 1);
      } else {
        return;
      }
    }
  }
  act_leave(act, sizeof(act_inc_t));
}

stepf_t step_adder;

struct act *enter_adder(struct act *parent, priority_t priority, depth_t depth,
                        ptr_i32_svt a, ptr_i32_svt b, ptr_i32_svt y) {
  assert(parent);

  struct act *act =
      act_enter(sizeof(act_adder_t), step_adder, parent, priority, depth);
  DEBUG_ACT_NAME(act, "adder");
  act_adder_t *ac = container_of(act, act_adder_t, act);

  ac->a = a;
  ac->b = b;
  ac->y = y;

  return act;
}

void step_adder(struct act *act) {
  act_adder_t *a = container_of(act, act_adder_t, act);

  switch (act->pc) {
  case 0:
    for (;;) { /* loop */
      /* await @a or @b */
      a->trigger1.act = act;
      sensitize(a->a.ptr, &a->trigger1);

      a->trigger2.act = act;
      sensitize(a->b.ptr, &a->trigger2);
      act->pc = 1;
      return;
    case 1:
      if (last_updated_event(a->a.ptr) ||
          last_updated_event(a->b.ptr)) { /* @a or @b */
        desensitize(&a->trigger1);
        desensitize(&a->trigger2);
        PTR_ASSIGN(a->y, act->priority, *DEREF(int, a->a) + *DEREF(int, a->b));
      } else {
        return;
      }
    }
  }
  act_leave(act, sizeof(act_adder_t));
}

stepf_t step_main;

struct act *enter_main(struct act *parent, priority_t priority, depth_t depth) {
  struct act *act =
      act_enter(sizeof(act_main_t), step_main, parent, priority, depth);
  DEBUG_ACT_NAME(act, "main");
  act_main_t *a = container_of(act, act_main_t, act);

  initialize_event(&a->clk.sv, &bool_vtable);
  DEBUG_SV_NAME(&a->clk.sv, "clk");

  initialize_event(&a->d1.sv, &i32_vtable);
  DEBUG_SV_NAME(&a->d1.sv, "d1");

  initialize_event(&a->q1.sv, &i32_vtable);
  DEBUG_SV_NAME(&a->q1.sv, "q1");

  initialize_event(&a->d2.sv, &i32_vtable);
  DEBUG_SV_NAME(&a->d2.sv, "d2");

  initialize_event(&a->q2.sv, &i32_vtable);
  DEBUG_SV_NAME(&a->q2.sv, "q2");

  return act;
}

void step_main(struct act *act) {
  act_main_t *a = container_of(act, act_main_t, act);

  switch (act->pc) {
  case 0: { /* fork */
    a->clk.value = false;
    a->d1.value = 0;
    a->d2.value = 0;
    a->q1.value = 0;
    a->q2.value = 0;

    depth_t new_depth = act->depth - 3; /* 8 children */
    priority_t new_priority = act->priority;
    priority_t pinc = 1 << new_depth; /* increment for each thread */

    /* clock clk */
    act_fork(enter_clock(act, new_priority, new_depth, PTR_OF_SV(a->clk.sv)));
    new_priority += pinc;

    /* dff clk d1 q1 */
    act_fork(enter_dff(act, new_priority, new_depth, PTR_OF_SV(a->clk.sv),
                       PTR_OF_SV(a->d1.sv), PTR_OF_SV(a->q1.sv)));
    new_priority += pinc;

    /* dff clk d2 (q2 + 1) */
    act_fork(enter_dff(act, new_priority, new_depth, PTR_OF_SV(a->clk.sv),
                       PTR_OF_SV(a->d2.sv), PTR_OF_SV(a->q2.sv)));
    new_priority += pinc;

    /* inc q2 d2 */
    act_fork(enter_inc(act, new_priority, new_depth, PTR_OF_SV(a->q2.sv),
                       PTR_OF_SV(a->d2.sv)));
    new_priority += pinc;

    /* adder q1 d2 d1 */
    act_fork(enter_adder(act, new_priority, new_depth, PTR_OF_SV(a->q1.sv),
                         PTR_OF_SV(a->d2.sv), PTR_OF_SV(a->d1.sv)));
    /* new_priority += pinc; */
  }
    act->pc = 1;
    return;
  case 1:
    act_leave(act, sizeof(act_adder_t));
    return;
  }
}

void main_return(struct act *cont) { return; }

int main(int argc, char *argv[]) {
  ssm_time_t stop_at = argc > 1 ? atoi(argv[1]) : 10;

  initialize_ssm(0);

  struct act top = {.step = main_return};
  DEBUG_ACT_NAME(&top, "top");

  struct act *act = enter_main(&top, PRIORITY_AT_ROOT, DEPTH_AT_ROOT);
  act_fork(act);

  for (ssm_time_t next = tick(); stop_at > 0 && next != NO_EVENT_SCHEDULED;
       stop_at--, next = tick())
    printf("next %lu\n", get_now());

  return 0;
}
