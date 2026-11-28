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
#ifndef FX_LIB_DIR
#define FX_LIB_DIR "lib"
#endif

#define FX_IS(s) (!strcmp(name, (s)))

/* The libraries in the binary (make EMBED=1). The Ruby files of the three
   directories are a table of the build (tools/embed_gen.rb), under paths
   that no file system has: FX_PACKAGES_DIR, FX_BUILTINS_DIR and FX_LIB_DIR
   start with FX_EMBED_ROOT then. A path under the root is a file of the
   table and never a file of the disk, so the binary reads the same
   libraries on each machine. */
#define FX_EMBED_ROOT "/@fluxpinel/"
#ifdef FX_EMBED
#include "embed.inc"
#endif

static int fx_path_embedded(const char *path) {
#ifdef FX_EMBED
  return !strncmp(path, FX_EMBED_ROOT, sizeof FX_EMBED_ROOT - 1);
#else
  (void)path;
  return 0;
#endif
}

/* The row of path in the table, or -1. */
static int fx_embed_find(const char *path) {
#ifdef FX_EMBED
  int lo = 0, hi = fx_embed_count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2, c = strcmp(path, fx_embed_files[mid].path);
    if (!c) return mid;
    if (c < 0) hi = mid - 1; else lo = mid + 1;
  }
#else
  (void)path;
#endif
  return -1;
}

static const char *fx_path_clean(const char *path);

/* realpath(path, NULL): for a path of the libraries in the binary, the
   path with no "." and ".." when the table has it. */
static char *fx_real_path(const char *path) {
  if (!fx_path_embedded(path)) return realpath(path, NULL);
  const char *clean = fx_path_clean(path);
  return fx_embed_find(clean) >= 0 ? strdup(clean) : NULL;
}

