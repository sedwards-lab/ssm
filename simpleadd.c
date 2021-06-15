#include <stdio.h>
#include <stdlib.h>
#include "ssm-act.h"
#include "ssm-runtime.h"
#include "ssm-types.h"
#include "ssm-debug.h"

/* 
add &c &a &b
  wait a
  wait b
  c = a + b

main
  var a = 1
  var b = 1
  var c = 0
  after 1s a = 10
  after 2s b = 10
  fork add(c, a, b)

 */

typedef struct {
  
  struct act act;

  ptr_i32_svt c;      // Where we should write our result
  ptr_i32_svt a;
  ptr_i32_svt b;

  struct trigger trigger1, trigger2;

} act_add_t;

typedef struct {
  struct act act;
  
  i32_svt a;
  i32_svt b;
  i32_svt c;

} act_main_t;

stepf_t step_add;

struct act *enter_add(struct act *cont, priority_t priority,
		     depth_t depth, ptr_i32_svt a, ptr_i32_svt b, ptr_i32_svt c)
{
  
  struct act *act =
    act_enter(sizeof(act_add_t), step_add, cont, priority, depth);
  
  DEBUG_ACT_NAME(act, "add");

  act_add_t *ac = container_of(act, act_add_t, act);
  
  // copy the value from the arguments
  ac->c = c;
  ac->b = b;
  ac->a = a;

  return act;

}

void step_add(struct act *act)  
{
  act_add_t *a = container_of(act, act_add_t, act);
  
  switch (act->pc) {
  
  case 0:
    a->trigger1.act = act;

    sensitize(a->a.ptr, &a->trigger1);
    act->pc = 1;
    return;
  
  case 1:
    desensitize(&a->trigger1); 

    // wait for b
    a->trigger2.act = act;
    sensitize(a->b.ptr, &a->trigger2);
    act->pc = 2;
    return;
  
  case 2:
    desensitize(&a->trigger2); 

    // write to c
    PTR_ASSIGN(a->c, act->priority, *DEREF(int, a->a) + *DEREF(int, a->b));
    act_leave(act, sizeof(act_add_t));
    return;
  
  }

}

stepf_t step_main;

struct act *enter_main(struct act *cont, priority_t priority,
		     depth_t depth)
{
  struct act *act =
    act_enter(sizeof(act_main_t), step_main, cont, priority, depth);

  DEBUG_ACT_NAME(act, "main");

  act_main_t *a = container_of(act, act_main_t, act);

  initialize_event(&a->a.sv, &i32_vtable);
  initialize_event(&a->b.sv, &i32_vtable);
  initialize_event(&a->c.sv, &i32_vtable);

  DEBUG_SV_NAME(&a->a.sv, "a");
  DEBUG_SV_NAME(&a->b.sv, "a");
  DEBUG_SV_NAME(&a->c.sv, "a");

  return act;
}

void step_main(struct act *act)  
{
  
  act_main_t *a = container_of(act, act_main_t, act);

  switch (act->pc) {    
  case 0:
    a->a.value = 1;
    a->b.value = 1;
    a->c.value = 0;

    // after 1s a = 10
    // after 2s b = 10
    a->a.sv.vtable->later(&a->a.sv, now + TICKS_PER_SECOND, 10);
    a->b.sv.vtable->later(&a->b.sv, now + 2 * TICKS_PER_SECOND, 10);
    
    act_call(enter_add(act, act->priority, act->depth, 
      PTR_OF_SV(a->a.sv), PTR_OF_SV(a->b.sv), PTR_OF_SV(a->c.sv)));

    act->pc = 1;
    return;
  
  case 1:
    printf("c = %d\n", a->c.value);
    act_leave(act, sizeof(act_main_t));
    return;

  }
}

void top_return(struct act *cont) { return; }

int main()
{  
  initialize_ssm(0);

  struct act top = {.step = top_return};
  DEBUG_ACT_NAME(&top, "top");

  act_fork(enter_main(&top, PRIORITY_AT_ROOT, DEPTH_AT_ROOT));

  for (ssm_time_t next = tick(); next != NO_EVENT_SCHEDULED; next = tick())
    printf("tick: next = %llu\n", next);
  
  return 0;
}
