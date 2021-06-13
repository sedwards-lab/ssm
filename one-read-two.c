/**
 * onetwo example from paper:
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
 *     var a = 0
 *     read(a, STDIN_FILENO), after 1s a = 10
 *     fork one(a) two(a)
 *     // a = 22 here
 */

#include <stdio.h>
#include <stdlib.h>

#include "ssm-act.h"
#include "ssm-runtime.h"
#include "ssm-types.h"
#include "ssm-debug.h"

typedef struct {
  struct act act;
  ptr_io_read_svt a;
  struct trigger trigger1;
} act_one_t;

typedef struct {
  struct act act;
  ptr_io_read_svt a;
  struct trigger trigger1;
} act_two_t;

typedef struct {
  struct act act;
  io_read_svt a;
} act_main_t;

stepf_t step_one;

struct act *enter_one(struct act *cont, priority_t priority, depth_t depth,
                      ptr_io_read_svt a) {
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
    PTR_ASSIGN(a->a, act->priority, *DEREF(u8, a->a) + 1);
    printf("leaving step_one\n");
    act_leave(act, sizeof(act_one_t));
    return;
  }
}

stepf_t step_two;

struct act *enter_two(struct act *cont, priority_t priority, depth_t depth,
                      ptr_io_read_svt a) {
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
    PTR_ASSIGN(a->a, act->priority, *DEREF(u8, a->a) * 2);
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
  act_main_t *a = container_of(act, act_main_t, act);

  initialize_event(&a->a.sv, &io_read_vtable);
  DEBUG_SV_NAME(&a->a.sv, "a");

  a->a.sv.var_name = "a";

  return act;
}

void step_main(struct act *act) {
  act_main_t *a = container_of(act, act_main_t, act);
  switch (act->pc) {
  case 0: {
    /* We could initialize a->a here, but no need */
    a->a.sv.vtable->later(&a->a.sv,
                          /*timeout=*/get_now() + TICKS_PER_SECOND,
                          /*default_value=*/10);

    depth_t new_depth = act->depth - 1; /* 2 children */
    priority_t new_priority = act->priority;
    priority_t pinc = 1 << new_depth;

    act_fork(enter_one(act, new_priority, new_depth, PTR_OF_SV(a->a.sv)));
    new_priority += pinc;

    act_fork(enter_two(act, new_priority, new_depth, PTR_OF_SV(a->a.sv)));
    act->pc = 1;
    return;
  }
  case 1:
    printf("a = %d\n", a->a.value);
    act_leave(act, sizeof(act_main_t));
    return;
  }
}

void top_return(struct act *cont) { return; }

int main() {
  initialize_ssm(0);

  struct act top = {.step = top_return};
  DEBUG_ACT_NAME(&top, "top");

  act_fork(enter_main(&top, PRIORITY_AT_ROOT, DEPTH_AT_ROOT));

  for (ssm_time_t next = tick(); next != NO_EVENT_SCHEDULED; next = tick())
    printf("tick: next = %lu\n", next);

  return 0;
}
