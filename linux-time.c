#include <time.h>

#include "ssm-queue.h"
#include "ssm-sv.h"
#include "ssm-runtime.h"

static struct timespec system_time;

extern struct sv *event_queue[];
extern size_t event_queue_len;

static inline void timespec_diff(struct timespec *a, struct timespec *b,
                                 struct timespec *result) {
  result->tv_sec  = a->tv_sec  - b->tv_sec;
  result->tv_nsec = a->tv_nsec - b->tv_nsec;
  if (result->tv_nsec < 0) {
      result->tv_sec--;
      result->tv_nsec += 1000000000L;
  }
}

static inline bool timespec_lt(struct timespec *a, struct timespec *b) {
    if (a->tv_sec == b->tv_sec)
        return a->tv_nsec < b->tv_nsec;
    else
        return a->tv_sec < b->tv_sec;
}

void initialize_time_driver() {
  clock_gettime(CLOCK_MONOTONIC, &system_time);
}

ssm_time_t timestep() {
  const struct sv *event_head = peek_event_queue();
  ssm_time_t next = event_head ? event_head->later_time
                               : NO_EVENT_SCHEDULED;

  ssm_time_t now = get_now();
  if (next != NO_EVENT_SCHEDULED) {
    time_t secs = (next - now) / 1000000;
    long ns = ((next - now) % 1000000) * 1000;

    struct timespec ssm_sleep_dur = { secs, ns };

    struct timespec system_time_now;
    clock_gettime(CLOCK_MONOTONIC, &system_time_now);

    // get delta between last sleep
    struct timespec delta;
    timespec_diff(&system_time_now, &system_time, &delta);
    // if delta > dur, dont sleep
    if (timespec_lt(&delta, &ssm_sleep_dur)) {
      struct timespec ssm_sleep_dur_adjusted;
      timespec_diff(&ssm_sleep_dur, &delta, &ssm_sleep_dur_adjusted);
      nanosleep(&ssm_sleep_dur_adjusted, NULL);
    }
  }

  set_now(next);
  clock_gettime(CLOCK_MONOTONIC, &system_time);

  return now;
}
