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
#ifndef FX_BUILTINS_DIR
#define FX_BUILTINS_DIR "builtins"
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

/* Gives prism the locals of the scopes around the code, outermost first,
   so that it parses each of these names as a local of an outer scope.
   prism takes the last scope of the option as the top level of the code,
   so an empty scope follows them. */
static void fx_options_scopes(pm_options_t *o, const pm_constant_id_list_t *const *scopes, int n) {
  if (!n) return;
  pm_options_scopes_init(o, (size_t)n + 1);
  pm_options_scope_init((pm_options_scope_t *)pm_options_scope_get(o, (size_t)n), 0);
  for (int i = 0; i < n; i++) {
    pm_options_scope_t *sc = (pm_options_scope_t *)pm_options_scope_get(o, (size_t)i);
    pm_options_scope_init(sc, scopes[i]->size);
    for (size_t j = 0; j < scopes[i]->size; j++) {
      const pm_constant_t *c = pm_constant_pool_id_to_constant(&L.pool, scopes[i]->ids[j]);
      pm_string_constant_init(&sc->locals[j], (const char *)c->start, c->length);
    }
  }
}

/* pm_locals_hash of prism.c, which is static there */
static uint32_t fx_locals_hash(pm_constant_id_t name) {
  name = ((name >> 16) ^ name) * 0x45d9f3b;
  name = ((name >> 16) ^ name) * 0x45d9f3b;
  return (name >> 16) ^ name;
}

/* pm_parser_init adds the locals of the scopes option to the constant pool
   of the parser, which fx_parse_in then replaces with the shared pool. So
   each local gets its id in the shared pool here: the scopes hold their
   locals in the order of the option (pm_local_t.index). A table of the hash
   kind (pm_locals_write in prism.c) takes its locals again by the new ids. */
static void fx_scopes_rebind(pm_parser_t *p, const pm_constant_id_list_t *const *scopes, int n) {
  if (!n) return;
  pm_scope_t *sc = p->current_scope->previous;   /* past the empty top level */
  for (int i = n - 1; i >= 0 && sc; i--, sc = sc->previous) {
    pm_locals_t *ls = &sc->locals;
    if (ls->capacity < 9) {   /* PM_LOCALS_HASH_THRESHOLD: a plain list */
      for (uint32_t j = 0; j < ls->size; j++) ls->locals[j].name = scopes[i]->ids[ls->locals[j].index];
      continue;
    }
    pm_local_t *old = malloc(sizeof *old * ls->capacity);
    memcpy(old, ls->locals, sizeof *old * ls->capacity);
    memset(ls->locals, 0, sizeof *old * ls->capacity);
    uint32_t mask = ls->capacity - 1;
    for (uint32_t j = 0; j < ls->capacity; j++) {
      if (old[j].name == PM_CONSTANT_ID_UNSET) continue;
      pm_local_t l = old[j];
      l.name = scopes[i]->ids[l.index];
      l.hash = fx_locals_hash(l.name);
      uint32_t h = l.hash;
      while (ls->locals[h & mask].name != PM_CONSTANT_ID_UNSET) h++;
      ls->locals[h & mask] = l;
    }
    free(old);
  }
}

static void fx_side_broken(uint32_t id) {
  fprintf(stderr, "fluxpinel: internal error: node id %u is out of the side table\n", id);
  fflush(stdout);
  _exit(2);
}

/* Grows the side table to hold the node ids up to last. A new chunk is
   all zero: no record keeps a field yet. */
static void fx_side_grow(uint32_t last) {
  uint32_t need = (last >> FX_SIDE_BITS) + 1;
  if (need <= L.side_chunks) return;
  L.side = realloc(L.side, sizeof *L.side * need);
  for (uint32_t i = L.side_chunks; i < need; i++) L.side[i] = calloc(FX_SIDE_CHUNK, sizeof(FxSide));
  L.side_chunks = need;
}

/* Parses the bytes of a file into a new source, which starts at the line
   line, inside the scopes scopes (a piece of the console). Answers the
   message of the first syntax error, or NULL. */
