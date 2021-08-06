/**
 * Main loop for a Zephyr example, adapted from flipfloploop.
 *
 * I/O (specifically, GPIO) and 64-bit timing are separated out of this
 * compilation unit, which focuses on handling events arriving in the event
 * queue.
 */
#include <device.h>
#include <devicetree.h>
#include <drivers/gpio.h>
#include <stdlib.h>
#include <zephyr.h>

#include <logging/log.h>
#include <logging/log_ctrl.h>

#include "timer64.h"

LOG_MODULE_REGISTER(main);

#if !DT_NODE_HAS_STATUS(DT_ALIAS(timer), okay)
#error "timer device is not supported on this board"
#endif

#if !DT_NODE_HAS_STATUS(DT_ALIAS(sw1), okay)
#error "sw0 device alias not defined"
#endif

#define STACKSIZE 4096
#define PRIORITY 7
#define TIMER_SECOND 16000000L
#define TIMER_MILLISECOND (16000000L / 1000)

const struct device *timer_dev = 0;

int sema = 0;

static inline void intr_enter(uint64_t time, int val) {
  LOG_INF("[%llx] intr: entered", time);
  int s = sema;
  if (s) {
    log_panic();
    LOG_ERR("[%llx] intr: encountered reentrancy %d from %d", time, s, val);
    exit(1);
  }
  else {
    sema = val;
  }
  compiler_barrier();
}

static inline void intr_leave(uint64_t time) {
  compiler_barrier();
  sema = 0;
}

int alarm_epoch_limit = 5;

static void alarm_handler(const struct device *dev, uint8_t chan,
                          uint32_t ticks, void *user_data) {
  uint64_t now = timer64_read(timer_dev);
  LOG_INF("[%llx] alarm: running (%d)", now, alarm_epoch_limit);
  if (alarm_epoch_limit-- <= 0) {
    LOG_INF("[%llx] alarm: done with epoch, quitting", now);
    return;
  }

  intr_enter(now, 1);

  // set alarm 1ms in future
  timer64_set_alarm(timer_dev, now + TIMER_MILLISECOND, alarm_handler, NULL);

  // busy wait for 10ms
  k_busy_wait(10000);

  LOG_INF("[%llx] alarm: leaving after busy wait (at %llx)", now,
          timer64_read(timer_dev));

  intr_leave(now);
}

void alarm_thread_body(void *p1, void *p2, void *p3) {
  timer64_start(timer_dev);

  LOG_INF("about to start");

  for (;;) {
    alarm_epoch_limit = 20;
    LOG_INF("-----------------------------------------");
    uint64_t now = timer64_read(timer_dev);
    LOG_INF("[%llx] thread: woke up", now);
    timer64_set_alarm(timer_dev, now + TIMER_MILLISECOND * 20, alarm_handler,
                      NULL);
    LOG_INF("[%llx] thread: set alarm for %llx", now,
            now + TIMER_MILLISECOND * 20);
    k_sleep(K_SECONDS(1));
  }
}

struct gpio_callback cb;
const char *label = DT_GPIO_LABEL(DT_ALIAS(sw1), gpios);
gpio_pin_t pin = DT_GPIO_PIN(DT_ALIAS(sw1), gpios);
gpio_flags_t flags = DT_GPIO_FLAGS(DT_ALIAS(sw1), gpios);

static void gpio_handler(const struct device *port,
                                struct gpio_callback *cb,
                                gpio_port_pins_t pins) {

  uint64_t now = timer64_read(timer_dev);
  intr_enter(now, 2);
  LOG_INF("[%llx] gpio: in handler", now);
  k_busy_wait(2000);
  LOG_INF("[%llx] gpio: leaving handler", now);
  intr_leave(now);
}

void gpio_thread_body(void *p1, void *p2, void *p3) {
  const struct device *port = device_get_binding(label);
  gpio_pin_configure(port, pin, GPIO_INPUT | flags);
  gpio_pin_interrupt_configure(port, pin, GPIO_INT_EDGE_TO_ACTIVE);

  gpio_init_callback(&cb, gpio_handler, BIT(pin));
  gpio_add_callback(port, &cb);

  timer64_start(timer_dev);
  for (;;) {
    uint64_t now = timer64_read(timer_dev);
    LOG_INF("[%llx] thread: just hanging out", now);
    k_sleep(K_SECONDS(1));
  }
}

K_THREAD_STACK_DEFINE(alarm_thread_stack, STACKSIZE);
struct k_thread alarm_thread;

K_THREAD_STACK_DEFINE(gpio_thread_stack, STACKSIZE);
struct k_thread gpio_thread;

void main() {
  int err;

  printk("Sleeping for a second for you to start a terminal\r\n");
  k_sleep(K_SECONDS(1));
  printk("Starting...\r\n");

  timer_dev = device_get_binding(DT_LABEL(DT_ALIAS(timer)));
  if (!timer_dev)
    printk("device_get_binding failed with timer\r\n"), exit(1);

  err = timer64_init(timer_dev);
  if (err)
    printk("timer64_init: %d\r\n", err), exit(1);

  k_sleep(K_SECONDS(1));

  k_thread_create(&gpio_thread, gpio_thread_stack, K_THREAD_STACK_SIZEOF(gpio_thread_stack),
                  gpio_thread_body, 0, 0, 0, PRIORITY, 0, K_NO_WAIT);
  k_thread_create(&alarm_thread, alarm_thread_stack, K_THREAD_STACK_SIZEOF(alarm_thread_stack),
                  alarm_thread_body, 0, 0, 0, PRIORITY, 0, K_NO_WAIT);
}
