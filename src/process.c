/* process.c -- the module functions of Process, GC and Math.

   codegen emits a call of the runtime or of libc for each function of these
   modules (src/codegen_call_module.c of spinel). Fluxpinel makes each one a
   builtin module (fx_builtin) and calls the same functions here.

   The control of processes is here too: Process.spawn and Kernel#spawn,
   Kernel#system and the backtick, Kernel#exec, and the waits (wait,
   waitpid, wait2, waitpid2, detach). The runtime's sp_process_spawn takes
   the options as the array that spinel's codegen builds; fx_spawn_opts
   builds the same array from the options Hash. */

#include "fluxpinel.h"

#define FX_IS(s) (!strcmp(name, (s)))

static int fx_process_wait(const char *name, int argc, sp_RbVal *argv, sp_RbVal *out);

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
  /* Process.kill(sig, *pids): one send for each pid, counted */
  if (FX_IS("kill") && argc >= 2) {
    sp_int n = 0;
    for (int i = 1; i < argc; i++) n += sp_process_kill1(argv[0], sp_poly_arg_int_chk(argv[i]));
    *out = sp_box_int(n);
    return 1;
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
  if ((FX_IS("spawn") || FX_IS("exec")) && fx_process_kernel(name, argc, argv, out)) return 1;
  return fx_process_wait(name, argc, argv, out);
}

/* ---- spawn, system, the backtick and the waits ---- */

/* A trailing Hash of a call: the keywords, or a Hash in their place. */
static int fx_opts_hash(int argc, sp_RbVal *argv) { return argc >= 2 && fx_is_hash(argv[argc - 1]); }

/* The value of one keyword of an options Hash, or nil for none. */
static sp_RbVal fx_opt_get(sp_RbVal opts, const char *key) {
  sp_bool found;
  sp_RbVal v = sp_poly_hash_get_pair_val(opts, sp_box_sym(sp_sym_intern(key)), &found);
  return found ? v : sp_box_nil();
}

/* One standard descriptor of a spawn (0 in, 1 out, 2 err), as the code
   that spinel emits resolves it (emit_spawn_fd_options): -1 for none, nil
   and false; an Integer; the descriptor of an IO; a filename, which the
   runtime opens and owns; [:child, :out], [:child, :err] or [:child, fd].
   A later slot of a group key ([:out, :err]) shares the first slot's. */
static sp_RbVal fx_spawn_fd(sp_RbVal opts, sp_PolyArray *slots, int slot, int *owned) {
  sp_bool found;
  int prior;
  sp_RbVal v = sp_process_redirect_value(opts, slot, &found, &prior);
  if (!found) return sp_box_int(-1);
  if (prior >= 0) return slots->data[prior];
  v = sp_poly_strbuf_deref(v);
  if (v.tag == SP_TAG_NIL || (v.tag == SP_TAG_BOOL && !v.v.i)) return sp_box_int(-1);
  if (v.tag == SP_TAG_INT) return v;
  if (v.tag == SP_TAG_OBJ && v.cls_id == SP_BUILTIN_IO && v.v.p) return sp_box_int(sp_File_fileno((sp_File *)v.v.p));
  if (v.tag == SP_TAG_STR) return sp_box_int(sp_process_open_redirect(v.v.s, slot, owned));
  if (v.tag == SP_TAG_OBJ && v.cls_id == SP_BUILTIN_POLY_ARRAY) {
    sp_PolyArray *a = (sp_PolyArray *)v.v.p;
    if (a->len < 2 || a->data[0].tag != SP_TAG_SYM || a->data[0].v.i != sp_sym_intern("child"))
      sp_process_spawn_fail(owned, "ArgumentError", "bad child-array shape");
    sp_RbVal fd = a->data[1];
    if (fd.tag == SP_TAG_INT) return sp_box_int((int)fd.v.i);
    if (fd.tag == SP_TAG_SYM) {
      const char *n = sp_sym_to_s((sp_sym)fd.v.i);
      if (!strcmp(n, "out")) return sp_box_int(1);
      if (!strcmp(n, "err")) return sp_box_int(2);
    }
    sp_process_spawn_fail(owned, "ArgumentError", "bad redirect value");
  }
  sp_process_spawn_fail(owned, "ArgumentError", "bad redirect type");
}