/* The bytes of a file, or NULL when it cannot be read. */
static char *fx_read_file(const char *path, size_t *len) {
#ifdef FX_EMBED
  if (fx_path_embedded(path)) {
    /* the path as a load or Fluxpinel.load gives it can have "." and ".." */
    int i = fx_embed_find(fx_path_clean(path));
    if (i < 0) return NULL;
    char *copy = malloc(fx_embed_files[i].len + 1);
    if (!copy) return NULL;
    memcpy(copy, fx_embed_files[i].src, fx_embed_files[i].len + 1);
    *len = fx_embed_files[i].len;
    return copy;
  }
#endif
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

/* ---- the message of a SyntaxError, as prism's formatter writes it for CRuby
   (src/errors_format.c of prism, in its plain form) ---- */

/* The bytes of the line li (from 0) of the source, with its newline when it
   has one, in *len; the end of the source for a line past the last. */
static const uint8_t *fx_source_line(const FxSource *s, int li, size_t *len) {
  const uint8_t *p = s->start;
  for (int i = 0; i < li; i++) {
    const uint8_t *nl = memchr(p, '\n', (size_t)(s->end - p));
    if (!nl) { *len = 0; return s->end; }
    p = nl + 1;
  }
  const uint8_t *nl = memchr(p, '\n', (size_t)(s->end - p));
  *len = (size_t)((nl ? nl + 1 : s->end) - p);
  return p;
}

/* The columns of the bytes [from, to) of a line: a character of several
   bytes is one column, as prism counts them. */
static int fx_cols(const uint8_t *p, size_t from, size_t to) {
  int n = 0;
  for (size_t i = from; i < to; i++) if ((p[i] & 0xC0) != 0x80) n++;
  return n;
}

typedef struct { char *s; size_t len, cap; } FxMsgBuf;
static void fx_msg_add(FxMsgBuf *b, const char *s, size_t n) {
  if (b->len + n + 1 > b->cap) {
    b->cap = (b->len + n + 1) * 2;
    b->s = realloc(b->s, b->cap);
  }
  memcpy(b->s + b->len, s, n);
  b->len += n;
  b->s[b->len] = 0;
}
static void fx_msg_append(FxMsgBuf *b, const char *s) { fx_msg_add(b, s, strlen(s)); }
static void fx_msg_repeat(FxMsgBuf *b, char c, int n) { for (int i = 0; i < n; i++) fx_msg_add(b, &c, 1); }

/* One error: its line (from 0), the byte columns of its start and of its end
   on that line (at least one column), and its place in the parse order. */
typedef struct { const pm_diagnostic_t *d; int li, cs, ce, idx; } FxRichErr;

/* The order the errors are shown in: by line and column, and the later
   recorded first among those at one place, as prism sorts them. */
static int fx_rich_err_cmp(const void *a, const void *b) {
  const FxRichErr *l = a, *r = b;
  if (l->li != r->li) return l->li < r->li ? -1 : 1;
  if (l->cs != r->cs) return l->cs < r->cs ? -1 : 1;
  if (l->idx != r->idx) return l->idx < r->idx ? 1 : -1;
  return 0;
}

/* A line of the source, after its gutter: the text with its newline, or a
   newline after a last line that has none; nothing for the empty line after
   a trailing newline, whose newline the caller adds. */
static void fx_msg_source_line(FxMsgBuf *b, const FxSource *s, int li) {
  size_t len;
  const uint8_t *p = fx_source_line(s, li, &len);
  fx_msg_add(b, (const char *)p, len);
  if (len && p[len - 1] != '\n') fx_msg_add(b, "\n", 1);
}

/* The message of a SyntaxError, as CRuby's is: the path and the line of
   the first error parsed, with "syntax error(s) found"; then the lines of
   the source: up to two of context before and after each error, a divider
   where more are left out, the lines with an error marked ">", and under
   each a caret at the error's column with a tilde for each further column
   of its span and the error's words. */
static const char *fx_syntax_error_message(const FxSource *s, const char *path) {
  const pm_parser_t *pr = &s->parser;
  int n = (int)pr->error_list.size, m = 0;
  FxRichErr *e = malloc(sizeof *e * (size_t)(n > 0 ? n : 1));
  for (const pm_diagnostic_t *d = (const pm_diagnostic_t *)pr->error_list.head; d && m < n;
       d = (const pm_diagnostic_t *)d->node.next, m++) {
    pm_line_column_t a = pm_newline_list_line_column(&pr->newline_list, d->location.start, 0);
    pm_line_column_t z = pm_newline_list_line_column(&pr->newline_list, d->location.end, 0);
    size_t len;
    const uint8_t *lp = fx_source_line(s, a.line, &len);
    size_t text = len && lp[len - 1] == '\n' ? len - 1 : len;
    int cs = (int)a.column, ce = z.line == a.line ? (int)z.column : (int)text;
    if (ce <= cs) ce = cs + 1;
    e[m] = (FxRichErr){ d, a.line, cs, ce, m };
  }
  n = m;
  FxMsgBuf b = { NULL, 0, 0 };
  int head_line = n ? e[0].li : 0;
  fx_msg_append(&b, sp_sprintf("%s:%d: syntax error%s found\n", path, head_line + pr->start_line, n > 1 ? "s" : ""));
  if (!n) { free(e); const char *r0 = sp_sprintf("%s", b.s); free(b.s); return r0; }
  qsort(e, (size_t)n, sizeof *e, fx_rich_err_cmp);
  /* the last line that is shown as context: the empty line after a
     trailing newline is not, though an error's own line always is */
  int final_line = 0;
  for (const uint8_t *p = s->start; p < s->end; p++) if (*p == '\n') final_line++;
  if (final_line > 0 && s->end > s->start && s->end[-1] == '\n') final_line--;
  int first_disp = e[0].li > 2 ? e[0].li - 2 : 0;
  int last_disp = e[n - 1].li + 2 < final_line ? e[n - 1].li + 2 : final_line;
  if (last_disp < e[n - 1].li) last_disp = e[n - 1].li;
  int width = 1;
  for (int v = last_disp + pr->start_line; v >= 10; v /= 10) width++;
  for (int v = first_disp + pr->start_line, w = 1; v >= 10; v /= 10) if (++w > width) width = w;
  int last_line = 0, window_end = 0;
  for (int idx = 0; idx < n; idx++) {
    int li = e[idx].li;
    int first_context = li > 2 ? li - 2 : 0;
    if (idx) {
      if (li > last_line && li - last_line > 3) { fx_msg_append(&b, "  "); fx_msg_repeat(&b, '~', width + 4); fx_msg_append(&b, "\n"); }
      if (last_line >= li) first_context = li;
      else if (first_context <= last_line) first_context = last_line + 1;
    }
    for (int l = first_context; l < li; l++) {
      fx_msg_append(&b, sp_sprintf("  %*d | ", width, l + pr->start_line));
      fx_msg_source_line(&b, s, l);
    }
    if (idx == window_end) {
      fx_msg_append(&b, sp_sprintf("> %*d | ", width, li + pr->start_line));
      fx_msg_source_line(&b, s, li);
      for (window_end = idx + 1; window_end < n && e[window_end].li == li; window_end++) {}
    }
    size_t len;
    const uint8_t *lp = fx_source_line(s, li, &len);
    if (lp == s->end) fx_msg_append(&b, "\n");   /* the empty line at the end has no newline of its own */
    fx_msg_append(&b, "  ");
    fx_msg_repeat(&b, ' ', width + 1);
    fx_msg_append(&b, "| ");
    fx_msg_repeat(&b, ' ', fx_cols(lp, 0, (size_t)e[idx].cs));
    fx_msg_append(&b, "^");
    fx_msg_repeat(&b, '~', fx_cols(lp, (size_t)e[idx].cs, (size_t)e[idx].ce) - 1);
    fx_msg_append(&b, " ");
    fx_msg_append(&b, e[idx].d->message);
    fx_msg_append(&b, "\n");
    last_line = li;
    int last_context = final_line;
    if (idx + 1 < n) { int next = e[idx + 1].li; last_context = next > last_line ? next - 1 : last_line; }
    for (int count = 0; count < 2 && last_line < last_context; count++) {
      ++last_line;
      fx_msg_append(&b, sp_sprintf("  %*d | ", width, last_line + pr->start_line));
      fx_msg_source_line(&b, s, last_line);
    }
  }
  free(e);
  const char *r = sp_sprintf("%s", b.s);
  free(b.s);
  return r;
}

/* Parses the bytes of a file into a new source, which starts at the line
   line, inside the scopes scopes (a piece of the console). Answers the
   message of the first syntax error, or NULL. */
/* The warnings of the parser for a source with no error, as CRuby gives
   them: each one goes to Warning.warn as "path:line: warning: message\n",
   so a program that defines Warning.warn gets them, and the Warning of
   Fluxpinel writes them to $stderr (lib/fluxpinel/warning.rb). prism
   tells which ones are for -w only: $VERBOSE false leaves those out, and
   $VERBOSE nil leaves all of them out. A source of Fluxpinel itself (the
   prelude) gives none. A Warning.warn that raises ends the load of the
   file, as in CRuby. */
static void fx_parse_warnings(const FxSource *s, const char *path) {
  if (!s->parser.warning_list.size || !L.object || path[0] == '<') return;
  sp_RbVal verbose = fx_gvar_get("$VERBOSE");
  if (verbose.tag == SP_TAG_NIL) return;
  int all = sp_poly_truthy(verbose);
  SP_GC_SAVE();
  sp_RbVal warning = sp_box_nil(), msg = sp_box_nil();
  const char *text = NULL;
  SP_GC_ROOT_RBVAL(warning);
  SP_GC_ROOT_RBVAL(msg);
  SP_GC_ROOT_STR(text);
  for (const pm_diagnostic_t *d = (const pm_diagnostic_t *)s->parser.warning_list.head; d;
       d = (const pm_diagnostic_t *)d->node.next) {
    if (d->level == PM_WARNING_LEVEL_VERBOSE && !all) continue;
    if (warning.tag == SP_TAG_NIL && !fx_const_lookup(L.object, sp_sym_intern("Warning"), &warning)) return;
    int line = (int)pm_newline_list_line_column(&s->parser.newline_list, d->location.start, s->parser.start_line).line;
    text = sp_sprintf("%s:%d: warning: %s\n", path, line, d->message);
    msg = fx_new_string((const uint8_t *)text, strlen(text));
    fx_send_v(warning, "warn", 1, &msg, NULL, 0, NULL);
    if (L.ctl != FX_CTL_NONE) return;
  }
}

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
  fx_source_index(s);
  *out = s;
  if (s->parser.error_list.size) return fx_syntax_error_message(s, path);
  fx_parse_warnings(s, path);
  return NULL;
}

