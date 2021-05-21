#include <stdio.h>
#include <stdlib.h>

#include "ssm-act.h"
#include "ssm-runtime.h"
#include "ssm-types.h"

/*  Synchronous counter example, like scheduler-1/counter3.c)
 *
 *  Note that this implementation uses static links to access variables in the
 *  parent process, rather than using pointers

fork
  loop
    clk = 0
    after 100ms clk = 1     await 100ms
    await @clk              clk = 1
    after 100ms clk = 0     await 100ms
    await @clk              clk = 0
  loop
    await
      clk == true: q1 = d1
  loop
    await
      clk == true: q2 = d2
  loop
    await
      @q2: d2 = q2 + 1
  loop
    await
      @q2 or @d2: d1 = q1 + d2
*/

typedef struct {
  struct act act;

  i32_svt clk;
  i32_svt d1;
  i32_svt q1;
  i32_svt d2;
  i32_svt q2;
} act_main_t;

typedef struct {
  struct act act;

  act_main_t *static_link;
  struct trigger trigger1;
} act_clk_t;

typedef struct {
  struct act act;

  act_main_t *static_link;
  struct trigger trigger1;
} act_dff1_t;

typedef struct {
  struct act act;

  act_main_t *static_link;
  struct trigger trigger1;
} act_dff2_t;

typedef struct {
  struct act act;

  act_main_t *static_link;
  struct trigger trigger1;
} act_inc_t;

typedef struct {
  struct act act;

  act_main_t *static_link;
  struct trigger trigger1;
  struct trigger trigger2;
} act_adder_t;

stepf_t step_clk;

/* Create a new activation for clk */
struct act *enter_clk(struct act *parent, priority_t priority, depth_t depth,
                      act_main_t *static_link) {
  assert(parent);
  assert(static_link);

  struct act *act =
      act_enter(sizeof(act_clk_t), step_clk, parent, priority, depth);
  act_clk_t *a = container_of(act, act_clk_t, act);
  a->static_link = static_link;

  sel_t selector = 0;
  a->trigger1.act = act;
  a->trigger1.selector = selector;
  a->trigger1.span = a->static_link->clk.sv.vtable->sel_info[selector].span;

  /*
   * Putting sensitize here is an optimization; more typical to put it at each
   * "await" site
   */
  sensitize(&a->static_link->clk.sv, &a->trigger1);

  return act;
}

void step_clk(struct act *act) {
  act_clk_t *a = container_of(act, act_clk_t, act);

  /* printf("step_clk @%d\n", act->pc); */
  printf("clk = %d\n", a->static_link->clk.value);

  switch (act->pc) {
  case 0:
    a->static_link->clk.sv.vtable->later(&a->static_link->clk.sv, now + 100, 1,
                                         0);
    act->pc = 1;
    return;

  case 1:
    a->static_link->clk.sv.vtable->later(&a->static_link->clk.sv, now + 100, 0,
                                         0);
    act->pc = 0;
    return;
  }
  assert(0);
}

stepf_t step_dff1;

struct act *enter_dff1(struct act *parent, priority_t priority, depth_t depth,
                       act_main_t *static_link) {
  assert(parent);
  assert(static_link);

  struct act *act =
      act_enter(sizeof(act_dff1_t), step_dff1, parent, priority, depth);
  act_dff1_t *a = container_of(act, act_dff1_t, act);

  a->static_link = static_link;

  sel_t selector = 0;
  a->trigger1.act = act;
  a->trigger1.selector = selector;
  a->trigger1.span = a->static_link->clk.sv.vtable->sel_info[selector].span;
  sensitize(&a->static_link->clk.sv, &a->trigger1);
  return act;
}

void step_dff1(struct act *act) {
  act_dff1_t *a = container_of(act, act_dff1_t, act);

  /* printf("step_dff1 @%d\n", act->pc); */
  printf("q1 = %d d1 = %d\n", a->static_link->q1.value,
         a->static_link->d1.value);

  switch (act->pc) {
  case 0:
    if (a->static_link->clk.value)
      a->static_link->q1.sv.vtable->assign(
          &a->static_link->q1.sv, act->priority, a->static_link->d1.value, 0);
    return;
  }
  assert(0);
}

stepf_t step_dff2;

struct act *enter_dff2(struct act *parent, priority_t priority, depth_t depth,
                       act_main_t *static_link) {
  assert(parent);
  assert(static_link);

  struct act *act =
      act_enter(sizeof(act_dff2_t), step_dff2, parent, priority, depth);
  act_dff2_t *a = container_of(act, act_dff2_t, act);

  a->static_link = static_link;

  sel_t selector = 0;
  a->trigger1.act = act;
  a->trigger1.selector = selector;
  a->trigger1.span = a->static_link->clk.sv.vtable->sel_info[selector].span;
  sensitize(&a->static_link->clk.sv, &a->trigger1);
  return act;
}

