/**
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
#define SSM_INPUT_PBUF_SIZE 8192

const struct device *ssm_timer_dev = 0;

K_SEM_DEFINE(tick_sem, 0, 1);
uint32_t input_pbuf_data[SSM_INPUT_PBUF_SIZE];
struct mpsc_pbuf_buffer input_pbuf;

/** Send a timeout event to wake up the ssm_tick_thread. */
static void send_timeout_event(const struct device *dev, uint8_t chan,
                               uint32_t ticks, void *user_data) {
  k_sem_give(&tick_sem);
}

uint32_t packet_get_len(union mpsc_pbuf_generic *packet) {
  return sizeof(ssm_input_packet_t);
}

uint32_t dropped = 0;

/**
 * Thread responsible for processing events (messages), calling ssm_tick(), and
 * starting the timeout counter.
 */
void ssm_tick_thread_body(void *p1, void *p2, void *p3) {
  timer64_start(ssm_timer_dev);

  struct mpsc_pbuf_buffer_config input_pbuf_cfg = {
      .buf = input_pbuf_data,
      .size = SSM_INPUT_PBUF_SIZE,
      .get_wlen = packet_get_len,
      .notify_drop = NULL, // not necessary as long as we don't use overwrite policy
      .flags = 0,
  };

  mpsc_pbuf_init(&input_pbuf, &input_pbuf_cfg);

  ssm_program_initialize();

  // TODO: we are assuming no input events at time 0.
  ssm_tick();

  ssm_input_packet_t *input_packet = NULL;

  /* ssm_time_t _last_input_time = 0l; // For debugging/asserting */

  for (;;) {
    /* ssm_time_t _wall_time = */
    /*     timer64_read(ssm_timer_dev); // For debbugging/testing */

    {
      uint32_t d = dropped;
      if (d && d > 2000) {
        LOG_ERR("Dropped at least %u packets\r\n", dropped);
        dropped = 0;
      }
    }

    /* { // Invariant: now < wall time */
    /*   SSM_DEBUG_ASSERT(ssm_now() < _wall_time, */
    /*                    "SSM logical time raced past wallclock time:\r\n" */
    /*                    "now:  %llx\r\nwall: %llx\r\n", */
    /*                    ssm_now(), _wall_time); */
    /* } */

    ssm_time_t next_time = ssm_next_event_time();

    if (input_packet) {
      ssm_time_t packet_time = TIMER64_CALC(input_packet->tick, input_packet->mtk0, input_packet->mtk1);
      /* { // Invariant: input time < wall time, due to monotonicity of timer */
      /*   SSM_DEBUG_ASSERT(packet_time < _wall_time, */
      /*                    "Obtained input from the future:\r\n" */
      /*                    "input: %llx\r\nwall:  %llx\r\n", */
      /*                    packet_time, _wall_time); */
      /* } */

      if (packet_time <= next_time) {
        // TODO: handle values too
        ssm_schedule(lookup_input_device(&input_packet->input), packet_time);

        mpsc_pbuf_free(&input_pbuf, (union mpsc_pbuf_generic *)input_packet);
        input_packet = (ssm_input_packet_t *)mpsc_pbuf_claim(&input_pbuf);

        /* if (input_packet) { // Invariant: input time' <= input time */
        /*   ssm_time_t packet_time = TIMER64_CALC(input_packet->tick, input_packet->mtk0, input_packet->mtk1); */
        /*   SSM_DEBUG_ASSERT(_last_input_time < packet_time, */
        /*                    "Inputs queued out of order:\r\n" */
        /*                    "input': %016llx\r\n" */
        /*                    "input:  %016llx\r\n", */
        /*                    _last_input_time, packet_time); */
        /*   // Note: for now, we assume input events cannot be simultaneous. */
        /*   _last_input_time = packet_time; */
        /* } */
      }

      ssm_tick();

    } else {
      if (next_time < timer64_read(ssm_timer_dev)) {
        // It's possible that we received input since last checking input.
        // Double check one last time.
        input_packet = (ssm_input_packet_t *)mpsc_pbuf_claim(&input_pbuf);
        if (input_packet)
          // We can't tick here. TODO: don't put this here, elide this with
          // another branch, even though this is very unlikely.
          continue;
        ssm_tick();
        input_packet = (ssm_input_packet_t *)mpsc_pbuf_claim(&input_pbuf);
      } else {
        if (next_time != SSM_NEVER) {
          SSM_DEBUG_PRINT(":: setting alarm for [next_time: %016llx]\r\n",
                          next_time);

          int err = timer64_set_alarm(ssm_timer_dev, next_time,
                                      send_timeout_event, NULL);

          switch (err) {
          case 0:
            SSM_DEBUG_PRINT(":: alarm set successfully\r\n");
            k_sem_take(&tick_sem, K_FOREVER);
            break;
          case -ETIME:
            SSM_DEBUG_PRINT(":: set_alarm: alarm expired\r\n");
            k_sem_take(&tick_sem, K_FOREVER); // FIXME: shouldn't be necessary??
            break;
          case -EBUSY:
            SSM_DEBUG_ASSERT(-EBUSY, "set_alarm failed: already set\r\n");
          case -ENOTSUP:
            SSM_DEBUG_ASSERT(-ENOTSUP, "set_alarm failed: not supported\r\n");
          case -EINVAL:
            SSM_DEBUG_ASSERT(-EINVAL, "set_alarm failed: invalid settings\r\n");
          default:
            SSM_DEBUG_ASSERT(err, "set_alarm failed for unknown reasons\r\n");
          }
        }
        // Cancel any potential pending alarm if it hasn't gone off yet.
        timer64_cancel_alarm(ssm_timer_dev);

        // It's possible that the alarm had gone off before we cancelled it; we
        // make sure that its sem_give doesn't stick around and cause premature
        // wake-up next time around.
        //
        // Note that it's also possible for this to clobber the sem_give of an
        // input handler. So, we _must_ follow this with another input check.
        k_sem_reset(&tick_sem);
        input_packet = (ssm_input_packet_t *)mpsc_pbuf_claim(&input_pbuf);

        /* if (input_packet) { // Invariant: input time' <= input time */
        /*   ssm_time_t packet_time = TIMER64_CALC(input_packet->tick, input_packet->mtk0, input_packet->mtk1); */
        /*   SSM_DEBUG_ASSERT(_last_input_time < packet_time, ""); */
        /*   _last_input_time = packet_time; */
        /* } */
      }
    }
  }
}

K_THREAD_STACK_DEFINE(ssm_tick_thread_stack, SSM_TICK_STACKSIZE);
struct k_thread ssm_tick_thread;

void main() {
  int err;

  LOG_INF("Sleeping for a second for you to start a terminal\r\n");
  k_sleep(K_SECONDS(1));
  LOG_INF("Starting...\r\n");

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
  log_panic();
  LOG_ERR("ssm_throw: %d (%s:%d in %s)\n", reason, file, line, func);
  LOG_ERR("%016llx/%016llx\r\n", ssm_now(), timer64_read(ssm_timer_dev));
  exit(reason);
}
