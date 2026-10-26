/* process.c -- the module functions of Process, GC and Math.

   codegen emits a call of the runtime or of libc for each function of these
   modules (src/codegen_call_module.c of spinel). Fluxpinel makes each one a
   builtin module (fx_builtin) and calls the same functions here.

   The control of processes (spawn, wait, kill, daemon, ...) is not here. */

#include "fluxpinel.h"

#define FX_IS(s) (!strcmp(name, (s)))

/* The constants of Process, with the values of the platform. */
static void fx_process_consts(FxClass *c) {
  static const struct { const char *name; sp_int v; } k[] = {
    { "CLOCK_MONOTONIC", CLOCK_MONOTONIC },
    { "CLOCK_REALTIME", CLOCK_REALTIME },
    { "CLOCK_PROCESS_CPUTIME_ID", CLOCK_PROCESS_CPUTIME_ID },
    { "CLOCK_THREAD_CPUTIME_ID", CLOCK_THREAD_CPUTIME_ID },
    { "PRIO_PROCESS", PRIO_PROCESS },
    { "PRIO_PGRP", PRIO_PGRP },
    { "PRIO_USER", PRIO_USER },
  };
  for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
    fx_const_set(c, sp_sym_intern(k[i].name), sp_box_int(k[i].v));
}

/* The clock of clock_gettime: an id, or a Symbol that names a clock
   constant, as CRuby takes :CLOCK_MONOTONIC. */
static sp_int fx_clock_id(sp_RbVal v) {
  if (v.tag == SP_TAG_SYM) {
    sp_RbVal c;
    FxClass *p = fx_builtin("Process");
    if (!strncmp(sp_sym_to_s((sp_sym)v.v.i), "CLOCK_", 6) && fx_const_scoped(p, (sp_sym)v.v.i, &c)) return c.v.i;
  }
  return sp_poly_arg_int_chk(v);
}

/* clock_gettime and clock_getres in the unit of the call: an Integer for
   an integer unit, a Float for a float unit and for none. */
static sp_RbVal fx_clock(const char *name, int argc, sp_RbVal *argv) {
  sp_int id = fx_clock_id(argv[0]);
  int64_t ns = FX_IS("clock_getres") ? sp_process_clock_res_ns(id) : sp_process_clock_ns(id);
  const char *unit = argc >= 2 && argv[1].tag == SP_TAG_SYM ? sp_sym_to_s((sp_sym)argv[1].v.i) : "float_second";
  if (!strcmp(unit, "nanosecond")) return sp_box_int(ns);
  if (!strcmp(unit, "microsecond")) return sp_box_int(ns / 1000);
  if (!strcmp(unit, "millisecond")) return sp_box_int(ns / 1000000);
  if (!strcmp(unit, "second")) return sp_box_int(ns / 1000000000);
  if (!strcmp(unit, "float_microsecond")) return sp_box_float(ns / 1e3);
  if (!strcmp(unit, "float_millisecond")) return sp_box_float(ns / 1e6);
  if (!strcmp(unit, "float_second")) return sp_box_float(ns / 1e9);
  sp_raise_cls("ArgumentError", sp_sprintf("unexpected unit: %s", unit));
}

static int fx_process_fn(const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (argc == 0) {
    if (FX_IS("pid")) { *out = sp_box_int((sp_int)getpid()); return 1; }
    if (FX_IS("ppid")) { *out = sp_box_int(sp_process_ppid()); return 1; }
    if (FX_IS("uid")) { *out = sp_box_int((sp_int)getuid()); return 1; }
    if (FX_IS("gid")) { *out = sp_box_int((sp_int)getgid()); return 1; }
    if (FX_IS("euid")) { *out = sp_box_int((sp_int)geteuid()); return 1; }
    if (FX_IS("egid")) { *out = sp_box_int((sp_int)getegid()); return 1; }
    if (FX_IS("getpgrp")) { *out = sp_box_int((sp_int)getpgrp()); return 1; }
    if (FX_IS("times")) { *out = sp_box_tms(sp_process_times()); return 1; }
    if (FX_IS("groups")) { *out = sp_box_int_array(sp_process_groups()); return 1; }
  }
  if (FX_IS("getsid") && argc <= 1) {
    *out = sp_box_int((sp_int)getsid(argc ? (pid_t)sp_poly_arg_int_chk(argv[0]) : 0));
    return 1;
  }
  if ((FX_IS("clock_gettime") || FX_IS("clock_getres")) && argc >= 1 && argc <= 2) {
    *out = fx_clock(name, argc, argv);
    return 1;
  }
  if (FX_IS("getpriority") && argc == 2) {
    *out = sp_box_int(sp_process_getpriority(sp_poly_arg_int_chk(argv[0]), sp_poly_arg_int_chk(argv[1])));
    return 1;
  }
  return 0;
}

/* GC.start(full_mark:, immediate_mark:, immediate_sweep:): one collector
   and one kind of collection, so the options change nothing. */
static int fx_gc_start_opts(int argc, sp_RbVal *argv) {
  if (argc == 0) return 1;
  if (argc > 1 || !fx_is_hash(argv[0]) || argv[0].v.p != L.kwhash) return 0;
  sp_PolyPolyHash *kw = sp_poly_as_poly_poly_hash(argv[0]);
  for (sp_int i = 0; i < kw->len; i++) {
    sp_RbVal k = kw->keys[kw->order[i]];
    const char *kn = k.tag == SP_TAG_SYM ? sp_sym_to_s((sp_sym)k.v.i) : "";
    if (strcmp(kn, "full_mark") && strcmp(kn, "immediate_mark") && strcmp(kn, "immediate_sweep")) return 0;
  }
  return 1;
}

