#ifndef _SSM_QUEUE_TEST_H
#define _SSM_QUEUE_TEST_H

#include "ssm-queue.h"

void enqueue_test(long *long_queue, long to_insert);
void dequeue_test(long *long_queue, idx_t to_dequeue);
void requeue_test(long *long_queue, idx_t to_requeue);
idx_t index_of_test(long *long_queue, long to_find);

#endif /* ifndef _SSM_QUEUE_TEST_H */
