#ifndef _SSM_PLATFORM_H
#define _SSM_PLATFORM_H

#include <drivers/gpio.h>
#include <ssm.h>
#include <sys/__assert.h>
#include <logging/log.h>

extern struct ssm_act *(*ssm_entry_point)(struct ssm_act *, ssm_priority_t,
                                          ssm_depth_t);

#define SSM_DEBUG_ASSERT(cond, ...)                                            \
  if (!(cond))                                                                 \
  printk(__VA_ARGS__), exit(1)

/* #define SSM_DEBUG_PRINT(...) printk(__VA_ARGS__) */

/* #define SSM_DEBUG_PRINT(...) do; while(0) */
#define SSM_DEBUG_PRINT(...) LOG_DBG(__VA_ARGS__)

/* "Disable" trace-related stuff. */
#define SSM_DEBUG_TRACE(...)                                                   \
  do; while (0)
#define SSM_DEBUG_MICROTICK()                                                  \
  do; while (0)

/** The SSM timer device, declared here for ease of access. */
extern const struct device *ssm_timer_dev;

/**** I/O ****/

/** My recreation of Zephyr 2.6's struct dt_spec */
typedef struct {
  const char *label;
  gpio_pin_t pin;
  gpio_flags_t flags;
} gpio_device_t;

/** Create a gpio_device_t from a device id/alias */
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
  struct gpio_callback cb;
  ssm_event_t *sv;
  const struct device *port;
} ssm_input_event_t;

/** Bind sv to input dev, storing the data in in. */
void bind_input_handler(ssm_input_event_t *in, ssm_event_t *sv,
                        gpio_device_t dev);

/** Unbind the sv in in. */
void unbind_input_handler(ssm_input_event_t *in);

/** Bind an SV (and a kill switch) to an output device. */
ssm_act_t *enter_out_handler(ssm_act_t *parent, ssm_priority_t priority,
                             ssm_depth_t depth, ssm_bool_t *ref_led,
                             ssm_event_t *ref_off, gpio_device_t dev);

/**** Zephyr event queue ****/

/** Types of events from the environment */
/* typedef enum { SSM_TIMEOUT, SSM_EXT_INPUT } ssm_event_type_t; */

/** An event from the environment, e.g., a timeout */
typedef struct {
  /* ssm_event_type_t type; */
  ssm_time_t time;
  ssm_event_t *sv; // TODO: handle values
} ssm_env_event_t;

/** The queue */
extern struct k_msgq ssm_env_queue;
extern struct k_sem tick_sem;

#endif /* _SSM_PLATFORM_H */