static const char *fx_parse_in(const char *path, const char *real, char *buf, size_t len, int line,
                               const pm_constant_id_list_t *const *scopes, int nscopes, FxSource **out) {
  FxSource *s = calloc(1, sizeof *s);
  s->path = path;
  s->real = real;
  s->start = (const uint8_t *)buf;
  s->end = (const uint8_t *)buf + len;
  pm_options_filepath_set(&s->options, path);
  pm_options_line_set(&s->options, line);
  fx_options_scopes(&s->options, scopes, nscopes);
  pm_parser_init(&s->parser, (const uint8_t *)buf, len, &s->options);
  /* the parser takes the shared pool, and gives it back grown */
  if (L.nsources > 0) {
    pm_constant_pool_free(&s->parser.constant_pool);
    s->parser.constant_pool = L.pool;
    fx_scopes_rebind(&s->parser, scopes, nscopes);
  }
  /* the nodes continue the count of the parses before, so that a node id
     selects one record of the side table. pm_parser_init sets the count
     to 0, so the start goes in after it */
  s->parser.node_id = L.node_ids;
  s->root = pm_parse(&s->parser);
  /* the start depends on a field of the parser of prism: stop if a new
     prism counts in another way */
  if (s->root->node_id <= L.node_ids || s->root->node_id > s->parser.node_id) fx_side_broken(s->root->node_id);
  L.node_ids = s->parser.node_id;
  fx_side_grow(L.node_ids);
  L.pool = s->parser.constant_pool;
  if (L.nsources == L.sources_cap) {
    L.sources_cap = L.sources_cap ? L.sources_cap * 2 : 8;
    L.sources = realloc(L.sources, sizeof *L.sources * (size_t)L.sources_cap);
  }
  L.sources[L.nsources++] = s;
  *out = s;
  if (!s->parser.error_list.size) return NULL;
  const pm_diagnostic_t *d = (const pm_diagnostic_t *)s->parser.error_list.head;
  int at = pm_newline_list_line_column(&s->parser.newline_list, d->location.start, s->parser.start_line).line;
  return sp_sprintf("%s:%d: %s", path, at, d->message);
}

static const char *fx_parse(const char *path, const char *real, char *buf, size_t len, FxSource **out) {
  return fx_parse_in(path, real, buf, len, 1, NULL, 0, out);
}

/* Kernel#eval(src, binding = nil, file = "(eval)", line = 1) with no
   binding. The code runs in a new scope under the frame of the call, as a
   block does: it reads and writes the locals of that frame and of the
   scopes around it, its own new locals stay in it, and self, the lexical
   class and the block of the method are those of the call, as in CRuby.
   prism gets the names of these frames as the scopes around the code. */
