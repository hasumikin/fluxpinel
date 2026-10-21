/* process.c -- the module functions of Process and GC.

   spinel's class table names neither module: codegen emits a call of the
   runtime for each function (src/codegen_call_module.c of spinel). Lupe
   makes each one a builtin module of its own (lupe_builtin) and calls the
   same helpers of the runtime here.

   The control of processes (spawn, wait, kill, daemon, ...) is not here. */

#include "lupe.h"

#define LUPE_IS(s) (!strcmp(name, (s)))

/* The constants of Process, with the values of the platform. */
static void lupe_process_consts(LupeClass *c) {
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
    lupe_const_set(c, sp_sym_intern(k[i].name), sp_box_int(k[i].v));
}

/* The clock of clock_gettime: an id, or a Symbol that names a clock
   constant, as CRuby takes :CLOCK_MONOTONIC. */
static sp_int lupe_clock_id(sp_RbVal v) {
  if (v.tag == SP_TAG_SYM) {
    sp_RbVal c;
    LupeClass *p = lupe_builtin("Process");
    if (!strncmp(sp_sym_to_s((sp_sym)v.v.i), "CLOCK_", 6) && lupe_const_scoped(p, (sp_sym)v.v.i, &c)) return c.v.i;
  }
  return sp_poly_arg_int_chk(v);
}

/* clock_gettime and clock_getres in the unit of the call: an Integer for
   an integer unit, a Float for a float unit and for none. */
static sp_RbVal lupe_clock(const char *name, int argc, sp_RbVal *argv) {
  sp_int id = lupe_clock_id(argv[0]);
  int64_t ns = LUPE_IS("clock_getres") ? sp_process_clock_res_ns(id) : sp_process_clock_ns(id);
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

static int lupe_process_fn(const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (argc == 0) {
    if (LUPE_IS("pid")) { *out = sp_box_int((sp_int)getpid()); return 1; }
    if (LUPE_IS("ppid")) { *out = sp_box_int(sp_process_ppid()); return 1; }
    if (LUPE_IS("uid")) { *out = sp_box_int((sp_int)getuid()); return 1; }
    if (LUPE_IS("gid")) { *out = sp_box_int((sp_int)getgid()); return 1; }
    if (LUPE_IS("euid")) { *out = sp_box_int((sp_int)geteuid()); return 1; }
    if (LUPE_IS("egid")) { *out = sp_box_int((sp_int)getegid()); return 1; }
    if (LUPE_IS("getpgrp")) { *out = sp_box_int((sp_int)getpgrp()); return 1; }
    if (LUPE_IS("times")) { *out = sp_box_tms(sp_process_times()); return 1; }
    if (LUPE_IS("groups")) { *out = sp_box_int_array(sp_process_groups()); return 1; }
  }
  if (LUPE_IS("getsid") && argc <= 1) {
    *out = sp_box_int((sp_int)getsid(argc ? (pid_t)sp_poly_arg_int_chk(argv[0]) : 0));
    return 1;
  }
  if ((LUPE_IS("clock_gettime") || LUPE_IS("clock_getres")) && argc >= 1 && argc <= 2) {
    *out = lupe_clock(name, argc, argv);
    return 1;
  }
  if (LUPE_IS("getpriority") && argc == 2) {
    *out = sp_box_int(sp_process_getpriority(sp_poly_arg_int_chk(argv[0]), sp_poly_arg_int_chk(argv[1])));
    return 1;
  }
  return 0;
}

/* GC.start(full_mark:, immediate_mark:, immediate_sweep:): one collector
   and one kind of collection, so the options change nothing. */
static int lupe_gc_start_opts(int argc, sp_RbVal *argv) {
  if (argc == 0) return 1;
  if (argc > 1 || !lupe_is_hash(argv[0]) || argv[0].v.p != L.kwhash) return 0;
  sp_PolyPolyHash *kw = sp_poly_as_poly_poly_hash(argv[0]);
  for (sp_int i = 0; i < kw->len; i++) {
    sp_RbVal k = kw->keys[kw->order[i]];
    const char *kn = k.tag == SP_TAG_SYM ? sp_sym_to_s((sp_sym)k.v.i) : "";
    if (strcmp(kn, "full_mark") && strcmp(kn, "immediate_mark") && strcmp(kn, "immediate_sweep")) return 0;
  }
  return 1;
}

static int lupe_gc_fn(const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if ((LUPE_IS("start") && lupe_gc_start_opts(argc, argv)) || (LUPE_IS("compact") && argc == 0)) {
    sp_gc_collect_request();
    *out = sp_box_nil();
    return 1;
  }
  if (LUPE_IS("stat") && argc == 0) { *out = sp_box_obj(sp_gc_stat(), SP_BUILTIN_STR_INT_HASH); return 1; }
  /* GC.stat(key): the one statistic, ArgumentError for a key not kept */
  if (LUPE_IS("stat") && argc == 1 && !(lupe_is_hash(argv[0]) && argv[0].v.p == L.kwhash)) {
    *out = sp_box_int(sp_gc_stat_key(argv[0]));
    return 1;
  }
  /* the time of every collection so far, in nanoseconds as CRuby answers */
  if (LUPE_IS("total_time") && argc == 0) { *out = sp_box_int((sp_int)(sp_gc_stat_seconds * 1e9)); return 1; }
  return 0;
}

/* respond_to? on Process or GC: the functions above. */
static int lupe_process_responds(sp_RbVal r, const char *name) {
  static const char *const process[] = { "pid", "ppid", "uid", "gid", "euid", "egid", "getpgrp", "times", "groups",
                                         "getsid", "clock_gettime", "clock_getres", "getpriority", NULL };
  static const char *const gc[] = { "start", "compact", "stat", "total_time", NULL };
  LupeClass *k = lupe_class_value(r);
  if (!k || k->cls_id >= 0 || !k->is_module) return 0;
  const char *const *names = !strcmp(k->name, "Process") ? process : !strcmp(k->name, "GC") ? gc : NULL;
  for (int i = 0; names && names[i]; i++)
    if (LUPE_IS(names[i])) return 1;
  return 0;
}

/* A module function of Process or GC. Answers 1 and sets *out when it
   handles the call. */
static int lupe_process_class(const LupeClass *k, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (k->cls_id >= 0 || !k->is_module) return 0;
  if (!strcmp(k->name, "Process")) return lupe_process_fn(name, argc, argv, out);
  if (!strcmp(k->name, "GC")) return lupe_gc_fn(name, argc, argv, out);
  return 0;
}
