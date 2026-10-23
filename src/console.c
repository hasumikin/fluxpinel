/* console.c -- the console: a session takes lines one at a time and
   evaluates each complete piece (docs/design.md, section 5.1).

   A session keeps the frame of each piece. The frame is a heap frame
   (FxEnv) whose parent is the frame of the piece before, so a later piece
   reads a local of an earlier one as a local of an outer scope, as a block
   does. prism gets the names of these frames as the scopes around the new
   piece (pm_options_scopes), so that it parses each name as a local. The
   first frame holds `_`, the value of the last piece.

   Fluxpinel.session and Fluxpinel.feed (kernel/fluxpinel.rb) give a session
   to AOT code, which owns the socket. `fluxpinel --console` runs a session
   on stdin. */

#include "fluxpinel.h"

#define FX_PROMPT "fx> "
#define FX_PROMPT_MORE "fx* "

typedef struct {
  sp_int cls_id;                         /* first, as in an object of spinel */
  char *buf;                             /* the lines of the piece so far */
  size_t len, cap;
  FxFrame *frame;                        /* the frame of the last piece */
  FxFrame *root;                         /* the frame of `_` */
  const pm_constant_id_list_t **scopes;  /* the names of each frame, root first */
  int nscopes, scopes_cap;
  int line;                              /* the first line of the next piece */
  int closed;                            /* a piece raised SystemExit */
  int status;                            /* the status of that SystemExit */
} FxSession;

static FxClass *fx_session_cls;

static void fx_session_scan(void *p) {
  FxSession *s = (FxSession *)p;
  if (s->frame) sp_gc_mark(s->frame);
  if (s->root) sp_gc_mark(s->root);
}

/* Fluxpinel::Session, in the module Fluxpinel of the kernel when there is
   one. Its instance is a C struct, as that of a native class is. */
static FxClass *fx_session_class(void) {
  if (fx_session_cls) return fx_session_cls;
  sp_sym fx = sp_sym_intern("Fluxpinel");
  sp_RbVal v;
  FxClass *mod = fx_const_lookup(L.object, fx, &v) ? fx_class_value(v) : NULL;
  if (!mod) {
    mod = fx_class_new("Fluxpinel", 1, NULL, L.object);
    fx_const_set(L.object, fx, fx_class_box(mod));
  }
  fx_session_cls = fx_class_new("Fluxpinel::Session", 0, L.object, mod);
  fx_session_cls->native = 1;
  fx_const_set(mod, sp_sym_intern("Session"), fx_class_box(fx_session_cls));
  return fx_session_cls;
}

static void fx_session_scope(FxSession *s, const pm_constant_id_list_t *names) {
  if (s->nscopes == s->scopes_cap) {
    s->scopes_cap = s->scopes_cap ? s->scopes_cap * 2 : 8;
    s->scopes = realloc(s->scopes, sizeof *s->scopes * (size_t)s->scopes_cap);
  }
  s->scopes[s->nscopes++] = names;
}

static sp_RbVal fx_session_new(void) {
  SP_GC_SAVE();
  static pm_constant_id_list_t names;   /* `_` */
  if (!names.size) {
    pm_constant_id_list_init(&names);
    pm_constant_id_list_append(&names, pm_constant_pool_insert_constant(&L.pool, (const uint8_t *)"_", 1));
  }
  FxSession *s = (FxSession *)sp_gc_alloc(sizeof *s, NULL, fx_session_scan);
  s->cls_id = fx_session_class()->inst_id;
  s->line = 1;
  SP_GC_ROOT(s);
  FX_FRAME(top, &names, L.main, L.object);
  top.vis = FX_VIS_PRIVATE;
  FxFrame *h = fx_frame_heap(&top);
  sp_gc_wb(s);
  s->root = s->frame = h;
  fx_session_scope(s, &names);
  return sp_box_obj(s, (int)s->cls_id);
}

static FxSession *fx_session_of(sp_RbVal v) {
  if (v.tag != SP_TAG_OBJ || !fx_session_cls || v.cls_id != fx_session_cls->inst_id || !v.v.p)
    sp_raise_cls("TypeError", sp_sprintf("wrong argument type %s (expected Fluxpinel::Session)",
                                         sp_poly_class_name(v)));
  return (FxSession *)v.v.p;
}

/* A diagnostic of prism whose name ends in _term or has _term_ (a missing
   end or close), or ends in _eof (an open literal). */
static int fx_wants_more(const char *name) {
  size_t n = strlen(name);
  if (n > 4 && !strcmp(name + n - 4, "_eof")) return 1;
  for (const char *t = strstr(name, "_term"); t; t = strstr(t + 1, "_term"))
    if (t[5] == 0 || t[5] == '_') return 1;
  return 0;
}

/* Whether the buffer is a complete piece (0), the start of one (1), or a
   syntax error (2, with *msg). The start of a piece has an error at its end
   or an error of a missing close, and no stray token before its end (`)`,
   `end`, `1 2`). */
