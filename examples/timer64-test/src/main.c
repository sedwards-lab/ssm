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
#include <stdlib.h>

#include "timer64.h"

#if !DT_NODE_HAS_STATUS(DT_ALIAS(timer), okay)
#error "timer device is not supported on this board"
#endif

#define STACKSIZE 4096
#define PRIORITY 7

const struct device *timer_dev = 0;

void thread_body(void *p1, void *p2, void *p3) {
  timer64_start(timer_dev);

  uint64_t last = 0;
  printk("about to start\r\n");

  for (;;) {
    uint64_t read = timer64_read(timer_dev);
    if (read <= last) {
      printk("NON-MONOTONICITY:\r\n  last: %llx\r\n  read: %llx\r\n", last, read);
      exit(2);
    }
    /* printk(".. time: %llx\r\n", read); */
    /* k_sleep(K_SECONDS(1)); */
    last = read;
  }
}

K_THREAD_STACK_DEFINE(thread_stack, STACKSIZE);
struct k_thread thread;

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

  k_thread_create(&thread, thread_stack,
                  K_THREAD_STACK_SIZEOF(thread_stack),
                  thread_body, 0, 0, 0, PRIORITY, 0,
                  K_NO_WAIT);
}
