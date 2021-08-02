#include "timer64.h"

#define U1 ((uint32_t)0x1)

#define TIMER64_LOGICAL_BITS 31
#define TIMER64_TOTAL_BITS (TIMER64_LOGICAL_BITS + 1)

#define TIMER64_LO_PARITY_BIT(ctr) (!!((ctr) & (U1 << TIMER64_LOGICAL_BITS)))
#define TIMER64_HI_PARITY_BIT(mtk) ((mtk)&U1)

#define TIMER64_LO_MSB(ctr) (!!((ctr) << (TIMER64_LOGICAL_BITS - 1)))

#define TIMER64_HI_SHIFT(hi) (((uint64_t)(hi)) << TIMER64_LOGICAL_BITS)
#define TIMER64_LO_MASK(lo) ((lo) & ((U1 << TIMER64_LOGICAL_BITS) - 1))

#define TIMER64_COMBINE(hi, lo) (TIMER64_HI_SHIFT(hi) + TIMER64_LO_MASK(lo))

#define TIMER64_TOP ((U1 << (TIMER64_TOTAL_BITS - 1)) | ((U1 << (TIMER64_TOTAL_BITS - 1)) - 1))
#define TIMER64_MID (U1 << (TIMER64_LOGICAL_BITS))
#define TIMER64_GUARD (TIMER64_TOP / 2)

enum { TIMER64_MID_ALARM, TIMER64_USER_ALARM };

static volatile uint32_t macroticks;

uint64_t timer64_read(const struct device *dev) {
  uint32_t ctr;
  uint32_t mtk0;
  uint32_t mtk1;

  mtk0 = macroticks;

  compiler_barrier();

  counter_get_value(dev, &ctr);

  compiler_barrier();

  mtk1 = macroticks;

  /* printk("======== Read times: %08x/%08x, %08x (%x/%x)\n", mtk0, mtk1, ctr, */
  /*        TIMER64_LO_PARITY_BIT(ctr), TIMER64_HI_PARITY_BIT(mtk0)); */

  if (mtk0 == mtk1)
    if (TIMER64_LO_PARITY_BIT(ctr) == TIMER64_HI_PARITY_BIT(mtk0))
      return TIMER64_COMBINE(mtk0, ctr);
    else
      return TIMER64_COMBINE(mtk0 + 1, ctr);
  else if (TIMER64_LO_MSB(ctr))
    return TIMER64_COMBINE(mtk0, ctr);
  else
    return TIMER64_COMBINE(mtk1, ctr);
}

static inline void incr_macroticks(const struct device *dev) { macroticks++; }

static void mid_overflow_handler(const struct device *dev, uint8_t chan,
                                 uint32_t ticks, void *user_data) {
  uint32_t ctr;
  counter_get_value(dev, &ctr);
  printk("\r\n%%%%%%%%%%%% mid overflow: macroticks %d -> %d (ctr: %08x) "
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
  printk("\r\n%%%%%%%%%%%% Full overflow: macroticks %d -> %d (ctr: %08x) "
         "%%%%%%%%%%%%\r\n",
         macroticks, macroticks + 1, ctr);

  incr_macroticks(dev);
  schedule_mid_overflow_handler(dev);
}

int timer64_init(const struct device *dev) {
  int err;
  struct counter_top_cfg top_cfg;

  __ASSERT(dev, "dev was a null pointer\r\n");
  __ASSERT(TIMER64_TOP < counter_get_max_top_value(dev),
           "Top value too large\r\n");
  __ASSERT(2 <= counter_get_num_of_channels(dev),
           "Insufficient alarm channels\r\n");

  printk("timer will run at %d Hz\r\n", counter_get_frequency(dev));
  printk("timer will wraparound at %08x ticks\r\n", TIMER64_TOP);

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

int timer64_set_alarm(const struct device *dev, uint64_t wake_time,
                      counter_alarm_callback_t cb, void *user_data) {
  struct counter_alarm_cfg cfg;
  __ASSERT(wake_time - timer64_read(dev) > TIMER64_GUARD,
           "Trying to sleep for too long\r\n");
  cfg.flags = COUNTER_ALARM_CFG_ABSOLUTE | COUNTER_ALARM_CFG_EXPIRE_WHEN_LATE;
  cfg.ticks = wake_time & TIMER64_TOP;
  cfg.callback = cb;
  cfg.user_data = user_data;
  return counter_set_channel_alarm(dev, TIMER64_USER_ALARM, &cfg);
}

int timer64_cancel_alarm(const struct device *dev) {
  return counter_cancel_channel_alarm(dev, TIMER64_USER_ALARM);
}