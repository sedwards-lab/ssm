#ifndef _PENG_H
#define _PENG_H

#include "peng-base.h"
#include "peng-int.h"
#include "peng-int64.h"
#include "peng-uint8.h"
#include "peng-bool.h"

#ifdef DEBUG
extern uint64_t debug_count;
extern uint64_t limit;
#define DEBUG_PRINT(...) {    \
    if(debug_count >= limit) {    \
      exit(1);                \
    }                         \
    debug_count++;            \
    printf(__VA_ARGS__);      \
}
#else
#define DEBUG_PRINT(x) while(0) {}
#endif

#endif
