#ifndef _PENG_SCALAR_CODE_H
#define _PENG_SCALAR_CODE_H

#include <stdio.h>
#include "formatters.h"
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
void to_string_##type(sv_t *v, char *buffer, size_t size) { \
    sv_##type##_t* iv = (sv_##type##_t *) v; \
    char str[] = str_##type " " format_##type; \
    snprintf(buffer, size, str, iv->value); \
}\
\
void initialize_##type(sv_##type##_t *v) \
{ \
  assert(v); \
  *v = (sv_##type##_t) { .to_string = to_string_##type,\
                         .update = update_##type, \
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
void later_##type(sv_##type##_t *var, peng_time_t then, type val) \
{ \
  assert(var); \
  DEBUG_ASSERT(now < then, "bad after\n"); \
  DEBUG_ASSERT(can_schedule((sv_t *) var), "eventqueue full\n"); \
  var->event_value = val; \
  later_event((sv_t *) var, then); \
}

#endif
