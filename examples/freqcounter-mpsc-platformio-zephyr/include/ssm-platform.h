#ifndef _SSM_PLATFORM_H
#define _SSM_PLATFORM_H

#include <drivers/gpio.h>
#include <logging/log.h>
#include <logging/log_ctrl.h>
#include <ssm.h>
#include <sys/__assert.h>
#include <sys/mpsc_pbuf.h>
#include <sys/ring_buffer.h>

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
  MPSC_PBUF_HDR;
  uint16_t unused : 16 - MPSC_PBUF_HDR_BITS; // 14 bits of unused space here.
  input_info input;
  uint32_t tick;
  uint32_t mtk0;
  uint32_t mtk1;
} ssm_input_packet_t;

extern struct k_sem tick_sem;
extern struct mpsc_pbuf_buffer input_pbuf;
extern uint32_t dropped;
extern uint32_t input_count;

#endif /* _SSM_PLATFORM_H */
