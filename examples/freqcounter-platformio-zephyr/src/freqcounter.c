/**
 * A frequency counter with a fixed gate period.
 *
 * Press sw0 to begin sampling; samples taken from sw1. Reports frequency count
 * to serial console.
 */
#include "ssm-platform.h"

#define GATE_PERIOD ((ssm_time_t) 1 * SSM_SECOND)

typedef struct {
  struct ssm_act act;
  ssm_event_t gate;
  ssm_event_t signal;
  ssm_input_event_t sw0;
  ssm_input_event_t sw1;
} act_main_t;

struct ssm_act *enter_main(struct ssm_act *caller, ssm_priority_t priority,
                           ssm_depth_t depth);

void step_main(struct ssm_act *actg);

typedef struct {
  struct ssm_act act;
  ssm_event_t *gate;
  ssm_event_t *signal;
  ssm_event_t wake;
  struct ssm_trigger trig1;
  struct ssm_trigger trig2;
  ssm_time_t start_time;
  uint32_t count;
} act_freq_count_t;

struct ssm_act *enter_freq_count(struct ssm_act *caller, ssm_priority_t priority,
                           ssm_depth_t depth, ssm_event_t *gate,
                           ssm_event_t *signal);

void step_freq_count(struct ssm_act *actg);

struct ssm_act *enter_main(struct ssm_act *caller, ssm_priority_t priority,
                           ssm_depth_t depth) {
  struct ssm_act *actg =
      ssm_enter(sizeof(act_main_t), step_main, caller, priority, depth);
  act_main_t *acts = container_of(actg, act_main_t, act);

  ssm_initialize_event(&acts->gate);
  ssm_initialize_event(&acts->signal);

  return actg;
}

void step_main(struct ssm_act *actg) {
  act_main_t *acts = container_of(actg, act_main_t, act);

  switch (actg->pc) {

  case 0:;
    bind_input_handler(&acts->sw0, &acts->gate, DT_GPIO_DEV(sw0));
    bind_input_handler(&acts->sw1, &acts->signal, DT_GPIO_DEV(sw1));

    if (actg->depth < 0)
      SSM_THROW(SSM_EXHAUSTED_PRIORITY);

    ssm_activate(enter_freq_count(actg, actg->priority + 1 * (1 << actg->depth - 1),
                            actg->depth - 1, &acts->gate, &acts->signal));
    actg->pc = 1;
    return;

  case 1:;

  default:
    break;
  }

  unbind_input_handler(&acts->sw0);
  unbind_input_handler(&acts->sw1);

  ssm_unschedule(&acts->gate.sv);
  ssm_unschedule(&acts->signal.sv);
  ssm_leave(actg, sizeof(act_main_t));
}

struct ssm_act *enter_freq_count(struct ssm_act *caller, ssm_priority_t priority,
                           ssm_depth_t depth, ssm_event_t *gate,
                           ssm_event_t *signal) {
  struct ssm_act *actg =
      ssm_enter(sizeof(act_freq_count_t), step_freq_count, caller, priority, depth);
  act_freq_count_t *acts = container_of(actg, act_freq_count_t, act);

  acts->gate = gate;
  acts->signal = signal;
  acts->trig1.act = actg;
  acts->trig2.act = actg;

  ssm_initialize_event(&acts->wake);
  acts->start_time = 0;
  acts->count = 0;

  return actg;
}

void step_freq_count(struct ssm_act *actg) {
  act_freq_count_t *acts = container_of(actg, act_freq_count_t, act);

  switch (actg->pc) {
  case 0:; // Gate not active
    SSM_DEBUG_PRINT("Starting freq_counter with period: %llu\r\n", GATE_PERIOD);

    while (true) {
      while (true) { // Wait for gate
        if (ssm_event_on(&acts->gate->sv)) {
          SSM_DEBUG_PRINT("Received gate (%llu), starting to count...\r\n",
                 acts->start_time);
          break;
        }
        ssm_sensitize(&acts->gate->sv, &acts->trig1);
        actg->pc = 1;
        return;
  case 1:;
        ssm_desensitize(&acts->trig1);
      }

      // Inclusive of gate period start
      acts->count = !!ssm_event_on(&acts->signal->sv);
      ssm_later_event(&acts->wake, ssm_now() + GATE_PERIOD);

      while (true) {
        ssm_sensitize(&acts->signal->sv, &acts->trig1);
        ssm_sensitize(&acts->wake.sv, &acts->trig2);
        actg->pc = 2;
        return;
  case 2:;
        ssm_desensitize(&acts->trig1);
        ssm_desensitize(&acts->trig2);
        SSM_DEBUG_PRINT("Received signal, count: %u\r\n", acts->count);

        // Exclusive of gate period end
        if (ssm_event_on(&acts->wake.sv))
          break;
        acts->count++;
      }
      SSM_DEBUG_PRINT("Count: %u\r\n", acts->count);

      // FIXME: Zephyr doesn't seem to support FP arith by default, so this is
      // pretty inaccurate for low frequencies due to rounding errors.
      printk("Frequency: %llu Hz\r\n", acts->count * SSM_SECOND / GATE_PERIOD);
    }
  default:
    break;
  }

  ssm_leave(actg, sizeof(act_freq_count_t));
}

struct ssm_act *(*ssm_entry_point)(struct ssm_act *, ssm_priority_t,
                                   ssm_depth_t) = enter_main;
