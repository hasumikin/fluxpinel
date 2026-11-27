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
#include <fcntl.h>
#include <sys/resource.h>   /* RLIMIT_* and getrlimit */

#define FX_IS(s) (!strcmp(name, (s)))

static int fx_process_wait(const char *name, int argc, sp_RbVal *argv, sp_RbVal *out);

/* The resources of getrlimit and setrlimit that the platform names, as
   CRuby defines the constants it finds. */
static const struct { const char *name; int v; } fx_rlimits[] = {
#ifdef RLIMIT_AS
  { "AS", RLIMIT_AS },
#endif
#ifdef RLIMIT_CORE
  { "CORE", RLIMIT_CORE },
#endif
#ifdef RLIMIT_CPU
  { "CPU", RLIMIT_CPU },
#endif
#ifdef RLIMIT_DATA
  { "DATA", RLIMIT_DATA },
#endif
#ifdef RLIMIT_FSIZE
  { "FSIZE", RLIMIT_FSIZE },
#endif
#ifdef RLIMIT_MEMLOCK
  { "MEMLOCK", RLIMIT_MEMLOCK },
#endif
#ifdef RLIMIT_MSGQUEUE
  { "MSGQUEUE", RLIMIT_MSGQUEUE },
#endif
#ifdef RLIMIT_NICE
  { "NICE", RLIMIT_NICE },
#endif
#ifdef RLIMIT_NOFILE
  { "NOFILE", RLIMIT_NOFILE },
#endif
#ifdef RLIMIT_NPROC
  { "NPROC", RLIMIT_NPROC },
#endif
#ifdef RLIMIT_RSS
  { "RSS", RLIMIT_RSS },
#endif
#ifdef RLIMIT_RTPRIO
  { "RTPRIO", RLIMIT_RTPRIO },
#endif
#ifdef RLIMIT_RTTIME
  { "RTTIME", RLIMIT_RTTIME },
#endif
#ifdef RLIMIT_SBSIZE
  { "SBSIZE", RLIMIT_SBSIZE },
#endif
#ifdef RLIMIT_SIGPENDING
  { "SIGPENDING", RLIMIT_SIGPENDING },
#endif
#ifdef RLIMIT_STACK
  { "STACK", RLIMIT_STACK },
#endif
};

/* An rlim_t as an Integer: unsigned, so a value past sp_int (RLIM_INFINITY
   on Linux) is a Bignum, as CRuby gives it. */
static sp_RbVal fx_box_rlim(rlim_t v) {
  if ((unsigned long long)v <= (unsigned long long)INT64_MAX) return sp_box_int((sp_int)v);
  unsigned char b[8];
  for (int i = 0; i < 8; i++) b[i] = (unsigned char)(((unsigned long long)v >> (8 * i)) & 0xff);
  return sp_box_bigint(sp_bigint_from_le_bytes(0, b, 8));
}

/* A limit given to setrlimit, as CRuby reads one: an Integer of the width
   of rlim_t (a Bignum up to 2**64 - 1), a Float, or one of the names
   INFINITY, SAVED_MAX and SAVED_CUR as a Symbol or a String, with no
   prefix; RangeError for an Integer outside the type, ArgumentError for
   another name. */
static rlim_t fx_rlim_arg(sp_RbVal v) {
  if (v.tag == SP_TAG_BIGINT) {
    sp_Bigint *b = (sp_Bigint *)v.v.p;
    unsigned char bytes[sizeof(rlim_t)] = { 0 };
    if (sp_bigint_sign(b) < 0 || sp_bigint_to_le_bytes(b, bytes, sizeof bytes) > sizeof bytes)
      sp_raise_cls("RangeError", "bignum too big to convert into 'unsigned long long'");
    unsigned long long u = 0;
    for (size_t i = 0; i < sizeof bytes; i++) u |= (unsigned long long)bytes[i] << (8 * i);
    return (rlim_t)u;
  }
  if (v.tag == SP_TAG_SYM || fx_is_string(v)) {
    const char *n = v.tag == SP_TAG_SYM ? sp_sym_to_s((sp_sym)v.v.i) : sp_poly_recv_s(v, "setrlimit");
    SP_GC_ROOT_STR(n);
    if (!strcmp(n, "INFINITY")) return RLIM_INFINITY;
#ifdef RLIM_SAVED_MAX
    if (!strcmp(n, "SAVED_MAX")) return RLIM_SAVED_MAX;
#endif
#ifdef RLIM_SAVED_CUR
    if (!strcmp(n, "SAVED_CUR")) return RLIM_SAVED_CUR;
#endif
    sp_raise_cls("ArgumentError", sp_sprintf("invalid resource value: %s", n));
  }
  if (v.tag == SP_TAG_FLT) return (rlim_t)v.v.f;
  return (rlim_t)sp_poly_arg_int_chk(v);
}

/* The resource of getrlimit and setrlimit: an Integer, or a Symbol or a
   String that names one without the RLIMIT_ prefix; ArgumentError for a
   name of no resource, or with a NUL byte, as CRuby words them. */
static int fx_rlimit_resource(sp_RbVal v) {
  if (v.tag != SP_TAG_SYM && !fx_is_string(v)) return (int)sp_poly_arg_int_chk(v);
  const char *n = v.tag == SP_TAG_SYM ? sp_sym_to_s((sp_sym)v.v.i) : sp_poly_recv_s(v, "getrlimit");
  SP_GC_ROOT_STR(n);
  if (strlen(n) != sp_str_byte_len(n)) sp_raise_cls("ArgumentError", "string contains null byte");
  for (size_t i = 0; i < sizeof fx_rlimits / sizeof fx_rlimits[0]; i++)
    if (!strcmp(n, fx_rlimits[i].name)) return fx_rlimits[i].v;
  sp_raise_cls("ArgumentError", sp_sprintf("invalid resource name: %s", n));
}

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
    /* the flags of a wait (sys/wait.h, through the runtime's header) */
    { "WNOHANG", WNOHANG },
    { "WUNTRACED", WUNTRACED },
  };
  for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
    fx_const_set(c, sp_sym_intern(k[i].name), sp_box_int(k[i].v));
  for (size_t i = 0; i < sizeof fx_rlimits / sizeof fx_rlimits[0]; i++)
    fx_const_set(c, sp_sym_intern(sp_sprintf("RLIMIT_%s", fx_rlimits[i].name)), sp_box_int(fx_rlimits[i].v));
  /* the Bignum first, rooted, since C may make the Symbol after it */
  sp_RbVal inf = fx_box_rlim(RLIM_INFINITY);
  SP_GC_ROOT_RBVAL(inf);
  fx_const_set(c, sp_sym_intern("RLIM_INFINITY"), inf);
