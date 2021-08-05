/**
 * A frequency counter with a fixed gate period.
 *
 * Samples taken from sw1. Reports frequency to serial console.
 *
 * freq_count signal = do
 *   count <- var u32 -- How to write this
 *   gate <- var ()
 *   after 1s, gate <~ ()
 *
 *   while True $ do
 *     if event_on gate then $ do
 *       print count
 *       count <~ if event_on signal then 1 else 0
 *       after 1s, gate <~ ()
 *     else $ do
 *       count++
 *
 *     wait [gate, signal]
 */
#include "ssm-platform.h"

#define GATE_PERIOD ((ssm_time_t)1 * SSM_SECOND)

typedef struct {
  struct ssm_act act;
  ssm_event_t signal;
  ssm_input_event_t sw1;
} act_main_t;

struct ssm_act *enter_main(struct ssm_act *caller, ssm_priority_t priority,
                           ssm_depth_t depth);

void step_main(struct ssm_act *actg);

typedef struct {
  struct ssm_act act;
  ssm_event_t gate;
  ssm_event_t *signal;
  struct ssm_trigger trig1;
  struct ssm_trigger trig2;
  uint32_t count;
  ssm_time_t gate_period;
} act_freq_count_t;

struct ssm_act *enter_freq_count(struct ssm_act *caller,
                                 ssm_priority_t priority, ssm_depth_t depth,
                                 ssm_event_t *signal, ssm_time_t gate_period);

void step_freq_count(struct ssm_act *actg);

struct ssm_act *enter_main(struct ssm_act *caller, ssm_priority_t priority,
                           ssm_depth_t depth) {
  struct ssm_act *actg =
      ssm_enter(sizeof(act_main_t), step_main, caller, priority, depth);
  act_main_t *acts = container_of(actg, act_main_t, act);

  ssm_initialize_event(&acts->signal);

  return actg;
}

void step_main(struct ssm_act *actg) {
  act_main_t *acts = container_of(actg, act_main_t, act);

  switch (actg->pc) {

  case 0:;
    bind_input_handler(&acts->sw1, &acts->signal, DT_GPIO_DEV(sw1));

    if (actg->depth < 0)
      SSM_THROW(SSM_EXHAUSTED_PRIORITY);

    ssm_activate(enter_freq_count(actg,
                                  actg->priority + 1 * (1 << actg->depth - 1),
                                  actg->depth - 1, &acts->signal, GATE_PERIOD));
    actg->pc = 1;
    return;

  case 1:;

  default:
    break;
  }

  unbind_input_handler(&acts->sw1);

  ssm_unschedule(&acts->signal.sv);
  ssm_leave(actg, sizeof(act_main_t));
}

struct ssm_act *enter_freq_count(struct ssm_act *caller,
                                 ssm_priority_t priority, ssm_depth_t depth,
                                 ssm_event_t *signal, ssm_time_t gate_period) {
  struct ssm_act *actg = ssm_enter(sizeof(act_freq_count_t), step_freq_count,
                                   caller, priority, depth);
  act_freq_count_t *acts = container_of(actg, act_freq_count_t, act);

  acts->signal = signal;
  acts->trig1.act = actg;
  acts->trig2.act = actg;

  ssm_initialize_event(&acts->gate);
  acts->count = 0;
  acts->gate_period = gate_period;

  return actg;
}

void step_freq_count(struct ssm_act *actg) {
  act_freq_count_t *acts = container_of(actg, act_freq_count_t, act);
  switch (actg->pc) {
  case 0:;
    SSM_DEBUG_PRINT("Starting freq_counter with period: %llu\r\n", GATE_PERIOD);

    ssm_later_event(&acts->gate, ssm_now() + acts->gate_period);

    while (true) {
      if (ssm_event_on(&acts->gate.sv)) {
        SSM_DEBUG_PRINT("freq_count [%llu]: Received gate...\r\n", ssm_now());

        printk("Frequency: %llu Hz\r\n",
               acts->count * SSM_SECOND / acts->gate_period);

        // Inclusive of gate period start, exclusive of gate period end
        acts->count = ssm_event_on(&acts->signal->sv) ? 1 : 0;

        // Reset internal gate signal
        ssm_later_event(&acts->gate, ssm_now() + acts->gate_period);
      } else {
        acts->count++;
      }

      SSM_DEBUG_PRINT("freq_count [%llu]: signal: %u, count: %u\r\n", ssm_now(),
                      ssm_event_on(&acts->signal->sv), acts->count);

      ssm_sensitize(&acts->signal->sv, &acts->trig1);
      ssm_sensitize(&acts->gate.sv, &acts->trig2);
      actg->pc = 1;
      return;

    case 1:;
      ssm_desensitize(&acts->trig2);
      ssm_desensitize(&acts->trig1);
    }
  default:
    break;
  }

  ssm_leave(actg, sizeof(act_freq_count_t));
}

struct ssm_act *(*ssm_entry_point)(struct ssm_act *, ssm_priority_t,
                                   ssm_depth_t) = enter_main;
