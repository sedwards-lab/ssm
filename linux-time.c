/**
 * Implementation of a linux time driver, which uses nanosleep to progress time
 * between events.
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

  ssm_time_t now = get_now();
  if (next != NO_EVENT_SCHEDULED) {
    // The runtime system should not have been marked as completed if there are
    // still events in the queue.
    assert(!ssm_is_complete());

    // If there is an event scheduled, calculate our drift from ssm time (i.e.
    // time spent since last tick) and subtract it from the time difference
    // between the next event and now.
    time_t secs = (next - now) / 1000000;
    long ns = ((next - now) % 1000000) * 1000;

    struct timespec ssm_sleep_dur = { secs, ns };

    struct timespec expected_system_time_next;
    timespec_add(&system_time, &ssm_sleep_dur, &expected_system_time_next);

    clock_gettime(CLOCK_MONOTONIC, &system_time);

    bool running_behind = !timespec_lt(&system_time, &expected_system_time_next);
    struct timespec instant_timeout = {0 , 0};

    struct timespec ssm_sleep_dur_adjusted;
    timespec_diff(&expected_system_time_next, &system_time,
                  &ssm_sleep_dur_adjusted);

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
      struct timespec *timeout = (running_behind ? &instant_timeout
                                                 : &ssm_sleep_dur_adjusted);
      int ret = pselect(ssm_max_fd + 1, &ssm_read_fds, NULL, NULL, timeout, NULL);
      if (ret < 0) {
        perror("pselect");
        exit(1);
      } else if (ret) {
        // Calculate current time and enqueue events for ready fds.
        clock_gettime(CLOCK_MONOTONIC, &system_time);
        struct timespec remaining_sleep_time;
        timespec_diff(&expected_system_time_next, &system_time, &remaining_sleep_time);
        next -= ((remaining_sleep_time.tv_sec * 1000000)
                 + (remaining_sleep_time.tv_nsec / 1000));
        for (int i = 0; i <= ssm_max_fd; i++) { // iterate to max_fd?
          struct io_read_svt *io_sv = io_vars + i;
          if (FD_ISSET(io_sv->fd, &ssm_read_fds)) {
            read(io_sv->fd, &io_sv->u8_sv.later_value, 1);
            later_event(&io_sv->u8_sv.sv, next);
          }
        }
        // Restore our fd_set.
        ssm_read_fds = ssm_read_fds_copy;
      }
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
      struct timespec old_system_time = system_time;
      clock_gettime(CLOCK_MONOTONIC, &system_time);
      struct timespec delta_last_tick;
      timespec_diff(&system_time, &old_system_time, &delta_last_tick);
      next = (now + (delta_last_tick.tv_sec * 1000000)
              + (delta_last_tick.tv_nsec / 1000));
      for (int i = 0; i <= ssm_max_fd; i++) {
        struct io_read_svt *io_sv = io_vars + i;
        if (FD_ISSET(io_sv->fd, &ssm_read_fds)) {
          read(io_sv->fd, &io_sv->u8_sv.later_value, 1);
          later_event(&io_sv->u8_sv.sv, next);
        }
      }
      // Restore our fd_set.
      ssm_read_fds = ssm_read_fds_copy;
    }
  }

  set_now(next); // Only tick() should update now
  clock_gettime(CLOCK_MONOTONIC, &system_time);

  return next;
}

/*** Time driver API, exposed via time-driver.h }}} ***/
