#ifndef _PENG_SCALAR_CODE_H
#define _PENG_SCALAR_CODE_H

#include "peng-scalar.h"

#define PENG_SCALAR_CODE(type) \
void update_##type(sv_t *var) \
{ \
  assert(var); \
  assert(var->event_time == now); \
  sv_##type##_t *iv = (sv_##type##_t *) var; \
  iv->value = iv->event_value; \
} \
\
void initialize_##type(sv_##type##_t *v) \
{ \
  assert(v); \
  *v = (sv_##type##_t) { .update = update_##type, \
    		         .triggers = NULL, \
			 .last_updated = now, \
			 .event_time = NO_EVENT_SCHEDULED }; \
} \
\
void assign_##type(sv_##type##_t *iv, priority_t priority, type value) \
{ \
  iv->value = value; \
  iv->last_updated = now; \
  schedule_sensitive((sv_t *) iv, priority); \
} \
\
void later_##type(sv_##type##_t *var, peng_time_t time, type val) \
{ \
  assert(var); \
  var->event_value = val; \
  later_event((sv_t *) var, time); \
}

#endif
