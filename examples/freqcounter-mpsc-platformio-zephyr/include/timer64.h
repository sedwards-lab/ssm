#ifndef _TIMER64_H
#define _TIMER64_H

#include <drivers/counter.h>

int timer64_init(const struct device *dev);
int timer64_start(const struct device *dev);
uint64_t timer64_read(const struct device *dev);
int timer64_set_alarm(const struct device *dev, uint64_t wake_time,
                      counter_alarm_callback_t cb, void *user_data);
int timer64_cancel_alarm(const struct device *dev);

#endif /* _TIMER64_H */
