#include "ssm-platform.h"

#define TIME_SHORT (u64)200 * SSM_MILLISECOND
#define TIME_LONG TIME_SHORT * 2
#define TIME_VERY_LONG TIME_LONG * 4
#define TIME_BLIP (u64) SSM_MICROSECOND

typedef struct {
  struct ssm_act act;
  ssm_bool_t led_ctl;
  ssm_event_t led_off;
  ssm_event_t led_pause;
  ssm_input_event_t sw0;
  ssm_input_event_t sw1;
} act_fun0_t;

struct ssm_act *enter_fun0(struct ssm_act *caller, ssm_priority_t priority,
                           ssm_depth_t depth);

void step_fun0(struct ssm_act *actg);

typedef struct {
  struct ssm_act act;
  ssm_bool_t *led_ctl;
  ssm_event_t *led_off;
  ssm_event_t *led_pause;
  struct ssm_trigger trig1;
  struct ssm_trigger trig2;
  struct ssm_trigger trig3;
} act_fun1_t;

struct ssm_act *enter_fun1(struct ssm_act *caller, ssm_priority_t priority,
                           ssm_depth_t depth, ssm_bool_t *led_ctl,
                           ssm_event_t *led_off, ssm_event_t *led_pause);

void step_fun1(struct ssm_act *actg);

struct ssm_act *enter_fun0(struct ssm_act *caller, ssm_priority_t priority,
                           ssm_depth_t depth) {
  struct ssm_act *actg =
      ssm_enter(sizeof(act_fun0_t), step_fun0, caller, priority, depth);
  act_fun0_t *acts = container_of(actg, act_fun0_t, act);

  ssm_initialize_bool(&acts->led_ctl);
  ssm_initialize_event(&acts->led_off);
  ssm_initialize_event(&acts->led_pause);

  return actg;
}

void step_fun0(struct ssm_act *actg) {
  act_fun0_t *acts = container_of(actg, act_fun0_t, act);

  switch (actg->pc) {

  case 0:;
    ssm_assign_bool(&acts->led_ctl, actg->priority, true);

    bind_input_handler(&acts->sw0, &acts->led_off, DT_GPIO_DEV(sw0));
    bind_input_handler(&acts->sw1, &acts->led_pause, DT_GPIO_DEV(sw1));

    if (actg->depth < 0)
      SSM_THROW(SSM_EXHAUSTED_PRIORITY);

    /** Bind led_ctl to output, and fork handler **/
    ssm_activate(enter_out_handler(
        actg, actg->priority + 0 * (1 << actg->depth - 1), actg->depth - 1,
        &acts->led_ctl, &acts->led_off, DT_GPIO_DEV(led0)));

    ssm_activate(enter_fun1(actg, actg->priority + 1 * (1 << actg->depth - 1),
                            actg->depth - 1, &acts->led_ctl, &acts->led_off,
                            &acts->led_pause));
    actg->pc = 1;
    return;

  case 1:;

  default:
    break;
  }

  unbind_input_handler(&acts->sw0);
  unbind_input_handler(&acts->sw1);

  ssm_unschedule(&acts->led_ctl.sv);
  ssm_unschedule(&acts->led_off);
  ssm_unschedule(&acts->led_pause);
  ssm_leave(actg, sizeof(act_fun0_t));
}

struct ssm_act *enter_fun1(struct ssm_act *caller, ssm_priority_t priority,
                           ssm_depth_t depth, ssm_bool_t *led_ctl,
                           ssm_event_t *led_off, ssm_event_t *led_pause) {
  struct ssm_act *actg =
      ssm_enter(sizeof(act_fun1_t), step_fun1, caller, priority, depth);
  act_fun1_t *acts = container_of(actg, act_fun1_t, act);

  acts->led_ctl = led_ctl;
  acts->led_off = led_off;
  acts->led_pause = led_pause;
  acts->trig1.act = actg;
  acts->trig2.act = actg;
  acts->trig3.act = actg;

  ssm_sensitize(&acts->led_ctl->sv, &acts->trig1);
  ssm_sensitize(acts->led_off, &acts->trig2);
  ssm_sensitize(acts->led_pause, &acts->trig3);
  return actg;
}

void step_fun1(struct ssm_act *actg) {
  act_fun1_t *acts = container_of(actg, act_fun1_t, act);

  switch (actg->pc) {
  case 0:;
    while (true) {
      if (ssm_event_on(acts->led_off))
        break;

      if (ssm_event_on(acts->led_pause)) {
        ssm_later_bool(acts->led_ctl, ssm_now() + TIME_BLIP, false);
        actg->pc = 3;
        return;

      case 3:;
        if (ssm_event_on(acts->led_off))
          break;

        ssm_later_bool(acts->led_ctl, ssm_now() + TIME_VERY_LONG, true);
        actg->pc = 2;
        return;
      }

      ssm_later_bool(acts->led_ctl, ssm_now() + TIME_LONG, false);

      actg->pc = 1;
      return;

    case 1:;
      if (ssm_event_on(acts->led_off))
        break;

      if (ssm_event_on(acts->led_pause)) {
        ssm_later_bool(acts->led_ctl, ssm_now() + TIME_BLIP, false);
        actg->pc = 4;
        return;

      case 4:;
        if (ssm_event_on(acts->led_off))
          break;

        ssm_later_bool(acts->led_ctl, ssm_now() + TIME_VERY_LONG, true);
        actg->pc = 2;
        return;
      }

      ssm_later_bool(acts->led_ctl, ssm_now() + TIME_SHORT, true);

      actg->pc = 2;
      return;

    case 2:;
    }

  default:
    break;
  }

  ssm_desensitize(&acts->trig1);
  ssm_desensitize(&acts->trig2);
  ssm_desensitize(&acts->trig3);
  ssm_leave(actg, sizeof(act_fun1_t));
}

struct ssm_act *(*ssm_entry_point)(struct ssm_act *, ssm_priority_t,
                                   ssm_depth_t) = enter_fun0;
