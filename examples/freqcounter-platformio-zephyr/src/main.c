/**
 * This implementation uses a kernel message queue which both the alarm and the
 * input handlers write to. The tick thread blocks on this queue.
 *
 * A fundamental problem with this implementation is that mixing alarm and input
 * events in the message queue forces us deplete the queue in order to
 * successfully cancel the alarm. This would be ok (if a bit inefficient),
 * except that it doesn't account for input events overwriting one another.
 * It also involves a lot of memcpying internally, due to the value semantics of
 * the message queue abstraction.
 */
#include <device.h>
#include <devicetree.h>
#include <zephyr.h>

#include "ssm-platform.h"
#include "timer64.h"

LOG_MODULE_REGISTER(main);

#if !DT_NODE_HAS_STATUS(DT_ALIAS(ssm_timer), okay)
#error "ssm-timer device is not supported on this board"
#endif

#define SSM_TICK_STACKSIZE 4096
#define SSM_TICK_PRIORITY 7

const struct device *ssm_timer_dev = 0;

K_SEM_DEFINE(tick_sem, 0, 1);
K_MSGQ_DEFINE(ssm_env_queue, sizeof(ssm_env_event_t), 100, 1);

/** Send a timeout event to wake up the ssm_tick_thread. */
static void send_timeout_event(const struct device *dev, uint8_t chan,
                               uint32_t ticks, void *user_data) {
  k_sem_give(&tick_sem);
}

/**
 * Thread responsible for processing events (messages), calling ssm_tick(), and
 * starting the timeout counter.
 */
void ssm_tick_thread_body(void *p1, void *p2, void *p3) {
  timer64_start(ssm_timer_dev);

  /* ssm_time_t _last_input_time = 0; */

  ssm_activate(
      ssm_entry_point(&ssm_top_parent, SSM_ROOT_PRIORITY, SSM_ROOT_DEPTH));

  for (;;) {
    SSM_DEBUG_PRINT(
        ":: before tick, [now: %016llx] [next: %016llx] (ctr: %016llx)\r\n",
        ssm_now(), ssm_next_event_time(), timer64_read(ssm_timer_dev));

    ssm_tick();

    ssm_time_t time = timer64_read(ssm_timer_dev);
    SSM_DEBUG_ASSERT(ssm_now() < time,
                     "After tick, SSM logical time raced past wallclock "
                     "time:\r\n%llx\r\n%llx\r\n",
                     ssm_now(), time);

    SSM_DEBUG_PRINT(
        ":: after tick, [now: %016llx] [next: %016llx] (ctr: %016llx)\r\n",
        ssm_now(), ssm_next_event_time(), timer64_read(ssm_timer_dev));

    ssm_time_t wake = ssm_next_event_time();

    if (wake != SSM_NEVER) {
      int err =
          timer64_set_alarm(ssm_timer_dev, wake, send_timeout_event, NULL);

      SSM_DEBUG_PRINT(":: set alarm for [wake: %016llx] -> %s\r\n", wake,
          err == 0 ? "success" : err == -ETIME ? "already expired" : "error");

      switch (err) {
      case 0: // Successful
        goto wait;
      case -ETIME: // Alarm already expired
        // We still need to remove the event from the queue
        goto wait;
      case -EBUSY:
        SSM_DEBUG_ASSERT(
            -EBUSY, "counter_set_channel_alarm failed: alarm already set\r\n");
      case -ENOTSUP:
        SSM_DEBUG_ASSERT(-ENOTSUP,
                         "counter_set_channel_alarm failed: not supported\r\n");
      case -EINVAL:
        SSM_DEBUG_ASSERT(
            -EINVAL, "counter_set_channel_alarm failed: invalid settings\r\n");
      default:
        SSM_DEBUG_ASSERT(
            err, "counter_set_channel_alarm failed for unknown reasons\r\n");
      }
    }

  wait:
    SSM_DEBUG_PRINT(":: blocking on queue\r\n");

    k_sem_take(&tick_sem, K_FOREVER);

    // Cancel any potential pending alarm if it hasn't gone off yet.
    timer64_cancel_alarm(ssm_timer_dev);

    k_sem_reset(&tick_sem);

    ssm_env_event_t msg;
    if (k_msgq_peek(&ssm_env_queue, &msg) == 0) {
      /* { // Check for input monotonicity */
      /*   SSM_DEBUG_ASSERT( */
      /*       _last_input_time <= msg.time, */
      /*       "inputs were not monotonic\r\ninput': %llx\r\ninput:  %llx\r\n", */
      /*       _last_input_time, msg.time); */
      /*   _last_input_time = msg.time; */
      /* } */

      if (msg.time < ssm_next_event_time()) {
        k_msgq_get(&ssm_env_queue, &msg, K_NO_WAIT);
        ssm_later_event(msg.sv, msg.time);
      }
    }
  }
}

K_THREAD_STACK_DEFINE(ssm_tick_thread_stack, SSM_TICK_STACKSIZE);
struct k_thread ssm_tick_thread;

void main() {
  int err;

  SSM_DEBUG_PRINT("Sleeping for a second for you to start a terminal\r\n");
  k_sleep(K_SECONDS(1));
  SSM_DEBUG_PRINT("Starting...\r\n");

  ssm_timer_dev = device_get_binding(DT_LABEL(DT_ALIAS(ssm_timer)));
  SSM_DEBUG_ASSERT(ssm_timer_dev,
                   "device_get_binding failed with ssm-timer\r\n");

  err = timer64_init(ssm_timer_dev);
  SSM_DEBUG_ASSERT(!err, "timer64_init: %d\r\n", err);

  k_sleep(K_SECONDS(1));

  k_thread_create(&ssm_tick_thread, ssm_tick_thread_stack,
                  K_THREAD_STACK_SIZEOF(ssm_tick_thread_stack),
                  ssm_tick_thread_body, 0, 0, 0, SSM_TICK_PRIORITY, 0,
                  K_NO_WAIT);
}

/** Override ssm_throw function with some platform-specific printing. */
void ssm_throw(int reason, const char *file, int line, const char *func) {
  LOG_ERR("ssm_throw: %d (%s:%d in %s)\n", reason, file, line, func);
  LOG_ERR("%016llx/%016llx\r\n", ssm_now(), timer64_read(ssm_timer_dev));
  exit(reason);
}
