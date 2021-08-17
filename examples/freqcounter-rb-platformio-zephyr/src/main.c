/**
 */
#include <device.h>
#include <devicetree.h>
#include <sys/atomic.h>
#include <zephyr.h>

#include <drivers/clock_control.h>
#include <drivers/clock_control/nrf_clock_control.h>

#include "ssm-platform.h"
#include "timer64.h"

LOG_MODULE_REGISTER(main);

#if !DT_NODE_HAS_STATUS(DT_ALIAS(ssm_timer), okay)
#error "ssm-timer device is not supported on this board"
#endif

#define CLOCK_NODE DT_INST(0, nordic_nrf_clock)

#define SSM_TICK_STACKSIZE 4096
#define SSM_TICK_PRIORITY 7
#define SSM_INPUT_PBUF_SIZE 4096

const struct device *ssm_timer_dev = 0;

K_SEM_DEFINE(tick_sem, 0, 1);
ssm_input_packet_t input_buffer[INPUT_BUFFER_SIZE];

/** Send a timeout event to wake up the ssm_tick_thread. */
static void send_timeout_event(const struct device *dev, uint8_t chan,
                               uint32_t ticks, void *user_data) {
  k_sem_give(&tick_sem);
}

uint32_t packet_get_len(union mpsc_pbuf_generic *packet) {
  return sizeof(ssm_input_packet_t);
}

uint32_t input_count = 0;
uint32_t dropped = 0;
#define LOG_DROP_NOTIF_PERIOD (SSM_SECOND * 2)

static void log_dropped(const struct device *dev, uint8_t chan, uint32_t ticks,
                        void *user_data) {
  if (dropped) {
    LOG_WRN("Dropped %u packets\r\n", dropped);
    dropped = 0;
  }
  LOG_INF("Raw input count: %u (%lu/second)\r\n", input_count,
          input_count / (LOG_DROP_NOTIF_PERIOD / SSM_SECOND));
  input_count = 0;
  if (timer64_set_alarm(ssm_timer_dev, 1,
                        timer64_read(ssm_timer_dev) + LOG_DROP_NOTIF_PERIOD,
                        log_dropped, NULL))
    LOG_ERR("could not set drop log alarm");
}

/* atomic_t rb_wclaim = ATOMIC_INIT(0); */
atomic_t rb_wcommit = ATOMIC_INIT(0);
atomic_t rb_rclaim = ATOMIC_INIT(0);
/* atomic_t rb_rcommit = ATOMIC_INIT(0); */

/**
 * Thread responsible for processing events (messages), calling ssm_tick(), and
 * starting the timeout counter.
 */
