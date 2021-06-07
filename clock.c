/**
 * Simple time keeper
 *
 *   def second_clock second_event : event ref =
 *      var timer : event
 *      loop
 *        seconds = Event
 *        after 1s timer = Event
 *        await @timer
 *
 *   def report_seconds second_event : event ref =
 *      var seconds : int = 0
 *      loop
 *        await @second_event
 *        seconds = seconds + 1
 *        print(seconds)
 *
 *   def main : void =
 *     var second : event
 *       second_clock(second) || report_seconds(second)
 */
#include <stdio.h>

#include "ssm-act.h"
#include "ssm-debug.h"
#include "ssm-runtime.h"
#include "ssm-types.h"

typedef struct {
  struct act act;
  unit_svt second;
} act_main_t;

typedef struct {
  struct act act;
  ptr_unit_svt second_event;
  unit_svt timer;
  struct trigger trigger1;
} act_second_clock_t;

typedef struct {
  struct act act;
  ptr_unit_svt second_event;
  i32_svt seconds;
  struct trigger trigger1;
} act_report_seconds_t;

stepf_t step_second_clock;

struct act *enter_second_clock(struct act *parent, priority_t priority,
                               depth_t depth, ptr_unit_svt second_event) {
  assert(parent);

  struct act *act = act_enter(sizeof(act_second_clock_t), step_second_clock,
                              parent, priority, depth);
  DEBUG_ACT_NAME(act, "second_clock");
  act_second_clock_t *a = container_of(act, act_second_clock_t, act);

  a->second_event = second_event;

  initialize_event(&a->timer, unit_vtable);
  DEBUG_SV_NAME(&a->timer, "timer");

  return act;
}

void step_second_clock(struct act *act) {
  act_second_clock_t *a = container_of(act, act_second_clock_t, act);

  switch (act->pc) {
  case 0:
    for (;;) {                                          /* loop */
      assign_event(a->second_event.ptr, act->priority); /* seconds = Event */

      /* after 1s timer = Event */
      later_event(&a->timer, get_now() + 1 * TICKS_PER_SECOND);

      a->trigger1.act = act;
      sensitize(&a->timer, &a->trigger1); /* await @timer */
      act->pc = 1;
      return;
    case 1:
      if (last_updated_event(&a->timer)) { /* @timer */
        desensitize(&a->trigger1);
      } else {
        return;
      }
    } /* end loop */
  }
  act_leave(act, sizeof(act_second_clock_t));
}

stepf_t step_report_seconds;

struct act *enter_report_seconds(struct act *parent, priority_t priority,
                                 depth_t depth, ptr_unit_svt second_event) {
  assert(parent);

  struct act *act = act_enter(sizeof(act_report_seconds_t), step_report_seconds,
                              parent, priority, depth);
  DEBUG_ACT_NAME(act, "report_seconds");
  act_report_seconds_t *a = container_of(act, act_report_seconds_t, act);

  a->second_event = second_event;

  initialize_event(&a->seconds.sv, &i32_vtable);
  DEBUG_SV_NAME(&a->seconds.sv, "seconds");

  return act;
}

void step_report_seconds(struct act *act) {
  act_report_seconds_t *a = container_of(act, act_report_seconds_t, act);

  switch (act->pc) {
  case 0:
    a->seconds.value = 0;

    for (;;) { /* loop */
      a->trigger1.act = act;
      sensitize(a->second_event.ptr, &a->trigger1); /* await @timer */
      act->pc = 1;
      return;
    case 1:
      if (last_updated_event(a->second_event.ptr)) { /* @second_event */
        desensitize(&a->trigger1);
      } else {
        return;
      }

      a->seconds.sv.vtable->assign(&a->seconds.sv, act->priority,
                                   a->seconds.value + 1);

      printf("%d\n", a->seconds.value);
    } /* end of loop */
  }
  act_leave(act, sizeof(act_report_seconds_t));
}

stepf_t step_main;

// Create a new activation record for main
struct act *enter_main(struct act *parent, priority_t priority, depth_t depth) {

  struct act *act =
      act_enter(sizeof(act_main_t), step_main, parent, priority, depth);
  DEBUG_ACT_NAME(act, "main");
  act_main_t *a = container_of(act, act_main_t, act);

  /* Initialize managed variables */
  initialize_event(&a->second, unit_vtable);
  DEBUG_SV_NAME(&a->second, "second");

  return act;
}

void step_main(struct act *act) {
  act_main_t *a = container_of(act, act_main_t, act);

  switch (act->pc) {
  case 0: {                             /* fork */
    depth_t new_depth = act->depth - 1; /* 2 children */
    priority_t new_priority = act->priority;
    priority_t pinc = 1 << new_depth; /* increment for each thread */

    /* clock clk */
    act_fork(
        enter_second_clock(act, new_priority, new_depth, PTR_OF_SV(a->second)));
    new_priority += pinc;

    /* dff clk d1 q1 */
    act_fork(enter_report_seconds(act, new_priority, new_depth,
                                  PTR_OF_SV(a->second)));
  }
    act->pc = 1;
    return;
  case 1:
    act_leave(act, sizeof(act_main_t));
    return;
  }
}

void main_return(struct act *cont) { return; }

int main(int argc, char *argv[]) {
  ssm_time_t stop_at = argc > 1 ? atoi(argv[1]) : 20;

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
