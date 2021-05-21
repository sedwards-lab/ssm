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

  i32 n;
  ptr_i32_svt r;
  i32_svt r2;
} fib_act_t;

stepf_t step_fib;

struct act *enter_fib(struct act *cont, priority_t priority, depth_t depth,
                      i32 n, ptr_i32_svt r) {

  struct act *act =
      act_enter(sizeof(fib_act_t), step_fib, cont, priority, depth);
  fib_act_t *a = container_of(act, fib_act_t, act);
  a->n = n;
  a->r = r;
  initialize_event(&a->r2.sv, &i32_vtable);

  return act;
}

void step_fib(struct act *act) {
  fib_act_t *a = container_of(act, fib_act_t, act);
  switch (act->pc) {
  case 0: {
    a->r2.value = 0; /* initialize value */

    if (a->n < 2) {
      PTR_ASSIGN(a->r, act->priority, 1);
      act_leave(act, sizeof(fib_act_t));
      return;
    }
    depth_t new_depth = act->depth - 1; /* 2 children */
    priority_t new_priority = act->priority;
    priority_t pinc = 1 << new_depth;
    act_fork(enter_fib(act, new_priority, new_depth, a->n - 1, a->r));
    new_priority += pinc;
    act_fork(
        enter_fib(act, new_priority, new_depth, a->n - 2, PTR_OF_SV(a->r2.sv)));
    act->pc = 1;
    return;
  }
  case 1:
    PTR_ASSIGN(a->r, act->priority, *DEREF(int, a->r) + a->r2.value);
    act_leave(act, sizeof(fib_act_t));
    return;
  }
  assert(0);
}

void top_return(struct act *cont) { return; }

int main(int argc, char *argv[]) {
  int n = argc > 1 ? atoi(argv[1]) : 3;

  i32_svt result;
  initialize_event(&result.sv, &i32_vtable);
  result.value = 0;

  struct act top = {.step = top_return};
  act_fork(enter_fib(&top, PRIORITY_AT_ROOT, DEPTH_AT_ROOT, n,
                     PTR_OF_SV(result.sv)));

  initialize_ssm(0);
  tick();

  printf("%d\n", result.value);

  return 0;
}
