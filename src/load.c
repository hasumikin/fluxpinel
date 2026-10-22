/* load.c -- the source files of a program: parsing, require_relative,
   __FILE__ and __dir__.

   spinel splices a required file into one source at compile time. Fluxpinel
   loads it at run time, as CRuby does: the file runs once, at the first
   require, as a top-level program of its own (self is main, and its locals
   are its own).

   Each file has a prism parser of its own, and all the parsers share one
   constant pool (L.pool), so that a constant id names the same Symbol in
   any file. The pool only grows, so an id stays valid. */

#include "fluxpinel.h"
#include <sys/stat.h>

/* the bundled packages of spinel (set, json, ...), from the Makefile */
#ifndef FX_PACKAGES_DIR
#define FX_PACKAGES_DIR "packages"
#endif

#define FX_IS(s) (!strcmp(name, (s)))

/* The bytes of a file, or NULL when it cannot be read. */
static char *fx_read_file(const char *path, size_t *len) {
  FILE *fp = fopen(path, "rb");
  if (!fp) return NULL;
  fseek(fp, 0, SEEK_END);
  long n = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  char *buf = n >= 0 ? malloc((size_t)n + 1) : NULL;
  if (!buf || fread(buf, 1, (size_t)n, fp) != (size_t)n) {
    fclose(fp);
    free(buf);
    return NULL;
  }
  fclose(fp);
  buf[n] = 0;
  *len = (size_t)n;
  return buf;
}

/* Parses the bytes of a file into a new source. Answers the message of
   the first syntax error, or NULL. */
static const char *fx_parse(const char *path, const char *real, char *buf, size_t len, FxSource **out) {
  FxSource *s = calloc(1, sizeof *s);
  s->path = path;
  s->real = real;
  s->start = (const uint8_t *)buf;
  s->end = (const uint8_t *)buf + len;
  pm_options_filepath_set(&s->options, path);
  pm_options_line_set(&s->options, 1);
  pm_parser_init(&s->parser, (const uint8_t *)buf, len, &s->options);
  /* the parser takes the shared pool, and gives it back grown */
  if (L.nsources > 0) {
    pm_constant_pool_free(&s->parser.constant_pool);
    s->parser.constant_pool = L.pool;
  }
  s->root = pm_parse(&s->parser);
  L.pool = s->parser.constant_pool;
  if (L.nsources == L.sources_cap) {
    L.sources_cap = L.sources_cap ? L.sources_cap * 2 : 8;
    L.sources = realloc(L.sources, sizeof *L.sources * (size_t)L.sources_cap);
  }
  L.sources[L.nsources++] = s;
  *out = s;
  if (!s->parser.error_list.size) return NULL;
  const pm_diagnostic_t *d = (const pm_diagnostic_t *)s->parser.error_list.head;
  int line = pm_newline_list_line_column(&s->parser.newline_list, d->location.start, s->parser.start_line).line;
  return sp_sprintf("%s:%d: %s", path, line, d->message);
}

/* Runs the top level of a source, as a program of its own. A return at
   its top level ends the file. */
static sp_RbVal fx_run_source(const FxSource *s) {
  SP_GC_SAVE();
  const pm_program_node_t *prog = (const pm_program_node_t *)s->root;
  FX_FRAME(top, &prog->locals, L.main, L.object);
  top.vis = FX_VIS_PRIVATE;   /* a top-level def is a private method of Object */
  sp_RbVal v = fx_eval_stmts(&top, prog->statements);
  if (L.ctl == FX_CTL_RETURN && L.ctl_target == &top) L.ctl = FX_CTL_NONE;
  return v;
}

/* The main program from the source buf. A syntax error ends Fluxpinel, as
   it ends ruby. */
static void fx_load_main_src(const char *path, const char *real, char *buf, size_t len) {
  FxSource *s;
  const char *err = fx_parse(path, real, buf, len, &s);
  if (err) {
    fprintf(stderr, "fluxpinel: syntax error: %s\n", err);
    exit(1);
  }
  fx_run_source(s);
}

/* The main program from the file path. */
static void fx_load_main(const char *path) {
  size_t len;
  char *buf = fx_read_file(path, &len);
  if (!buf) {
    fprintf(stderr, "fluxpinel: cannot read %s\n", path);
    exit(1);
  }
  char *real = realpath(path, NULL);
  fx_load_main_src(path, real ? real : path, buf, len);
}

