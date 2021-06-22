/**
 * Poor man's property testing for the binary heap implementation in
 * ssm-queue.c. The specification of each heap operation is asserted by the
 * enqueue, dequeue, and requeue wrapper functions.
 *
 * The test_heap_sort function tests uses heap sort to reverse increasing
 * sequences of integers. TODO: randomly permute integer sequences.
 *
 * The test_chaos function generates a random array, heapifies it, and subjects
 * it to random operations.
 *
 * The number of iterations and the seed for the randomness can be controlled
 * by command line parameters (see main).
 */
#include "ssm-queue-test.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Tests are written monadically; each function returns an integer indicating
 * success (zero) or failure (non-zero).
 */
#define OK 0
#define ERR 1

/**
 * This special value should not appear in any array, and is used to test for
 * array element membership.
 */
#define MAGIC_VAL 0xffffffff

/**
 * Repeatedly evaluated rhs to choose a value for lhs that isn't MAGIC_VAL.
 */
#define choose(lhs, rhs) for ((lhs) = (rhs); (lhs) == MAGIC_VAL; (lhs) = (rhs))

#ifdef DEBUG
#define DBG(stmt) stmt
#else
#define DBG(stmt)                                                              \
  do {                                                                         \
  } while (0)
#endif

/**
 * Print the contents of a 1-indexed integer array.
 */
void print_queue(long *a) {
  size_t len = a[QUEUE_LEN];

  printf("(%ld) [", len);
  if (len == 0)
    goto out;

  printf(" %ld: %ld", (long)QUEUE_HEAD, a[QUEUE_HEAD]);

  for (idx_t i = QUEUE_HEAD + 1; i < len + QUEUE_HEAD; i++)
    printf(", %ld: %ld", i, a[i]);

out:
  printf(" ]\n");
}

/**
 * Scans through the heap q and ensures that the heap invariant is valid.
 */
int validate_heap_property(long *a) {
  size_t len = a[QUEUE_LEN];

  if (len < 2)
    return OK;

  for (idx_t i = QUEUE_HEAD << 1; i < len + QUEUE_HEAD; i++) {
    /* child is less than the parent */
    if (a[i] < a[i >> 1]) {
      printf("Error: heap property violated at index (%ld: %ld)\n", i, a[i]);
      printf("       child less than parent at index (%ld: %ld)\n", i >> 1,
             a[i >> 1]);
      return ERR;
    }
  }

  return OK;
}

long *remember_members(long *a) {
  size_t len = a[QUEUE_LEN];
  size_t size = (len + 1) * sizeof(long);
  if (len == 0)
    return NULL;

  long *ms = (long *)malloc(size);
  if (ms == NULL) {
    /* Don't even bother trying to recover from this */
    perror("malloc");
    exit(1);
  }

  memcpy(ms, a, size);
  return ms;
}

void forget_members(long *ms) {
  if (ms != NULL) /* Not necessary but for the sake of consistency */
    free(ms);
}

/**
 * Guarded enqueue operation that ensures:
 *
 * (1) the length is 1 greater than before;
 * (2) the heap property is maintained;
 * (3) all previously present members are still present exactly once; and
 * (4) newly added member is present exactly once.
 *
 * Extremely inefficient.
 */
int enqueue(long *a, long x) {
  size_t len = a[QUEUE_LEN];
  long *ms = remember_members(a);
  long mx = 0;

  DBG(printf("enqueue %ld\n", x));
  DBG(print_queue(a));

  enqueue_test(a, x);

  if (a[QUEUE_LEN] != len + 1) {
    printf("Error: unexpected length after enqueue: %ld != %ld\n", a[QUEUE_LEN],
           len + 1);
    goto err;
  }

  if (validate_heap_property(a) != OK)
    goto err;

  if (ms == NULL) {
    if (a[QUEUE_HEAD] == x)
      goto ok;
    printf("Error: did not find specified element (%ld) after enqueue\n", x);
    goto err;
  }

  for (idx_t i = QUEUE_HEAD; i < a[QUEUE_LEN] + QUEUE_HEAD; i++) {
    for (idx_t j = QUEUE_HEAD; j < ms[QUEUE_LEN] + QUEUE_HEAD; j++) {
      if (mx != MAGIC_VAL && a[i] == x) {
        mx = MAGIC_VAL;
        goto a_next;
      }

      if (a[i] == ms[j]) {
        ms[j] = MAGIC_VAL;
        goto a_next;
      }
    }
    printf("Error: unexpected element (%ld: %ld) after enqueue\n", i, a[i]);
    goto err;

  a_next: /* Found a[i] in ms */;
  }

  if (mx != MAGIC_VAL) {
    printf("Error: did not find member (%ld) after enqueue\n", x);
    goto err;
  }

  for (idx_t j = QUEUE_HEAD; j < ms[QUEUE_LEN] + QUEUE_HEAD; j++) {
    if (ms[j] != MAGIC_VAL) {
      printf("Error: did not find member (%ld) after enqueue\n", ms[j]);
      goto err;
    }
  }

ok:
  forget_members(ms);
  return OK;
err:
  printf("Context: enqueue\n");
  print_queue(a);

  forget_members(ms);
  return ERR;
}