static const char *fx_parse(const char *path, const char *real, char *buf, size_t len, FxSource **out) {
  return fx_parse_in(path, real, buf, len, 1, NULL, 0, out);
}

/* Gives the new frame top the context of the frame ctx: the def target,
   the default visibility, the method (for super) and its block. */
static void fx_frame_context(FxFrame *top, const FxFrame *ctx) {
  top->definee = ctx->definee;
  top->singleton_defs = ctx->singleton_defs;
  top->vis = ctx->vis;
  top->vis_scope = ctx->vis_scope;   /* a scope of its own (a String of instance_eval), or the caller's */
  top->method = ctx->method;
  top->block = ctx->block;
}

/* Runs the code src in a new scope under the frame under, as a block
   does: it reads and writes the locals of that frame and of the scopes
   around it, and its own new locals stay in it. self, the lexical class
   and the context are those of the frame ctx. prism gets the names of the
   frames as the scopes around the code. With kept, the new scope moves to
   the heap after the run, into *kept, so that a later run under it sees
   its locals (Binding#eval). */
static sp_RbVal fx_eval_in(FxFrame *under, const FxFrame *ctx, const char *src, const char *path, int line,
                           FxFrame **kept) {
  int n = 0;
  for (FxFrame *p = under; p; p = p->parent) n++;
  const pm_constant_id_list_t **scopes = malloc(sizeof *scopes * (size_t)n);
  int i = n;
  for (FxFrame *p = under; p; p = p->parent) scopes[--i] = p->names;
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
  FX_FRAME(top, &prog->locals, ctx->self, ctx->cref);
  fx_frame_context(&top, ctx);
  top.parent = under;
  /* a backtrace names the frame of an eval after its method, as CRuby does */
  fx_frame_tag(&top, top.method ? FX_TR_METHOD : FX_TR_TOP, top.method, NULL, NULL, top.self.tag == SP_TAG_CLASS, 0);
  sp_RbVal v = fx_eval_stmts(&top, prog->statements);
  if (kept) {
    SP_GC_ROOT_RBVAL(v);
    *kept = fx_frame_heap(&top);
  }
  return v;
}

