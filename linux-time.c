/**
 * Implementation of a linux time driver, which uses nanosleep to progress time
 * between events.
 */

#include <time.h>
#include <sys/select.h>

#include "ssm-queue.h"
#include "ssm-sv.h"
#include "ssm-runtime.h"

/**
 * Timestamp of the last tick in the runtime.
 */
static struct timespec system_time;

/*** Timespec helpers {{{ ***/

/**
 * Calculate the delta between two timestamps.
 */
static inline void timespec_diff(struct timespec *a, struct timespec *b,
                                 struct timespec *result) {
  result->tv_sec  = a->tv_sec - b->tv_sec;
  result->tv_nsec = a->tv_nsec - b->tv_nsec;
  if (result->tv_nsec < 0) {
      result->tv_sec--;
      result->tv_nsec += 1000000000L;
  }
}

/**
 * Sum two time values.
 */
static inline void timespec_add(struct timespec *a, struct timespec *b,
                                struct timespec *result) {
  result->tv_sec  = a->tv_sec + b->tv_sec;
  result->tv_nsec = a->tv_nsec + b->tv_nsec;
  if (result->tv_nsec > 1000000000L) {
      result->tv_sec++;
      result->tv_nsec -= 1000000000L;
  }
}

/**
 * Determine whether one timestamp's value is less than another's.
 */
static inline bool timespec_lt(struct timespec *a, struct timespec *b) {
    if (a->tv_sec == b->tv_sec)
        return a->tv_nsec < b->tv_nsec;
    else
        return a->tv_sec < b->tv_sec;
}

/*** Timespec helpers }}} ***/

/*** Time driver API, exposed via time-driver.h {{{ ***/

void initialize_time_driver() {
  clock_gettime(CLOCK_MONOTONIC, &system_time);
}

ssm_time_t timestep() {
  const struct sv *event_head = peek_event_queue();
  ssm_time_t next = event_head ? event_head->later_time
                               : NO_EVENT_SCHEDULED;
  fd_set read_fds;
  FD_ZERO(&read_fds);
  int max_fd = -1;
  for (io_read_svt *io_sv = io_events; io_sv; io_sv = io_sv->next) {
    FD_SET(io_sv->fd, &read_fds);
    if (io_sv->fd > max_fd)
      max_fd = io_sv->fd;
  }

  ssm_time_t now = get_now();
  if (next != NO_EVENT_SCHEDULED) {
    time_t secs = (next - now) / 1000000;
    long ns = ((next - now) % 1000000) * 1000;

    struct timespec ssm_sleep_dur = { secs, ns };

    struct timespec expected_system_time_next;
    timespec_add(&system_time, &ssm_sleep_dur, &expected_system_time_next);

    clock_gettime(CLOCK_MONOTONIC, &system_time);

    if (timespec_lt(&system_time, &expected_system_time_next)) {
      struct timespec ssm_sleep_dur_adjusted;
      timespec_diff(&expected_system_time_next, &system_time,
                    &ssm_sleep_dur_adjusted);

      if (io_events) {
        int ret = pselect(max_fd + 1, &read_fds, NULL, NULL, &ssm_sleep_dur_adjusted, NULL);
        if (ret < 0) {
          // err, but lets ignore for now
        } else if (ret == 0) {
          // timeout, which means this was just a nonosleep
        } else {
          clock_gettime(CLOCK_MONOTONIC, &system_time);
          struct timespec blocked_time;
          timespec_diff(&expected_system_time_next, &system_time, &blocked_time);
          ssm_time_t time_now = next - (blocked_time.tv_sec * 1000000) - (blocked_time.tv_nsec / 1000);
          for (io_read_svt *io_sv = io_events; io_sv; io_sv = io_sv->next) {
            if(FD_ISSET(io_sv->fd, &read_fds)) {
              // enqueue event
              later_event(&io_sv->sv, time_now);
            }
          }

          set_now(time_now);
          return time_now;
        }
      } else {
        nanosleep(&ssm_sleep_dur_adjusted, NULL);
      }
    }
  }

  set_now(next);
  clock_gettime(CLOCK_MONOTONIC, &system_time);

  return next;
}

/*** Time driver API, exposed via time-driver.h }}} ***/
