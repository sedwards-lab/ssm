/**
 * modified onetwo example from paper, for linux io:
 *
 *   one &a
 *     wait a
 *     a = a + 1
 *
 *   two &a
 *     wait a
 *     a = a * 2
 *
 *   main
 *     fork one(stdin) two(stdin)
 *     // a = (stdin + 1) * 2 here
 */

#include <stdio.h>
#include <stdlib.h>

#include "ssm-act.h"
#include "ssm-runtime.h"
#include "ssm-time-driver.h"
#include "ssm-types.h"
#include "ssm-debug.h"

typedef struct {
  struct act act;
  ptr_u32_svt a;
  struct trigger trigger1;
} act_one_t;

typedef struct {
  struct act act;
  ptr_u32_svt a;
  struct trigger trigger1;
} act_two_t;

typedef struct {
  struct act act;
  ptr_u8_svt stdin_sv;
} act_main_t;

stepf_t step_one;

struct act *enter_one(struct act *cont, priority_t priority, depth_t depth,
                      ptr_u32_svt a) {
  struct act *act =
      act_enter(sizeof(act_one_t), step_one, cont, priority, depth);
  DEBUG_ACT_NAME(act, "one");

  act_one_t *ac = container_of(act, act_one_t, act);

  ac->a = a;

  return act;
}

void step_one(struct act *act) {
  act_one_t *a = container_of(act, act_one_t, act);
  switch (act->pc) {
  case 0:
    a->trigger1.act = act;
    sensitize(a->a.ptr, &a->trigger1);

    act->pc = 1;
    return;
  case 1:
    desensitize(&a->trigger1);
    PTR_ASSIGN(a->a, act->priority, *DEREF(int, a->a) + 1);
    printf("leaving step_one\n");
    act_leave(act, sizeof(act_one_t));
    return;
  }
}

stepf_t step_two;

struct act *enter_two(struct act *cont, priority_t priority, depth_t depth,
                      ptr_u32_svt a) {
  struct act *act =
      act_enter(sizeof(act_two_t), step_two, cont, priority, depth);
  DEBUG_ACT_NAME(act, "two");

  act_two_t *ac = container_of(act, act_two_t, act);
  ac->a = a;

  return act;
}

void step_two(struct act *act) {
  act_two_t *a = container_of(act, act_two_t, act);
  switch (act->pc) {
  case 0:
    a->trigger1.act = act;
    sensitize(a->a.ptr, &a->trigger1);
    act->pc = 1;
    return;
  case 1:
    desensitize(&a->trigger1);
    PTR_ASSIGN(a->a, act->priority, *DEREF(int, a->a) * 2);
    printf("leaving step_two\n");
    act_leave(act, sizeof(act_two_t));
    return;
  }
}

stepf_t step_main;

struct act *enter_main(struct act *cont, priority_t priority, depth_t depth) {
  struct act *act =
      act_enter(sizeof(act_main_t), step_main, cont, priority, depth);
  DEBUG_ACT_NAME(act, "main");

  act_main_t *ac = container_of(act, act_main_t, act);
  ac->stdin_sv = PTR_OF_SV(get_stdin_var()->sv);

  return act;
}

void step_main(struct act *act) {
  act_main_t *a = container_of(act, act_main_t, act);
  switch (act->pc) {
  case 0: {
    depth_t new_depth = act->depth - 1; /* 2 children */
    priority_t new_priority = act->priority;
    priority_t pinc = 1 << new_depth;

    act_fork(enter_one(act, new_priority, new_depth, a->stdin_sv));
    new_priority += pinc;

    act_fork(enter_two(act, new_priority, new_depth, a->stdin_sv));
    act->pc = 1;
    return;
  }
  case 1: {
    printf("a = %d\n", *DEREF(uint8_t, a->stdin_sv));
    act_leave(act, sizeof(act_main_t));
    ssm_mark_complete();
    return;
  }
  }
}

void top_return(struct act *cont) { return; }

int main() {
  initialize_ssm(0);
  initialize_time_driver(0);
  initialize_io();

  struct act top = {.step = top_return};
  DEBUG_ACT_NAME(&top, "top");

  act_fork(enter_main(&top, PRIORITY_AT_ROOT, DEPTH_AT_ROOT));

  for (ssm_time_t next = tick(); next != NO_EVENT_SCHEDULED; next = tick())
    printf("tick: next = %lu\n", next);

  deinitialize_io();
  return 0;
}