/* The directory of a path, as a new C string. */
static char *fx_dirname(const char *path) {
  const char *slash = strrchr(path, '/');
  if (!slash) return strdup(".");
  if (slash == path) return strdup("/");
  return strndup(path, (size_t)(slash - path));
}

/* path with its "." and ".." segments taken out by their text, as
   File.expand_path does, so that "a/b/.." is "a". */
static const char *fx_path_clean(const char *path) {
  /* the segments that stay, as (start, length) pairs */
  size_t n = strlen(path), nseg = 0;
  const char *seg[n / 2 + 1];
  size_t len[n / 2 + 1];
  int abs = path[0] == '/';
  for (const char *p = path; *p;) {
    while (*p == '/') p++;
    const char *s = p;
    while (*p && *p != '/') p++;
    size_t l = (size_t)(p - s);
    if (l == 0 || (l == 1 && s[0] == '.')) continue;
    if (l == 2 && s[0] == '.' && s[1] == '.') {
      int top_up = nseg > 0 && len[nseg - 1] == 2 && !strncmp(seg[nseg - 1], "..", 2);
      if (nseg > 0 && !top_up) { nseg--; continue; }
      if (abs) continue;   /* "/.." is "/" */
    }
    seg[nseg] = s;
    len[nseg++] = l;
  }
  char *out = sp_str_alloc(n + 1);
  size_t o = 0;
  if (abs) out[o++] = '/';
  for (size_t i = 0; i < nseg; i++) {
    if (i) out[o++] = '/';
    memcpy(out + o, seg[i], len[i]);
    o += len[i];
  }
  if (o == 0) out[o++] = '.';
  out[o] = 0;
  sp_str_set_len(out, o);
  return out;
}

/* Runs the file at real (an absolute path from realpath, which the source
   keeps) once, with the text of the file extra after it when extra is not
   NULL. Answers 0, and runs nothing, for a file that is loaded already. */
static int fx_load_once(char *real, const char *extra) {
  for (int i = 0; i < L.nsources; i++)
    if (!strcmp(L.sources[i]->real, real)) {
      free(real);
      return 0;
    }
  size_t len, xlen = 0;
  char *buf = fx_read_file(real, &len);
  if (!buf) sp_raise_cls("LoadError", sp_sprintf("cannot load such file -- %s", real));
  char *xbuf = extra ? fx_read_file(extra, &xlen) : NULL;
  if (xbuf) {
    buf = realloc(buf, len + 1 + xlen + 1);
    buf[len++] = '\n';
    memcpy(buf + len, xbuf, xlen + 1);
    len += xlen;
    free(xbuf);
  }
  FxSource *s;
  const char *err = fx_parse(real, real, buf, len, &s);
  if (err) sp_raise_cls("SyntaxError", err);
  fx_run_source(s);
  return 1;
}

/* require_relative "x", and require "./x" or require "/x". Answers true
   after it runs the file, and false for a file that is loaded already. */
static sp_RbVal fx_require(const pm_node_t *n, const char *arg, int rel) {
  const char *path = arg, *file = NULL, *shown = NULL;
  SP_GC_ROOT_STR(path);
  SP_GC_ROOT_STR(file);
  SP_GC_ROOT_STR(shown);
  if (rel) {
    char *dir = fx_dirname(fx_source_of(n)->real);
    path = sp_sprintf("%s/%s", dir, arg);
    free(dir);
  }
  /* `require_relative ".."` names the parent directory, plus .rb */
  path = fx_path_clean(path);
  size_t plen = strlen(path);
  file = plen > 3 && !strcmp(path + plen - 3, ".rb") ? path : sp_sprintf("%s.rb", path);
  char *real = realpath(file, NULL);
  if (!real) {
    /* the message names the path that it tried, with no .rb: as given for
       require, and absolute for require_relative */
    shown = arg;
    if (rel) {
      char *dir = fx_dirname(path), *abs = realpath(dir, NULL);
      shown = abs ? sp_sprintf("%s/%s", abs, path + strlen(dir) + 1) : path;
      free(dir);
      free(abs);
    }
    size_t sl = strlen(shown);
    if (sl > 3 && !strcmp(shown + sl - 3, ".rb")) shown = sp_sprintf("%.*s", (int)(sl - 3), shown);
    sp_raise_cls("LoadError", sp_sprintf("cannot load such file -- %s", shown));
  }
  if (!fx_load_once(real, NULL)) return sp_box_bool(0);
  return L.ctl == FX_CTL_NONE ? sp_box_bool(1) : sp_box_nil();
}

