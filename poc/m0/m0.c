/* m0.c -- M0: drive libspinel_rt with no spinel-generated code.

   The program makes boxed values, calls sp_poly_* operations on them, catches
   a raised exception and runs the GC. Each check prints "ok" or "NG". The
   exit status is the number of failed checks.

   spinel_rt.h declares hooks such as sp_class_to_s as static, so each TU
   that includes it must define them. This file therefore includes
   lupe_tu.c, and the program is one TU. */

#include "lupe_tu.c"

static int failures = 0;

static void check_str(const char *what, const char *got, const char *want) {
  int ok = got && strcmp(got, want) == 0;
  printf("%s %-28s %s\n", ok ? "ok" : "NG", what, got ? got : "(null)");
  if (!ok) { printf("   want %s\n", want); failures++; }
}

static void check_val(const char *what, sp_RbVal v, const char *want) {
  check_str(what, sp_poly_inspect(v), want);
}

/* Calls fn under a rescue frame, in the shape spinel emits for begin/rescue.
   Answers the class name of the raised exception, or NULL. */
static const char *try_raise(sp_RbVal (*fn)(sp_RbVal, sp_RbVal), sp_RbVal a, sp_RbVal b, const char **msg) {
  const char *cls = NULL;
  sp_exc_check_depth();
  sp_exc_rootmark[sp_exc_top] = sp_gc_nroots; sp_rescue_mark[sp_exc_top] = sp_rescue_sp;
  sp_exc_msg[sp_exc_top] = 0; sp_exc_obj[sp_exc_top] = 0; sp_exc_top++;
  if (setjmp(sp_exc_stack[sp_exc_top - 1]) == 0) {
    (void)fn(a, b);
    sp_exc_top--;
  } else {
    sp_exc_top--;
    sp_gc_nroots = sp_exc_rootmark[sp_exc_top]; sp_rescue_sp = sp_rescue_mark[sp_exc_top];
    cls = (const char *)sp_last_exc_cls;
    *msg = sp_exc_msg[sp_exc_top];
  }
  return cls;
}

static void test_scalars(void) {
  check_val("int + int", sp_poly_add(sp_box_int(2), sp_box_int(3)), "5");
  check_val("int + float", sp_poly_add(sp_box_int(1), sp_box_float(0.5)), "1.5");
  check_val("str + str", sp_poly_add(sp_box_str(SPL("ab")), sp_box_str(SPL("cd"))), "\"abcd\"");
  check_val("str * int", sp_poly_mul(sp_box_str(SPL("ab")), sp_box_int(3)), "\"ababab\"");
  check_val("nil", sp_box_nil(), "nil");
}

static void test_containers(void) {
  sp_PolyArray *a = sp_PolyArray_new();
  SP_GC_ROOT(a);
  sp_PolyArray_push(a, sp_box_int(1));
  sp_PolyArray_push(a, sp_box_str(SPL("x")));
  sp_PolyArray_push(a, sp_box_float(2.5));
  sp_RbVal av = sp_box_poly_array(a);
  SP_GC_ROOT_RBVAL(av);
  check_val("array", av, "[1, \"x\", 2.5]");
  check_val("array[1]", sp_PolyArray_get(a, 1), "\"x\"");
  check_val("array + array", sp_poly_add(av, av), "[1, \"x\", 2.5, 1, \"x\", 2.5]");

  sp_PolyPolyHash *h = sp_PolyPolyHash_new();
  SP_GC_ROOT(h);
  sp_PolyPolyHash_set(h, sp_box_int(1), sp_box_str(SPL("a")));
  sp_PolyPolyHash_set(h, sp_box_str(SPL("k")), sp_box_float(2.5));
  check_str("hash", sp_PolyPolyHash_inspect(h), "{1 => \"a\", \"k\" => 2.5}");
  check_val("hash[\"k\"]", sp_PolyPolyHash_get(h, sp_box_str(SPL("k"))), "2.5");
}

static void test_exception(void) {
  const char *msg = NULL;
  const char *cls = try_raise(sp_poly_add, sp_box_int(1), sp_box_str(SPL("a")), &msg);
  check_str("1 + \"a\" raises", cls, "TypeError");
  check_str("message", msg, "String can't be coerced into Integer");
}

/* The overflow mode is a compile-time choice (spinel --int-overflow). Lupe
   must use the mode of the AOT build that it runs with. */
static void test_overflow(void) {
  sp_RbVal big = sp_box_int((sp_int)1 << 62), four = sp_box_int(4);
#if defined(SP_INT_OVERFLOW_MODE_PROMOTE)
  check_val("overflow (promote)", sp_poly_mul(big, four), "18446744073709551616");
#elif defined(SP_INT_OVERFLOW_MODE_WRAP)
  check_val("overflow (wrap)", sp_poly_mul(big, four), "0");
#else
  const char *msg = NULL;
  check_str("overflow (raise)", try_raise(sp_poly_mul, big, four, &msg), "RangeError");
#endif
}

/* Makes many short-lived strings and keeps every 1000th in a rooted array.
   The GC must free the rest and keep the rooted ones intact. */
static void test_gc(void) {
  enum { N = 200000, KEEP_EVERY = 1000 };
  unsigned long long before = sp_gc_stat_collections;
  sp_PolyArray *keep = sp_PolyArray_new();
  SP_GC_ROOT(keep);
  for (int i = 0; i < N; i++) {
    sp_RbVal s = sp_poly_add(sp_box_str(SPL("item-")), sp_poly_to_s_box(sp_box_int(i)));
    if (i % KEEP_EVERY == 0) sp_PolyArray_push(keep, s);
  }
  sp_gc_collect();
  /* A new String is a mutable handle (SP_TAG_OBJ, SP_BUILTIN_STRBUF), not
     SP_TAG_STR, so compare through inspect. */
  int bad = 0;
  char want[32];
  for (int i = 0; i < N / KEEP_EVERY; i++) {
    snprintf(want, sizeof want, "\"item-%d\"", i * KEEP_EVERY);
    if (strcmp(sp_poly_inspect(sp_PolyArray_get(keep, i)), want) != 0) bad++;
  }
  char got[64];
  snprintf(got, sizeof got, "%d kept, %d bad", N / KEEP_EVERY, bad);
  check_str("rooted strings survive GC", got, "200 kept, 0 bad");
  unsigned long long runs = sp_gc_stat_collections - before;
  printf("%s %-28s %llu\n", runs > 1 ? "ok" : "NG", "GC collections", runs);
  if (runs <= 1) failures++;
}

static void m0_body(void) {
  SP_GC_SAVE();
  lupe_tu_init();
  test_scalars();
  test_containers();
  test_exception();
  test_overflow();
  test_gc();
  printf("%s\n", failures ? "M0: FAILED" : "M0: all checks passed");
}

int main(void) {
  sp_main_stack_run(m0_body);
  return failures;
}
