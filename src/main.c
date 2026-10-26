/* main.c -- the Fluxpinel command and the root of the one TU.

   Usage: fluxpinel [options] [--] [FILE.rb | -] [ARG...]
          fluxpinel --console    a console on stdin (console.c)

   The options are a part of those of ruby: -c, -e, -I, -r, -v, -w, -W,
   -h and --version (fx_options). With no file and no -e, the program
   comes from stdin.

   A host build with FX_KERNEL_MAIN (an entry such as "Logq.main") takes
   no FILE.rb: it calls the entry with ARGV, which holds every argument.

   spinel_rt.h declares hooks as static, so all Fluxpinel code that uses the
   runtime is one TU. This file includes the others in order. */

#include "fx_tu.c"
#include "bop_gen_out.c"
#include "fluxpinel.h"
#include "object.c"
#include "walk.c"
#include "builtin.c"
#include "responds.c"
#include "iter.c"
#include "hash.c"
#include "array.c"
#include "string.c"
#include "exc.c"
#include "re.c"
#include "global.c"
#include "struct.c"
#include "fiber.c"
#include "load.c"
#include "prelude.c"
#include "ffi.c"
#include "process.c"
#include "file.c"
#include "host.c"
#include "console.c"

#ifndef FX_KERNEL_MAIN
/* The options of the command line, as ruby takes them. */
static struct {
  int console;            /* --console */
  int check;              /* -c */
  char *code;             /* -e: each code, joined by newlines; NULL for none */
  size_t code_len;
  const char **libs;      /* -r */
  int nlibs;
  const char **dirs;      /* -I */
  int ndirs;
  int verbose;            /* $VERBOSE: -1 as it is, 0 nil, 1 false, 2 true */
  const char *script;     /* the file of the program; "-" or NULL for stdin */
} fx_opt = { .verbose = -1 };

static const char fx_usage[] =
  "Usage: fluxpinel [options] [--] [filepath] [arguments]\n"
  "  -c              Check syntax (no execution)\n"
  "  -e 'code'       Execute given Ruby code; multiple -e allowed\n"
  "  -Idirpath       Prepend specified directory to load paths ($LOAD_PATH)\n"
  "  -rlibrary       Require the given library\n"
  "  -v              Print version; set $VERBOSE to true\n"
  "  -w              Set $VERBOSE to true\n"
  "  -W[level=2]     Set $VERBOSE: 0 nil, 1 false, 2 true\n"
  "  -h, --help      Print this help message\n"
  "  --version       Print the version\n"
  "  --console       Read Ruby from stdin, one line at a time, as irb does\n";

/* A wrong command line ends Fluxpinel, as it ends ruby. fx_options runs
   before the runtime starts, so these use no string of the runtime. */
static __attribute__((noreturn)) void fx_opt_fail(const char *msg, const char *arg, const char *cls) {
  fprintf(stderr, "fluxpinel: %s%s (%s)\n", msg, arg, cls);
  exit(1);
}

static __attribute__((noreturn)) void fx_opt_invalid(const char *dash, const char *opt, int len) {
  fprintf(stderr, "fluxpinel: invalid option %s%.*s  (-h will show valid options) (RuntimeError)\n", dash, len, opt);
  exit(1);
}

static void fx_opt_code(const char *code) {
  size_t n = strlen(code);
  fx_opt.code = realloc(fx_opt.code, fx_opt.code_len + n + 2);
  memcpy(fx_opt.code + fx_opt.code_len, code, n);
  fx_opt.code_len += n;
  fx_opt.code[fx_opt.code_len++] = '\n';
  fx_opt.code[fx_opt.code_len] = 0;
}

static void fx_opt_push(const char ***list, int *n, const char *s) {
  *list = realloc(*list, sizeof **list * (size_t)(*n + 1));
  (*list)[(*n)++] = s;
}

/* Reads the options in argv. Answers the index of the first argument
   after them: the file of the program, unless -e gave the code. A short
   option takes its argument from the rest of its word or from the next
   word (-Ilib, -I lib), and the options of no argument join (-ve). */
static int fx_options(int argc, char **argv) {
  int i = 1;
  for (; i < argc; i++) {
    const char *a = argv[i];
    if (a[0] != '-' || !a[1]) break;   /* the file, or "-" for stdin */
    if (!strcmp(a, "--")) { i++; break; }
    if (a[1] == '-') {
      if (!strcmp(a, "--console")) fx_opt.console = 1;
      else if (!strcmp(a, "--help")) { fputs(fx_usage, stdout); exit(0); }
      else if (!strcmp(a, "--version")) { puts(FX_DESCRIPTION); exit(0); }
      else fx_opt_invalid("", a, (int)strlen(a));
      continue;
    }
    for (const char *p = a + 1; *p; p++) {
      char c = *p;
      if (c == 'c') fx_opt.check = 1;
      else if (c == 'h') { fputs(fx_usage, stdout); exit(0); }
      else if (c == 'v') { puts(FX_DESCRIPTION); fx_opt.verbose = 2; if (!p[1] && i == argc - 1) exit(0); }
      else if (c == 'w') fx_opt.verbose = 2;
      else if (c == 'W') {
        /* -W, -W2 and up: true; -W1: false; -W0: nil */
        int level = p[1] >= '0' && p[1] <= '9' ? p[1] - '0' : 2;
        if (p[1] >= '0' && p[1] <= '9') p++;
        fx_opt.verbose = level >= 2 ? 2 : level;
      } else if (c == 'e' || c == 'I' || c == 'r') {
        const char *arg = p[1] ? p + 1 : i + 1 < argc ? argv[++i] : NULL;
        if (c == 'e') {
          if (!arg) fx_opt_fail("no code specified for -e", "", "RuntimeError");
          fx_opt_code(arg);
        } else if (arg) {
          /* ruby takes a -I or an -r with no argument as nothing */
          if (c == 'I') fx_opt_push(&fx_opt.dirs, &fx_opt.ndirs, arg);
          else fx_opt_push(&fx_opt.libs, &fx_opt.nlibs, arg);
        }
        break;
      } else {
        fx_opt_invalid("-", p, 1);
      }
    }
  }
  return i;
}