static int fx_lib_in(const char *name, const char *const *list) {
  for (int i = 0; list[i]; i++)
    if (!strcmp(list[i], name)) return 1;
  return 0;
}

/* require "name" of a library, as spinel_parse.c resolves it:
   - packages/<first>/<name>.rb, the package of the first segment of the
     name, runs once, with <name>.share.rb after it when that exists.
   - A feature of the runtime (socket, ostruct, ...) and a core capability
     (thread, fiber, ...) load nothing, and the require answers false.
   - Any other name is a LoadError. (spinel refuses it at compile time.)
   A require of set answers false, as Set is loaded before the program in
   CRuby. */
static sp_RbVal fx_require_lib(const char *name) {
  static const char *const builtin[] = { "io/console", "monitor", "time", "socket", "ostruct",
                                         "thread", "enumerator", "fiber", "ffi", "rational", "complex",
                                         "io/wait", "rbconfig", "rubygems", NULL };
  static const char *const preloaded[] = { "set", NULL };
  const char *path = NULL, *extra = NULL;
  SP_GC_ROOT_STR(path);
  SP_GC_ROOT_STR(extra);
  size_t nl = strlen(name), first = strcspn(name, "/");
  if (nl > 3 && !strcmp(name + nl - 3, ".rb")) nl -= 3;
  if (first > nl) first = nl;
  /* the ffi package and fiddle over it need libffi, as in spinel */
  int pkg = FX_FFI || !((first == 3 && !strncmp(name, "ffi", 3)) || (first == 6 && !strncmp(name, "fiddle", 6)));
  path = sp_sprintf("%s/%.*s/%.*s", FX_PACKAGES_DIR, (int)first, name, (int)nl, name);
  char *real = pkg ? realpath(sp_sprintf("%s.rb", path), NULL) : NULL;
  if (real) {
    struct stat st;
    extra = sp_sprintf("%s.share.rb", path);
    int ran = fx_load_once(real, stat(extra, &st) ? NULL : extra);
    if (!ran || fx_lib_in(name, preloaded)) return sp_box_bool(0);
    return L.ctl == FX_CTL_NONE ? sp_box_bool(1) : sp_box_nil();
  }
  if (fx_lib_in(name, builtin)) return sp_box_bool(0);
  sp_raise_cls("LoadError", sp_sprintf("cannot load such file -- %s", name));
}

/* A constant that spinel loads the package of when a program names it,
   with no require (sp_require_preloaded of spinel_parse.c): Set. Answers 1
   when it loaded the package, so that the caller looks again. */
static int fx_autoload(sp_sym name) {
  static int set_done;
  if (set_done || strcmp(sp_sym_to_s(name), "Set")) return 0;
  set_done = 1;
  fx_require_lib("set");
  return 1;
}

/* require_relative, require of a path, and __dir__: the calls that answer
   from the file of the call node n, unless the program defines a method of
   the name for self. Answers 1 and sets *out when it handles the call. */
static int fx_load_call(const pm_node_t *n, sp_RbVal self, const char *name, int argc, sp_RbVal *argv,
                          sp_RbVal *out) {
  if (!FX_IS("__dir__") && !FX_IS("require_relative") && !FX_IS("require")) return 0;
  if (fx_find_method(fx_class_of(self), sp_sym_intern(name))) return 0;
  if (FX_IS("__dir__") && argc == 0) {
    char *dir = fx_dirname(fx_source_of(n)->real);
    *out = fx_new_string((const uint8_t *)dir, strlen(dir));
    free(dir);
    return 1;
  }
  int rel = FX_IS("require_relative");
  if (!(rel || FX_IS("require")) || argc != 1) return 0;
  if (!fx_is_string(argv[0]))
    sp_raise_cls("TypeError", sp_sprintf("no implicit conversion of %s into String",
                                         argv[0].tag == SP_TAG_NIL ? "nil" : sp_poly_class_name(argv[0])));
  const char *arg = sp_poly_recv_s(argv[0], name);
  if (!rel && !(arg[0] == '/' || !strncmp(arg, "./", 2) || !strncmp(arg, "../", 3)))
    *out = fx_require_lib(arg);
  else
    *out = fx_require(n, arg, rel);
  return 1;
}

#undef FX_IS
