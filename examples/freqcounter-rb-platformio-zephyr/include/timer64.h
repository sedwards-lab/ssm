#ifndef _TIMER64_H
#define _TIMER64_H

#include <drivers/counter.h>

int timer64_init(const struct device *dev);
int timer64_start(const struct device *dev);
uint64_t timer64_read(const struct device *dev);
int timer64_set_alarm(const struct device *dev, uint8_t channel,
                      uint64_t wake_time, counter_alarm_callback_t cb,
                      void *user_data);
int timer64_cancel_alarm(const struct device *dev, uint8_t channel);

extern volatile uint32_t macroticks;

#define TIMER64_LOGICAL_BITS 31
#define TIMER64_TOTAL_BITS (TIMER64_LOGICAL_BITS + 1)

#define TIMER64_LO_PARITY_BIT(ctr) (!!((ctr) & (0x1u << TIMER64_LOGICAL_BITS)))
#define TIMER64_HI_PARITY_BIT(mtk) ((mtk)&0x1u)

#define TIMER64_LO_MSB(ctr) (!!((ctr) & (0x1u << (TIMER64_LOGICAL_BITS - 1))))

#define TIMER64_HI_SHIFT(hi) (((uint64_t)(hi)) << TIMER64_LOGICAL_BITS)
#define TIMER64_LO_MASK(lo) ((lo) & ((0x1u << TIMER64_LOGICAL_BITS) - 1))

#define TIMER64_COMBINE(hi, lo) (TIMER64_HI_SHIFT(hi) + TIMER64_LO_MASK(lo))

#define TIMER64_TOP                                                            \
  ((0x1u << (TIMER64_TOTAL_BITS - 1)) |                                        \
   ((0x1u << (TIMER64_TOTAL_BITS - 1)) - 1))
#define TIMER64_MID (0x1u << TIMER64_LOGICAL_BITS)
#define TIMER64_GUARD (TIMER64_TOP / 2)

#define TIMER64_READ(dev, ctr, mtk0, mtk1)                                     \
  do {                                                                         \
    *(mtk0) = macroticks;                                                      \
    compiler_barrier();                                                        \
    counter_get_value(dev, ctr);                                               \
    compiler_barrier();                                                        \
    *(mtk1) = macroticks;                                                      \
  } while (0)

#define TIMER64_CALC(ctr, mtk0, mtk1)                                          \
  (mtk0 == mtk1 ? TIMER64_LO_PARITY_BIT(ctr) == TIMER64_HI_PARITY_BIT(mtk0)    \
                      ? TIMER64_COMBINE(mtk0, ctr)                             \
                      : TIMER64_COMBINE(mtk0 + 1, ctr)                         \
   : TIMER64_LO_MSB(ctr) ? TIMER64_COMBINE(mtk0, ctr)                          \
                         : TIMER64_COMBINE(mtk1, ctr))
#endif /* _TIMER64_H */
