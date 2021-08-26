#ifndef _SSM_PLATFORM_H
#define _SSM_PLATFORM_H

#include <drivers/gpio.h>
#include <logging/log.h>
#include <logging/log_ctrl.h>
#include <ssm.h>
#include <sys/__assert.h>
#include <sys/atomic.h>

extern int ssm_program_initialize(void);

#define SSM_DEBUG_ASSERT(cond, ...)                                            \
  if (!(cond))                                                                 \
  printk(__VA_ARGS__), exit(1)

/* #define SSM_DEBUG_PRINT(...) printk(__VA_ARGS__) */

/* #define SSM_DEBUG_PRINT(...) do; while(0) */
#define SSM_DEBUG_PRINT(...) LOG_DBG(__VA_ARGS__)

/* "Disable" trace-related stuff. */
#define SSM_DEBUG_TRACE(...)                                                   \
  do                                                                           \
    ;                                                                          \
  while (0)
#define SSM_DEBUG_MICROTICK()                                                  \
  do                                                                           \
    ;                                                                          \
  while (0)

typedef struct {
  uint8_t type;
  uint8_t index;
} input_info;

/** The SSM timer device, declared here for ease of access. */
extern const struct device *ssm_timer_dev;

/** Static I/O devices */
extern ssm_event_t *const sw0;
extern ssm_event_t *const sw1;
extern ssm_bool_t *const led0;

extern int initialize_static_input_device(ssm_sv_t *sv);
extern ssm_sv_t *lookup_input_device(input_info *input);
extern ssm_act_t *initialize_static_output_device(ssm_act_t *parent,
                                                  ssm_priority_t priority,
                                                  ssm_depth_t depth,
                                                  ssm_sv_t *sv);

/**** Zephyr event queue ****/

enum { SSM_EVENT_T };

typedef struct {
  uint32_t tick;
  uint32_t mtk0;
  uint32_t mtk1;
  input_info input;
} ssm_input_packet_t;


// Input buffer size must be power of 2 for fast mod operation.
#define INPUT_BUFFER_EXPONENT 12
#define INPUT_BUFFER_SIZE (1 << INPUT_BUFFER_EXPONENT)

extern ssm_input_packet_t input_buffer[INPUT_BUFFER_SIZE];
/* extern atomic_t rb_wclaim; */
extern atomic_t rb_wcommit; // this might not need to be atomic
extern atomic_t rb_rclaim;
/* extern atomic_t rb_rcommit; */

#define IBI_MOD(idx) ((idx) % INPUT_BUFFER_SIZE)

extern struct k_sem tick_sem;

extern struct mpsc_pbuf_buffer input_pbuf;
extern uint32_t dropped;
/* extern uint32_t input_count; */


/**** Profiling ****/

extern volatile uint32_t profile_tag;
extern const struct device *gpio_dev;

#define LED_MASK(n) (1u << DT_GPIO_PIN(DT_ALIAS(led##n), gpios))
#define LED_MASKS (LED_MASK(0) | LED_MASK(1) | LED_MASK(2) | LED_MASK(3))
#define SYM_TO_VAL(sym, n) ((sym & (1u << n)) ? LED_MASK(n) : 0 )
#define SYM_TO_VALS(sym) (SYM_TO_VAL(sym, 0) | SYM_TO_VAL(sym, 1) | SYM_TO_VAL(sym, 2) | SYM_TO_VAL(sym, 3))

#define SSM_PROFILE(sym) do { compiler_barrier(); gpio_port_set_masked_raw(gpio_dev, LED_MASKS, SYM_TO_VALS(sym)); compiler_barrier(); } while (0)

/* #define SSM_PROFILE(sym) do { compiler_barrier(); profile_tag = sym; compiler_barrier(); } while (0) */

enum {
  PROF_MAIN_SETUP,
  PROF_MAIN_BEFORE_LOOP_CONTINUE,
  PROF_MAIN_BEFORE_INPUT_CONSUME,
  PROF_MAIN_BEFORE_TICK,
  PROF_MAIN_BEFORE_SLEEP_ALARM,
  PROF_MAIN_BEFORE_SLEEP_BLOCK,
  PROF_MAIN_BEFORE_WAKE_RESET,
  PROF_INPUT_BEFORE_READTIME,
  PROF_INPUT_BEFORE_ALLOC,
  PROF_INPUT_BEFORE_COMMIT,
  PROF_INPUT_BEFORE_WAKE,
  PROF_INPUT_BEFORE_LEAVE,
  PROF_USER_BEFORE_STEP,
  PROF_USER_BEFORE_YIELD,
  PROF_USER_BEFORE_LEAVE,
  PROF_LIMIT,
};


#endif /* _SSM_PLATFORM_H */
