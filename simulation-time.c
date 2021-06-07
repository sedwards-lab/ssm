#include "ssm-queue.h"
#include "ssm-sv.h"
#include "ssm-runtime.h"

void initialize_time_driver() {
  // No-op
}

ssm_time_t timestep() {
  const struct sv *event_head = peek_event_queue();
  ssm_time_t next = event_head ? event_head->later_time
                               : NO_EVENT_SCHEDULED;

  set_now(next);
  return next;
}
