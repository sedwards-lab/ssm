/**
 * Implementation of a linux time driver, which uses nanosleep/pselect to
 * progress time between events.
 */

#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <sys/select.h>

#include "ssm-queue.h"
#include "ssm-sv.h"
#include "ssm-runtime.h"
#include "ssm-time-driver.h"

/**
 * Conversion between ssm time and system time.
 */
static struct timespec ssm_offset;
static bool offset_is_positive;

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

/**
 * Convert from ssm time to system time.
 */
static inline void ssm_to_sys_time(ssm_time_t ssm_time, struct timespec *ts) {
  struct timespec ssm_ts;
  ssm_ts.tv_sec = ssm_time / 1000000;
  ssm_ts.tv_nsec = (ssm_time % 1000000) * 1000;

  if (offset_is_positive)
    timespec_add(&ssm_ts, &ssm_offset, ts);
  else
    timespec_diff(&ssm_ts, &ssm_offset, ts);
}

/**
 * Convert from system time to ssm time.
 */
static inline ssm_time_t sys_to_ssm_time(struct timespec *ts) {
  struct timespec ssm_ts;

  if (offset_is_positive)
    timespec_diff(ts, &ssm_offset, &ssm_ts);
  else
    timespec_add(ts, &ssm_offset, &ssm_ts);

  return ssm_ts.tv_sec * 1000000 + ssm_ts.tv_nsec / 1000;
}

/*** Timespec helpers }}} ***/

/*** Time driver API, exposed via time-driver.h {{{ ***/

void initialize_time_driver(ssm_time_t epoch) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  struct timespec epoch_ts = { .tv_sec = epoch / 1000000,
                               .tv_nsec = (epoch % 1000000) * 1000 };

  offset_is_positive = timespec_lt(&epoch_ts, &now);
  if (offset_is_positive)
    timespec_diff(&now, &epoch_ts, &ssm_offset);
  else
    timespec_diff(&epoch_ts, &now, &ssm_offset);

}

void timestep() {
  const struct sv *event_head = peek_event_queue();
  ssm_time_t next = event_head ? event_head->later_time
                               : NO_EVENT_SCHEDULED;
  struct timespec current_time;
  if (next != NO_EVENT_SCHEDULED) {
    // If there is an event scheduled, account for our drift from ssm time (i.e.
    // time spent since last tick).
    struct timespec next_ts;
    ssm_to_sys_time(next, &next_ts);

    clock_gettime(CLOCK_MONOTONIC, &current_time);
    bool running_behind = !timespec_lt(&current_time, &next_ts);

    struct timespec ssm_sleep_dur_adjusted;
    timespec_diff(&next_ts, &current_time, &ssm_sleep_dur_adjusted);

    if (ssm_max_fd == -1) {
      // No files to select on.
      if (!running_behind) {
        // If we're not running behind schedule, let's sleep to sync with ssm
        // time.
        nanosleep(&ssm_sleep_dur_adjusted, NULL);
      }
    } else {
      // Select on any open files.
      // Immediately wake up if we're running behind schedule (this will ensure
      // that we don't skip real-time io events.

      // Snapshot since fd_set gets modfied in pselect().
      fd_set ssm_read_fds_copy = ssm_read_fds;
      struct timespec instant_timeout = {0 , 0};
      struct timespec *timeout = (running_behind ? &instant_timeout
                                                 : &ssm_sleep_dur_adjusted);
      int ret = pselect(ssm_max_fd + 1, &ssm_read_fds, NULL, NULL, timeout, NULL);
      if (ret < 0) {
        perror("pselect");
        exit(1);
      } else if (ret) {
        // Calculate current time and enqueue events for ready fds.
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        ssm_time_t ssm_io_event_time = sys_to_ssm_time(&current_time);
        for (int i = 0; i <= ssm_max_fd; i++) {
          struct io_read_svt *io_sv = io_vars + i;
          if (FD_ISSET(io_sv->fd, &ssm_read_fds)) {
            read(io_sv->fd, &io_sv->u8_sv.later_value, 1);
            later_event(&io_sv->u8_sv.sv, ssm_io_event_time);
          }
        }
      }
      // Restore our fd_set.
      ssm_read_fds = ssm_read_fds_copy;
    }
  } else {
    // No events scheduled and the system still hasn't been marked as completed.
    // Check if any files open to select on.
    if (!ssm_is_complete() && ssm_max_fd != -1) {
      // Snapshot since fd_set gets modfied in pselect().
      fd_set ssm_read_fds_copy = ssm_read_fds;
      int ret = pselect(ssm_max_fd + 1, &ssm_read_fds, NULL, NULL, NULL, NULL);
      if (ret < 0) {
          perror("pselect");
          exit(1);
      }

      // Calculate current time and enqueue events for ready fds.
      clock_gettime(CLOCK_MONOTONIC, &current_time);
      ssm_time_t ssm_io_event_time = sys_to_ssm_time(&current_time);
      for (int i = 0; i <= ssm_max_fd; i++) {
        struct io_read_svt *io_sv = io_vars + i;
        if (FD_ISSET(io_sv->fd, &ssm_read_fds)) {
          read(io_sv->fd, &io_sv->u8_sv.later_value, 1);
          later_event(&io_sv->u8_sv.sv, ssm_io_event_time);
        }
      }
      // Restore our fd_set.
      ssm_read_fds = ssm_read_fds_copy;
    }
  }

}

/*** Time driver API, exposed via time-driver.h }}} ***/
