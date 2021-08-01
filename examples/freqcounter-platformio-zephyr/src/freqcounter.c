#include "ssm-platform.h"

#define TIME_SHORT (u64)200 * SSM_MILLISECOND
#define TIME_LONG TIME_SHORT * 2
#define TIME_VERY_LONG TIME_LONG * 4
#define TIME_BLIP (u64) SSM_MICROSECOND

typedef struct {
  struct ssm_act act;
  ssm_event_t gate;
  ssm_event_t signal;
  ssm_input_event_t sw0;
  ssm_input_event_t sw1;
} act_fun0_t;

struct ssm_act *enter_fun0(struct ssm_act *caller, ssm_priority_t priority,
                           ssm_depth_t depth);

void step_fun0(struct ssm_act *actg);

typedef struct {
  struct ssm_act act;
  ssm_event_t *gate;
  ssm_event_t *signal;
  struct ssm_trigger trig1;
  struct ssm_trigger trig2;
  ssm_time_t start_time;
  uint32_t count;
} act_fun1_t;

struct ssm_act *enter_fun1(struct ssm_act *caller, ssm_priority_t priority,
                           ssm_depth_t depth, ssm_event_t *gate,
                           ssm_event_t *signal);

void step_fun1(struct ssm_act *actg);

struct ssm_act *enter_fun0(struct ssm_act *caller, ssm_priority_t priority,
                           ssm_depth_t depth) {
  struct ssm_act *actg =
      ssm_enter(sizeof(act_fun0_t), step_fun0, caller, priority, depth);
  act_fun0_t *acts = container_of(actg, act_fun0_t, act);

  ssm_initialize_event(&acts->gate);
  ssm_initialize_event(&acts->signal);

  return actg;
}

void step_fun0(struct ssm_act *actg) {
  act_fun0_t *acts = container_of(actg, act_fun0_t, act);

  switch (actg->pc) {

  case 0:;
    bind_input_handler(&acts->sw0, &acts->gate, DT_GPIO_DEV(sw0));
    bind_input_handler(&acts->sw1, &acts->signal, DT_GPIO_DEV(sw1));

    if (actg->depth < 0)
      SSM_THROW(SSM_EXHAUSTED_PRIORITY);

    ssm_activate(enter_fun1(actg, actg->priority + 1 * (1 << actg->depth - 1),
                            actg->depth - 1, &acts->gate, &acts->signal));
    actg->pc = 1;
    return;

  case 1:;

  default:
    break;
  }

  unbind_input_handler(&acts->sw0);
  unbind_input_handler(&acts->sw1);

  ssm_unschedule(&acts->gate);
  ssm_unschedule(&acts->signal);
  ssm_leave(actg, sizeof(act_fun0_t));
}

/** Generated code, unmodified **/

struct ssm_act *enter_fun1(struct ssm_act *caller, ssm_priority_t priority,
                           ssm_depth_t depth, ssm_event_t *gate,
                           ssm_event_t *signal) {
  struct ssm_act *actg =
      ssm_enter(sizeof(act_fun1_t), step_fun1, caller, priority, depth);
  act_fun1_t *acts = container_of(actg, act_fun1_t, act);

  acts->gate = gate;
  acts->signal = signal;
  acts->trig1.act = actg;
  acts->trig2.act = actg;

  acts->start_time = 0;
  acts->count = 0;

  ssm_sensitize(acts->gate, &acts->trig1);
  ssm_sensitize(acts->signal, &acts->trig2);
  return actg;
}

void step_fun1(struct ssm_act *actg) {
  act_fun1_t *acts = container_of(actg, act_fun1_t, act);

  switch (actg->pc) {
  case 0:; // Gate not active
    if (ssm_event_on(acts->gate)) {
      // Start counting
      actg->pc = 1;
      acts->start_time = ssm_now();
      acts->count = !!ssm_event_on(acts->signal);
      SSM_DEBUG_PRINT("Received gate (%llu), starting to count...\r\n",
             acts->start_time);
    }
    return;

  case 1:; // Gate active
    if (ssm_event_on(acts->gate)) {
      ssm_time_t period = ssm_now() - acts->start_time;
      printk("Time period: %llu\r\n", period);
      printk("Count: %u\r\n", acts->count);

      // FIXME: Zephyr doesn't seem to support floating point, so this is going
      // to be pretty heavily rounded for low frequencies.
      printk("Frequency: %llu Hz\r\n", acts->count * SSM_SECOND / period);

      // Stop counting
      actg->pc = 0;
      return;
    }

    // NOTE: we don't need to check event_on(signal); there's no other reason we
    // could have woken up.
    acts->count++;
    SSM_DEBUG_PRINT("Received signal, count: %u\r\n", acts->count);
    return;
  default:
    break;
  }

  ssm_desensitize(&acts->trig1);
  ssm_desensitize(&acts->trig2);
  ssm_leave(actg, sizeof(act_fun1_t));
}

struct ssm_act *(*ssm_entry_point)(struct ssm_act *, ssm_priority_t,
                                   ssm_depth_t) = enter_fun0;
