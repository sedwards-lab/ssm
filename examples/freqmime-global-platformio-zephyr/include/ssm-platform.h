#ifndef _SSM_PLATFORM_H
#define _SSM_PLATFORM_H

#include <drivers/gpio.h>
#include <ssm.h>
#include <sys/__assert.h>

extern int ssm_program_initialize(void);

#define SSM_DEBUG_ASSERT(cond, ...)                                            \
  if (!(cond))                                                                 \
  printk(__VA_ARGS__), exit(1)

#define SSM_DEBUG_PRINT(...) printk(__VA_ARGS__)

/* "Disable" trace-related stuff. */
#define SSM_DEBUG_TRACE(...)                                                   \
  do; while (0)
#define SSM_DEBUG_MICROTICK()                                                  \
  do; while (0)

/** The SSM timer device, declared here for ease of access. */
extern const struct device *ssm_timer_dev;

/** Static I/O devices */
extern ssm_event_t *const sw0;
extern ssm_event_t *const sw1;
extern ssm_bool_t *const led0;

extern int initialize_static_input_device(ssm_sv_t *sv);
extern ssm_act_t *initialize_static_output_device(ssm_act_t *parent,
                                           ssm_priority_t priority,
                                           ssm_depth_t depth, ssm_sv_t *sv);

/**** Zephyr event queue ****/

/** Types of events from the environment */
typedef enum { SSM_TIMEOUT, SSM_EXT_INPUT } ssm_event_type_t;

/** An event from the environment, e.g., a timeout */
typedef struct {
  ssm_event_type_t type;
  ssm_time_t time;
  ssm_event_t *sv; // TODO: handle values
} ssm_env_event_t;

/** The queue */
extern struct k_msgq ssm_env_queue;

#endif /* _SSM_PLATFORM_H */
