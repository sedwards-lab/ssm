/**
 * A frequency counter, feeding the frequency to a pulse generator.
 *
 * Press sw0 to begin sampling; samples taken from sw1; pulse written to led0.
 *
 * gatePeriod, blinkTime :: Duration
 *
 * main :: SSM ()
 * main = do
 *   gate <- extIn Event "sw0"
 *   signal <- extIn Event "sw1"
 *   (led_ctl, led_handler)  <- extOut Bool "led0"
 *   freq <- sv U64 0
 *   fork [ freq_count gate signal freq
 *        , freq_mime freq led_ctl
 *        , led_handler
 *        , one_shot freq led_ctl
 *        ]
 *
 * freq_count :: Ref Event -> Ref Event -> Ref Bool -> SSM ()
 * freq_count gate signal freq = do
 *   count <- var U64 0
 *   wake <- sv event ()
 *   while True $ do
 *     while True $ do
 *       when (eventOn gate) break
 *       wait [gate]
 *     count <~ if eventOn signal then 1 else 0
 *     after gatePeriod wake <~ ()
 *     while True $ do
 *       wait [signal, wake]
 *       when (eventOn wake) break
 *       count <~ deref count + 1
 *     freq <~ deref count * seconds / gatePeriod
 *
 * freq_mime :: Ref U64 -> Ref Bool -> SSM ()
 * freq_mime freq led_ctl = do
 *   wake <- sv event ()
 *   while True $ do
 *     when (deref freq > 0) $ do
 *       led_ctl <~ True
 *       after (sec 1 / deref freq) wake <~ ()
 *     wait [freq, wake]
 *
 * one_shot :: Ref U64 -> Ref Bool -> SSM ()
 * one_shot freq led_ctl = do
 *   while True $ do
 *     wait [led_ctl]
 *     let f     = deref freq
 *         delay = if f > 0 then blinkTime `min` (sec 1 / f / 2) else blinkTime
 *     when (deref led_ctl) $ after delay led_ctl <~ False
 */
#include "ssm-platform.h"

#define GATE_PERIOD ((ssm_time_t)1 * SSM_SECOND)
#define BLINK_TIME ((ssm_time_t)100 * SSM_MILLISECOND)

typedef struct {
  struct ssm_act act;

  ssm_event_t gate;
  ssm_event_t signal;
  ssm_input_event_t sw0;
  ssm_input_event_t sw1;

  ssm_u64_t freq;

  ssm_bool_t led_ctl;

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
                              ssm_depth_t depth, ssm_bool_t *led_ctl,
                              ssm_u64_t *freq);

void step_one_shot(struct ssm_act *actg);

struct ssm_act *enter_main(struct ssm_act *caller, ssm_priority_t priority,
                           ssm_depth_t depth) {
  struct ssm_act *actg =
      ssm_enter(sizeof(act_main_t), step_main, caller, priority, depth);
  act_main_t *acts = container_of(actg, act_main_t, act);

  ssm_initialize_event(&acts->gate);
  ssm_initialize_event(&acts->signal);
  ssm_initialize_bool(&acts->led_ctl);

  return actg;
}

void step_main(struct ssm_act *actg) {
  act_main_t *acts = container_of(actg, act_main_t, act);

  switch (actg->pc) {

  case 0:;
    bind_input_handler(&acts->sw0, &acts->gate, DT_GPIO_DEV(sw0));
    bind_input_handler(&acts->sw1, &acts->signal, DT_GPIO_DEV(sw1));
    ssm_assign_u64(&acts->freq, actg->priority, 0);
    ssm_assign_bool(&acts->led_ctl, actg->priority, false);

    if (actg->depth < 0)
      SSM_THROW(SSM_EXHAUSTED_PRIORITY);

    ssm_activate(enter_out_handler(
        actg, actg->priority + 2 * (1 << actg->depth - 2), actg->depth - 2,
        &acts->led_ctl, NULL, DT_GPIO_DEV(led0)));

    ssm_activate(enter_freq_count(
        actg, actg->priority + 0 * (1 << actg->depth - 2), actg->depth - 2,
        &acts->gate, &acts->signal, &acts->freq));

    ssm_activate(enter_freq_mime(actg,
                                 actg->priority + 1 * (1 << actg->depth - 2),
                                 actg->depth - 2, &acts->freq, &acts->led_ctl));

    ssm_activate(enter_one_shot(actg,
                               actg->priority + 3 * (1 << actg->depth - 2),
                               actg->depth - 2, &acts->led_ctl, &acts->freq));

    ssm_activate(enter_out_handler(
        actg, actg->priority + 2 * (1 << actg->depth - 2), actg->depth - 2,
        &acts->led_ctl, NULL, DT_GPIO_DEV(led0)));

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
    SSM_DEBUG_PRINT("Starting freq_counter with period: %llu\r\n", GATE_PERIOD);

    while (true) {
      while (true) { // Wait for gate
        if (ssm_event_on(&acts->gate->sv)) {
          SSM_DEBUG_PRINT("Received gate (%llu), starting to count...\r\n",
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

      ssm_time_t freq = acts->count * SSM_SECOND / GATE_PERIOD;

      SSM_DEBUG_PRINT("Count: %u\r\n", acts->count);
      SSM_DEBUG_PRINT("Frequency: %llu Hz\r\n", freq);

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
        SSM_DEBUG_PRINT("freq_mime [%llu]: frequency set to %llu, scheduling "
                        "output at [%llu]\r\n",
                        ssm_now(), acts->freq->value, later);
        ssm_assign_bool(acts->led_ctl, actg->priority, true);
        ssm_later_event(&acts->wake,
                        ssm_now() + SSM_SECOND / acts->freq->value);
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
                              ssm_depth_t depth, ssm_bool_t *led_ctl,
                              ssm_u64_t *freq) {

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
      SSM_DEBUG_PRINT(
          "one_shot [%llu]: received input, turning off in %llu at [%llu]\r\n",
          ssm_now(), delay, ssm_now() + delay);
      if (acts->led_ctl->value)
        ssm_later_bool(acts->led_ctl, ssm_now() + delay, false);
    }
  default:
    break;
  }
}

struct ssm_act *(*ssm_entry_point)(struct ssm_act *, ssm_priority_t,
                                   ssm_depth_t) = enter_main;
