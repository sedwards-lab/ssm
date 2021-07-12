#ifndef __SSM_TIME_DRIVER_H
#define __SSM_TIME_DRIVER_H

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
extern void initialize_time_driver(ssm_time_t epoch);

/**
 * Progress the system to the next event time and handles io.
 */
extern void timestep();

#endif /* ifndef __SSM_TIME_DRIVER_H */