static sp_RbVal fx_eval_string(FxFrame *f, int argc, sp_RbVal *argv) {
  if (argc > 4) fx_arity_error(argc, 1, 3, 0);
  if (argc >= 2 && argv[1].tag != SP_TAG_NIL) fx_gap("eval with a binding");
  const char *src = sp_poly_arg_str_chk(argv[0]);
  const char *path = argc >= 3 ? sp_poly_arg_str_chk(argv[2]) : "(eval)";
  int line = argc >= 4 ? (int)sp_poly_arg_int_chk(argv[3]) : 1;
  int n = 0;
  for (FxFrame *p = f; p; p = p->parent) n++;
  const pm_constant_id_list_t **scopes = malloc(sizeof *scopes * (size_t)n);
  int i = n;
  for (FxFrame *p = f; p; p = p->parent) scopes[--i] = p->names;
  size_t len = sp_str_byte_len(src);
  char *buf = malloc(len + 1);
  memcpy(buf, src, len);
  buf[len] = 0;
  FxSource *s;
  const char *err = fx_parse_in(strdup(path), strdup(path), buf, len, line, scopes, n, &s);
  free(scopes);
  if (err) sp_raise_cls("SyntaxError", err);
  SP_GC_SAVE();
  const pm_program_node_t *prog = (const pm_program_node_t *)s->root;
  FX_FRAME(top, &prog->locals, f->self, f->cref);
  top.parent = f;
  top.definee = f->definee;
  top.singleton_defs = f->singleton_defs;
  top.vis = f->vis;
  top.method = f->method;
  top.block = f->block;
  return fx_eval_stmts(&top, prog->statements);
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

/* Parses the main program from the source buf. A syntax error ends
   Fluxpinel, as it ends ruby. */
static FxSource *fx_parse_main(const char *path, const char *real, char *buf, size_t len) {
  FxSource *s;
  const char *err = fx_parse(path, real, buf, len, &s);
  if (err) {
    fprintf(stderr, "fluxpinel: syntax error: %s\n", err);
    exit(1);
  }
  return s;
}

static void fx_load_main_src(const char *path, const char *real, char *buf, size_t len) {
  fx_run_source(fx_parse_main(path, real, buf, len));
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
  if (!buf) sp_raise_load_error(real);
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
  /* an absolute path names the file itself */
  if (rel && arg[0] != '/') {
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
    sp_raise_load_error(shown);
  }
  if (!fx_load_once(real, NULL)) return sp_box_bool(0);
  return L.ctl == FX_CTL_NONE ? sp_box_bool(1) : sp_box_nil();
}

static int fx_lib_in(const char *name, const char *const *list) {
  for (int i = 0; list[i]; i++)
    if (!strcmp(list[i], name)) return 1;
  return 0;
}

/* The file of require "name" in a directory of $LOAD_PATH (-I, or a push
   of the program), as a new realpath; NULL for none. */
static char *fx_load_path_find(const char *name) {
  sp_StrArray *lp = sp_get_load_path();
  size_t nl = strlen(name);
  int rb = nl > 3 && !strcmp(name + nl - 3, ".rb");
  for (sp_int i = 0; i < lp->len; i++) {
    const char *dir = sp_StrArray_get(lp, i);
    if (!dir) continue;
    char *real = realpath(sp_sprintf("%s/%s%s", dir, name, rb ? "" : ".rb"), NULL);
    if (real) return real;
  }
  return NULL;
}

/* require "name" of a library: first a file in $LOAD_PATH, as ruby finds
   it, then as spinel_parse.c resolves it:
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
  char *found = fx_load_path_find(name);
  if (found) {
    if (!fx_load_once(found, NULL)) return sp_box_bool(0);
    return L.ctl == FX_CTL_NONE ? sp_box_bool(1) : sp_box_nil();
  }
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
  if (fx_lib_in(name, builtin)) {
    fx_feature_loaded = 1;
    return sp_box_bool(0);
  }
  sp_raise_load_error(name);
}

/* require_relative, require of a path, and __dir__: the calls that answer
   from the file of the call node n, unless the program defines a method of
   the name for self. Answers 1 and sets *out when it handles the call. */
static int fx_load_call(const pm_node_t *n, sp_RbVal self, const char *name, int argc, sp_RbVal *argv,
                          sp_RbVal *out) {
  /* the first letter turns away most calls, before a strcmp */
  if (name[0] != '_' && name[0] != 'r' && name[0] != 'l') return 0;
  if (!FX_IS("__dir__") && !FX_IS("require_relative") && !FX_IS("require") && !FX_IS("load")) return 0;
  if (fx_find_method(fx_class_of(self), sp_sym_intern(name))) return 0;
  /* load(path): the file runs at each call, from the path as given, which
     is relative to the current directory. A true wrap is not modeled. */
  if (FX_IS("load") && (argc == 1 || (argc == 2 && !sp_poly_truthy(argv[1])))) {
    if (!fx_is_string(argv[0]))
      sp_raise_cls("TypeError", sp_sprintf("no implicit conversion of %s into String",
                                           argv[0].tag == SP_TAG_NIL ? "nil" : sp_poly_class_name(argv[0])));
    const char *path = sp_poly_recv_s(argv[0], name);
    SP_GC_ROOT_STR(path);
    size_t len;
    char *buf = fx_read_file(path, &len);
    if (!buf) sp_raise_load_error(path);
    char *real = realpath(path, NULL);
    FxSource *s;
    const char *err = fx_parse(strdup(path), real ? real : strdup(path), buf, len, &s);
    if (err) sp_raise_cls("SyntaxError", err);
    fx_run_source(s);
    *out = L.ctl == FX_CTL_NONE ? sp_box_bool(1) : sp_box_nil();
    return 1;
  }
  if (FX_IS("load")) return 0;
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

/* -r lib of the command line: require "lib" before the main program. */
static void fx_require_option(const char *lib) {
  if (lib[0] == '/' || !strncmp(lib, "./", 2) || !strncmp(lib, "../", 3)) fx_require(NULL, lib, 0);
  else fx_require_lib(lib);
}

#undef FX_IS