/* The options array of sp_process_spawn: [in, out, err, pgroup,
   rlimit_cpu, rlimit_as, chdir, owned, env]. opts is the options Hash or
   nil, env the environment Hash or nil. */
static sp_PolyArray *fx_spawn_opts(sp_RbVal opts, sp_RbVal env) {
  sp_PolyArray *a = sp_PolyArray_new();
  SP_GC_ROOT(a);
  int owned[3] = { -1, -1, -1 };
  if (opts.tag == SP_TAG_NIL) {
    for (int i = 0; i < 3; i++) sp_PolyArray_push(a, sp_box_int(-1));
    for (int i = 3; i < 7; i++) sp_PolyArray_push(a, sp_box_nil());
  } else {
    for (int i = 0; i < 3; i++) sp_PolyArray_push(a, fx_spawn_fd(opts, a, i, owned));
    sp_RbVal v = fx_opt_get(opts, "pgroup");
    sp_PolyArray_push(a, v.tag == SP_TAG_BOOL && v.v.i ? sp_box_int(1) : v.tag == SP_TAG_INT ? v : sp_box_nil());
    v = fx_opt_get(opts, "rlimit_cpu");
    sp_PolyArray_push(a, v.tag == SP_TAG_INT ? v : sp_box_nil());
    v = fx_opt_get(opts, "rlimit_as");
    sp_PolyArray_push(a, v.tag == SP_TAG_INT ? v : sp_box_nil());
    v = sp_poly_strbuf_deref(fx_opt_get(opts, "chdir"));
    sp_PolyArray_push(a, v.tag == SP_TAG_STR ? v : sp_box_nil());
  }
  sp_PolyArray_push(a, sp_box_int((owned[0] >= 0) | ((owned[1] >= 0) << 1) | ((owned[2] >= 0) << 2)));
  sp_PolyArray_push(a, env);
  return a;
}

/* spawn(env, cmd, *args, opts): the pid of the child. A leading Hash is
   the environment, a trailing one the options; the command is a String (a
   command line when it is alone) or [program, argv0]; an Array of Strings
   in its place reads as a general one. */
static sp_int fx_spawn(int argc, sp_RbVal *argv) {
  sp_RbVal env = sp_box_nil(), opts = sp_box_nil();
  SP_GC_ROOT_RBVAL(env);
  SP_GC_ROOT_RBVAL(opts);
  if (fx_opts_hash(argc, argv)) opts = argv[--argc];
  if (argc >= 1 && fx_is_hash(argv[0])) { env = argv[0]; argv++; argc--; }
  if (argc < 1) sp_raise_cls("ArgumentError", "wrong number of arguments (given 0, expected 1+)");
  sp_RbVal cmd = argv[0];
  if (cmd.tag == SP_TAG_OBJ && cmd.cls_id != SP_BUILTIN_POLY_ARRAY && sp_rbval_is_array(cmd))
    cmd = sp_box_poly_array(sp_poly_to_poly_array(cmd));
  SP_GC_ROOT_RBVAL(cmd);
  sp_PolyArray *args = sp_PolyArray_new();
  SP_GC_ROOT(args);
  for (int i = 1; i < argc; i++) sp_PolyArray_push(args, argv[i]);
  sp_PolyArray *o = fx_spawn_opts(opts, env);
  SP_GC_ROOT(o);
  sp_int pid;
  FX_UNLOCKED(pid = sp_process_spawn(cmd, sp_box_poly_array(args), sp_box_poly_array(o)));
  return pid;
}