static int fx_session_state(FxSession *s, const char **msg) {
  pm_options_t o = { 0 };
  pm_options_filepath_set(&o, "(console)");
  pm_options_line_set(&o, s->line);
  fx_options_scopes(&o, s->scopes, s->nscopes);
  pm_parser_t p;
  pm_parser_init(&p, (const uint8_t *)s->buf, s->len, &o);
  pm_node_t *root = pm_parse(&p);
  size_t n = s->len;
  while (n > 0 && isspace((unsigned char)s->buf[n - 1])) n--;
  int more = 0, stray = 0;
  for (const pm_diagnostic_t *d = (const pm_diagnostic_t *)p.error_list.head; d;
       d = (const pm_diagnostic_t *)d->node.next) {
    size_t at = (size_t)(d->location.start - p.start);
    const char *name = pm_diagnostic_id_human(d->diag_id);
    if (at >= n || fx_wants_more(name)) more = 1;
    if (at < n && (!strcmp(name, "unexpected_token_ignore") || !strcmp(name, "expect_eol_after_statement")))
      stray = 1;
  }
  int state = 0;
  if (p.error_list.size) {
    state = more && !stray ? 1 : 2;
    const pm_diagnostic_t *d = (const pm_diagnostic_t *)p.error_list.head;
    int at = pm_newline_list_line_column(&p.newline_list, d->location.start, p.start_line).line;
    *msg = sp_sprintf("(console):%d: %s", at, d->message);
  }
  pm_node_destroy(&p, root);
  pm_parser_free(&p);
  pm_options_free(&o);
  return state;
}

/* Runs a complete piece in a new frame under the frame of the last piece,
   and keeps that frame. Answers the reply: the inspect of the value, or
   the exception. A SystemExit (exit, abort) closes the session, and the
   answer is NULL. A throw out of the piece goes on. */
static const char *fx_session_run(FxSession *s, char *buf, size_t len, int line) {
  SP_GC_SAVE();
  SP_GC_ROOT(s);
  FxSource *src;
  const char *err = fx_parse_in("(console)", "(console)", buf, len, line, s->scopes, s->nscopes, &src);
  if (err) return sp_sprintf("%s (SyntaxError)\n" FX_PROMPT, err);
  const pm_program_node_t *prog = (const pm_program_node_t *)src->root;
  FX_FRAME(top, &prog->locals, L.main, L.object);
  top.vis = FX_VIS_PRIVATE;
  top.parent = s->frame;
  sp_RbVal v = sp_box_nil();
  SP_GC_ROOT_RBVAL(v);
  const char *out = NULL;
  SP_GC_ROOT_STR(out);
  const int depth = fx_fs()->depth;
  FX_TRY_PUSH();
  if (setjmp(sp_exc_stack[sp_exc_top - 1]) == 0) {
    v = fx_eval_stmts(&top, prog->statements);
    L.ctl = FX_CTL_NONE;
    out = sp_sprintf("=> %s\n" FX_PROMPT, sp_poly_inspect(v));
    sp_exc_top--;
    fx_frame_wb(s->root);
    s->root->locals[0] = v;
  } else {
    FX_TRY_LANDED(depth);
    if (sp_unwind_kind != SP_UNWIND_NONE) sp_unwind_resume();
    L.ctl = FX_CTL_NONE;
    FxCaught c = fx_caught();
    if (sp_exc_cls_matches(c.cls, "SystemExit")) {
      s->closed = 1;
      s->status = (int)sp_exc_exit_status(fx_caught_obj(c));
    } else {
      out = sp_sprintf("%s (%s)\n" FX_PROMPT, c.msg ? c.msg : "", c.cls);
    }
  }
  FxFrame *h = fx_frame_heap(&top);
  sp_gc_wb(s);
  s->frame = h;
  fx_session_scope(s, &prog->locals);
  return out;
}

/* Adds a line to the piece, and runs the piece when it is complete.
   Answers the reply and the next prompt, or NULL when the session is
   closed. */
static const char *fx_session_feed(FxSession *s, const char *line, size_t n) {
  if (s->closed) return NULL;
  if (s->len + n + 2 > s->cap) {
    s->cap = (s->len + n + 2) * 2;
    s->buf = realloc(s->buf, s->cap);
  }
  memcpy(s->buf + s->len, line, n);
  s->len += n;
  if (!s->len || s->buf[s->len - 1] != '\n') s->buf[s->len++] = '\n';
  s->buf[s->len] = 0;
  const char *msg = NULL;
  int state = fx_session_state(s, &msg);
  if (state == 1) return FX_PROMPT_MORE;
  /* the piece leaves the buffer; its source keeps its own bytes */
  char *piece = s->buf;
  size_t len = s->len;
  int first = s->line;
  for (size_t i = 0; i < len; i++) s->line += piece[i] == '\n';
  s->buf = NULL;
  s->len = s->cap = 0;
  if (state == 2) {
    free(piece);
    return sp_sprintf("%s (SyntaxError)\n" FX_PROMPT, msg);
  }
  if (len == 1) {   /* an empty line */
    free(piece);
    return FX_PROMPT;
  }
  return fx_session_run(s, piece, len, first);
}

#ifdef FX_KERNEL

/* Fluxpinel.session */
sp_RbVal fx_api_session(void) {
  fx_api_check();
  return fx_session_new();
}

/* Fluxpinel.feed(session, line) */
const char *fx_api_feed(sp_RbVal session, const char *line) {
  fx_api_check();
  SP_GC_SAVE();
  SP_GC_ROOT_RBVAL(session);
  return fx_session_feed(fx_session_of(session), line, sp_str_byte_len(line));
}

#endif

/* fluxpinel --console: a session on stdin and stdout. An exit in the
   session ends the program with its status. */
static void fx_console_stdin(void) {
  SP_GC_SAVE();
  sp_RbVal sv = fx_session_new();
  SP_GC_ROOT_RBVAL(sv);
  FxSession *s = fx_session_of(sv);
  char *line = NULL;
  size_t cap = 0;
  ssize_t n;
  fputs(FX_PROMPT, stdout);
  fflush(stdout);
  while ((n = getline(&line, &cap, stdin)) >= 0) {
    const char *reply = fx_session_feed(s, line, (size_t)n);
    if (!reply) break;
    fputs(reply, stdout);
    fflush(stdout);
  }
  free(line);
  if (s->closed) sp_exit_raise(s->status);
  fputs("\n", stdout);
}
