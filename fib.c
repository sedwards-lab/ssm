#include <stdio.h>
#include <stdlib.h>

#include "ssm-act.h"
#include "ssm-runtime.h"
#include "ssm-types.h"

/* From my "runtime" PLT lecture

int fib(int n) {
    int tmp1, tmp2, tmp3;
    tmp1 = n < 2;
    if (!tmp1) goto L1;
    return 1;
L1: tmp1 = n - 1;
    tmp2 = fib(tmp1);
L2: tmp1 = n - 2;
    tmp3 = fib(tmp1);
L3: tmp1 = tmp2 + tmp3;
    return tmp1;
}

0 1 2 3 4 5  6  7  8  9 10  11  12  13
1 1 2 3 5 8 13 21 34 55 89 144 233 377

 */
typedef struct {
  struct act act;

  ptr_i32_svt result; /* Where we should write our result */
  i32_svt tmp2, tmp3; /* Local variables */
  i32 n, tmp1;
} fib_act_t;

stepf_t step_fib;

struct act *enter_fib(struct act *cont, priority_t priority, depth_t depth,
                      ptr_i32_svt result, int n) {
  struct act *a = act_enter(sizeof(fib_act_t), step_fib, cont, priority, depth);
  fib_act_t *act = container_of(a, fib_act_t, act);
  act->n = n;
  act->result = result;
  initialize_i32(&act->tmp2, 0);
  initialize_i32(&act->tmp3, 0);
  return a;
}

void step_fib(struct act *a) {
  fib_act_t *act = container_of(a, fib_act_t, act);

  printf("fib_step @%d n=%d\n", a->pc, act->n);
  switch (a->pc) {
  case 0:
    act->tmp1 = act->n < 2;                  /* tmp1 = n < 2 */
    if (!act->tmp1)                          /* if (!tmp1) */
      goto L1;                               /*   goto L1; */
    PTR_ASSIGN(act->result, a->priority, 1); /* return 1; */
    act_leave(a, sizeof(fib_act_t));
    return;

  L1:
    act->tmp1 = act->n - 1; /* tmp1 = n - 1 */
    a->pc = 1;              /* tmp2 = fib(tmp1) */
    act_call(enter_fib(a, a->priority, a->depth, PTR_OF_SV(act->tmp2.sv),
                       act->tmp1));
    return;

  case 1:                   /* L2: */
    act->tmp1 = act->n - 2; /* tmp1 = n - 2 */
    a->pc = 2;              /* tmp3 = fib(tmp1) */
    act_call(enter_fib(a, a->priority, a->depth, PTR_OF_SV(act->tmp3.sv),
                       act->tmp1));
    return;

  case 2: /* L3: */
    act->tmp1 = act->tmp2.value + act->tmp3.value;
    PTR_ASSIGN(act->result, a->priority, act->tmp1);
    act_leave(a, sizeof(fib_act_t));
    return;
  }
}

void top_return(struct act *cont) { return; }

int main(int argc, char *argv[]) {
  i32_svt result;
  initialize_i32(&result, 0);
  int n = argc > 1 ? atoi(argv[1]) : 3;

  /*
   * Note we don't even call tick() here, and instead directly invoke fib's step
   * function (essentially doing tick()'s job). This is not really how the
   * runtime is supposed to behave, but it is a nice test case that allows us to
   * sidestep tick()'s implementation.
   */
  struct act top = {.step = top_return};
  act_call(enter_fib(&top, PRIORITY_AT_ROOT, DEPTH_AT_ROOT,
                     PTR_OF_SV(result.sv), n));

  printf("%d\n", result.value);

  return 0;
}