/* Kernel#eval(src, binding = nil, file = "(eval at FILE:LINE)", line = 1):
   the default file names the call (fx_eval_source_name). With no
   binding, the code runs under the frame of the call, with its self, its
   lexical class and the block of its method, as in CRuby. With a Binding,
   it runs under the frames of the Binding (binding.c). */
static sp_RbVal fx_eval_string(FxFrame *f, int argc, sp_RbVal *argv) {
  if (argc > 4) fx_arity_error(argc, 1, 3, 0);
  const char *src = sp_poly_arg_str_chk(argv[0]);
  const char *path = argc >= 3 && argv[2].tag != SP_TAG_NIL ? sp_poly_arg_str_chk(argv[2]) : NULL;
  int line = argc >= 4 && argv[3].tag != SP_TAG_NIL ? (int)sp_poly_arg_int_chk(argv[3]) : 1;
  if (argc >= 2 && argv[1].tag != SP_TAG_NIL) {
    if (!fx_is_binding(argv[1]))
      sp_raise_cls("TypeError", sp_sprintf("wrong argument type %s (expected binding)", sp_poly_class_name(argv[1])));
    return fx_binding_eval(argv[1], src, path, line);
  }
  return fx_eval_in(f, f, src, path ? path : fx_eval_source_name(f), line, NULL);
}