/**
 * Guarded dequeue operation that ensures:
 *
 * (1) the length is 1 less than before;
 * (2) the heap property is maintained;
 * (3) all non-dequeued members are still present exactly once; and
 * (4) dequeued member is not present.
 *
 * Extremely inefficient.
 */
int dequeue(long *a, idx_t to_dq) {
  size_t len = a[QUEUE_LEN];
  long *ms = remember_members(a);
  long x = a[to_dq], mx = 0;

  DBG(printf("dequeue %ld: %ld\n", to_dq, x));
  DBG(print_queue(a));

  dequeue_test(a, to_dq);

  if (a[QUEUE_LEN] != len - 1) {
    printf("Error: unexpected length after dequeue: %ld != %ld\n", a[QUEUE_LEN],
           len - 1);
    goto err;
  }

  if (validate_heap_property(a) != OK)
    goto err;

  for (idx_t i = QUEUE_HEAD; i < a[QUEUE_LEN] + QUEUE_HEAD; i++) {
    for (idx_t j = QUEUE_HEAD; j < ms[QUEUE_LEN] + QUEUE_HEAD; j++) {
      if (a[i] == ms[j]) {
        ms[j] = MAGIC_VAL;
        goto a_next;
      }
    }

    printf("Error: unexpected element (%ld: %ld) after dequeue\n", i, a[i]);
    goto err;

  a_next: /* Found a[i] in ms */;
  }

  for (idx_t j = QUEUE_HEAD; j < ms[QUEUE_LEN] + QUEUE_HEAD; j++) {
    if (mx != MAGIC_VAL && ms[j] == x) {
      mx = MAGIC_VAL;
      continue;
    }

    if (ms[j] != MAGIC_VAL) {
      printf("Error: did not find member (%ld) after dequeue\n", ms[j]);
      goto err;
    }
  }

  if (mx != MAGIC_VAL) {
    printf("Error: dequeued member (%ld) still present after dequeue\n", x);
    goto err;
  }

  forget_members(ms);
  return OK;
err:
  printf("Context: dequeue\n");
  print_queue(a);
  forget_members(ms);
  return ERR;
}

/**
 * Guarded requeue operation that ensures:
 *
 * (1) the length is same as before;
 * (2) the heap property is maintained; and
 * (3) all members are still present exactly once.
 *
 * Extremely inefficient.
 */
int requeue(long *a, idx_t to_rq) {
  size_t len = a[QUEUE_LEN];
  long *ms = remember_members(a);

  DBG(printf("requeue %ld: %ld\n", to_rq, a[to_rq]));
  DBG(print_queue(a));

  requeue_test(a, to_rq);

  if (a[QUEUE_LEN] != len) {
    printf("Error: unexpected length after requeue: %ld != %ld\n", a[QUEUE_LEN],
           len);
    return ERR;
  }

  if (validate_heap_property(a) != OK)
    return ERR;

  for (idx_t i = QUEUE_HEAD; i < a[QUEUE_LEN] + QUEUE_HEAD; i++) {
    for (idx_t j = QUEUE_HEAD; j < ms[QUEUE_LEN] + QUEUE_HEAD; j++) {
      if (a[i] == ms[j]) {
        ms[j] = MAGIC_VAL;
        goto a_next;
      }
    }

    printf("Error: unexpected element (%ld: %ld) after requeue\n", i, a[i]);
    goto err;

  a_next: /* Found a[i] in ms */;
  }

  for (idx_t j = QUEUE_HEAD; j < ms[QUEUE_LEN] + QUEUE_HEAD; j++) {
    if (ms[j] != MAGIC_VAL) {
      printf("Error: did not find member (%ld) after requeue\n", ms[j]);
      goto err;
    }
  }

  forget_members(ms);
  return OK;
err:
  printf("Context: requeue\n");
  print_queue(a);
  forget_members(ms);
  return ERR;
}

/**
 * Convert an array to a heap by incrementally enqueueing each element.
 */
int heapify(long *a) {
  size_t len = a[QUEUE_LEN];

  DBG(printf("Heapify:\n"));
  DBG(print_queue(a));

  a[QUEUE_LEN] = 1;
  for (idx_t i = QUEUE_HEAD + 1; i < len + QUEUE_HEAD; i++)
    if (enqueue(a, a[i]) != OK)
      return ERR;

  if (a[QUEUE_LEN] != len) {
    printf("Error: unexpected length after heapify: %ld != %ld\n", a[QUEUE_LEN],
           len);
    return ERR;
  }

  return OK;
}

/**
 * Validate that all elements [1..len] are present in a queue.
 */
