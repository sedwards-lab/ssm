/**
 * A frequency counter, feeding the frequency to a pulse generator.
 *
 * Press sw0 to begin sampling; samples taken from sw1; pulse written to led0.
 */
#include "ssm-platform.h"

#define GATE_PERIOD ((ssm_time_t)1 * SSM_SECOND)
#define BLINK_TIME ((ssm_time_t)100 * SSM_MILLISECOND)

extern ssm_event_t *const sw0;
extern ssm_event_t *const sw1;
extern ssm_bool_t *const led0;

typedef struct {
  struct ssm_act act;
  ssm_u64_t freq;
} act_main_t;

struct ssm_act *enter_main(struct ssm_act *caller, ssm_priority_t priority,
                           ssm_depth_t depth);

void step_main(struct ssm_act *actg);

typedef struct {
  struct ssm_act act;
  ssm_event_t *gate;
  ssm_event_t *signal;
  ssm_u64_t *freq;
  ssm_event_t wake;
  struct ssm_trigger trig1;
  struct ssm_trigger trig2;
  u32 count;
} act_freq_count_t;

struct ssm_act *enter_freq_count(struct ssm_act *caller,
                                 ssm_priority_t priority, ssm_depth_t depth,
                                 ssm_event_t *gate, ssm_event_t *signal,
                                 ssm_u64_t *freq);

void step_freq_count(struct ssm_act *actg);

typedef struct {
  struct ssm_act act;
  ssm_u64_t *freq;
  ssm_bool_t *led_ctl;
  ssm_event_t wake;

  struct ssm_trigger trig1;
  struct ssm_trigger trig2;
} act_freq_mime_t;

struct ssm_act *enter_freq_mime(struct ssm_act *caller, ssm_priority_t priority,
                                ssm_depth_t depth, ssm_u64_t *freq,
                                ssm_bool_t *led_ctl);

void step_freq_mime(struct ssm_act *actg);

typedef struct {
  struct ssm_act act;
  ssm_bool_t *led_ctl;
  ssm_u64_t *freq;

  struct ssm_trigger trig1;
} act_one_shot_t;

struct ssm_act *enter_one_shot(struct ssm_act *caller, ssm_priority_t priority,
                               ssm_depth_t depth, ssm_u64_t *freq,
                               ssm_bool_t *led_ctl);

void step_one_shot(struct ssm_act *actg);

struct ssm_act *enter_main(struct ssm_act *caller, ssm_priority_t priority,
                           ssm_depth_t depth) {
  struct ssm_act *actg =
      ssm_enter(sizeof(act_main_t), step_main, caller, priority, depth);
  act_main_t *acts = container_of(actg, act_main_t, act);
  ssm_initialize_u64(&acts->freq);
  return actg;
}

void step_main(struct ssm_act *actg) {
  act_main_t *acts = container_of(actg, act_main_t, act);

  switch (actg->pc) {

  case 0:;
    ssm_assign_u64(&acts->freq, actg->priority, 0);

    if (actg->depth < 0)
      SSM_THROW(SSM_EXHAUSTED_PRIORITY);

    ssm_activate(enter_freq_count(actg,
                                  actg->priority + 0 * (1 << actg->depth - 2),
                                  actg->depth - 2, sw0, sw1, &acts->freq));

    ssm_activate(enter_freq_mime(actg,
                                 actg->priority + 1 * (1 << actg->depth - 2),
                                 actg->depth - 2, &acts->freq, led0));

    ssm_activate(enter_one_shot(actg,
                                actg->priority + 3 * (1 << actg->depth - 2),
                                actg->depth - 2, &acts->freq, led0));

    actg->pc = 1;
    return;

  case 1:;

  default:
    break;
  }
  ssm_leave(actg, sizeof(act_main_t));
}

struct ssm_act *enter_freq_count(struct ssm_act *caller,
                                 ssm_priority_t priority, ssm_depth_t depth,
                                 ssm_event_t *gate, ssm_event_t *signal,
                                 ssm_u64_t *freq) {
  struct ssm_act *actg = ssm_enter(sizeof(act_freq_count_t), step_freq_count,
                                   caller, priority, depth);
  act_freq_count_t *acts = container_of(actg, act_freq_count_t, act);

  acts->gate = gate;
  acts->signal = signal;
  acts->freq = freq;
  acts->trig1.act = actg;
  acts->trig2.act = actg;

  ssm_initialize_event(&acts->wake);
  acts->count = 0;

  return actg;
}

