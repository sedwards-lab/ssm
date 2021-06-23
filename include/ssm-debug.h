#ifndef _SSM_DEBUG_H
#define _SSM_DEBUG_H

#include <ssm-platform.h>

#ifndef DEBUG_TRACE
#define DEBUG_TRACE(...)                                                       \
  do {                                                                         \
  } while (0)
#endif

#ifndef DEBUG_PRINT
#define DEBUG_PRINT(...)                                                       \
  do {                                                                         \
  } while (0)
#endif

#ifndef DEBUG_ASSERT
#define DEBUG_ASSERT(assertion, ...)                                           \
  do {                                                                         \
  } while (0)
#endif

#ifdef DEBUG

/** Attach a procedure name to an activation record struct */
#define DEBUG_ACT_NAME(act, name) (act)->act_name = (name)

/** Attach a variable name to a scheduled variable struct */
#define DEBUG_SV_NAME(sv, name) (sv)->var_name = (name)

#else

#define DEBUG_ACT_NAME(act, name)                                              \
  do {                                                                         \
  } while (0)

#define DEBUG_SV_NAME(sv, name)                                                \
  do {                                                                         \
  } while (0)

#endif /* ifdef DEBUG */

#endif /* ifndef _SSM_DEBUG_H */
