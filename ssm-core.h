#ifndef _SSM_CORE_H
#define _SSM_CORE_H

/**
 * Header file shared across all SSM components.
 */

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/** Logical timestamps: assumed never to overflow */
typedef uint64_t ssm_time_t;

/** 32-bit thread priority */
typedef uint32_t priority_t;

/** Index of least significant priority bit */
typedef uint8_t depth_t;

/**
 * The logical time of the current instant.
 * Defined and maintained by ssm-sched.c. Should not be modified by anyone else.
 */
extern ssm_time_t now;

/** Member selector for aggregate data types. */
typedef uint16_t sel_t;

/** Scheduling an event for this time is the same as unscheduling it. */
#define NO_EVENT_SCHEDULED ULONG_MAX

/*** Forward struct declarations ***/

struct sv;      /* Defined in ssm-sv.h */
struct trigger; /* Defined in ssm-act.h */
struct act;     /* Defined in ssm-act.h */

#endif /* _SSM_CORE_H */