/* system(env, cmd, *args, opts): a spawn and a wait, as CRuby's and as
   spinel's __sp_system_spawn. true for an exit status of zero, false for
   another, nil when the command cannot run; exception: true raises the
   spawn's Errno and a RuntimeError for a failed status. */
static sp_RbVal fx_system(int argc, sp_RbVal *argv) {
  SP_GC_SAVE();
  int raise_on = 0;
  if (fx_opts_hash(argc, argv)) {
    sp_RbVal e = fx_opt_get(argv[argc - 1], "exception");
    raise_on = e.tag != SP_TAG_NIL && !(e.tag == SP_TAG_BOOL && !e.v.i);
  }
  sp_int pid = -1;
  const int depth = fx_fs()->depth;
  FX_TRY_PUSH();
  if (setjmp(sp_exc_stack[sp_exc_top - 1]) == 0) {
    pid = fx_spawn(argc, argv);
    sp_exc_top--;
  } else {
    FX_TRY_LANDED(depth);
    if (sp_unwind_kind != SP_UNWIND_NONE) sp_unwind_resume();
    FxCaught c = fx_caught();
    sp_RbVal e = fx_exc_box(fx_caught_obj(c)), k = fx_class_box(fx_builtin("SystemCallError"));
    SP_GC_ROOT_RBVAL(e);
    if (raise_on || !fx_send(e, "is_a?", 1, &k).v.i) fx_reraise(c);
    return sp_box_nil();
  }
  FX_UNLOCKED(sp_process_waitpid(pid));
  sp_int st = sp_last_status;
  int ok = sp_process_status_exited_p(st) && sp_process_status_exitstatus(st).v == 0;
  if (raise_on && !ok) {
    int i = argc >= 1 && fx_is_hash(argv[0]) ? 1 : 0;
    const char *cmd = i < argc ? sp_poly_to_s(argv[i]) : "";
    SP_GC_ROOT_STR(cmd);
    const char *how = sp_process_status_exited_p(st) ? sp_sprintf("exit %d", (int)sp_process_status_exitstatus(st).v)
                                                     : sp_process_status_to_s(pid, st, 0);
    SP_GC_ROOT_STR(how);
    sp_raise_cls("RuntimeError", sp_sprintf("Command failed with %s: %s", how, cmd));
  }
  return sp_box_bool(ok);
}

/* The body of the Thread of Process.detach: the wait, whose value is the
   status of the child. */
static sp_RbVal fx_detach_step(void *ctx, int argc, sp_RbVal *argv) {
  (void)argc;
  (void)argv;
  sp_PolyArray *r;
  FX_UNLOCKED_ROOT(r = sp_process_waitpid2((sp_int)(intptr_t)ctx), SP_GC_ROOT(r));
  return r->data[1];
}

/* The functions of Kernel that control processes: spawn, system, exec and
   the backtick, which a backtick literal calls by name. */
static int fx_process_kernel(const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (FX_IS("spawn")) { *out = sp_box_int(fx_spawn(argc, argv)); return 1; }
  if (FX_IS("system")) { *out = fx_system(argc, argv); return 1; }
  if (FX_IS("`") && argc == 1) {
    const char *cmd = sp_poly_arg_str_chk(argv[0]), *s;
    SP_GC_ROOT_STR(cmd);
    FX_UNLOCKED_ROOT(s = sp_backtick(cmd), SP_GC_ROOT_STR(s));
    *out = sp_box_str_new(s);
    return 1;
  }
  if (FX_IS("exec") && argc >= 1) {
    if (fx_is_hash(argv[0]) || fx_opts_hash(argc, argv)) fx_gap("exec with an environment or an options Hash");
    sp_PolyArray *args = sp_PolyArray_new();
    SP_GC_ROOT(args);
    for (int i = 1; i < argc; i++) sp_PolyArray_push(args, argv[i]);
    sp_process_exec(argv[0], sp_box_poly_array(args));
    *out = sp_box_nil();
    return 1;
  }
  return 0;
}

