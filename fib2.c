#include <stdio.h>
#include <stdlib.h>
#include "ssm.h"

/* 

fib n &r
  var r2 = 0
  if n < 2 then r = 1 else
    fork fib(n-1, r) fib(n-2, r2)
    r = r + r2

0 1 2 3 4 5  6  7  8  9 10  11  12  13
1 1 2 3 5 8 13 21 34 55 89 144 233 377

 */
typedef struct {
  ACTIVATION_RECORD_FIELDS;

  int_cvt n;       // Local variable
  ptr_int_cvt r;
  int_cvt r2;
} rar_fib_t;

stepf_t step_fib;

rar_fib_t *enter_fib(rar_t *cont, priority_t priority,
		     depth_t depth, int n, ptr_int_cvt r)
{
  rar_fib_t *rar = (rar_fib_t *) enter(sizeof(rar_fib_t), step_fib, cont,
				       priority, depth);
  initialize_int(&rar->n, n);
  rar->r = r;
  initialize_int(&rar->r2, 0);

  return rar;
}

void step_fib(rar_t *act)
{
	rar_fib_t *rar = (rar_fib_t *) act;
	switch (rar->pc) {
	case 0: {
		if (rar->n.value < 2) {
			rar->r.ptr->assign(rar->r.ptr, rar->priority, 1, rar->r.offset);
			leave((rar_t *) rar, sizeof(rar_fib_t));
			return;
		}
		depth_t new_depth = rar->depth - 1; // 2 children
		priority_t new_priority = rar->priority;
		priority_t pinc = 1 << new_depth;
		fork((rar_t *) enter_fib( (rar_t *) rar, new_priority, new_depth,
					rar->n.value - 1, rar->r));
		new_priority += pinc;
		fork((rar_t *) enter_fib( (rar_t *) rar, new_priority, new_depth,
					rar->n.value - 2, ptr_of_int(&rar->r2)));
		rar->pc = 1;
		return;
	}
	case 1:
		rar->r.ptr->assign(rar->r.ptr, rar->priority, *DEREF(int, rar->r) + rar->r2.value, rar->r.offset);
		leave((rar_t *) rar, sizeof(rar_fib_t));
		return;
	}
	assert(0);
}

void top_return(rar_t *cont)
{
  return;
}

int main(int argc, char *argv[])
{
	int_cvt result;
	initialize_int(&result, 0);
	int n = argc > 1 ? atoi(argv[1]) : 3;

	rar_t top = { .step = top_return };
	fork((rar_t *) enter_fib(&top, PRIORITY_AT_ROOT, DEPTH_AT_ROOT,
			   n, ptr_of_int(&result)));

  now = 0;
  tick();

  printf("%d\n", result.value);

  return 0;
}