/* Runs the top level of a source, as a program of its own. A return at
   its top level ends the file. The first main program sets
   TOPLEVEL_BINDING to its scope. */
static sp_RbVal fx_run_source_as(const FxSource *s, int main) {
  static int toplevel;
  SP_GC_SAVE();
  const pm_program_node_t *prog = (const pm_program_node_t *)s->root;
  FX_FRAME(top, &prog->locals, L.main, L.object);
  top.vis = FX_VIS_PRIVATE;   /* a top-level def is a private method of Object */
  /* a backtrace names the top level of a file that a require or a load
     runs "<top (required)>" (levels of a FX_TR_TOP entry, fx_bt_label) */
  if (!main) fx_frame_tag(&top, FX_TR_TOP, NULL, NULL, NULL, 0, 1);
  if (main && !toplevel) {
    toplevel = 1;
    fx_const_set(L.object, sp_sym_intern("TOPLEVEL_BINDING"), fx_binding_new(&top, "<main>", 0));
  }
  sp_RbVal v = fx_eval_stmts(&top, prog->statements);
  if (L.ctl == FX_CTL_RETURN && L.ctl_target == &top) L.ctl = FX_CTL_NONE;
  return v;
}

static sp_RbVal fx_run_source(const FxSource *s) {
  return fx_run_source_as(s, 0);
}

/* Parses the main program from the source buf. A syntax error ends
   Fluxpinel, as it ends ruby. */
static FxSource *fx_parse_main(const char *path, const char *real, char *buf, size_t len) {
  FxSource *s;
  const char *err = fx_parse(path, real, buf, len, &s);
  if (err) {
    /* as ruby reports it: the path, the first line of the message with the
       class, then the lines of the source */
    const char *nl = strchr(err, '\n');
    fprintf(stderr, "%s: %.*s (SyntaxError)\n%s\n", path, (int)(nl ? nl - err : (long)strlen(err)), err, nl ? nl + 1 : "");
    exit(1);
  }
  return s;
}

/* Runs the main program. An exception that reaches its top gets the
   backtrace of its frames at the landing (fx_bt_land) and goes on, as the
   same object, to the report of the runtime, which prints the backtrace
   of the exception when sp_bt_enabled is set (as a debug build of spinel
   sets it). The report then reads as CRuby's: the frame of the raise, the
   message and the class, then the callers. */
static void fx_run_source_top(const FxSource *s, int main) {
  SP_GC_SAVE();
  const int depth = fx_fs()->depth;
  FX_TRY_PUSH();
  if (setjmp(sp_exc_stack[sp_exc_top - 1]) == 0) {
    fx_run_source_as(s, main);
    sp_exc_top--;
    return;
  }
  FX_TRY_LANDED(depth);
  if (sp_unwind_kind != SP_UNWIND_NONE) sp_unwind_resume();
  FxCaught c = fx_caught();
  fx_caught_obj(&c);
  sp_bt_enabled = 1;
  fx_reraise(c);
}

static void fx_run_main_source(const FxSource *s) {
  fx_run_source_top(s, 1);
}