void step_freq_count(struct ssm_act *actg) {
  act_freq_count_t *acts = container_of(actg, act_freq_count_t, act);

  switch (actg->pc) {
  case 0:; // Gate not active
    SSM_DEBUG_PRINT("Starting freq_counter with period: %llx\r\n", GATE_PERIOD);

    while (true) {
      while (true) { // Wait for gate
        if (ssm_event_on(&acts->gate->sv)) {
          SSM_DEBUG_PRINT("Received gate (%llx), starting to count...\r\n",
                          ssm_now());
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

      // FIXME: Zephyr doesn't seem to support FP arith by default, so this is
      // pretty inaccurate for low frequencies due to rounding errors.
      ssm_time_t freq = acts->count * SSM_SECOND / GATE_PERIOD;

      SSM_DEBUG_PRINT("Count: %u\r\n", acts->count);
      SSM_DEBUG_PRINT("Frequency: %llx Hz\r\n", freq);

      ssm_assign_u64(acts->freq, actg->priority, freq);
    }
  default:
    break;
  }

  ssm_leave(actg, sizeof(act_freq_count_t));
}

struct ssm_act *enter_freq_mime(struct ssm_act *caller, ssm_priority_t priority,
                                ssm_depth_t depth, ssm_u64_t *freq,
                                ssm_bool_t *led_ctl) {

  struct ssm_act *actg = ssm_enter(sizeof(act_freq_mime_t), step_freq_mime,
                                   caller, priority, depth);
  act_freq_mime_t *acts = container_of(actg, act_freq_mime_t, act);

  acts->freq = freq;
  acts->led_ctl = led_ctl;
  acts->trig1.act = actg;
  acts->trig2.act = actg;

  ssm_initialize_event(&acts->wake);

  return actg;
}

void step_freq_mime(struct ssm_act *actg) {
  act_freq_mime_t *acts = container_of(actg, act_freq_mime_t, act);
  switch (actg->pc) {
  case 0:;
    while (true) {
      if (acts->freq->value) {
        ssm_time_t later = ssm_now() + SSM_SECOND / acts->freq->value;
        SSM_DEBUG_PRINT("freq_mime [%llx]: frequency set to %llx, scheduling "
                        "next wake at [%llx]\r\n",
                        ssm_now(), acts->freq->value, later);
        ssm_assign_bool(acts->led_ctl, actg->priority, true);
        ssm_later_event(&acts->wake, later);
      }
      ssm_sensitize(&acts->wake.sv, &acts->trig1);
      ssm_sensitize(&acts->freq->sv, &acts->trig2);
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

struct ssm_act *enter_one_shot(struct ssm_act *caller, ssm_priority_t priority,
                               ssm_depth_t depth, ssm_u64_t *freq,
                               ssm_bool_t *led_ctl) {
  struct ssm_act *actg =
      ssm_enter(sizeof(act_one_shot_t), step_one_shot, caller, priority, depth);
  act_one_shot_t *acts = container_of(actg, act_one_shot_t, act);
  acts->led_ctl = led_ctl;
  acts->freq = freq;
  acts->trig1.act = actg;
  return actg;
}

void step_one_shot(struct ssm_act *actg) {
  act_one_shot_t *acts = container_of(actg, act_one_shot_t, act);
  switch (actg->pc) {
  case 0:;
    while (true) {
      ssm_sensitize(&acts->led_ctl->sv, &acts->trig1);
      actg->pc = 1;
      return;
    case 1:;
      ssm_desensitize(&acts->trig1);
      ssm_time_t delay =
          acts->freq->value
              ? MIN(BLINK_TIME, SSM_SECOND / acts->freq->value / 2)
              : BLINK_TIME;

      SSM_DEBUG_PRINT("one_shot [%llx]: received input (%d), turning off in "
                      "%llx at [%llx]\r\n",
                      ssm_now(), acts->led_ctl->value, delay,
                      ssm_now() + delay);

      if (acts->led_ctl->value)
        ssm_later_bool(acts->led_ctl, ssm_now() + delay, false);
    }
  default:
    break;
  }
}

int ssm_program_initialize(void) {
  initialize_static_input_device(&sw0->sv);
  initialize_static_input_device(&sw1->sv);

  ssm_activate(
      enter_main(&ssm_top_parent, SSM_ROOT_PRIORITY, SSM_ROOT_DEPTH - 1));

  ssm_activate(initialize_static_output_device(
      &ssm_top_parent, SSM_ROOT_PRIORITY + (1 << (SSM_ROOT_DEPTH - 1)),
      SSM_ROOT_DEPTH - 1, &led0->sv));

  return 0;
}
