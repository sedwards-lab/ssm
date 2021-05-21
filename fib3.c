#include <stdio.h>
#include <stdlib.h>

#include "ssm-act.h"
#include "ssm-runtime.h"
#include "ssm-types.h"

/*
mywait &r
  wait r

sum &r1 &r2 &r
  fork mywait(r1) mywait(r2)
  after 1s r = r1 + r2

fib n &r
  var r1 = 0
  var r2 = 0
  if n < 2 then after 1s r = 1 else
    fork sum(r1, r2, r)  fib(n-1, r1)  fib(n-2, r2)

0 1 2 3 4 5  6  7  8  9 10  11  12  13
1 1 2 3 5 8 13 21 34 55 89 144 233 377

 */

typedef struct {
  struct act act;

  ptr_i32_svt r;
  struct trigger trigger1;
} act_mywait_t;

typedef struct {
  struct act act;
  ptr_i32_svt r1, r2, r;
} act_sum_t;

typedef struct {
  struct act act;

  int n;         // Local variable
  ptr_i32_svt r; // Where we should write our result
  i32_svt r1, r2;
} act_fib_t;

stepf_t step_mywait;

struct act *enter_mywait(struct act *cont, priority_t priority, depth_t depth,
                         ptr_i32_svt r) {
  struct act *act =
      act_enter(sizeof(act_mywait_t), step_mywait, cont, priority, depth);
  act_mywait_t *a = container_of(act, act_mywait_t, act);
  a->r = r;
  return act;
}

void step_mywait(struct act *act) {
  act_mywait_t *a = container_of(act, act_mywait_t, act);

  switch (act->pc) {
  case 0:
    a->trigger1.act = act;
    sensitize(a->r.ptr, &a->trigger1);
    act->pc = 1;
    return;
  case 1:
    desensitize(&a->trigger1);
    act_leave(act, sizeof(act_mywait_t));
    return;
  }
}

stepf_t step_sum;

struct act *enter_sum(struct act *cont, priority_t priority, depth_t depth,
                      ptr_i32_svt r1, ptr_i32_svt r2, ptr_i32_svt r) {

  struct act *act =
      act_enter(sizeof(act_sum_t), step_sum, cont, priority, depth);
  act_sum_t *a = container_of(act, act_sum_t, act);
  a->r1 = r1;
  a->r2 = r2;
  a->r = r;
  return act;
}

void step_sum(struct act *act) {
  act_sum_t *a = container_of(act, act_sum_t, act);
  switch (act->pc) {
  case 0: {
    depth_t new_depth = act->depth - 1; // 2 children
    priority_t new_priority = act->priority;
    priority_t pinc = 1 << new_depth;
    act_fork(enter_mywait(act, new_priority, new_depth, a->r1));
    new_priority += pinc;
    act_fork(enter_mywait(act, new_priority, new_depth, a->r2));
    act->pc = 1;
    return;
  }
  case 1:
    PTR_LATER(a->r, now + 1, *DEREF(int, a->r1) + *DEREF(int, a->r2));
    act_leave(act, sizeof(act_sum_t));
    return;
  }
}

stepf_t step_fib;

struct act *enter_fib(struct act *cont, priority_t priority, depth_t depth,
                      int n, ptr_i32_svt r) {
  struct act *act =
      act_enter(sizeof(act_fib_t), step_fib, cont, priority, depth);
  act_fib_t *a = container_of(act, act_fib_t, act);
  a->n = n;
  a->r = r;
  initialize_event(&a->r1.sv, &i32_vtable);
  initialize_event(&a->r2.sv, &i32_vtable);
  return act;
}

void step_fib(struct act *act) {
  act_fib_t *a = container_of(act, act_fib_t, act);
  switch (act->pc) {
  case 0: {
    a->r1.value = 0;
    a->r2.value = 0;
    if (a->n < 2) {
      PTR_LATER(a->r, now + 1, 1);
      act_leave(act, sizeof(act_fib_t));
      return;
    }
    depth_t new_depth = act->depth - 2; // 4 children
    priority_t new_priority = act->priority;
    priority_t pinc = 1 << new_depth;
    act_fork(
        enter_fib(act, new_priority, new_depth, a->n - 1, PTR_OF_SV(a->r1.sv)));
    new_priority += pinc;
    act_fork(
        enter_fib(act, new_priority, new_depth, a->n - 2, PTR_OF_SV(a->r2.sv)));
    new_priority += pinc;
    act_fork(enter_sum(act, new_priority, new_depth, PTR_OF_SV(a->r1.sv),
                       PTR_OF_SV(a->r2.sv), a->r));

    act->pc = 1;
    return;
  }
  case 1:
    act_leave(act, sizeof(act_fib_t));
    return;
  }
}

void top_return(struct act *cont) { return; }

int main(int argc, char *argv[]) {
  i32_svt result;
  initialize_event(&result.sv, &i32_vtable);
  result.value = 0;
  int n = argc > 1 ? atoi(argv[1]) : 3;

  struct act top = {.step = top_return};
  act_fork(enter_fib(&top, PRIORITY_AT_ROOT, DEPTH_AT_ROOT, n,
                     PTR_OF_SV(result.sv)));

  initialize_ssm(0);
  for (ssm_time_t next = tick(); next != NO_EVENT_SCHEDULED; next = tick())
    printf("now %lu\n", next);

  printf("%d\n", result.value);
  return 0;
}
