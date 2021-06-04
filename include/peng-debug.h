/**
 * Default debug interface
 *
 * Each platform may define its own debugging facilities, but this file provides
 * a default nop implementation (and documents the interface).
 */
#ifndef _PENG_DEBUG_H
#define _PENG_DEBUG_H

#include <peng-platform.h>

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

#endif /* ifndef _PENG_DEBUG_H */