/* Process::Status#success?, which bop_gen leaves out: nil when the child
   did not exit, else whether its status is zero. */
static int fx_process_status_core(sp_RbVal r, const char *name, int argc, sp_RbVal *out) {
  if (r.tag != SP_TAG_OBJ || r.cls_id != SP_BUILTIN_PROCESS_STATUS || !r.v.p) return 0;
  if (FX_IS("success?") && argc == 0) {
    int s = sp_process_status_success_p(((sp_ProcessStatus *)r.v.p)->status);
    *out = s < 0 ? sp_box_nil() : sp_box_bool(s);
    return 1;
  }
  return 0;
}

/* The waits of Process: wait and waitpid answer the pid, wait2 and
   waitpid2 [pid, status]; no pid waits for any child. detach answers a
   Thread whose value is the status. */
static int fx_process_wait(const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if ((FX_IS("wait") || FX_IS("waitpid")) && argc <= 1) {
    sp_int pid = argc ? sp_poly_arg_int_chk(argv[0]) : -1, r;
    FX_UNLOCKED(r = sp_process_waitpid(pid));
    *out = sp_box_int(r);
    return 1;
  }
  if ((FX_IS("wait2") || FX_IS("waitpid2")) && argc <= 1) {
    sp_int pid = argc ? sp_poly_arg_int_chk(argv[0]) : -1;
    sp_PolyArray *r;
    FX_UNLOCKED_ROOT(r = sp_process_waitpid2(pid), SP_GC_ROOT(r));
    *out = sp_box_poly_array(r);
    return 1;
  }
  if (FX_IS("detach") && argc == 1) {
    FxBlock cb = { .cfn = fx_detach_step, .ctx = (void *)(intptr_t)sp_poly_arg_int_chk(argv[0]) };
    *out = fx_send_b(fx_class_box(fx_builtin("Thread")), "new", 0, NULL, &cb);
    return 1;
  }
  if (FX_IS("last_status") && argc == 0) {
    *out = sp_box_nullable_obj((void *)sp_last_process_status(), SP_BUILTIN_PROCESS_STATUS);
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
                                         "getsid", "clock_gettime", "clock_getres", "getpriority", "kill", "spawn",
                                         "exec", "wait", "waitpid", "wait2", "waitpid2", "detach", "last_status",
                                         NULL };
  static const char *const gc[] = { "start", "compact", "stat", "total_time", NULL };
  FxClass *k = fx_class_value(r);
  if (!k || k->cls_id >= 0 || !k->is_module) return 0;
  if (!strcmp(k->name, "Marshal")) return fx_marshal_responds(name);
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
  if (!strcmp(k->name, "Marshal")) return fx_marshal_fn(name, argc, argv, out);
  /* Signal.trap(sig, handler): the block form is in fx_class_core */
  if (!strcmp(k->name, "Signal") && FX_IS("trap") && argc == 2) {
    *out = fx_trap(argc, argv, NULL);
    return 1;
  }
  /* Signal.list: the names and numbers of the signal table of the
     runtime, EXIT first as in CRuby; Signal.signame(no): the name, or nil
     for an unknown number */
  if (!strcmp(k->name, "Signal") && FX_IS("list") && argc == 0) {
    *out = sp_box_obj(sp_signal_list(), SP_BUILTIN_STR_INT_HASH);
    return 1;
  }
  if (!strcmp(k->name, "Signal") && FX_IS("signame") && argc == 1) {
    const char *s = sp_signal_signame(sp_poly_arg_int_chk(argv[0]));
    *out = s ? sp_box_str_new(s) : sp_box_nil();
    return 1;
  }
  /* Kernel.select, the module function of Kernel#select */
  if (!strcmp(k->name, "Kernel") && FX_IS("select") && argc >= 1 && argc <= 4) {
    *out = fx_io_select(argc, argv);
    return 1;
  }
  return 0;
}
