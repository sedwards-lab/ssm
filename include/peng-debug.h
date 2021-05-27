#ifndef _PENG_DEBUG_H
#define _PENG_DEBUG_H

#ifdef DEBUG
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define AT __FILE__ ":" TOSTRING(__LINE__)

extern uint64_t debug_count;
extern uint64_t limit;

#define DEBUG_PRINT(...)                                                       \
  {                                                                            \
    if (debug_count >= limit) {                                                \
      exit(1);                                                                 \
    }                                                                          \
    debug_count++;                                                             \
    printf(__VA_ARGS__);                                                       \
  }

#define DEBUG_ASSERT(assertion, msg, ...)                                      \
  do {                                                                         \
    if (!(assertion)) {                                                        \
      printf(msg, ##__VA_ARGS__);                                           \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

#else

#define DEBUG_PRINT(...)                                                       \
  do {                                                                         \
  } while (0)

#define DEBUG_ASSERT(assertion, ...)                                           \
  do {                                                                         \
  } while (0)

#endif

#endif /* ifndef _PENG_DEBUG_H */
