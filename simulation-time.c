/**
 * Implementation of a simulation time driver, which doesn't actually suspend
 * program executation between events, but rather jumps the runtime to the next
 * event time.
 */

#include "ssm-queue.h"
#include "ssm-sv.h"
#include "ssm-runtime.h"
#include "ssm-time-driver.h"

/*** Time driver API, exposed via time-driver.h {{{ ***/

void initialize_time_driver(ssm_time_t epoch) {
  // No-op
}

void timestep() {
  // No-op
}

/*** Time driver API, exposed via time-driver.h }}} ***/
