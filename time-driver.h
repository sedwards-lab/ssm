#ifndef __TIME_DRIVER_H
#define __TIME_DRIVER_H

#include "ssm-queue.h"
#include "ssm-runtime.h"

/**
 * This header file should be implemented by an implementation of a time
 * driver, which is responsible for progressing time in the ssm runtime.
 *
 * The driver is meant to be initialized before use and then called once per
 * tick invocation.
 */

/**
 * Initialize the time driver so it can progress time whenever tick is called.
 *
 * This should only be called once, after initializing the ssm runtime.
 */
extern void initialize_time_driver();

/**
 * Progress the system to the next event time and return that time value.
 */
extern ssm_time_t timestep();

#endif /* ifndef __TIME_DRIVER_H */