#ifdef RLIM_SAVED_MAX
  inf = fx_box_rlim(RLIM_SAVED_MAX);
  fx_const_set(c, sp_sym_intern("RLIM_SAVED_MAX"), inf);
#endif
#ifdef RLIM_SAVED_CUR
  inf = fx_box_rlim(RLIM_SAVED_CUR);
  fx_const_set(c, sp_sym_intern("RLIM_SAVED_CUR"), inf);
#endif
}

/* The constants of Fcntl (require "fcntl"): the commands and flags of
   fcntl(2) and open(2) that CRuby's fcntl defines, with the values of the
   platform. The module is made when the require has run (fx_builtin). */
static void fx_fcntl_consts(FxClass *c) {
  static const struct { const char *name; sp_int v; } k[] = {
    { "F_DUPFD", F_DUPFD }, { "F_GETFD", F_GETFD }, { "F_SETFD", F_SETFD }, { "F_GETFL", F_GETFL },
    { "F_SETFL", F_SETFL }, { "F_GETLK", F_GETLK }, { "F_SETLK", F_SETLK }, { "F_SETLKW", F_SETLKW },
    { "FD_CLOEXEC", FD_CLOEXEC }, { "F_RDLCK", F_RDLCK }, { "F_WRLCK", F_WRLCK }, { "F_UNLCK", F_UNLCK },
#ifdef F_DUPFD_CLOEXEC
    { "F_DUPFD_CLOEXEC", F_DUPFD_CLOEXEC },
#endif
#ifdef F_DUP2FD
    { "F_DUP2FD", F_DUP2FD },
#endif
    { "O_CREAT", O_CREAT }, { "O_EXCL", O_EXCL }, { "O_NOCTTY", O_NOCTTY }, { "O_TRUNC", O_TRUNC },
    { "O_APPEND", O_APPEND }, { "O_NONBLOCK", O_NONBLOCK }, { "O_NDELAY", O_NDELAY }, { "O_RDONLY", O_RDONLY },
    { "O_RDWR", O_RDWR }, { "O_WRONLY", O_WRONLY }, { "O_ACCMODE", O_ACCMODE },
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
  int64_t ns = FX_IS("clock_getres") ? sp_process_clock_res_ns(id) : sp_process_clock_units(id, 1);
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

/* Process.setproctitle writes the title over the memory of the command
   line, the strings of argv and the environment strings that follow them,
   as CRuby does where the system has no setproctitle(3)
   (missing/setproctitle.c): ps reads that memory. fx_proctitle_init runs
   first in main and copies the strings of argv and environ out of it, so
   that $0, ARGV and ENV keep theirs. A title is cut to 1023 bytes (the
   buffer of CRuby's setproctitle) and to what the memory holds; the rest
   of the strings of argv is zeroed, as in CRuby, so that ps shows the
   title alone. */
static char *fx_title_mem;        /* the memory of the command line */
static size_t fx_title_argv_len;  /* the strings of argv in it */
static size_t fx_title_len;       /* with the environment strings after them */
static const char *fx_argv0;      /* Process.argv0: $0 as the program started */

static void fx_proctitle_init(int argc, char **argv) {
  extern char **environ;
  if (argc == 0 || !argv[0]) return;
  char *last = NULL;
  for (int i = 0; i < argc; i++)
    if (!last || last + 1 == argv[i]) last = argv[i] + strlen(argv[i]);
  char *last_env = last;
  int n = 0;
  for (; environ[n]; n++)
    if (last_env + 1 == environ[n]) last_env = environ[n] + strlen(environ[n]);
  fx_title_mem = argv[0];
  fx_title_argv_len = (size_t)(last - argv[0]);
  fx_title_len = (size_t)(last_env - argv[0]);
  for (int i = 0; i < argc; i++) argv[i] = strdup(argv[i]);
  char **env = calloc((size_t)n + 1, sizeof *env);
  for (int i = 0; i < n; i++) env[i] = strdup(environ[i]);
  environ = env;
}

static void fx_proctitle_set(const char *title) {
  if (!fx_title_len) return;
  size_t len = strlen(title);
  if (len > 1023) len = 1023;
  size_t n = len < fx_title_len ? len : fx_title_len - 1;
  memcpy(fx_title_mem, title, n);
  fx_title_mem[n] = '\0';
  size_t pad = len > fx_title_argv_len ? fx_title_len : fx_title_argv_len;
  for (len = n + 1; len < pad; len++) fx_title_mem[len] = '\0';
}

static int fx_process_fn(const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  /* Process.exit, exit! and abort are Kernel's (fx_exit_kernel) */
  if (FX_IS("exit") || FX_IS("exit!") || FX_IS("abort")) return fx_exit_kernel(name, argc, argv);
  if (argc == 0) {
    if (FX_IS("pid")) { *out = sp_box_int((sp_int)getpid()); return 1; }
    if (FX_IS("ppid")) { *out = sp_box_int(sp_process_ppid()); return 1; }
    if (FX_IS("argv0")) {
      sp_RbVal s = fx_new_string((const uint8_t *)fx_argv0, strlen(fx_argv0));
      *out = fx_send(s, "freeze", 0, NULL);
      return 1;
    }
    if (FX_IS("uid")) { *out = sp_box_int((sp_int)getuid()); return 1; }
    if (FX_IS("gid")) { *out = sp_box_int((sp_int)getgid()); return 1; }
    if (FX_IS("euid")) { *out = sp_box_int((sp_int)geteuid()); return 1; }
    if (FX_IS("egid")) { *out = sp_box_int((sp_int)getegid()); return 1; }
    if (FX_IS("getpgrp")) { *out = sp_box_int((sp_int)getpgrp()); return 1; }
    if (FX_IS("fork")) { *out = fx_fork(NULL); return 1; }
    if (FX_IS("times")) { *out = sp_box_tms(sp_process_times()); return 1; }
    if (FX_IS("groups")) { *out = sp_box_int_array(sp_process_groups()); return 1; }
    /* a session of its own; the runtime has no function for the process
       group calls (spinel refuses them), so they are the system calls */
    if (FX_IS("setsid")) {
      pid_t r = setsid();
      if (r < 0) sp_raise_cls(sp_errno_class_name(errno), strerror(errno));
      *out = sp_box_int((sp_int)r);
      return 1;
    }
  }
  /* Process.getrlimit(resource) and setrlimit(resource, soft, hard = soft):
     the runtime has neither, so they are the system calls */
  if (FX_IS("getrlimit") && argc == 1) {
    struct rlimit rl;
    if (getrlimit(fx_rlimit_resource(argv[0]), &rl) < 0)
      sp_raise_cls(sp_errno_class_name(errno), sp_sprintf("%s - getrlimit", strerror(errno)));
    sp_PolyArray *a = sp_PolyArray_new();
    SP_GC_ROOT(a);
    sp_PolyArray_push(a, fx_box_rlim(rl.rlim_cur));
    sp_PolyArray_push(a, fx_box_rlim(rl.rlim_max));
    *out = sp_box_poly_array(a);
    return 1;
  }
  if (FX_IS("setrlimit") && (argc == 2 || argc == 3)) {
    int res = fx_rlimit_resource(argv[0]);
    /* a hard limit left out, or nil, is the soft one */
    struct rlimit rl;
    rl.rlim_cur = fx_rlim_arg(argv[1]);
    rl.rlim_max = argc == 3 && argv[2].tag != SP_TAG_NIL ? fx_rlim_arg(argv[2]) : rl.rlim_cur;
    if (setrlimit(res, &rl) < 0) sp_raise_cls(sp_errno_class_name(errno), sp_sprintf("%s - setrlimit", strerror(errno)));
    *out = sp_box_nil();
    return 1;
  }
  if (FX_IS("setproctitle") && argc == 1) {
    /* the String, or what to_str answers: CRuby's StringValue */
    sp_RbVal s = fx_to_str_arg(argv[0]);
    if (!fx_is_string(s))
      sp_raise_cls("TypeError", sp_sprintf("no implicit conversion of %s into String",
                                           s.tag == SP_TAG_NIL ? "nil" : sp_poly_class_name(s)));
    fx_proctitle_set(sp_poly_recv_s(s, "setproctitle"));
    *out = s;
    return 1;
  }
  if (FX_IS("getpgid") && argc == 1) {
    pid_t r = getpgid((pid_t)sp_poly_arg_int_chk(argv[0]));
    if (r < 0) sp_raise_cls(sp_errno_class_name(errno), strerror(errno));
    *out = sp_box_int((sp_int)r);
    return 1;
  }
  if (FX_IS("setpgid") && argc == 2) {
    if (setpgid((pid_t)sp_poly_arg_int_chk(argv[0]), (pid_t)sp_poly_arg_int_chk(argv[1])) < 0)
      sp_raise_cls(sp_errno_class_name(errno), strerror(errno));
    *out = sp_box_int(0);
    return 1;
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
   A later slot of a group key ([:out, :err]) shares the first slot's.
   fx_spawn_slot makes each once. */
static sp_RbVal fx_spawn_fd(sp_RbVal opts, sp_PolyArray *slots, int slot, int *owned, int *done) {
  sp_bool found;
  int prior, cfd;
  sp_RbVal v = sp_process_redirect_value(opts, slot, &found, &prior);
  if (!found) return sp_box_int(-1);
  if (prior >= 0) return fx_spawn_slot(opts, slots, prior, owned, done);
  if (v.tag == SP_TAG_NIL || (v.tag == SP_TAG_BOOL && !v.v.i)) return sp_box_int(-1);
  if (v.tag == SP_TAG_INT) return v;
  if (v.tag == SP_TAG_OBJ && v.cls_id == SP_BUILTIN_IO && v.v.p) return sp_box_int(sp_File_fileno((sp_File *)v.v.p));
  if (fx_is_string(v) && v.v.p)
    return sp_box_int(sp_process_open_redirect(sp_String_cstr((sp_String *)v.v.p), slot, owned));
  if (fx_child_fd(v, &cfd)) {
    /* [:child, fd]: the descriptor of the child after its own redirections,
       so the source of the option that redirects it, in whichever order
       the options name them (fx_child_check saw that one does) */
    if (cfd < 0) sp_process_spawn_fail(owned, "ArgumentError", "bad redirect value");
    if (cfd > 2) fx_gap(sp_sprintf("a redirection to descriptor %d of the child", cfd));
    return fx_spawn_slot(opts, slots, cfd, owned, done);
  }
  if (v.tag == SP_TAG_OBJ && v.cls_id == SP_BUILTIN_POLY_ARRAY)
    sp_process_spawn_fail(owned, "ArgumentError", "bad child-array shape");
  sp_process_spawn_fail(owned, "ArgumentError", "bad redirect type");
}

/* The value of one standard slot, made once: a slot that names this one
   ([:child, fd], a later member of a group) reads what it made, whichever
   order the options come in. */
static sp_RbVal fx_spawn_slot(sp_RbVal opts, sp_PolyArray *slots, int slot, int *owned, int *done) {
  if (!done[slot]) {
    done[slot] = 1;
    sp_PolyArray_set(slots, slot, fx_spawn_fd(opts, slots, slot, owned, done));
  }
  return slots->data[slot];
}

/* The options Hash with a standard descriptor named by its number (`1 =>
   w`) or by an IO on it (`$stdout => w`) renamed to the Symbol the
   runtime reads (:in, :out, :err), as CRuby takes all three forms; the Hash
   itself when no key is of those kinds. A number past 2 stays as it is,
   for the caller to refuse. */
static sp_RbVal fx_spawn_opts_keys(sp_RbVal opts) {
  static const char *const names[] = { "in", "out", "err" };
  sp_int n = sp_poly_length(opts);
  int renamed = 0;
  for (sp_int j = 0; j < n && !renamed; j++) {
    sp_RbVal key, value;
    sp_poly_hash_pair(opts, j, &key, &value);
    renamed = (key.tag == SP_TAG_INT && key.v.i >= 0 && key.v.i <= 2) ||
              (key.tag == SP_TAG_OBJ && key.cls_id == SP_BUILTIN_IO && key.v.p &&
               sp_File_fileno((sp_File *)key.v.p) <= 2);
  }
  if (!renamed) return opts;
  sp_PolyPolyHash *h = sp_PolyPolyHash_new();
  sp_RbVal hv = sp_box_obj(h, SP_BUILTIN_POLY_POLY_HASH);
  SP_GC_ROOT_RBVAL(hv);
  for (sp_int j = 0; j < n; j++) {
    sp_RbVal key, value;
    sp_poly_hash_pair(opts, j, &key, &value);
    int fd = key.tag == SP_TAG_INT ? (int)key.v.i
           : key.tag == SP_TAG_OBJ && key.cls_id == SP_BUILTIN_IO && key.v.p ? sp_File_fileno((sp_File *)key.v.p)
           : -1;
    if (fd >= 0 && fd <= 2) key = sp_box_sym(sp_sym_intern(names[fd]));
    sp_PolyPolyHash_set(h, key, value);
  }
  return hv;
}

/* The options array of sp_process_spawn: [in, out, err, pgroup,
   rlimit_cpu, rlimit_as, chdir, owned, env]. opts is the options Hash or
   nil, env the environment Hash or nil. */
static sp_PolyArray *fx_spawn_opts(sp_RbVal opts, sp_RbVal env) {
  SP_GC_ROOT_RBVAL(opts);
  SP_GC_ROOT_RBVAL(env);
  if (opts.tag != SP_TAG_NIL) opts = fx_spawn_opts_keys(opts);
  sp_PolyArray *a = sp_PolyArray_new();
  SP_GC_ROOT(a);
  int owned[3] = { -1, -1, -1 };
  if (opts.tag == SP_TAG_NIL) {
    for (int i = 0; i < 3; i++) sp_PolyArray_push(a, sp_box_int(-1));
    for (int i = 3; i < 7; i++) sp_PolyArray_push(a, sp_box_nil());
  } else {
    int done[3] = { 0, 0, 0 };
    fx_child_check(opts);
    for (int i = 0; i < 3; i++) sp_PolyArray_push(a, sp_box_int(-1));
    for (int i = 0; i < 3; i++) fx_spawn_slot(opts, a, i, owned, done);
    sp_RbVal v = fx_opt_get(opts, "pgroup");
    sp_PolyArray_push(a, v.tag == SP_TAG_BOOL && v.v.i ? sp_box_int(1) : v.tag == SP_TAG_INT ? v : sp_box_nil());
    v = fx_opt_get(opts, "rlimit_cpu");
    sp_PolyArray_push(a, v.tag == SP_TAG_INT ? v : sp_box_nil());
    v = fx_opt_get(opts, "rlimit_as");
    sp_PolyArray_push(a, v.tag == SP_TAG_INT ? v : sp_box_nil());
    v = fx_opt_get(opts, "chdir");
    sp_PolyArray_push(a, fx_is_string(v) ? v : sp_box_nil());
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
  if (opts.tag != SP_TAG_NIL) fx_spawn_opts_check(opts);
  sp_RbVal cmd = argv[0];
  if (cmd.tag == SP_TAG_OBJ && cmd.cls_id != SP_BUILTIN_POLY_ARRAY && sp_rbval_is_array(cmd))
    cmd = sp_box_poly_array(sp_poly_to_poly_array(cmd));
  SP_GC_ROOT_RBVAL(cmd);
  /* The program, the pair [program, argv0] and each argument convert in
     their order, as spinel emits for system: a to_str of the program runs,
     a null byte is an ArgumentError, and each value is a copy that a later
     conversion does not change (sp_system_arg_box). */
  if (fx_is_array(cmd)) {
    sp_PolyArray *pair = sp_PolyArray_new();
    sp_RbVal pv = sp_box_poly_array(pair);
    SP_GC_ROOT_RBVAL(pv);
    /* the values of the pair are read first: a to_str of the program can
       change the Array that holds the pair */
    sp_int n = sp_poly_length(cmd);
    for (sp_int i = 0; i < n; i++) sp_PolyArray_push(pair, sp_poly_arr_get(cmd, i));
    for (sp_int i = 0; i < n; i++) sp_PolyArray_set(pair, i, sp_system_arg_box(pair->data[i]));
    cmd = pv;
  } else {
    cmd = sp_system_arg_box(cmd);
  }
  sp_PolyArray *args = sp_PolyArray_new();
  SP_GC_ROOT(args);
  for (int i = 1; i < argc; i++) sp_PolyArray_push(args, sp_system_arg_box(argv[i]));
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
    sp_RbVal e = fx_exc_box(fx_caught_obj(&c)), k = fx_class_box(fx_builtin("SystemCallError"));
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
/* The end of a forked child, as the end of the main program: the at_exit
   hooks, which read the status, then the scheduler, then exit(3), or the
   signal of a SignalException nothing rescued: the child dies of it, as
   CRuby's does (rb_ec_cleanup raises it again), so the parent reads
   signaled? and termsig, as a shell does for a killed job. */
static __attribute__((noreturn)) void fx_child_exit(int status, int sig) {
  int rc = sp_at_exit_run(status);
  fx_lock_set_depth(0);
  fx_sched_end();
  if (sig) {
    fflush(NULL);
    signal(sig, SIG_DFL);
    raise(sig);
  }
  exit(rc);
}

/* The signal of a SignalException, or 0 when none can be read: the number
   the runtime put beside it (a SIGINT or SIGTERM delivered, Interrupt.new),
   else the message the program gave to raise, a name with or without the
   SIG or a number (`raise SignalException, 15`). Fluxpinel checks the name
   only here, so this raises nothing: a raise on the landing side would reach
   the frames of the parent that the child copied. A child with no signal
   to die of ends as with any other exception. */
static int fx_signal_of(sp_Exception *e, const char *cls, const char *msg) {
  if (e && e->xkey.tag == SP_TAG_INT) return (int)e->xkey.v.i;
  if (!strcmp(cls, "Interrupt")) return SIGINT;
  if (!msg || !msg[0]) return 0;
  char *end;
  long no = strtol(msg, &end, 10);
  if (end != msg && !*end) return no > 0 && no < NSIG && sp_signal_signame((sp_int)no) ? (int)no : 0;
  const char *nm = !strncmp(msg, "SIG", 3) ? msg + 3 : msg;
  for (int i = 1; i < NSIG; i++) {
    const char *s = sp_signal_signame(i);
    if (s && !strcmp(s, nm)) return i;
  }
  return 0;
}

/* Kernel#fork and Process.fork. The child runs the block and ends as the
   main program ends. A raise that leaves the block ends the child with the
   report of an uncaught exception and status 1, with a SystemExit's
   status, or by the signal of a SignalException (reported only for an
   Interrupt, as CRuby reports it), and never reaches the frames of the
   parent that the child
   copied, as CRuby keeps it in the child (rb_protect in rb_fork_ruby).
   Without a block, the pid in the parent and nil in the child. What
   $stdout and $stderr hold is written before the fork, so it goes out
   once. The fork is the runtime's, which leaves the child a sound
   scheduler; the walker lock is given up for it, as for a wait. */
extern int sp_sched_fork(void);   /* lib/sp_sched.c of spinel */
static sp_RbVal fx_fork(const FxBlock *blk) {
  SP_GC_SAVE();
  fx_send(fx_gvar_get("$stdout"), "flush", 0, NULL);
  fx_send(fx_gvar_get("$stderr"), "flush", 0, NULL);
  fflush(NULL);
  int pid;
  FX_UNLOCKED(pid = sp_sched_fork());
  if (pid < 0) sp_raise_cls(sp_errno_class_name(errno), strerror(errno));
  if (pid > 0) return sp_box_int((sp_int)pid);
  if (!blk) return sp_box_nil();
  int st = 0, sig = 0;
  FX_TRY_PUSH();
  if (setjmp(sp_exc_stack[sp_exc_top - 1]) == 0) {
    fx_yield(blk, 0, NULL);
    sp_exc_top--;
  } else {
    FX_TRY_LANDED(0);
    FxCaught c = fx_caught();
    const char *cls = c.cls ? c.cls : "RuntimeError";
    sp_Exception *e = fx_caught_obj(&c);
    SP_GC_ROOT(e);
    if (sp_exc_cls_matches(cls, "SystemExit")) {
      st = sp_exc_exit_status(e);
    } else if (sp_exc_cls_matches(cls, "SignalException")) {
      sig = fx_signal_of(e, cls, c.msg);
      if (!sig || strcmp(cls, "SignalException")) sp_exc_print_uncaught(cls, c.msg, e->backtrace);
      st = 1;
    } else {
      sp_exc_print_uncaught(cls, c.msg, e->backtrace);
      st = 1;
    }
    /* the hooks read it as `$!`, as after an uncaught raise of the main program */
    sp_at_exit_errinfo = sp_exc_ending_obj(cls, c.msg, e, sp_pending_cause);
    sp_pending_cause = NULL;
  }
  L.ctl = FX_CTL_NONE;
  fx_child_exit(st, sig);
}

/* The descriptor a redirection names by its number or by an IO on it, or
   -1 for another value. */
static int fx_fd_of(sp_RbVal v) {
  if (v.tag == SP_TAG_INT) return v.v.i >= 0 && v.v.i <= INT_MAX ? (int)v.v.i : -1;
  if (v.tag == SP_TAG_OBJ && v.cls_id == SP_BUILTIN_IO && v.v.p) return sp_File_fileno((sp_File *)v.v.p);
  return -1;
}

/* Whether v is [:child, fd], a descriptor of the child; *fd is its number
   (:in, :out and :err are 0, 1 and 2), or -1 for a value that names none. */
static int fx_child_fd(sp_RbVal v, int *fd) {
  if (!(v.tag == SP_TAG_OBJ && v.cls_id == SP_BUILTIN_POLY_ARRAY)) return 0;
  sp_PolyArray *a = (sp_PolyArray *)v.v.p;
  if (a->len < 1 || a->data[0].tag != SP_TAG_SYM || a->data[0].v.i != sp_sym_intern("child")) return 0;
  *fd = a->len > 1 ? fx_fd_of(a->data[1]) : -1;
  if (a->len > 1 && a->data[1].tag == SP_TAG_SYM) {
    const char *n = sp_sym_to_s((sp_sym)a->data[1].v.i);
    *fd = !strcmp(n, "in") ? 0 : !strcmp(n, "out") ? 1 : !strcmp(n, "err") ? 2 : -1;
  }
  return 1;
}

/* The descriptor that a key of the options redirects: a number, an IO by
   its number, or :in, :out and :err; -1 for another key. */
static int fx_redirect_key_fd(sp_RbVal key) {
  int fd = fx_fd_of(key);
  if (fd >= 0 || key.tag != SP_TAG_SYM) return fd;
  const char *n = sp_sym_to_s((sp_sym)key.v.i);
  return !strcmp(n, "in") ? 0 : !strcmp(n, "out") ? 1 : !strcmp(n, "err") ? 2 : -1;
}

/* The value of the option that redirects the descriptor fd of the child,
   by its key or by a member of a group key, in *v; 0 when none does. */
static int fx_redirect_of(sp_RbVal opts, int fd, sp_RbVal *v) {
  for (sp_int j = 0, n = sp_poly_length(opts); j < n; j++) {
    sp_RbVal key, value;
    sp_poly_hash_pair(opts, j, &key, &value);
    int hit = fx_redirect_key_fd(key) == fd;
    if (!hit && sp_rbval_is_array(key))
      for (sp_int i = 0, len = sp_poly_length(key); i < len && !hit; i++) hit = fx_redirect_key_fd(sp_poly_arr_get(key, i)) == fd;
    if (hit) { *v = value; return 1; }
  }
  return 0;
}

/* The descriptors of the child that the options name ([:child, fd]), as
   CRuby checks them before anything opens: the one named must be
   redirected by an option of the call, through other such names at most,
   else the ArgumentError of CRuby, which names the descriptor; the first
   name in a cycle is the one reported. */
static void fx_child_check(sp_RbVal opts) {
  sp_int n = sp_poly_length(opts);
  for (sp_int j = 0; j < n; j++) {
    sp_RbVal key, value, v;
    int fd, cur;
    sp_poly_hash_pair(opts, j, &key, &value);
    if (!fx_child_fd(value, &fd) || fd < 0) continue;
    cur = fd;
    for (sp_int depth = 0;; depth++) {
      if (!fx_redirect_of(opts, cur, &v)) sp_raise_cls("ArgumentError", sp_sprintf("child fd %d is not redirected", fd));
      if (!fx_child_fd(v, &cur) || cur < 0) break;
      if (depth >= n) sp_raise_cls("ArgumentError", sp_sprintf("cyclic child fd redirection from %d", fd));
    }
  }
}

/* The options of spawn and system: a redirection of a descriptor past 2
   has no slot in the options array of the runtime (an exec makes its own,
   fx_exec_fds), so it stops as a gap rather than go unread. */
static void fx_spawn_opts_check(sp_RbVal opts) {
  for (sp_int j = 0, n = sp_poly_length(opts); j < n; j++) {
    sp_RbVal key, value;
    sp_poly_hash_pair(opts, j, &key, &value);
    int fd = fx_fd_of(key);
    if (fd > 2) fx_gap(sp_sprintf("spawn with a redirection of descriptor %d", fd));
    if (sp_rbval_is_array(key))
      for (sp_int i = 0, len = sp_poly_length(key); i < len; i++)
        if ((fd = fx_fd_of(sp_poly_arr_get(key, i))) > 2) fx_gap(sp_sprintf("spawn with a redirection of descriptor %d", fd));
  }
}

/* A file value of a redirection past 2, as File.open reads it: [path,
   mode] or [path, flags, perm], or a bare path, which is opened to read
   (CRuby reads a path under a descriptor past 2). An element past the third
   is not read, as CRuby reads none. The open itself is the caller's, once
   every value of the call is read, so a value that raises leaves nothing
   open. */
static const char *fx_exec_file(sp_RbVal v, int *fl, int *perm) {
  sp_RbVal path = v, mode = sp_box_nil(), pm = sp_box_nil();
  if (sp_rbval_is_array(v)) {
    sp_int len = sp_poly_length(v);
    path = len > 0 ? sp_poly_arr_get(v, 0) : sp_box_nil();
    if (len > 1) mode = sp_poly_arr_get(v, 1);
    if (len > 2) pm = sp_poly_arr_get(v, 2);
  }
  const char *p = sp_poly_arg_str_chk(path);
  *fl = O_RDONLY;
  if (mode.tag == SP_TAG_INT) *fl = (int)mode.v.i;
  else if (mode.tag == SP_TAG_FLT) *fl = (int)mode.v.f; /* a number is the flags, as CRuby's NUM2INT reads it */
  else if (mode.tag != SP_TAG_NIL) {
    const char *m = sp_poly_arg_str_chk(mode);
    *fl = sp_io_mode_open_flags(m);
    if (*fl < 0) sp_raise_cls("ArgumentError", sp_sprintf("invalid access mode %s", m));
  }
  *perm = pm.tag == SP_TAG_NIL ? 0644 : (int)sp_poly_arg_int_chk(pm);
  return p;
}

/* The redirections of an exec to a descriptor past 2 (`3 => io`,
   `io3 => 4`, `3 => [path, "w"]`, `3 => :close`), which have no slot in
   the options array of the runtime. An exec applies its options to this
   process, which becomes the command, so the descriptors are made here,
   ahead of the runtime's own: a file is opened, each source is copied
   aside, so one redirection reads the descriptor another one replaces as
   it was before the call (`3 => r4, 4 => r3`), and a standard stream sent
   to one of them is the stream of before (`1 => w, 3 => $stdout`). A
   descriptor opened here is closed once copied, so the program gets the
   copy alone; a :close is done after the copies land, as CRuby closes
   after its dup2s. The copy made by dup2 stays open across the exec. As
   for the standard streams, the program gets a blocking descriptor. An
   exec that fails leaves them as made, as CRuby does. */
#define FX_EXEC_FDS 16
#define FX_EXEC_CLOSE (-2)
static void fx_exec_fds(sp_RbVal opts) {
  int dst[FX_EXEC_FDS], src[FX_EXEC_FDS], tmp[FX_EXEC_FDS], fl[FX_EXEC_FDS], perm[FX_EXEC_FDS], child[FX_EXEC_FDS];
  int n = 0, floor = 3;
  const char *path[FX_EXEC_FDS];
  sp_int len = sp_poly_length(opts);
  for (sp_int j = 0; j < len; j++) {
    sp_RbVal key, value;
    sp_poly_hash_pair(opts, j, &key, &value);
    int fd = fx_fd_of(key);
    if (fd <= 2) continue;
    if (n == FX_EXEC_FDS) fx_gap("exec with more than 16 redirections of a descriptor past 2");
    dst[n] = fd;
    path[n] = NULL;
    child[n] = -1;
    if (value.tag == SP_TAG_SYM && value.v.i == sp_sym_intern("close")) src[n] = FX_EXEC_CLOSE;
    else if (fx_child_fd(value, &child[n])) {
      if (child[n] < 0) sp_raise_cls("ArgumentError", "bad redirect value");
      src[n] = -1;
    } else if ((src[n] = fx_fd_of(value)) < 0) path[n] = fx_exec_file(value, &fl[n], &perm[n]);
    n++;
    if (fd >= floor) floor = fd + 1;
  }
  if (n == 0) return;
  /* the runtime applies a standard stream after these: one that reads a
     descriptor made or closed here would not read the one of before */
  for (sp_int j = 0; j < len; j++) {
    sp_RbVal key, value;
    sp_poly_hash_pair(opts, j, &key, &value);
    if (fx_fd_of(key) > 2) continue;
    int from = fx_fd_of(value);
    if (sp_rbval_is_array(value) && sp_poly_length(value) == 2) from = fx_fd_of(sp_poly_arr_get(value, 1));
    for (int i = 0; i < n; i++)
      if (from == dst[i]) fx_gap(sp_sprintf("exec with a standard stream sent to descriptor %d, which the call replaces", from));
  }
  for (int i = 0; i < n; i++) {
    if (!path[i]) continue;
    /* (close-on-exec: the descriptor is closed below, and a fork of another
       thread before that does not hand it to a command) */
    src[i] = open(path[i], fl[i] | O_CLOEXEC, (mode_t)perm[i]);
    if (src[i] < 0) {
      int e = errno;
      for (int k = 0; k < i; k++) if (path[k]) close(src[k]);
      sp_raise_cls(sp_errno_class_name(e), sp_sprintf("%s - %s", strerror(e), path[i]));
    }
  }
  /* a descriptor of the child ([:child, fd]): the source of the option that
     redirects fd, a descriptor made above or a standard stream sent to an
     IO or a number (fx_child_check saw that an option redirects it). A
     standard stream sent to a file is the runtime's to open, later, so its
     descriptor is not known here. A pass per entry follows a chain. */
  for (int pass = 0; pass < n; pass++)
    for (int i = 0; i < n; i++) {
      if (child[i] < 0 || src[i] >= 0) continue;
      int c = child[i], k = -1;
      for (int j = 0; j < n; j++) if (dst[j] == c) k = j;
      if (k >= 0) {
        if (src[k] >= 0) src[i] = src[k];
        else if (src[k] == FX_EXEC_CLOSE)
          fx_gap(sp_sprintf("exec with descriptor %d sent to descriptor %d of the child, which the call closes", dst[i], c));
        continue;
      }
      sp_RbVal v;
      if (!fx_redirect_of(opts, c, &v)) continue;
      int from = fx_fd_of(v);
      if (from >= 0) src[i] = from;
      else if (fx_child_fd(v, &c)) child[i] = c;
      else fx_gap(sp_sprintf("exec with descriptor %d sent to standard stream %d, which the call sends to a file", dst[i], c));
    }
  for (int i = 0; i < n; i++) {
    tmp[i] = -1;
    if (src[i] < 0) continue;
    tmp[i] = fcntl(src[i], F_DUPFD_CLOEXEC, floor);
    if (tmp[i] < 0) {
      int e = errno;
      for (int k = 0; k < i; k++) if (tmp[k] >= 0) close(tmp[k]);
      for (int k = 0; k < n; k++) if (path[k]) close(src[k]);
      sp_raise_cls(sp_errno_class_name(e), strerror(e));
    }
  }
  for (int i = 0; i < n; i++) if (path[i]) close(src[i]);
  for (int i = 0; i < n; i++) {
    if (tmp[i] < 0) continue;
    if (dup2(tmp[i], dst[i]) < 0) {
      int e = errno;
      for (int k = i; k < n; k++) if (tmp[k] >= 0) close(tmp[k]);
      sp_raise_cls(sp_errno_class_name(e), strerror(e));
    }
    close(tmp[i]);
    int f = fcntl(dst[i], F_GETFL);
    if (f >= 0 && (f & O_NONBLOCK)) fcntl(dst[i], F_SETFL, f & ~O_NONBLOCK);
  }
  for (int i = 0; i < n; i++) if (src[i] == FX_EXEC_CLOSE) close(dst[i]);
}

/* The keys of an exec's options Hash: the ones of spawn that the runtime
   reads (in, out, err, their groups, pgroup, rlimit_cpu, rlimit_as, chdir,
   and the ones every spawn ignores, close_others and unsetenv_others), and
   a descriptor past 2 sent to an IO, a number, a file (a path, or an Array
   of path, mode and permission) or :close, which fx_exec_fds makes. Another
   key, or another value for such a descriptor (one of a descriptor of the
   child among them), stops as a gap rather than go unread. */
static void fx_exec_opts_check(sp_RbVal opts) {
  static const char *const known[] = { "in", "out", "err", "pgroup", "rlimit_cpu", "rlimit_as", "chdir",
                                       "close_others", "unsetenv_others", NULL };
  for (sp_int j = 0, n = sp_poly_length(opts); j < n; j++) {
    sp_RbVal key, value;
    sp_poly_hash_pair(opts, j, &key, &value);
    int fd = fx_fd_of(key);
    if (fd >= 0 && fd <= 2) continue;
    /* a descriptor past 2: fx_exec_fds makes it, also from a descriptor of
       the child ([:child, fd]); another Array is a file's, and one whose
       first element is no String raises the TypeError File.open would. */
    if (fd > 2) {
      if (fx_fd_of(value) >= 0 || fx_is_string(value) || sp_rbval_is_array(value) ||
          (value.tag == SP_TAG_SYM && value.v.i == sp_sym_intern("close"))) continue;
      fx_gap(sp_sprintf("exec with a redirection of descriptor %d to a value that is no IO, number, file or :close", fd));
    }
    if (key.tag == SP_TAG_INT) fx_gap(sp_sprintf("exec with a redirection of descriptor %lld", (long long)key.v.i));
    /* a group: the runtime reads it when every member is :in, :out, :err
       or a number up to 2 (sp_process_redirect_group), and reads none of
       it otherwise */
    if (sp_rbval_is_array(key)) {
      for (sp_int i = 0, len = sp_poly_length(key); i < len; i++) {
        sp_RbVal m = sp_poly_arr_get(key, i);
        if (m.tag == SP_TAG_INT) {
          if (m.v.i >= 0 && m.v.i <= 2) continue;
          fx_gap(sp_sprintf("exec with a redirection of descriptor %lld", (long long)m.v.i));
        }
        if (m.tag == SP_TAG_SYM) {
          const char *k = sp_sym_to_s((sp_sym)m.v.i);
          if (!strcmp(k, "in") || !strcmp(k, "out") || !strcmp(k, "err")) continue;
        }
        fx_gap("exec with a redirection group of that member");
      }
      continue;
    }
    if (key.tag == SP_TAG_SYM) {
      const char *k = sp_sym_to_s((sp_sym)key.v.i);
      int ok = 0;
      for (int i = 0; known[i]; i++) if (!strcmp(k, known[i])) ok = 1;
      if (ok) continue;
      fx_gap(sp_sprintf("exec with the option %s", k));
    }
    fx_gap("exec with an option of that key");
  }
}

static int fx_process_kernel(const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (FX_IS("fork") && argc == 0) { *out = fx_fork(NULL); return 1; }
  if (FX_IS("spawn")) { *out = sp_box_int(fx_spawn(argc, argv)); return 1; }
  if (FX_IS("system")) { *out = fx_system(argc, argv); return 1; }
  if (FX_IS("`") && argc == 1) {
    const char *cmd = sp_poly_arg_str_chk(argv[0]), *s;
    SP_GC_ROOT_STR(cmd);
    FX_UNLOCKED_ROOT(s = sp_backtick(cmd), SP_GC_ROOT_STR(s));
    *out = sp_box_str_new(s);
    return 1;
  }
  /* exec(env, cmd, *args, opts): the Hashes of spawn, read as spawn reads
     them (fx_spawn_opts). A redirection of a descriptor past 2 has no slot
     in the options array of the runtime: fx_exec_fds makes it. */
  if (FX_IS("exec") && argc >= 1) {
    sp_RbVal env = sp_box_nil(), opts = sp_box_nil();
    SP_GC_ROOT_RBVAL(env);
    SP_GC_ROOT_RBVAL(opts);
    if (fx_opts_hash(argc, argv)) opts = argv[--argc];
    if (argc >= 1 && fx_is_hash(argv[0])) { env = argv[0]; argv++; argc--; }
    if (argc < 1) sp_raise_cls("ArgumentError", "wrong number of arguments (given 0, expected 1+)");
    if (opts.tag != SP_TAG_NIL) fx_exec_opts_check(opts);
    sp_PolyArray *args = sp_PolyArray_new();
    SP_GC_ROOT(args);
    for (int i = 1; i < argc; i++) sp_PolyArray_push(args, argv[i]);
    sp_PolyArray *o = fx_spawn_opts(opts, env);
    SP_GC_ROOT(o);
    if (opts.tag != SP_TAG_NIL) fx_exec_fds(opts);
    /* a pipe of the runtime is O_NONBLOCK; the program that replaces this
       one expects blocking stdio, which the child of spawn gets the same
       way (sp_process.c of spinel). An exec that fails raises, and the
       program goes on with the flags it had: a read of a blocking pipe
       would stop every thread of the scheduler. */
    int fl[3];
    for (int fd = 0; fd <= 2; fd++) {
      fl[fd] = fcntl(fd, F_GETFL);
      if (fl[fd] >= 0 && (fl[fd] & O_NONBLOCK)) fcntl(fd, F_SETFL, fl[fd] & ~O_NONBLOCK);
    }
    FX_TRY_PUSH();
    if (setjmp(sp_exc_stack[sp_exc_top - 1]) == 0) {
      sp_process_exec(argv[0], sp_box_poly_array(args), sp_box_poly_array(o));
      sp_exc_top--;
    } else {
      FX_TRY_LANDED(0);
      for (int fd = 0; fd <= 2; fd++)
        if (fl[fd] >= 0 && (fl[fd] & O_NONBLOCK)) fcntl(fd, F_SETFL, fl[fd]);
      fx_reraise(fx_caught());
    }
    *out = sp_box_nil();
    return 1;
  }
  return 0;
}

/* Process::Status#success?, which bop_gen leaves out: nil when the child
   did not exit, else whether its status is zero. to_s and inspect have rows
   in the table of spinel ("pid 12 exit 3", "#<Process::Status: pid 12
   SIGKILL (signal 9)>"), but fx_core answers the to_s and the inspect of
   every value before the table, so the call is handed to the generated
   wrapper here. */
static int fx_process_status_core(sp_RbVal r, const char *name, int argc, sp_RbVal *out) {
  if (r.tag != SP_TAG_OBJ || r.cls_id != SP_BUILTIN_PROCESS_STATUS || !r.v.p) return 0;
  if (FX_IS("success?") && argc == 0) {
    int s = sp_process_status_success_p(((sp_ProcessStatus *)r.v.p)->status);
    *out = s < 0 ? sp_box_nil() : sp_box_bool(s);
    return 1;
  }
  if ((FX_IS("to_s") || FX_IS("inspect")) && argc == 0) {
    fx_bop_fn fn = fx_bop_find(r, name, 0, NULL);
    if (fn) {
      *out = fn(r, NULL);
      fx_bop_mark();
      return 1;
    }
  }
  return 0;
}

/* The waits of Process: wait and waitpid answer the pid, wait2 and
   waitpid2 [pid, status]; no pid waits for any child. With the flags of
   waitpid(2) as the second argument, the runtime answers the same, or nil
   under WNOHANG for a child still running. detach answers a Thread whose
   value is the status. */
static int fx_process_wait(const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if ((FX_IS("wait") || FX_IS("waitpid") || FX_IS("wait2") || FX_IS("waitpid2")) && argc == 2) {
    sp_int pid = sp_poly_arg_int_chk(argv[0]), flags = sp_poly_arg_int_chk(argv[1]);
    int pair = FX_IS("wait2") || FX_IS("waitpid2");
    sp_RbVal v;
    FX_UNLOCKED_ROOT(v = sp_process_wait_flags(pid, flags, pair), SP_GC_ROOT_RBVAL(v));
    *out = v;
    return 1;
  }
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
                                         "exit", "exit!", "abort",
                                         "getsid", "setsid", "getpgid", "setpgid", "clock_gettime", "clock_getres", "getpriority", "kill", "spawn",
                                         "exec", "fork", "wait", "waitpid", "wait2", "waitpid2", "detach", "last_status",
                                         "setproctitle", "argv0", NULL };
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
  if (!strcmp(k->name, "Signal") && FX_IS("trap")) {
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