static void fx_load_main_src(const char *path, const char *real, char *buf, size_t len) {
  fx_run_main_source(fx_parse_main(path, real, buf, len));
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
   NULL. Answers 0, and runs nothing, for a file that is loaded already, or
   that is loading now (a require of the file from inside it). A file whose
   run raised, or that a throw (or another jump of the runtime) left, is
   not loaded, as in CRuby: the next require parses and runs it again, and
   $LOADED_FEATURES does not name it. The landing goes on with the jump
   first, as the other landings do. */
static int fx_load_once(char *real, const char *extra) {
  for (int i = 0; i < L.nsources; i++)
    if (L.sources[i]->loaded && !strcmp(L.sources[i]->real, real)) {
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
  s->file = 1;
  s->loaded = 1;
  const int depth = fx_fs()->depth;
  FX_TRY_PUSH();
  if (setjmp(sp_exc_stack[sp_exc_top - 1]) == 0) {
    fx_run_source(s);
    sp_exc_top--;
  } else {
    FX_TRY_LANDED(depth);
    s->loaded = 0;
    if (sp_unwind_kind != SP_UNWIND_NONE) sp_unwind_resume();
    fx_reraise(fx_caught());
  }
  /* $LOADED_FEATURES names the file that a require ran. The first call
     allocates the Array, so the Array comes before the copy of the path:
     a collection in between would free a copy that nothing holds yet.
     real is memory of malloc, with no marker byte of a String of spinel
     before it: sp_str_dup reads that byte, sp_str_dup_external does not. */
  sp_StrArray *feats = sp_get_loaded_features();
  fx_strs_push(feats, sp_str_dup_external(real));
  return 1;
}

/* require_relative "x", and require "./x" or require "/x". Answers true
   after it runs the file, and false for a file that is loaded already. */
/* Whether s is the source of an eval with no file of its own, named
   "(eval at FILE:LINE)" (fx_eval_source_name): it has no directory, so
   __dir__ is nil in it and require_relative cannot infer a base path. */
static int fx_source_is_eval(const FxSource *s) { return s->real && !strncmp(s->real, "(eval", 5); }

static sp_RbVal fx_require(const pm_node_t *n, const char *arg, int rel) {
  const char *path = arg, *file = NULL, *shown = NULL;
  SP_GC_ROOT_STR(path);
  SP_GC_ROOT_STR(file);
  SP_GC_ROOT_STR(shown);
  /* an absolute path names the file itself */
  if (rel && arg[0] != '/') {
    /* an eval with no file has no directory, as in CRuby */
    if (fx_source_is_eval(fx_source_of(n))) sp_raise_cls("LoadError", "cannot infer basepath");
    char *dir = fx_dirname(fx_source_of(n)->real);
    path = sp_sprintf("%s/%s", dir, arg);
    free(dir);
  }
  /* `require_relative ".."` names the parent directory, plus .rb */
  path = fx_path_clean(path);
  size_t plen = strlen(path);
  file = plen > 3 && !strcmp(path + plen - 3, ".rb") ? path : sp_sprintf("%s.rb", path);
  char *real = fx_real_path(file);
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
     (thread, fiber, ...) load nothing, and the require answers false;
     fcntl makes the Fcntl module, and its first require answers true.
   - Any other name is a LoadError. (spinel refuses it at compile time.)
   A require of set answers false, as Set is loaded before the program in
   CRuby. */
static sp_RbVal fx_require_lib(const char *name) {
  static const char *const builtin[] = { "io/console", "monitor", "time", "socket", "ostruct",
                                         "thread", "enumerator", "fiber", "ffi", "rational", "complex",
                                         "io/wait", "rbconfig", "rubygems", "fcntl", NULL };
  static const char *const preloaded[] = { "set", NULL };
  char *found = fx_load_path_find(name);
  if (found) {
    if (!fx_load_once(found, NULL)) return sp_box_bool(0);
    return L.ctl == FX_CTL_NONE ? sp_box_bool(1) : sp_box_nil();
  }
  /* lib/<name>.rb of Fluxpinel: a feature that spinel writes at compile
     time, whose package is a stub (forwardable) */
  char *own = fx_real_path(sp_sprintf("%s/%s.rb", FX_LIB_DIR, name));
  if (own) {
    if (!fx_load_once(own, NULL)) return sp_box_bool(0);
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
  char *real = pkg ? fx_real_path(sp_sprintf("%s.rb", path)) : NULL;
  if (real) {
    struct stat st;
    extra = sp_sprintf("%s.share.rb", path);
    int has_extra = fx_path_embedded(extra) ? fx_embed_find(extra) >= 0 : !stat(extra, &st);
    int ran = fx_load_once(real, has_extra ? extra : NULL);
    /* pty: PTY.open and the block form of PTY.spawn, which the package
       leaves out for the typing of a compiled program (lib/fluxpinel/pty.rb) */
    if (ran && !strcmp(name, "pty")) fx_load_once(strdup(FX_LIB_DIR "/fluxpinel/pty.rb"), NULL);
    /* etc: Etc.nprocessors, which the package does not have (lib/fluxpinel/etc.rb) */
    if (ran && !strcmp(name, "etc")) fx_load_once(strdup(FX_LIB_DIR "/fluxpinel/etc.rb"), NULL);
    /* strscan: StringScanner::Version and Id (lib/fluxpinel/strscan.rb) */
    if (ran && !strcmp(name, "strscan")) fx_load_once(strdup(FX_LIB_DIR "/fluxpinel/strscan.rb"), NULL);
    /* openssl: OpenSSL::KDF.pbkdf2_hmac with its loop in C (lib/fluxpinel/openssl.rb) */
    if (ran && !strcmp(name, "openssl")) fx_load_once(strdup(FX_LIB_DIR "/fluxpinel/openssl.rb"), NULL);
    if (!ran || fx_lib_in(name, preloaded)) return sp_box_bool(0);
    return L.ctl == FX_CTL_NONE ? sp_box_bool(1) : sp_box_nil();
  }
  if (fx_lib_in(name, builtin)) {
    fx_feature_loaded = 1;
    fx_feature_add(name);
    if (!strcmp(name, "io/console")) fx_console_loaded = 1;
    if (!strcmp(name, "ostruct")) fx_ostruct_loaded = 1;
    /* fcntl is the one feature a require makes here (the Fcntl module,
       fx_builtin), so its first require answers true, as CRuby's does */
    if (!strcmp(name, "fcntl")) {
      int first = !fx_fcntl_loaded;
      fx_fcntl_loaded = 1;
      return sp_box_bool(first);
    }
    return sp_box_bool(0);
  }
  sp_raise_load_error(name);
}

/* require_relative, require of a path, and __dir__: the calls that answer
   from the file of the call node n, unless the program defines a method of
   the name for self. on_kernel is set for `Kernel.load(path)` and its kin,
   which a method of self does not shadow. Answers 1 and sets *out when it
   handles the call. */
static int fx_load_call(const pm_node_t *n, sp_RbVal self, int on_kernel, const char *name, int argc,
                          sp_RbVal *argv, sp_RbVal *out) {
  /* the first letter turns away most calls, before a strcmp */
  if (name[0] != '_' && name[0] != 'r' && name[0] != 'l') return 0;
  if (!FX_IS("__dir__") && !FX_IS("require_relative") && !FX_IS("require") && !FX_IS("load")) return 0;
  if (!on_kernel && fx_find_method(fx_lookup_class(self), sp_sym_intern(name))) return 0;
  /* the file runs under a frame of the call, as in a backtrace of CRuby
     ("in 'Kernel#load'" above "in '<top (required)>'") */
  int bt __attribute__((cleanup(fx_bt_builtin_exit))) = FX_IS("__dir__") ? -1 : fx_bt_builtin_enter("Kernel", name, 0);
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
    char *real = fx_real_path(path);
    FxSource *s;
    const char *err = fx_parse(strdup(path), real ? real : strdup(path), buf, len, &s);
    if (err) sp_raise_cls("SyntaxError", err);
    s->file = 1;
    fx_run_source(s);
    *out = L.ctl == FX_CTL_NONE ? sp_box_bool(1) : sp_box_nil();
    return 1;
  }
  if (FX_IS("load")) return 0;
  if (FX_IS("__dir__") && argc == 0) {
    if (fx_source_is_eval(fx_source_of(n))) { *out = sp_box_nil(); return 1; }   /* nil, as in CRuby */
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
