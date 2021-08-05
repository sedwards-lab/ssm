/**
 * A sketch of what an I/O subsystem for SSM could look like, where regular SSM
 * variables are "bound" to input and output devices through functions provided
 * here. Specialized to work with the nRF52840-DK's LEDs and buttons.
 */
#include "ssm-platform.h"
#include "timer64.h"

#if !DT_NODE_HAS_STATUS(DT_ALIAS(led0), okay)
#error "led0 device alias not defined"
#endif

#if !DT_NODE_HAS_STATUS(DT_ALIAS(sw0), okay)
#error "sw0 device alias not defined"
#endif

#if !DT_NODE_HAS_STATUS(DT_ALIAS(sw1), okay)
#error "sw0 device alias not defined"
#endif

/** My recreation of Zephyr 2.6's struct dt_spec */
typedef struct {
  const char *label;
  gpio_pin_t pin;
  gpio_flags_t flags;
} gpio_device_t;

/** Shorthand to create a gpio_device_t from a device id/alias */
#define DT_GPIO_DEV(id)                                                        \
  (gpio_device_t) {                                                            \
    .label = DT_GPIO_LABEL(DT_ALIAS(id), gpios),                               \
    .pin = DT_GPIO_PIN(DT_ALIAS(id), gpios),                                   \
    .flags = DT_GPIO_FLAGS(DT_ALIAS(id), gpios)                                \
  }

/**
 * Contains data used in for binding input to (event) variables.
 * Must be persisted for as long that input is bound.
 */
typedef struct {
  /* Statically initialized */
  bool initialized;

  /* Statically initialized for static devices. We use a pointer here becuase
   * multiple ssm_input_event_ts may point to the same hardware device.
   */
  gpio_device_t dev;

  /* Can be statically initialized with some additional work */
  ssm_event_t sv;

  /* Should be initialized at runtime */
  struct gpio_callback cb;
  const struct device *port;

} ssm_input_event_t;

typedef struct {
  bool initialized;
  gpio_device_t dev;

  ssm_bool_t sv;

  const struct device *port;
} ssm_output_bool_t;

/**
 * OUTPUT: evaluated using an effect handler process.
 *
 * An output device is bound to an SV in enter_out_handler, which returns an
 * activation record for the effect handler that can be scheduled like any other
 * SSM process. When the bound SV is written to, the effect handler wakes up and
 * "evaluates" the effect by forwarding it to its bound GPIO pin.
 *
 * FIXME: this design currently uses an egregious level of indirection because
 * everything is kept in one data structure. I should push around some pointers
 * to flatten things out a bit.
 */
typedef struct {
  ssm_act_t act;
  ssm_trigger_t trigger1;
  ssm_output_bool_t *out;
} out_handler_act_t;

static ssm_stepf_t step_out_handler;

static ssm_act_t *enter_out_handler(ssm_act_t *parent, ssm_priority_t priority,
                                    ssm_depth_t depth, ssm_output_bool_t *out) {

  int err;
  ssm_act_t *actg = ssm_enter(sizeof(out_handler_act_t), step_out_handler,
                              parent, priority, depth);
  out_handler_act_t *acts = container_of(actg, out_handler_act_t, act);

  out->port = device_get_binding(out->dev.label);
  if (!out->port)
    return NULL;

  err = gpio_pin_configure(out->port, out->dev.pin,
                           GPIO_OUTPUT_ACTIVE | out->dev.flags);
  if (err)
    return NULL;
  gpio_pin_set(acts->out->port, acts->out->dev.pin, acts->out->sv.value);

  acts->out = out;
  ssm_initialize_bool(&out->sv);
  return actg;
}

/** Output effect handler process. */
static void step_out_handler(ssm_act_t *actg) {
  out_handler_act_t *acts = container_of(actg, out_handler_act_t, act);
  switch (actg->pc) {
  case 0:
    acts->trigger1.act = actg;
    ssm_sensitize(&acts->out->sv.sv, &acts->trigger1);
    actg->pc = 1;
    return;

  case 1:
    gpio_pin_set(acts->out->port, acts->out->dev.pin, acts->out->sv.value);
    return;
  }
  // Unreachable

  // "Reset" the output device. This might not be applicable in all
  // circumstances, and will differ on the output device.
  gpio_pin_set(acts->out->port, acts->out->dev.pin, 0);
  ssm_desensitize(&acts->trigger1);
  ssm_leave(actg, sizeof(out_handler_act_t));
}

static void input_event_handler(const struct device *port,
                                struct gpio_callback *cb,
                                gpio_port_pins_t pins) {
  static ssm_env_event_t timeout_msg = {.type = SSM_EXT_INPUT};
  timeout_msg.time = timer64_read(ssm_timer_dev);
  timeout_msg.sv = &container_of(cb, ssm_input_event_t, cb)->sv;
  k_msgq_put(&ssm_env_queue, &timeout_msg, K_NO_WAIT);
}

static int initialize_input_device(ssm_input_event_t *in) {
  int err;
  in->port = device_get_binding(in->dev.label);
  if (!in->port)
    return -ENODEV;

  ssm_initialize_event(&in->sv);

  err = gpio_pin_configure(in->port, in->dev.pin, GPIO_INPUT | in->dev.flags);

  if (err)
    return err;

  err = gpio_pin_interrupt_configure(in->port, in->dev.pin,
                                     GPIO_INT_EDGE_TO_ACTIVE);
  if (err)
    return err;

  gpio_init_callback(&in->cb, input_event_handler, BIT(in->dev.pin));

  err = gpio_add_callback(in->port, &in->cb);
  if (err)
    return err;

  return 0;
}

ssm_input_event_t sw0_in = {.initialized = false, .dev = DT_GPIO_DEV(sw0)};
ssm_input_event_t sw1_in = {.initialized = false, .dev = DT_GPIO_DEV(sw1)};
ssm_output_bool_t led0_out = {.initialized = false, .dev = DT_GPIO_DEV(led0)};

ssm_event_t *const sw0 = &sw0_in.sv;
ssm_event_t *const sw1 = &sw1_in.sv;
ssm_bool_t *const led0 = &led0_out.sv;

int initialize_static_input_device(ssm_sv_t *sv) {
  if (sv == &sw0->sv)
    return initialize_input_device(&sw0_in);
  else if (sv == &sw1->sv)
    return initialize_input_device(&sw1_in);
  else
    return -ENODEV;
}

ssm_act_t *initialize_static_output_device(ssm_act_t *parent,
                                           ssm_priority_t priority,
                                           ssm_depth_t depth, ssm_sv_t *sv) {
  if (sv == &led0->sv)
    return enter_out_handler(parent, priority, depth, &led0_out);
  else
    return NULL;
}