void step_dff2(struct act *act) {
  act_dff2_t *a = container_of(act, act_dff2_t, act);

  /* printf("step_dff2 @%d\n", act->pc); */
  printf("q2 = %d d2 = %d\n", a->static_link->q2.value,
         a->static_link->d2.value);
  switch (act->pc) {
  case 0:
    if (a->static_link->clk.value)
      a->static_link->q2.sv.vtable->assign(
          &a->static_link->q2.sv, act->priority, a->static_link->d2.value, 0);
    return;
  }
  assert(0);
}

stepf_t step_inc;

struct act *enter_inc(struct act *parent, priority_t priority, depth_t depth,
                      act_main_t *static_link) {
  assert(parent);
  assert(static_link);
  struct act *act =
      act_enter(sizeof(act_inc_t), step_inc, parent, priority, depth);
  act_inc_t *a = container_of(act, act_inc_t, act);

  a->static_link = static_link;

  sel_t selector = 0;
  a->trigger1.act = act;
  a->trigger1.selector = selector;
  a->trigger1.span = a->static_link->clk.sv.vtable->sel_info[selector].span;
  sensitize(&a->static_link->clk.sv, &a->trigger1);
  return act;
}

void step_inc(struct act *act) {
  act_inc_t *a = container_of(act, act_inc_t, act);

  switch (act->pc) {
  case 0:
    a->static_link->d2.sv.vtable->assign(&a->static_link->d2.sv, act->priority,
                                         a->static_link->q2.value + 1, 0);
    return;
  }
  assert(0);
}

stepf_t step_adder;

struct act *enter_adder(struct act *parent, priority_t priority, depth_t depth,
                        act_main_t *static_link) {
  assert(parent);
  assert(static_link);

  struct act *act =
      act_enter(sizeof(act_adder_t), step_adder, parent, priority, depth);
  act_adder_t *a = container_of(act, act_adder_t, act);

  a->static_link = static_link;

  sel_t selector = 0;
  a->trigger1.act = a->trigger2.act = (struct act *)act;
  a->trigger1.selector = a->trigger2.selector = selector;
  a->trigger1.span = a->static_link->q2.sv.vtable->sel_info[selector].span;

  a->trigger2.span = a->static_link->d2.sv.vtable->sel_info[selector].span;

  sensitize(&a->static_link->q2.sv, &a->trigger1);
  sensitize(&a->static_link->d2.sv, &a->trigger2);

  return act;
}

void step_adder(struct act *act) {
  act_adder_t *a = container_of(act, act_adder_t, act);

  switch (act->pc) {
  case 0:
    a->static_link->d1.sv.vtable->assign(
        &a->static_link->d1.sv, act->priority,
        a->static_link->q1.value + a->static_link->d2.value, 0);
  }
}

stepf_t step_main;

/* Create a new activation record for main */
struct act *enter_main(struct act *parent, priority_t priority, depth_t depth) {
  struct act *act =
      act_enter(sizeof(act_main_t), step_main, parent, priority, depth);
  act_main_t *a = container_of(act, act_main_t, act);

  initialize_event(&a->clk.sv, &i32_vtable);
  initialize_event(&a->d1.sv, &i32_vtable);
  initialize_event(&a->q1.sv, &i32_vtable);
  initialize_event(&a->d2.sv, &i32_vtable);
  initialize_event(&a->q2.sv, &i32_vtable);
  return act;
}

void step_main(struct act *act) {
  act_main_t *a = container_of(act, act_main_t, act);
  depth_t new_depth;
  priority_t pinc;

  switch (act->pc) {
  case 0:
    a->clk.value = 0;
    a->d1.value = 0;
    a->d2.value = 0;
    a->q1.value = 0;
    a->q2.value = 0;

    new_depth = act->depth - 3; // Make space for 8 children
    pinc = 1 << new_depth;      // priority increment for each thread
    act_fork(enter_clk(act, act->priority + 0 * pinc, new_depth, a));
    act_fork(enter_dff1(act, act->priority + 1 * pinc, new_depth, a));
    act_fork(enter_dff2(act, act->priority + 2 * pinc, new_depth, a));
    act_fork(enter_inc(act, act->priority + 3 * pinc, new_depth, a));
    act_fork(enter_adder(act, act->priority + 4 * pinc, new_depth, a));
    act->pc = 1;
    return;
  case 1:
    act_leave(act, sizeof(act_main_t));
    return;
  }
  assert(0);
}

void main_return(struct act *cont) { return; }

int main(int argc, char *argv[]) {
  ssm_time_t stop_at = argc > 1 ? atoi(argv[1]) : 10;

  initialize_ssm(0);

  struct act top = {.step = main_return};
  struct act *act = enter_main(&top, PRIORITY_AT_ROOT, DEPTH_AT_ROOT);
  act_fork(act);

  for (ssm_time_t next = tick(); stop_at > 0 && next != NO_EVENT_SCHEDULED;
       stop_at--, next = tick())
    printf("next %lu\n", now);

  /* We don't free() allocated memory before exiting, so we will leak memory */

  return 0;
}
