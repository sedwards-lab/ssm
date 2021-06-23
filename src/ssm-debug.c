#include <ssm-debug.h>

static struct debug_buffer value_repr_default(struct sv *sv) {
  return (struct debug_buffer) { .buf = "(unknown value)" };
}

void initialize_debug_sv(struct debug_sv *sv) {
  sv->var_name = "(unknown var name)";
  sv->type_name = "(unknown type name)";
  sv->value_repr = value_repr_default;
}

void initialize_debug_act(struct debug_act *act) {
  act->act_name = "(unknown act name)";
}
