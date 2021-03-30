#ifndef _PENG_SCALAR_H
#define _PENG_SCALAR_H

#include "peng-base.h"

#define PENG_SCALAR_HEADERS(type) \
typedef struct { \
  SCHEDULED_VARIABLE_FIELDS; \
  type value; \
  type event_value; \
} sv_##type##_t; \
\
extern void to_string_##type(sv_t *, char *, size_t); \
extern void initialize_##type(sv_##type##_t *); \
extern void assign_##type(sv_##type##_t *,  priority_t, type); \
extern void later_##type(sv_##type##_t *, peng_time_t, type);

#endif /* _PENG_SCALAR_H */