void ssm_tick_thread_body(void *p1, void *p2, void *p3) {
  timer64_start(ssm_timer_dev);

  if (timer64_set_alarm(ssm_timer_dev, 1,
                        timer64_read(ssm_timer_dev) + LOG_DROP_NOTIF_PERIOD,
                        log_dropped, NULL))
    LOG_ERR("could not set drop log alarm");

  ssm_program_initialize();

  // TODO: we are assuming no input events at time 0.
  ssm_tick();

  ssm_input_packet_t *input_packet = NULL;

  for (;;) {
    uint32_t wcommit, rclaim;

    ssm_time_t next_time = ssm_next_event_time();

    if (input_packet) {
      ssm_time_t packet_time = TIMER64_CALC(
          input_packet->tick, input_packet->mtk0, input_packet->mtk1);

      if (packet_time <= next_time) {
        // TODO: handle values too
        ssm_schedule(lookup_input_device(&input_packet->input), packet_time);
        atomic_inc(&rb_rclaim);
        wcommit = atomic_get(&rb_wcommit);
        rclaim = atomic_get(&rb_rclaim);
        input_packet = IBI_MOD(wcommit) == IBI_MOD(rclaim)
                           ? NULL
                           : &input_buffer[IBI_MOD(rclaim)];
      }

      /* k_sleep(K_MSEC(5)); */
      ssm_tick();

    } else {

      if (next_time <= timer64_read(ssm_timer_dev)) {
        // It's possible that we received input since last checking input.
        // Double check one last time
        wcommit = atomic_get(&rb_wcommit);
        rclaim = atomic_get(&rb_rclaim);
        input_packet = IBI_MOD(wcommit) == IBI_MOD(rclaim)
                           ? NULL
                           : &input_buffer[IBI_MOD(rclaim)];
        if (input_packet)
          // We can't tick here. TODO: don't put this here, elide this with
          // another branch, even though this is very unlikely.
          continue;

        ssm_tick();

        wcommit = atomic_get(&rb_wcommit);
        rclaim = atomic_get(&rb_rclaim);
        input_packet = IBI_MOD(wcommit) == IBI_MOD(rclaim)
                           ? NULL
                           : &input_buffer[IBI_MOD(rclaim)];
      } else {
        if (next_time != SSM_NEVER) {
          SSM_DEBUG_PRINT(":: setting alarm for [next_time: %016llx]\r\n",
                          next_time);

          int err = timer64_set_alarm(ssm_timer_dev, 0, next_time,
                                      send_timeout_event, NULL);

          switch (err) {
          case 0:
            SSM_DEBUG_PRINT(":: alarm set successfully\r\n");
            break;
          case -ETIME:
            SSM_DEBUG_PRINT(":: set_alarm: alarm expired\r\n");
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
        k_sem_take(&tick_sem, K_FOREVER);
        // Cancel any potential pending alarm if it hasn't gone off yet.
        timer64_cancel_alarm(ssm_timer_dev, 0);

        // It's possible that the alarm had gone off before we cancelled it; we
        // make sure that its sem_give doesn't stick around and cause premature
        // wake-up next time around.
        //
        // Note that it's also possible for this to clobber the sem_give of an
        // input handler. So, we _must_ follow this with another input check.
        k_sem_reset(&tick_sem);
        wcommit = atomic_get(&rb_wcommit);
        rclaim = atomic_get(&rb_rclaim);
        input_packet = IBI_MOD(wcommit) == IBI_MOD(rclaim)
                           ? NULL
                           : &input_buffer[IBI_MOD(rclaim)];
      }
    }
  }
}

K_THREAD_STACK_DEFINE(ssm_tick_thread_stack, SSM_TICK_STACKSIZE);
struct k_thread ssm_tick_thread;

static void show_clocks(void)
{
	static const char *const lfsrc_s[] = {
#if defined(CLOCK_LFCLKSRC_SRC_LFULP)
		[NRF_CLOCK_LFCLK_LFULP] = "LFULP",
#endif
		[NRF_CLOCK_LFCLK_RC] = "LFRC",
		[NRF_CLOCK_LFCLK_Xtal] = "LFXO",
		[NRF_CLOCK_LFCLK_Synth] = "LFSYNT",
	};
	static const char *const hfsrc_s[] = {
		[NRF_CLOCK_HFCLK_LOW_ACCURACY] = "HFINT",
		[NRF_CLOCK_HFCLK_HIGH_ACCURACY] = "HFXO",
	};
	static const char *const clkstat_s[] = {
		[CLOCK_CONTROL_STATUS_STARTING] = "STARTING",
		[CLOCK_CONTROL_STATUS_OFF] = "OFF",
		[CLOCK_CONTROL_STATUS_ON] = "ON",
		[CLOCK_CONTROL_STATUS_UNKNOWN] = "UNKNOWN",
	};
	union {
		unsigned int raw;
		nrf_clock_lfclk_t lf;
		nrf_clock_hfclk_t hf;
	} src;
	enum clock_control_status clkstat;
	bool running;

	clkstat = clock_control_get_status(ssm_timer_dev, CLOCK_CONTROL_NRF_SUBSYS_LF);
	running = nrf_clock_is_running(NRF_CLOCK, NRF_CLOCK_DOMAIN_LFCLK,
				       &src.lf);
	printk("LFCLK[%s]: %s %s ; ", clkstat_s[clkstat],
	       running ? "Running" : "Off", lfsrc_s[src.lf]);
	clkstat = clock_control_get_status(ssm_timer_dev, CLOCK_CONTROL_NRF_SUBSYS_HF);
	running = nrf_clock_is_running(NRF_CLOCK, NRF_CLOCK_DOMAIN_HFCLK,
				       &src.hf);
	printk("HFCLK[%s]: %s %s\n", clkstat_s[clkstat],
	       running ? "Running" : "Off", hfsrc_s[src.hf]);
}

void main() {
  int err;
  const struct device *clock;

  LOG_INF("Sleeping for a second for you to start a terminal\r\n");
  k_sleep(K_SECONDS(1));

  clock = device_get_binding(DT_LABEL(CLOCK_NODE));
  SSM_DEBUG_ASSERT(clock, "device_get_binding failed with clock\r\n");

  err = clock_control_on(clock, CLOCK_CONTROL_NRF_SUBSYS_HF);
  SSM_DEBUG_ASSERT(!err, "clock_control_on: %d\r\n", err);

  show_clocks();

  LOG_INF("Starting...\r\n");

  ssm_timer_dev = device_get_binding(DT_LABEL(DT_ALIAS(ssm_timer)));
  SSM_DEBUG_ASSERT(ssm_timer_dev,
                   "device_get_binding failed with ssm-timer\r\n");

  err = timer64_init(ssm_timer_dev);
  SSM_DEBUG_ASSERT(!err, "timer64_init: %d\r\n", err);


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
