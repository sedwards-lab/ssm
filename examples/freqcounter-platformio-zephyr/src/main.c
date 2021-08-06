/**
 * Main loop for a Zephyr example, adapted from flipfloploop.
 *
 * I/O (specifically, GPIO) and 64-bit timing are separated out of this
 * compilation unit, which focuses on handling events arriving in the event
 * queue.
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

K_MSGQ_DEFINE(ssm_env_queue, sizeof(ssm_env_event_t), 100, 1);

/** Send a timeout event to wake up the ssm_tick_thread. */
static void send_timeout_event(const struct device *dev, uint8_t chan,
                               uint32_t ticks, void *user_data) {
  static ssm_env_event_t timeout_msg = {.type = SSM_TIMEOUT};
  timeout_msg.time = ticks;
  k_msgq_put(&ssm_env_queue, &timeout_msg, K_NO_WAIT);
}

/**
 * Thread responsible for processing events (messages), calling ssm_tick(), and
 * starting the timeout counter.
 */
void ssm_tick_thread_body(void *p1, void *p2, void *p3) {
  timer64_start(ssm_timer_dev);

  ssm_activate(
      ssm_entry_point(&ssm_top_parent, SSM_ROOT_PRIORITY, SSM_ROOT_DEPTH));

  for (;;) {
    ssm_env_event_t msg;

    SSM_DEBUG_PRINT(":: before tick, [now: %016llx] [next: %016llx] (ctr: %016llx)\r\n",
           ssm_now(), ssm_next_event_time(), timer64_read(ssm_timer_dev));

    ssm_tick();

    ssm_time_t time = timer64_read(ssm_timer_dev);
    SSM_DEBUG_ASSERT(ssm_now() < time,
             "After tick, SSM logical time raced past wallclock time:\r\n%llx\r\n%llx\r\n", ssm_now(), time);

    SSM_DEBUG_PRINT(":: after tick, [now: %016llx] [next: %016llx] (ctr: %016llx)\r\n",
           ssm_now(), ssm_next_event_time(), timer64_read(ssm_timer_dev));

    ssm_time_t wake = ssm_next_event_time();

    if (wake != SSM_NEVER) {
      SSM_DEBUG_PRINT(":: setting alarm for [wake: %016llx]\r\n", wake);

      int err =
          timer64_set_alarm(ssm_timer_dev, wake, send_timeout_event, NULL);

      switch (err) {
      case 0: // Successful
        SSM_DEBUG_PRINT(":: alarm set successfully\r\n");
        goto wait;
      case -ETIME: // Alarm already expired
        SSM_DEBUG_PRINT(":: counter_set_channel_alarm: -ETIME\r\n");
        // We still need to remove the event from the queue
        goto wait;
      case -EBUSY:
        SSM_DEBUG_ASSERT(-EBUSY,
                 "counter_set_channel_alarm failed: alarm already set\r\n");
      case -ENOTSUP:
        SSM_DEBUG_ASSERT(-ENOTSUP,
                 "counter_set_channel_alarm failed: not supported\r\n");
      case -EINVAL:
        SSM_DEBUG_ASSERT(-EINVAL,
                 "counter_set_channel_alarm failed: invalid settings\r\n");
      default:
        SSM_DEBUG_ASSERT(err, "counter_set_channel_alarm failed for unknown reasons\r\n");
      }
    }

  wait:
    SSM_DEBUG_PRINT(":: blocking on queue\r\n");

    k_msgq_get(&ssm_env_queue, &msg, K_FOREVER); // Block for the next event

    // At this point, we are unblocked by one or more events in the event queue.
    // They are not guaranteed to be in timestamp order, because the interrupt
    // handler may itself be interrupted before having a chance to place the
    // evnet in the queue. Thus, we need to deplete the queue to make sure we
    // don't move forward without processing any potential earlier events.
    //
    // TODO: there is an optimization where we only need to remove events as
    // long as the timestamp is decreasing (earlier).

    // Cancel any potential pending alarm if it hasn't gone off yet. We can
    // always reset it later.
    timer64_cancel_alarm(ssm_timer_dev);
    // TODO: handle error

    do {
      switch (msg.type) {
      case SSM_TIMEOUT: // Alarm timed out
        SSM_DEBUG_PRINT(":: received SSM_TIMEOUT [ticks: %016llx] (ctr: %016llx)\r\n",
               msg.time, timer64_read(ssm_timer_dev));
        continue;
      case SSM_EXT_INPUT: // Received external input
        SSM_DEBUG_PRINT(":: received SSM_EXT_INPUT [time: %016llx] (ctr: %016llx)\r\n",
               msg.time, timer64_read(ssm_timer_dev));
        SSM_DEBUG_ASSERT(ssm_now() < msg.time,
            "Trying to schedule earlier time than now\r\nnow:  %llx\r\ntime: %llx\r\n", ssm_now(), msg.time);
        ssm_later_event(msg.sv, msg.time);
        continue;
      }
    } while (k_msgq_get(&ssm_env_queue, &msg, K_NO_WAIT) == 0);
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
  SSM_DEBUG_ASSERT(ssm_timer_dev, "device_get_binding failed with ssm-timer\r\n");

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
