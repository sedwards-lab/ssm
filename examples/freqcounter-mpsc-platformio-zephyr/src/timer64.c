#include "timer64.h"
#include <stdlib.h>

#include <logging/log.h>

LOG_MODULE_REGISTER(timer64);

enum { TIMER64_MID_ALARM, TIMER64_USER_ALARM, TIMER64_USER_PERIOD_ALARM };

volatile uint32_t macroticks;

uint64_t timer64_read(const struct device *dev) {

  uint32_t ctr, mtk0, mtk1;

  TIMER64_READ(dev, &ctr, &mtk0, &mtk1);
  return TIMER64_CALC(ctr, mtk0, mtk1);
}

static inline void incr_macroticks(const struct device *dev) { macroticks++; }

static void mid_overflow_handler(const struct device *dev, uint8_t chan,
                                 uint32_t ticks, void *user_data) {
  uint32_t ctr;
  counter_get_value(dev, &ctr);
  LOG_INF("\r\n%%%%%%%%%%%% mid overflow: macroticks %d -> %d (ctr: %08x) "
          "%%%%%%%%%%%%\r\n",
          macroticks, macroticks + 1, ctr);
  incr_macroticks(dev);
}

static inline int schedule_mid_overflow_handler(const struct device *dev) {
  struct counter_alarm_cfg cfg;
  cfg.flags = COUNTER_ALARM_CFG_ABSOLUTE | COUNTER_ALARM_CFG_EXPIRE_WHEN_LATE;
  cfg.ticks = TIMER64_MID;
  cfg.callback = mid_overflow_handler;
  cfg.user_data = NULL;
  return counter_set_channel_alarm(dev, TIMER64_MID_ALARM, &cfg);
}

static void overflow_handler(const struct device *dev, void *user_data) {

  uint32_t ctr;
  counter_get_value(dev, &ctr);
  LOG_INF("\r\n%%%%%%%%%%%% Full overflow: macroticks %d -> %d (ctr: %08x) "
          "%%%%%%%%%%%%\r\n",
          macroticks, macroticks + 1, ctr);

  incr_macroticks(dev);
  schedule_mid_overflow_handler(dev);
}

int timer64_init(const struct device *dev) {
  int err;
  struct counter_top_cfg top_cfg;

  if (!dev) {
    LOG_ERR("dev was a null pointer\r\n");
    exit(1);
  }
  if (!(TIMER64_TOP <= counter_get_max_top_value(dev))) {
    LOG_ERR("Top value too large top %x > max top %x\r\n", TIMER64_TOP,
            counter_get_max_top_value(dev));
    exit(1);
  }
  if (!(2 <= counter_get_num_of_channels(dev))) {
    LOG_ERR("Insufficient alarm channels\r\n");
    exit(1);
  }

  LOG_INF("timer will run at %d Hz\r\n", counter_get_frequency(dev));
  LOG_INF("timer will wraparound at %08x ticks\r\n", TIMER64_TOP);

  macroticks = 0;

  // Everytime the timer wraps around, we bump the macrotick.
  top_cfg.callback = overflow_handler;
  top_cfg.ticks = TIMER64_TOP;

  // No need for user data for now.
  top_cfg.user_data = NULL;
  // This only matters if we change the top value.
  top_cfg.flags = COUNTER_TOP_CFG_DONT_RESET;

  if ((err = counter_set_top_value(dev, &top_cfg)))
    return err;

  if ((err = counter_set_guard_period(dev, TIMER64_GUARD,
                                      COUNTER_GUARD_PERIOD_LATE_TO_SET)))
    return err;

  if ((err = schedule_mid_overflow_handler(dev)))
    return err;

  return 0;
}

int timer64_start(const struct device *dev) { return counter_start(dev); }

int timer64_set_alarm(const struct device *dev, uint8_t channel, uint64_t wake_time,
                      counter_alarm_callback_t cb, void *user_data) {
  struct counter_alarm_cfg cfg;
  __ASSERT(wake_time - timer64_read(dev) > TIMER64_GUARD,
           "Trying to sleep for too long\r\n");
  cfg.flags = COUNTER_ALARM_CFG_ABSOLUTE | COUNTER_ALARM_CFG_EXPIRE_WHEN_LATE;
  cfg.ticks = wake_time & TIMER64_TOP;
  cfg.callback = cb;
  cfg.user_data = user_data;
  return counter_set_channel_alarm(dev, TIMER64_USER_ALARM + channel, &cfg);
}

int timer64_cancel_alarm(const struct device *dev, uint8_t channel) {
  return counter_cancel_channel_alarm(dev, TIMER64_USER_ALARM + channel);
}
