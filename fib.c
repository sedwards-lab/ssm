/**
 * fib implementation using gotos:
 *
 *   int fib(int n) {
 *       int tmp1, tmp2, tmp3;
 *       tmp1 = n < 2;
 *       if (!tmp1) goto L1;
 *       return 1;
 *   L1: tmp1 = n - 1;
 *       tmp2 = fib(tmp1);
 *   L2: tmp1 = n - 2;
 *       tmp3 = fib(tmp1);
 *   L3: tmp1 = tmp2 + tmp3;
 *       return tmp1;
 *   }
 *
 * 0 1 2 3 4 5  6  7  8  9 10  11  12  13
 * 1 1 2 3 5 8 13 21 34 55 89 144 233 377
 *
 */
#include <stdio.h>
#include <stdlib.h>

#include "ssm-act.h"
#include "ssm-debug.h"
#include "ssm-runtime.h"
#include "ssm-types.h"

typedef struct {
  struct act act;

  ptr_i32_svt result; /* Where we should write our result */
  i32_svt tmp2, tmp3; /* Local variables */
  i32 n, tmp1;
} fib_act_t;

stepf_t step_fib;

struct act *enter_fib(struct act *cont, priority_t priority, depth_t depth,
                      ptr_i32_svt result, int n) {
  struct act *act =
      act_enter(sizeof(fib_act_t), step_fib, cont, priority, depth);
  DEBUG_ACT_NAME(act, "fib");
  fib_act_t *a = container_of(act, fib_act_t, act);

  a->n = n;
  a->result = result;
  initialize_event(&a->tmp2.sv, &i32_vtable);
  DEBUG_SV_NAME(&a->tmp2.sv, "tmp2");
  initialize_event(&a->tmp3.sv, &i32_vtable);
  DEBUG_SV_NAME(&a->tmp3.sv, "tmp3");
  return act;
}

void step_fib(struct act *act) {
  fib_act_t *a = container_of(act, fib_act_t, act);

  printf("fib_step @%d n=%d\n", act->pc, a->n);
  switch (act->pc) {
  case 0:
    a->tmp1 = a->n < 2;                      /* tmp1 = n < 2 */
    if (!a->tmp1)                            /* if (!tmp1) */
      goto L1;                               /*   goto L1; */
    PTR_ASSIGN(a->result, act->priority, 1); /* return 1; */
    act_leave(act, sizeof(fib_act_t));
    return;

  L1:
    a->tmp1 = a->n - 1; /* tmp1 = n - 1 */
    act->pc = 1;        /* tmp2 = fib(tmp1) */
    act_call(enter_fib(act, act->priority, act->depth, PTR_OF_SV(a->tmp2.sv),
                       a->tmp1));
    return;

  case 1:               /* L2: */
    a->tmp1 = a->n - 2; /* tmp1 = n - 2 */
    act->pc = 2;        /* tmp3 = fib(tmp1) */
    act_call(enter_fib(act, act->priority, act->depth, PTR_OF_SV(a->tmp3.sv),
                       a->tmp1));
    return;

  case 2: /* L3: */
    a->tmp1 = a->tmp2.value + a->tmp3.value;
    PTR_ASSIGN(a->result, act->priority, a->tmp1);
    act_leave(act, sizeof(fib_act_t));
    ssm_mark_complete();
    return;
  }
}

void top_return(struct act *cont) { return; }

int main(int argc, char *argv[]) {
  i32_svt result;
  initialize_event(&result.sv, &i32_vtable);
  DEBUG_SV_NAME(&result.sv, "result");

  int n = argc > 1 ? atoi(argv[1]) : 3;

  /*
   * Note we don't even call tick() here, and instead directly invoke fib's step
   * function (essentially doing tick()'s job). This is not really how the
   * runtime is supposed to behave, but it is a nice test case that allows us to
   * sidestep tick()'s implementation.
   */
  struct act top = {.step = top_return};
  DEBUG_ACT_NAME(&top, "top");

  act_call(enter_fib(&top, PRIORITY_AT_ROOT, DEPTH_AT_ROOT,
                     PTR_OF_SV(result.sv), n));

  printf("%d\n", result.value);

  return 0;
}
