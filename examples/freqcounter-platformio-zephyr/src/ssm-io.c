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

/**
 * OUTPUT: evaluated using an effect handler process.
 *
 * An output device is bound to an SV in enter_out_handler, which returns an
 * activation record for the effect handler that can be scheduled like any other
 * SSM process. When the bound SV is written to, the effect handler wakes up and
 * "evaluates" the effect by forwarding it to its bound GPIO pin.
 *
 * This current design also inclues a "killswitch", a pure (event type) SV that,
 * when written to, causes the effect handler to quit. The pointer passed in can
 * be a null pointer, but in that case the effect handler will never terminate
 * (and thus the program will never progrss past the fork statement where the
 * output device is created. Note that there is almost definitely a better way
 * to do resource management rather than like this (manually).
 */
typedef struct {
  ssm_act_t act;
  ssm_trigger_t trigger1;
  ssm_trigger_t trigger2;
  ssm_bool_t *ref_led;
  ssm_event_t *ref_off;
  const struct device *port;
  gpio_pin_t pin;
} out_handler_act_t;

ssm_stepf_t step_out_handler;

ssm_act_t *enter_out_handler(ssm_act_t *parent, ssm_priority_t priority,
                             ssm_depth_t depth, ssm_bool_t *ref_led,
                             ssm_event_t *ref_off, gpio_device_t dev) {

  ssm_act_t *actg = ssm_enter(sizeof(out_handler_act_t), step_out_handler,
                              parent, priority, depth);
  out_handler_act_t *acts = container_of(actg, out_handler_act_t, act);
  acts->ref_led = ref_led;
  acts->ref_off = ref_off;

  acts->port = device_get_binding(dev.label);
  SSM_DEBUG_ASSERT(acts->port != 0, "device_get_binding failed");
  acts->pin = dev.pin;
  if (gpio_pin_configure(acts->port, acts->pin,
                         GPIO_OUTPUT_ACTIVE | dev.flags) < 0)
    SSM_DEBUG_ASSERT(0, "gpio_pin_configure failed");
  return actg;
}

/** Output effect handler process. */
void step_out_handler(ssm_act_t *actg) {
  out_handler_act_t *acts = container_of(actg, out_handler_act_t, act);
  switch (actg->pc) {
  case 0:
    acts->trigger1.act = actg;
    ssm_sensitize(&acts->ref_led->sv, &acts->trigger1);
    if (acts->ref_off) {
      acts->trigger2.act = actg;
      ssm_sensitize(&acts->ref_off->sv, &acts->trigger2);
    }
    actg->pc = 1;
    return;

  case 1:
    if (acts->ref_off && ssm_event_on(&acts->ref_off->sv))
      goto leave;

    gpio_pin_set(acts->port, acts->pin, acts->ref_led->value);
    return;
  }
leave:
  // "Reset" the output device. This might not be applicable in all
  // circumstances, and will differ on the output device.
  gpio_pin_set(acts->port, acts->pin, 0);

  ssm_desensitize(&acts->trigger1);
  if (acts->ref_off)
    ssm_desensitize(&acts->trigger2);
  ssm_leave(actg, sizeof(out_handler_act_t));
}

/**
 * INPUT: received via events to a bound SV.
 *
 * The bind_input_handler function associates an input device with the given SV,
 * which registers a GPIO callback that inserts a timestamped event in the msgq
 * every time the device receives input.
 *
 * Note that Zephyr's API expects the function pointer for the input handler to
 * be persisted in some struct gpio_callback, whose memory the caller is
 * responsible for managing. This is because the callback is added as a node in
 * a singly-linked list of callbacks for the input device. By embedding the
 * struct gpio_callback within ssm_input_event_t, we may access other
 * callback-specific data in the input handler using container_of to reach the
 * other members of that struct, e.g., the SV pointer.
 *
 * The drawback of this approach is that, because the memory for the callback
 * is managed within the ssm_input_event_t, we also require the caller to
 * unbind_input_handler before freeing that memory, to avoid use-after-free
 * of the struct gpio_callback upon receiving input.
 */
static void ssm_gpio_input_handler(const struct device *port,
                                   struct gpio_callback *cb,
                                   gpio_port_pins_t pins) {
  static ssm_env_event_t timeout_msg = {.type = SSM_EXT_INPUT};
  timeout_msg.time = timer64_read(ssm_timer_dev);
  timeout_msg.sv = container_of(cb, ssm_input_event_t, cb)->sv;

  k_msgq_put(&ssm_env_queue, &timeout_msg, K_NO_WAIT);
}

void bind_input_handler(ssm_input_event_t *in, ssm_event_t *sv,
                        gpio_device_t dev) {
  in->port = device_get_binding(dev.label);

  if (gpio_pin_configure(in->port, dev.pin, GPIO_INPUT | dev.flags) < 0)
    SSM_DEBUG_ASSERT(0, "gpio_pin_configure failed");

  if (gpio_pin_interrupt_configure(in->port, dev.pin, GPIO_INT_EDGE_TO_ACTIVE) <
      0)
    SSM_DEBUG_ASSERT(0, "gpio_pin_interrupt_configure failed");

  gpio_init_callback(&in->cb, ssm_gpio_input_handler, BIT(dev.pin));
  if (gpio_add_callback(in->port, &in->cb))
    SSM_DEBUG_ASSERT(0, "gpio_add_callback failed");

  in->sv = sv;
}

void unbind_input_handler(ssm_input_event_t *in) {
  if (gpio_remove_callback(in->port, &in->cb))
    SSM_DEBUG_ASSERT(0, "gpio_remove_callback failed");
  in->sv = NULL;
}