/* All of a stream, as a new C string. */
static char *fx_read_stream(FILE *fp, size_t *len) {
  size_t n = 0, cap = 4096;
  char *buf = malloc(cap);
  for (size_t got; (got = fread(buf + n, 1, cap - n - 1, fp)) > 0;) {
    n += got;
    if (cap - n - 1 == 0) buf = realloc(buf, cap *= 2);
  }
  buf[n] = 0;
  *len = n;
  return buf;
}

/* A directory of -I as an absolute path, as ruby expands it: "~" is the
   home directory, and a relative path starts at the current one. */
static const char *fx_opt_dir(const char *dir) {
  const char *home = getenv("HOME");
  if (home && dir[0] == '~' && (!dir[1] || dir[1] == '/')) return fx_path_clean(sp_sprintf("%s%s", home, dir + 1));
  if (dir[0] == '/') return fx_path_clean(dir);
  char *cwd = getcwd(NULL, 0);
  const char *abs = fx_path_clean(sp_sprintf("%s/%s", cwd ? cwd : ".", dir));
  free(cwd);
  return abs;
}

/* The main program, as the command line names it: the code of -e, the
   file, or stdin. Then -c checks it, or the libraries of -r load and the
   program runs. */
static void fx_run_main(void) {
  SP_GC_SAVE();
  sp_StrArray *lp = sp_get_load_path();
  for (int i = 0; i < fx_opt.ndirs; i++) sp_StrArray_insert(lp, i, sp_str_dup(fx_opt_dir(fx_opt.dirs[i])));
  if (fx_opt.verbose >= 0)
    fx_gvar_set("$VERBOSE", fx_opt.verbose == 0 ? sp_box_nil() : sp_box_bool(fx_opt.verbose == 2));
  char *buf;
  size_t len;
  const char *real = L.path;
  if (fx_opt.code) {
    buf = fx_opt.code;
    len = fx_opt.code_len;
  } else if (!strcmp(L.path, "-")) {
    buf = fx_read_stream(stdin, &len);
  } else {
    buf = fx_read_file(L.path, &len);
    if (!buf) fx_opt_fail("No such file or directory -- ", L.path, "LoadError");
    char *r = realpath(L.path, NULL);
    if (r) real = r;
  }
  FxSource *s = fx_parse_main(L.path, real, buf, len);
  if (fx_opt.check) {
    puts("Syntax OK");
    return;
  }
  for (int i = 0; i < fx_opt.nlibs; i++) {
    fx_require_option(fx_opt.libs[i]);
    if (L.ctl != FX_CTL_NONE) return;
  }
  fx_run_source(s);
}
#endif

static int fx_main_rc;   /* the exit status of a program that ends at its last line */

static void fx_body(void) {
  SP_GC_SAVE();
  fx_tu_init();
  fx_bop_init();
  L.ctl_val = sp_box_nil();
  _sp_gc_root_push((void **)((uintptr_t)&L.ctl_val | (uintptr_t)1));
  fx_object_init();
  fx_host_init();
  fx_hash_init();
  fx_array_init();
  fx_exc_hooks();
  fx_re_init();
  fx_global_init();
  /* a fiber runs the walker, whose frames are larger than those of AOT
     code: the size that codegen asks for an unoptimised build */
  sp_fiber_stack_hint((size_t)1 << 20);
  /* The fewest workers, also for a thread of the runtime itself (see
     Thread.new in fiber.c). The threaded runtime needs its root fiber
     before any fiber runs. */
  setenv("SPINEL_WORKERS", "1", 1);
  fx_sched_start();
  fx_fs_init();

#ifdef FX_KERNEL_MAIN
  const char *src = FX_KERNEL_MAIN "(ARGV)";
  fx_load_main_src(L.path, L.path, strdup(src), strlen(src));
#else
  if (fx_opt.console) {
    /* an empty main program starts the constant pool of the sources */
    fx_load_main_src(L.path, L.path, strdup(""), 0);
    fx_console_stdin();
  } else {
    fx_run_main();
  }
#endif
  /* a break of a Proc whose call has returned reaches the top */
  if (L.ctl == FX_CTL_BREAK_BLOCK) fx_gap("break from a block whose call has returned");
  /* the at_exit hooks, then the finalizers: a hook that exits or raises
     decides the status, as in the main of AOT code. The hooks run before
     the drain of the scheduler, as on the paths of exit and of an uncaught
     raise: Fluxpinel always runs the threaded runtime, and a collection in
     a hook after the drain waits for the workers that the drain stopped */
  fx_main_rc = sp_at_exit_run(0);
  fx_sched_end();
}

int main(int argc, char **argv) {
#ifdef FX_KERNEL_MAIN
  L.path = argv[0];
  L.args = argv + 1;
  L.nargs = argc - 1;
#else
  int i = fx_options(argc, argv);
  /* $0: "-e" for the code of -e, "-" for stdin, else the file */
  if (fx_opt.console) L.path = "(console)";
  else if (fx_opt.code) L.path = "-e";
  else if (i < argc) L.path = argv[i++];
  else L.path = "-";
  L.args = argv + i;
  L.nargs = argc - i;
#endif
  sp_main_stack_run(fx_body);
  return fx_main_rc;
}
