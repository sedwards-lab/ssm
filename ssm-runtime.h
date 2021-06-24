#ifndef _SSM_RUNTIME_H
#define _SSM_RUNTIME_H

/**
 * This header file is implemented by ssm-sched.c, and is meant to be used by
 * the "driver" of the runtime---that which connects the internal scheduler to
 * the outside world.
 *
 * The primary interface is the tick function, which the driver is responsible
 * for calling at the appropriate time. For instance, for a real-time
 * implementation, the driver is responsible for calling tick at the right
 * wall-clock time, according to the next event time returned by the previous
 * call to tick.
 */

/**
 * Used by the runtime to control execution of a program.
 */
#include "ssm-core.h"
#include "ssm-io.h"

/**
 * Initialize the runtime system to start at some time, which is when tick
 * should next be called.
 *
 * This should only be called once.
 */
extern void initialize_ssm(ssm_time_t);

/**
 * Flag to indicate that the ssm system is still alive. Set to false once
 * all routines complete.
 */
extern bool ssm_runtime_alive;

/**
 * Execute the system for the current instant, and returns the next event time.
 */
extern ssm_time_t tick(void);

/**
 * Retrieve the head of the queue
 */
extern const struct sv *peek_event_queue();

/**
 * Getter and setter for ssm's time variable
 */
extern ssm_time_t get_now();
extern void set_now(ssm_time_t);

/**
 * File descriptor table, used for blocking between scheduled events.
 */
extern struct io_read_svt io_vars[MAX_IO_VARS];

#endif /* ifndef _SSM_RUNTIME_H */