int validate_seq(long *a) {
  size_t len = a[QUEUE_LEN];

  /** Keep track of seen elements */
  char *m = (char *)malloc(len + 1);

  for (size_t i = QUEUE_HEAD; i < len + QUEUE_HEAD; i++)
    m[i] = 0;

  for (idx_t i = QUEUE_HEAD; i < len + QUEUE_HEAD; i++) {
    if (a[i] > len) {
      printf("Error: unexpected content in q at index (%ld: %ld)\n", i, a[i]);
      print_queue(a);
      goto err;
    }

    if (m[a[i]]) {
      printf("Error: duplicate content in q at index (%ld: %ld)\n", i, a[i]);
      print_queue(a);
      goto err;
    }
    m[a[i]] = 1;
  }

  for (size_t i = QUEUE_HEAD; i < len + QUEUE_HEAD; i++) {
    if (m[i] == 0) {
      printf("Error: did not encounter value in queue: %ld\n", i);
      print_queue(a);
      goto err;
    }
  }

  free(m);
  return OK;

err:
  free(m);
  return ERR;
}

/**
 * Create a sequence of integers from [1..len], heapify it (should be a nop),
 * and then repeatedly dequeue the head of the queue and place it at the end.
 * The end result should be the reversed sequence [len..1].
 */
int test_heap_sort_seq(const size_t len) {
  long *a = malloc((len + QUEUE_HEAD) * sizeof(long));
  if (a == NULL) {
    perror("heap_sort_seq malloc");
    exit(1);
  }

  for (idx_t i = QUEUE_HEAD; i < len + QUEUE_HEAD; i++)
    a[i] = i;
  a[QUEUE_LEN] = len;

  if (heapify(a) != OK)
    goto err;

  if (validate_seq(a) != OK)
    goto err;

  for (idx_t i = QUEUE_HEAD; i < len + QUEUE_HEAD; i++) {
    int t = a[QUEUE_HEAD];
    dequeue(a, QUEUE_HEAD);
    a[a[QUEUE_LEN] + QUEUE_HEAD] = t;
  }
  a[QUEUE_LEN] = len;

  for (idx_t i = QUEUE_HEAD; i < len + QUEUE_HEAD; i++) {
    idx_t j = len + QUEUE_HEAD - i;
    if (a[j] != i) {
      printf("Error: test_heap_sort_seq failed.\n");
      printf("       Unexpected value at index: [%ld => %ld]\n", j, a[j]);
      printf("       Expected value: %d\n", (int)i);
      goto err;
    }
  }

  free(a);
  return OK;

err:
  free(a);
  return ERR;
}

/**
 * Test heap sort up for all array sequences up to the given bound.
 */
int test_heap_sort(unsigned int bound) {
  for (int i = 1; i < bound; i++)
    test_heap_sort_seq(i);
  return 0;
}

/**
 * Create a random array, heapify it, and subject it to random heap operations.
 */
int test_chaos(const size_t max_len, size_t iters, const unsigned int seed) {
  srand(seed);

  long *a = malloc((max_len + 1) * sizeof(long));
  if (a == NULL) {
    perror("heap_sort_seq malloc");
    exit(1);
  }

  size_t len = 0;
  while (len == 0)
    choose(len, rand() % max_len);

  for (idx_t i = QUEUE_HEAD; i < len + QUEUE_HEAD; i++)
    choose(a[i], rand() % 100);

  a[QUEUE_LEN] = len;

  heapify(a);

  while (iters--) {
    DBG(printf("test_chaos: iteration %d\n", iters));
  retry:
    switch (rand() % 3) {
    case 0: {
      if (a[QUEUE_LEN] >= max_len)
        goto retry;

      long x;
      choose(x, rand());

      if (enqueue(a, x) != OK)
        goto err;
      break;
    }
    case 1: {
      if (a[QUEUE_LEN] <= 0)
        goto retry;

      idx_t idx;
      choose(idx, (rand() % a[QUEUE_LEN]) + QUEUE_HEAD);

      if (dequeue(a, idx) != OK)
        goto err;
      break;
    }
    default: {
      if (a[QUEUE_LEN] <= 0)
        goto retry;

      idx_t idx;
      choose(idx, (rand() % a[QUEUE_LEN]) + QUEUE_HEAD);

      long x;
      choose(x, rand());

      a[idx] = x;

      if (requeue(a, idx) != OK)
        goto err;

      break;
    }
    }
  }

  free(a);
  return OK;

err:
  printf("context: test_chaos\n");
  free(a);
  return ERR;
}

/**
 * Usage:
 *
 *  ./ssm-queue-test [iters] [seed]
 */
int main(int argc, char **argv) {

  test_heap_sort(128);

  size_t iters = 1000;
  size_t seed = 42;

  if (argc > 1 && atoi(argv[1]) != 0)
    iters = atoi(argv[1]);

  if (argc > 2 && atoi(argv[2]) != 0)
    iters = atoi(argv[2]);

  test_chaos(256, iters, seed);

  return 0;
}
