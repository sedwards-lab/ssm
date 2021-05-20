#include <stdio.h>
#include <stdlib.h>

#include "ssm-act.h"
#include "ssm-runtime.h"
#include "ssm-types.h"

/*

fib n &r
  var r2 = 0
  if n < 2 then r = 1 else
    fork fib(n-1, r) fib(n-2, r2)
    r = r + r2

0 1 2 3 4 5  6  7  8  9 10  11  12  13
1 1 2 3 5 8 13 21 34 55 89 144 233 377

 */
typedef struct {
  struct act act;

  i32_svt n;
  ptr_i32_svt r;
  i32_svt r2;
} fib_act_t;

stepf_t step_fib;

struct act *enter_fib(struct act *cont, priority_t priority, depth_t depth,
                      int n, ptr_i32_svt r) {
  struct act *a = act_enter(sizeof(fib_act_t), step_fib, cont, priority, depth);
  fib_act_t *act = container_of(a, fib_act_t, act);
  initialize_i32(&act->n, n);
  act->r = r;
  initialize_i32(&act->r2, 0);

  return a;
}

void step_fib(struct act *a) {
  fib_act_t *act = container_of(a, fib_act_t, act);
  switch (a->pc) {
  case 0: {
    if (act->n.value < 2) {
      PTR_ASSIGN(act->r, a->priority, 1);
      act_leave(a, sizeof(fib_act_t));
      return;
    }
    depth_t new_depth = a->depth - 1; /* 2 children */
    priority_t new_priority = a->priority;
    priority_t pinc = 1 << new_depth;
    act_fork(enter_fib(a, new_priority, new_depth, act->n.value - 1, act->r));
    new_priority += pinc;
    act_fork(enter_fib(a, new_priority, new_depth, act->n.value - 2,
                       PTR_OF_SV(act->r2.sv)));
    a->pc = 1;
    return;
  }
  case 1:
    PTR_ASSIGN(act->r, a->priority, *DEREF(int, act->r) + act->r2.value);
    act_leave(a, sizeof(fib_act_t));
    return;
  }
  assert(0);
}

void top_return(struct act *cont) { return; }

int main(int argc, char *argv[]) {
  i32_svt result;
  initialize_i32(&result, 0);
  int n = argc > 1 ? atoi(argv[1]) : 3;

  struct act top = {.step = top_return};
  act_fork(
      enter_fib(&top, PRIORITY_AT_ROOT, DEPTH_AT_ROOT, n, PTR_OF_SV(result.sv)));

  initialize_ssm(0);
  tick();

  printf("%d\n", result.value);

  return 0;
}
