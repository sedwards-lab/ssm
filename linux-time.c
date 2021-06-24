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
  fd_set read_fds;
  FD_ZERO(&read_fds);
  int max_fd = -1;
  for (int i = 0; i < MAX_IO_VARS; i++) {
    struct io_read_svt *io_sv = io_vars + i;
    if (!io_sv->is_open) continue;
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

    bool running_behind = !timespec_lt(&system_time, &expected_system_time_next);
    struct timespec instant_timeout = {0 , 0};

    struct timespec ssm_sleep_dur_adjusted;
    timespec_diff(&expected_system_time_next, &system_time,
                  &ssm_sleep_dur_adjusted);

    if (max_fd == -1) {
      // No fds to block on
      if (!running_behind)
        nanosleep(&ssm_sleep_dur_adjusted, NULL);
    } else {
      // Block on fds
      struct timespec *timeout = (running_behind ? &instant_timeout
                                                 : &ssm_sleep_dur_adjusted);
      int ret = pselect(max_fd + 1, &read_fds, NULL, NULL, timeout, NULL);
      if (ret < 0) {
        perror("pselect");
        exit(1);
      } else if (ret) {
        // Calculate current time and enqueue events for ready fds
        clock_gettime(CLOCK_MONOTONIC, &system_time);
        struct timespec remaining_sleep_time;
        timespec_diff(&expected_system_time_next, &system_time, &remaining_sleep_time);
        next -= ((remaining_sleep_time.tv_sec * 1000000)
                 + (remaining_sleep_time.tv_nsec / 1000));
        for (int i = 0; i < MAX_IO_VARS; i++) {
          struct io_read_svt *io_sv = io_vars + i;
          if(FD_ISSET(io_sv->fd, &read_fds)) {
            read(io_sv->fd, &io_sv->u8_sv.later_value, 1);
            if(io_sv->u8_sv.sv.triggers) {
              later_event(&io_sv->u8_sv.sv, next);
            }
          }
        }
      }
    }
  } else {
    // No events scheduled, but check if any io to block on
    if (ssm_runtime_alive && max_fd != -1) {
      int ret = pselect(max_fd + 1, &read_fds, NULL, NULL, NULL, NULL);
      if (ret < 0) {
          perror("pselect");
          exit(1);
      }

      // Calculate current time and enqueue events for ready fds
      struct timespec old_system_time = system_time;
      clock_gettime(CLOCK_MONOTONIC, &system_time);
      struct timespec delta_last_tick;
      timespec_diff(&system_time, &old_system_time, &delta_last_tick);
      next = (now + (delta_last_tick.tv_sec * 1000000)
              + (delta_last_tick.tv_nsec / 1000));
      for (int i = 0; i < MAX_IO_VARS; i++) {
        struct io_read_svt *io_sv = io_vars + i;
        if(io_sv->is_open && FD_ISSET(io_sv->fd, &read_fds)) {
          read(io_sv->fd, &io_sv->u8_sv.later_value, 1);
          if(io_sv->u8_sv.sv.triggers) {
            later_event(&io_sv->u8_sv.sv, next);
          }
        }
      }
    }
  }

  set_now(next);
  clock_gettime(CLOCK_MONOTONIC, &system_time);

  return next;
}

/*** Time driver API, exposed via time-driver.h }}} ***/