static int fx_gc_fn(const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if ((FX_IS("start") && fx_gc_start_opts(argc, argv)) || (FX_IS("compact") && argc == 0)) {
    sp_gc_collect_request();
    *out = sp_box_nil();
    return 1;
  }
  if (FX_IS("stat") && argc == 0) { *out = sp_box_obj(sp_gc_stat(), SP_BUILTIN_STR_INT_HASH); return 1; }
  /* GC.stat(key): the one statistic, ArgumentError for a key not kept */
  if (FX_IS("stat") && argc == 1 && !(fx_is_hash(argv[0]) && argv[0].v.p == L.kwhash)) {
    *out = sp_box_int(sp_gc_stat_key(argv[0]));
    return 1;
  }
  /* the time of every collection so far, in nanoseconds as CRuby answers */
  if (FX_IS("total_time") && argc == 0) { *out = sp_box_int((sp_int)(sp_gc_stat_seconds * 1e9)); return 1; }
  return 0;
}

/* respond_to? on Process or GC: the functions above. */
static int fx_process_responds(sp_RbVal r, const char *name) {
  static const char *const process[] = { "pid", "ppid", "uid", "gid", "euid", "egid", "getpgrp", "times", "groups",
                                         "getsid", "clock_gettime", "clock_getres", "getpriority", NULL };
  static const char *const gc[] = { "start", "compact", "stat", "total_time", NULL };
  FxClass *k = fx_class_value(r);
  if (!k || k->cls_id >= 0 || !k->is_module) return 0;
  const char *const *names = !strcmp(k->name, "Process") ? process : !strcmp(k->name, "GC") ? gc : NULL;
  for (int i = 0; names && names[i]; i++)
    if (FX_IS(names[i])) return 1;
  return 0;
}

/* Math.cbrt as CRuby computes it (ruby_cbrt of math.c): the cbrt of glibc
   is off by a unit in the last place for some cubes (27.0), and one step
   of Newton's method puts it right. */
static double fx_cbrt(double x) {
  double f = cbrt(x);
#ifdef __GLIBC__
  if (isfinite(f) && !(f == 0.0 && x == 0.0)) f = (2.0 * f + x / f / f) / 3.0;
#endif
  return f;
}

/* A function of Math, as codegen emits it: libc for a function that takes
   any real, sp_math_* for one with a domain (Math::DomainError outside).
   An argument converts as sp_num_to_f converts it (TypeError for nil or a
   String). */
static int fx_math_fn(const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  static const struct { const char *name; double (*fn)(double); } one[] = {
    { "sin", sin }, { "cos", cos }, { "tan", tan }, { "asin", sp_math_asin }, { "acos", sp_math_acos },
    { "atan", atan }, { "sinh", sinh }, { "cosh", cosh }, { "tanh", tanh }, { "asinh", asinh },
    { "acosh", sp_math_acosh }, { "atanh", sp_math_atanh }, { "exp", exp }, { "sqrt", sp_math_sqrt },
    { "cbrt", fx_cbrt }, { "expm1", expm1 }, { "log1p", sp_math_log1p }, { "erf", erf }, { "erfc", erfc },
    { "gamma", sp_math_gamma }, { "log2", sp_math_log2 }, { "log10", sp_math_log10 },
  };
  if (argc == 1) {
    for (size_t i = 0; i < sizeof one / sizeof one[0]; i++)
      if (FX_IS(one[i].name)) {
        *out = sp_box_float(one[i].fn(sp_num_to_f(argv[0])));
        return 1;
      }
    if (FX_IS("lgamma")) {
      *out = sp_box_poly_array(sp_math_lgamma(sp_num_to_f(argv[0])));
      return 1;
    }
    if (FX_IS("frexp")) {
      int e;
      double f = frexp(sp_num_to_f(argv[0]), &e);
      sp_PolyArray *a = sp_PolyArray_new();
      SP_GC_ROOT(a);
      sp_PolyArray_push(a, sp_box_float(f));
      sp_PolyArray_push(a, sp_box_int(e));
      *out = sp_box_poly_array(a);
      return 1;
    }
  }
  if (FX_IS("log") && (argc == 1 || argc == 2)) {
    double x = sp_num_to_f(argv[0]);
    if (argc == 1) {
      *out = sp_box_float(sp_math_log(x));
      return 1;
    }
    /* log base 1 is NaN in CRuby */
    double b = sp_num_to_f(argv[1]);
    *out = sp_box_float(b == 1.0 ? 0.0 / 0.0 : sp_math_log(x) / sp_math_log(b));
    return 1;
  }
  if (argc == 2 && (FX_IS("atan2") || FX_IS("hypot"))) {
    double y = sp_num_to_f(argv[0]), x = sp_num_to_f(argv[1]);
    *out = sp_box_float(name[0] == 'a' ? atan2(y, x) : hypot(y, x));
    return 1;
  }
  if (argc == 2 && FX_IS("ldexp")) {
    if (argv[1].tag == SP_TAG_BIGINT) sp_raise_cls("RangeError", "bignum too big to convert into `long'");
    *out = sp_box_float(ldexp(sp_num_to_f(argv[0]), (int)sp_poly_arg_int_chk(argv[1])));
    return 1;
  }
  return 0;
}

/* A module function of Process, GC or Math. Answers 1 and sets *out when
   it handles the call. */
static int fx_process_class(const FxClass *k, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (k->cls_id >= 0 || !k->is_module) return 0;
  if (!strcmp(k->name, "Process")) return fx_process_fn(name, argc, argv, out);
  if (!strcmp(k->name, "GC")) return fx_gc_fn(name, argc, argv, out);
  if (!strcmp(k->name, "Math")) return fx_math_fn(name, argc, argv, out);
  return 0;
}
