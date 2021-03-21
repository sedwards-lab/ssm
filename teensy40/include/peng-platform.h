#ifndef _PENG_PLATFORM_H
#define _PENG_PLATFORM_H

#include "imxrt.h"

#define LED_OFF(x) GPIO7_DR_CLEAR = (1<<3)
#define LED_ON(x) GPIO7_DR_SET = (1<<3)

#define MICROSECOND_TICKS(x) (x)
#define MILLISECOND_TICKS(x) ((x) *      1000L)
#define SECOND_TICKS(x)      ((x) *   1000000L)
#define MINUTE_TICKS(x)      ((x) *  60000000L)
#define HOUR_TICKS(x)        ((x) * 120000000L)

#endif
