/* smoke.c -- call a few generated builtin wrappers and check the answers.

   The wrappers come from tools/bop_gen.c. This file finds each wrapper by
   receiver, name, variant and argument count, so it does not depend on the
   wrapper numbers. */

#include "bop_gen_out.c"

static int failures = 0;

static lupe_bop_fn find(const char *recv, const char *name, const char *variant, int argc) {
  for (int i = 0; i < lupe_bop_count; i++) {
    const LupeBop *b = &lupe_bop_table[i];
    if (b->fn && !strcmp(b->recv, recv) && !strcmp(b->name, name) && !strcmp(b->variant, variant) &&
        b->argc_min <= argc && argc <= b->argc_max)
      return b->fn;
  }
  return NULL;
}

static void check(const char *what, const char *recv, const char *name, const char *variant,
                  sp_RbVal self, int argc, sp_RbVal *argv, const char *want) {
  lupe_bop_fn fn = find(recv, name, variant, argc);
  const char *got = fn ? sp_poly_inspect(fn(self, argv)) : "(no wrapper)";
  int ok = strcmp(got, want) == 0;
  printf("%s %-24s %s\n", ok ? "ok" : "NG", what, got);
  if (!ok) { printf("   want %s\n", want); failures++; }
}

static void smoke_body(void) {
  SP_GC_SAVE();
  lupe_tu_init();
  sp_RbVal s = sp_box_str(SPL("abz"));
  sp_RbVal a[2];
  check("\"abz\".upcase", "string", "upcase", "string", s, 0, a, "\"ABZ\"");
  check("\"abz\".succ", "string", "succ", "string", s, 0, a, "\"aca\"");
  check("255.to_s", "int", "to_s", "int", sp_box_int(255), 0, a, "\"255\"");
  a[0] = sp_box_int(2);
  check("255.to_s(2)", "int", "to_s", "int", sp_box_int(255), 1, a, "\"11111111\"");
  a[0] = sp_box_int(7);
  check("\"abz\".center(7)", "string", "center", "string", s, 1, a, "\"  abz  \"");
  sp_IntArray *ia = sp_IntArray_new();
  SP_GC_ROOT(ia);
  sp_IntArray_push(ia, 1);
  sp_IntArray_push(ia, 2);
  sp_IntArray_push(ia, 3);
  check("[1, 2, 3].reverse", "ANY_ARRAY", "reverse", "int_array",
        sp_box_nullable_obj(ia, SP_BUILTIN_INT_ARRAY), 0, a, "[3, 2, 1]");
  printf("%s\n", failures ? "M1 smoke: FAILED" : "M1 smoke: all checks passed");
}

int main(void) {
  sp_main_stack_run(smoke_body);
  return failures;
}
