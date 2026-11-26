/* exc.c -- exceptions in the walker.

   An exception is a spinel sp_Exception, boxed with SP_BUILTIN_EXCEPTION.
   It carries its class by name (cls_name). For a class of the program,
   the name is the FxClass name, and the runtime asks Fluxpinel for the
   superclass through sp_user_exc_parent_fn. Thus sp_exc_cls_matches and
   the uncaught-exception report see the classes of the program.

   walk.c holds begin/rescue/else/ensure, which push a handler of the
   runtime as the code that spinel emits does. */

#include "fluxpinel.h"

#define FX_IS(s) (!strcmp(name, (s)))

static int fx_is_exc(sp_RbVal v) {
  return v.tag == SP_TAG_OBJ && v.cls_id == SP_BUILTIN_EXCEPTION && v.v.p;
}

static sp_RbVal fx_exc_box(sp_Exception *e) {
  return sp_box_obj(e, SP_BUILTIN_EXCEPTION);
}

/* Whether k is Exception or a subclass of it. */
static int fx_is_exc_class(const FxClass *k) {
  for (const FxClass *c = k; c; c = c->super)
    if (c->cls_id < 0 && !strcmp(c->name, "Exception")) return 1;
  return 0;
}

/* The runtime asks for the superclass of a class of the program by name:
   a class of Fluxpinel, else a class of the kernel. */
static const char *fx_exc_parent(const char *name) {
  FxClass *k;
  FX_HOOKED(k = fx_class_named(name));
  if (k && k->cls_id >= FX_CLS_BASE) return k->super ? k->super->name : NULL;
  return fx_kernel_hooks.exc_parent ? fx_kernel_hooks.exc_parent(name) : NULL;
}

/* The message of e: nil gives none (#message then answers the class name);
   any other value gives its to_s. */
static void fx_exc_set_msg(sp_Exception *e, sp_RbVal m) {
  SP_GC_ROOT(e);
  const char *t = m.tag == SP_TAG_NIL ? NULL : sp_poly_to_s(m);
  SP_GC_ROOT_STR(t);
  /* a copy by the byte length, which keeps a NUL in the message */
  const char *s = t ? sp_str_dup(t) : NULL;
  sp_gc_wb(e);
  e->msg = s;
  e->msg_h = NULL;
}

/* Whether the class of the exception v is the builtin class name, or a
   subclass of it. */
static int fx_exc_is(sp_RbVal v, const char *name) {
  for (FxClass *k = fx_class_of(v); k; k = k->super)
    if (!strcmp(k->name, name)) return 1;
  return 0;
}

/* Exception#initialize: an optional message. */
static void fx_exc_init(sp_RbVal v, int argc, sp_RbVal *argv) {
  sp_PolyPolyHash *kw = NULL;
  if (argc > 0 && argv[argc - 1].tag == SP_TAG_OBJ && argv[argc - 1].v.p == L.kwhash)
    kw = sp_poly_as_poly_poly_hash(argv[--argc]);
  /* KeyError.new(msg, receiver: r, key: k) and FrozenError.new(msg,
     receiver: r) record the values that #receiver and #key answer */
  if (kw) {
    sp_Exception *e = (sp_Exception *)v.v.p;
    int key_error = fx_exc_is(v, "KeyError");
    sp_RbVal recv_k = sp_box_sym(sp_sym_intern("receiver")), key_k = sp_box_sym(sp_sym_intern("key"));
    if ((key_error || fx_exc_is(v, "FrozenError")) && sp_PolyPolyHash_has_key(kw, recv_k)) {
      sp_gc_wb(e);
      e->xrecv = sp_PolyPolyHash_get(kw, recv_k);
      e->has_recv = 1;
    }
    if (key_error && sp_PolyPolyHash_has_key(kw, key_k)) {
      sp_gc_wb(e);
      e->xkey = sp_PolyPolyHash_get(kw, key_k);
      e->has_key = 1;
    }
  }
  /* SystemExit.new(status = 0, msg): a first argument of true (0), false
     (1) or an Integer (a Float through to_int) is the status, which the
     runtime keeps in the result slot (sp_exc_exit_status); any other value
     leaves the status 0 and stays the message */
  if (fx_exc_is(v, "SystemExit")) {
    sp_int st = 0;
    if (argc > 0 && argv[0].tag == SP_TAG_BOOL) { st = !argv[0].v.b; argc--; argv++; }
    else if (argc > 0 && argv[0].tag == SP_TAG_INT) { st = argv[0].v.i; argc--; argv++; }
    else if (argc > 0 && argv[0].tag == SP_TAG_FLT) { st = (sp_int)argv[0].v.f; argc--; argv++; }
    sp_Exception *e = (sp_Exception *)v.v.p;
    sp_gc_wb(e);
    e->result = sp_box_int(st);
  }
  /* SignalException.new(sig) and SignalException.new(signo, message): the
     runtime reads the signal, by its name with or without the SIG or by
     its number, and a value that is no signal is an ArgumentError. The
     message is the name with the SIG, or the message given. Interrupt has
     an initialize of its own (a message, SIGINT). */
  if (fx_exc_is(v, "SignalException") && !fx_exc_is(v, "Interrupt")) {
    if (argc == 0 || argc > 2 || (argc == 2 && argv[0].tag != SP_TAG_INT))
      sp_raise_cls("ArgumentError", sp_sprintf("wrong number of arguments (given %d, expected 1)", argc));
    int no = sp_signal_resolve(argv[0]);
    sp_Exception *se = (sp_Exception *)v.v.p;
    sp_gc_wb(se);
    se->xkey = sp_box_int((sp_int)no);
    if (argc == 2) {
      fx_exc_set_msg(se, argv[1]);
    } else {
      const char *nm = sp_signal_signame((sp_int)no);
      const char *text = sp_sprintf("SIG%s", nm ? nm : "?");
      SP_GC_ROOT_STR(text);
      fx_exc_set_msg(se, fx_new_string((const uint8_t *)text, strlen(text)));
    }
    return;
  }
  /* the builtin classes with more arguments (NameError, StopIteration,
     ...) keep them in fields that Fluxpinel does not set yet */
  if (argc > 1) {
    static const char *const more[] = { "NameError", "KeyError", "SignalException", "StopIteration",
                                        "FrozenError", "UncaughtThrowError", "NoMatchingPatternKeyError",
                                        "LocalJumpError", "SystemCallError", NULL };
    for (FxClass *k = fx_class_of(v); k; k = k->super)
      for (int i = 0; more[i]; i++)
        if (!strcmp(k->name, more[i])) fx_gap(sp_sprintf("%s.new with %d arguments", k->name, argc));
    sp_raise_cls("ArgumentError", sp_sprintf("wrong number of arguments (given %d, expected 0..1)", argc));
  }
  sp_Exception *e = (sp_Exception *)v.v.p;
  /* SystemCallError and Errno::*: the text of the number, then " - msg",
     as rb_syserr_initialize builds it, and the number for #errno */
  sp_int num = 0;
  int kind = sp_syserr_kind(e->cls_name, &num);
  if (kind == SP_SYSERR_BASE && argc == 0) fx_arity_error(0, 1, 2, 0);
  if (kind == SP_SYSERR_NUM || kind == SP_SYSERR_BASE) {
    const char *m = argc >= 1 && argv[0].tag != SP_TAG_NIL ? sp_poly_to_s(argv[0]) : NULL;
    SP_GC_ROOT_STR(m);
    const char *text = sp_syserr_text(kind == SP_SYSERR_NUM, num, NULL, m);
    SP_GC_ROOT_STR(text);
    fx_exc_set_msg(e, fx_new_string((const uint8_t *)text, strlen(text)));
    sp_exc_syserr_init(e);
    return;
  }
  if (argc >= 1) fx_exc_set_msg(e, argv[0]);
}

/* k.new(*args) for an exception class k: the program's initialize if it
   has one, else the builtin one. */
static sp_RbVal fx_exc_new(FxClass *k, int argc, sp_RbVal *argv, const FxBlock *blk) {
  sp_RbVal v = fx_exc_box(sp_exc_new(k->name, NULL));
  SP_GC_ROOT_RBVAL(v);
  /* sp_exc_new marks a receiver and a key as recorded, for a raise of the
     runtime; an explicit new records neither until its keywords do */
  sp_Exception *e = (sp_Exception *)v.v.p;
  e->has_recv = 0;
  e->has_key = 0;
  FxMethod *init = fx_find_method(k, sp_sym_intern("initialize"));
  if (init) fx_invoke_b(init, v, argc, argv, blk);
  else fx_exc_init(v, argc, argv);
  return v;
}

/* The status of exit and exit!: true is 0, false is 1. */
static int fx_exit_status(int argc, sp_RbVal *argv) {
  if (argc > 1) fx_arity_error(argc, 0, 1, 0);
  if (argc == 0) return 0;
  if (argv[0].tag == SP_TAG_BOOL) return argv[0].v.b ? 0 : 1;
  return (int)sp_poly_arg_int_chk(argv[0]);
}

/* Kernel#exit and #abort raise a SystemExit, which ends the program when
   no rescue takes it, as the runtime does for AOT code. exit! ends it at
   once, with no at_exit. */
static int fx_exit_kernel(const char *name, int argc, sp_RbVal *argv) {
  if (FX_IS("exit")) sp_exit_raise(fx_exit_status(argc, argv));
  if (FX_IS("exit!")) {
    int st = argc == 0 ? 1 : fx_exit_status(argc, argv);
    fflush(NULL);
    _exit(st);
  }
  if (FX_IS("abort")) {
    if (argc > 1) fx_arity_error(argc, 0, 1, 0);
    sp_abort_raise(argc ? sp_poly_arg_str_chk(argv[0]) : NULL);
  }
  return 0;
}

/* Kernel#raise and #fail, throw, and exit and abort. */
static int fx_exc_kernel(const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (fx_exit_kernel(name, argc, argv)) return 1;
  /* the runtime finds the catch of the tag, or raises UncaughtThrowError */
  if (FX_IS("throw")) {
    if (argc < 1 || argc > 2) fx_arity_error(argc, 1, 2, 0);
    sp_throw_boxed(argv[0], argc == 2 ? argv[1] : sp_box_nil());
  }
  if (!FX_IS("raise") && !FX_IS("fail")) return 0;
  (void)out;
  /* raise ..., cause: x: the runtime takes it for the next raise */
  if (argc > 0 && argv[argc - 1].tag == SP_TAG_OBJ && argv[argc - 1].v.p == L.kwhash) {
    sp_PolyPolyHash *kw = sp_poly_as_poly_poly_hash(argv[--argc]);
    sp_RbVal key = sp_box_sym(sp_sym_intern("cause"));
    if (kw && sp_PolyPolyHash_has_key(kw, key)) {
      sp_RbVal c = sp_PolyPolyHash_get(kw, key);
      if (c.tag != SP_TAG_NIL && !fx_is_exc(c)) sp_raise_cls("TypeError", "exception object expected");
      sp_explicit_cause = c.tag == SP_TAG_NIL ? NULL : c.v.p;
      sp_explicit_cause_set = 1;
    }
    if (argc == 0) sp_raise_cls("ArgumentError", "only cause is given with no message");
  }
  if (argc == 0) {
    /* a bare raise raises the exception that a rescue handles */
    if (sp_rescue_sp > 0) {
      sp_reraise_current = 1;
      sp_raise_exc((sp_Exception *)sp_exc_handling[sp_rescue_sp - 1]);
    }
    sp_raise_cls("RuntimeError", sp_exc_no_msg);
  }
  sp_RbVal a = argv[0];
  if (fx_is_string(a)) {
    if (argc > 1) sp_raise_cls("TypeError", "exception class/object expected");
    /* an empty message stays empty, and a NUL in it stays */
    sp_raise_cls("RuntimeError", sp_exc_msg_given(sp_poly_recv_s(a, name)));
  }
  if (fx_is_exc(a)) {
    if (argc >= 2) fx_exc_set_msg((sp_Exception *)a.v.p, argv[1]);
    sp_raise_exc((sp_Exception *)a.v.p);
  }
  FxClass *k = fx_class_value(a);
  if (!k || !fx_is_exc_class(k)) sp_raise_cls("TypeError", "exception class/object expected");
  sp_RbVal e = fx_send(a, "new", argc >= 2 ? 1 : 0, argv + 1);
  SP_GC_ROOT_RBVAL(e);
  if (!fx_is_exc(e)) sp_raise_cls("TypeError", "exception object expected");
  sp_raise_exc((sp_Exception *)e.v.p);
  return 1;   /* not reached */
}

/* ---- the mark under the source of a NameError ----

   CRuby's error_highlight writes, after the message of a NameError or a
   NoMethodError, an empty line, the line of the raise and carets under the
   name with its dot, `&.` or `::`; it finds the node of the raise through
   RubyVM. The walker has the node: the landing (fx_bt_land) notes the node
   of the innermost frame on the exception, in its ivar table under a name
   with no @, which no program reads and which goes with the exception;
   clone and dup copy it. */

static sp_sym fx_mark_key(void) {
  static sp_sym key;
  static int made;
  if (!made) { key = sp_sym_intern("fx.raise_node"); made = 1; }
  return key;
}

/* Notes n as the node of the raise of e, when e is a NameError. */
static void fx_mark_note(sp_Exception *e, const pm_node_t *n) {
  if (!e || !n || !sp_exc_cls_matches(e->cls_name, "NameError")) return;
  SP_GC_ROOT(e);
  sp_RbVal v = sp_box_int((sp_int)(intptr_t)n);
  /* made and written under the tables' lock, as sp_bivar_set does */
  FX_UNLOCKED(sp_ivtbl_tables_lock());   /* the lock is a park */
  sp_SymPolyHash *t = (sp_SymPolyHash *)sp_ivtbl_get(e);
  if (!t) {
    t = sp_SymPolyHash_new();
    sp_ivtbl_put(e, t);
  }
  sp_SymPolyHash_set(t, fx_mark_key(), v);
  sp_ivtbl_tables_unlock();
}

/* The node that the raise of e noted, or NULL. */
static const pm_node_t *fx_mark_node(const sp_Exception *e) {
  FX_UNLOCKED(sp_ivtbl_tables_lock());   /* the lock is a park */
  sp_SymPolyHash *t = (sp_SymPolyHash *)sp_ivtbl_get(e);
  sp_RbVal v = t && sp_SymPolyHash_has_key(t, fx_mark_key()) ? sp_SymPolyHash_get(t, fx_mark_key()) : sp_box_nil();
  sp_ivtbl_tables_unlock();
  return v.tag == SP_TAG_INT ? (const pm_node_t *)(intptr_t)v.v.i : NULL;
}

static void fx_mark_copy(const sp_Exception *from, sp_Exception *to) {
  const pm_node_t *n = fx_mark_node(from);
  if (n) fx_mark_note(to, n);
}

/* The name that the node n reads, for the search of the node of the error:
   the method of a call or of an operator assignment, or a constant. */
static int fx_mark_name(const pm_node_t *n, sp_sym *out) {
  switch (PM_NODE_TYPE(n)) {
    case PM_CALL_NODE: *out = fx_sym(((const pm_call_node_t *)n)->name); return 1;
    case PM_CONSTANT_READ_NODE: *out = fx_sym(((const pm_constant_read_node_t *)n)->name); return 1;
    case PM_CONSTANT_PATH_NODE: *out = fx_sym(((const pm_constant_path_node_t *)n)->name); return 1;
    case PM_CALL_OPERATOR_WRITE_NODE: *out = fx_sym(((const pm_call_operator_write_node_t *)n)->read_name); return 1;
    case PM_LOCAL_VARIABLE_OPERATOR_WRITE_NODE:
      *out = fx_sym(((const pm_local_variable_operator_write_node_t *)n)->binary_operator);
      return 1;
    default: return 0;
  }
}

typedef struct { sp_sym name; const pm_node_t *found; } FxMarkFind;

static bool fx_mark_visit(const pm_node_t *n, void *data) {
  FxMarkFind *f = (FxMarkFind *)data;
  sp_sym s;
  if (f->found) return false;
  if (fx_mark_name(n, &s) && s == f->name) { f->found = n; return false; }
  return true;
}

/* The node of the error, under at, the node of the raise: at itself when
   it is the call of the name; else the first node in at that reads the
   name, a constant or a call that a builtin evaluated (`puts Nosuch`);
   else at when it is a call, which a builtin raised in (`obj.send(:zz)`,
   `raise NameError`). */
static const pm_node_t *fx_mark_target(const pm_node_t *at, sp_RbVal xname) {
  int is_call = PM_NODE_TYPE(at) == PM_CALL_NODE;
  if (xname.tag != SP_TAG_SYM) return is_call ? at : NULL;
  FxMarkFind f = { (sp_sym)xname.v.i, NULL };
  pm_visit_node(at, fx_mark_visit, &f);
  return f.found ? f.found : is_call ? at : NULL;
}

/* The span of the mark of the node t, [*from, *to) in the bytes of its
   source, as error_highlight spots a prism node; 0 for none. */
static int fx_mark_span(const pm_node_t *t, const FxSource *src, const uint8_t **from, const uint8_t **to) {
  pm_location_t loc = { NULL, NULL }, op = { NULL, NULL };
  int assign = 0;
  switch (PM_NODE_TYPE(t)) {
    case PM_CALL_NODE: {
      const pm_call_node_t *c = (const pm_call_node_t *)t;
      const char *nm = sp_sym_to_s(fx_sym(c->name));
      size_t len = strlen(nm);
      if (!c->message_loc.start && !strcmp(nm, "call")) return 0;   /* `recv.()` */
      loc = c->message_loc.start ? c->message_loc : c->call_operator_loc.start ? c->call_operator_loc : t->location;
      op = c->call_operator_loc;
      /* `obj.name = v` writes name=: the mark runs to the `=` */
      assign = len && nm[len - 1] == '=' && !(c->message_loc.start && c->message_loc.end[-1] == '=');
      break;
    }
    case PM_CALL_OPERATOR_WRITE_NODE: {
      const pm_call_operator_write_node_t *c = (const pm_call_operator_write_node_t *)t;
      loc = c->message_loc;
      op = c->call_operator_loc;
      break;
    }
    case PM_LOCAL_VARIABLE_OPERATOR_WRITE_NODE:
      loc = ((const pm_local_variable_operator_write_node_t *)t)->binary_operator_loc;
      loc.end--;   /* `x += 1` marks the `+` */
      break;
    case PM_CONSTANT_READ_NODE: loc = t->location; break;
    case PM_CONSTANT_PATH_NODE: {
      const pm_constant_path_node_t *c = (const pm_constant_path_node_t *)t;
      loc = c->name_loc;
      op = c->delimiter_loc;
      break;
    }
    default: return 0;
  }
  if (!loc.start || loc.end <= loc.start) return 0;
  /* the dot, `&.` or `::` joins the name when it is on the same line */
  if (op.start && op.end <= loc.start && !memchr(op.start, '\n', (size_t)(loc.start - op.start))) loc.start = op.start;
  if (assign) {
    const uint8_t *p = loc.end;
    while (p < src->end && *p != '=' && *p != '\n') p++;
    if (p < src->end && *p == '=') loc.end = p + 1;
  }
  *from = loc.start;
  *to = loc.end;
  return 1;
}

/* The mark of e for its detailed message, or NULL: an empty line, the line
   of the raise and the carets, when the mark is on one line of ASCII, as
   error_highlight writes it. With hl, the two lines are bold. Code from a
   string (-e, stdin, eval, the input of IRB) has no mark: error_highlight
   reads the file of the raise back, and marks a string only where
   RubyVM.keep_script_lines kept it, which Fluxpinel has no RubyVM for. */
static const char *fx_mark_text(const sp_Exception *e, int hl) {
  const pm_node_t *at = fx_mark_node(e);
  if (!at) return NULL;
  const pm_node_t *t = fx_mark_target(at, e->xname);
  if (!t) return NULL;
  const FxSource *src = fx_source_of(t);
  if (!src->file) return NULL;
  const uint8_t *from, *to;
  if (!fx_mark_span(t, src, &from, &to)) return NULL;
  if (memchr(from, '\n', (size_t)(to - from))) return NULL;
  const uint8_t *ls = from, *le = to;
  while (ls > src->start && ls[-1] != '\n') ls--;
  while (le < src->end && *le != '\n') le++;
  for (const uint8_t *p = ls; p < le; p++)
    if (*p >= 0x80) return NULL;
  const char *bold = hl ? "\033[1m" : "", *plain = hl ? "\033[m" : "";
  size_t llen = (size_t)(le - ls), ind = (size_t)(from - ls), car = (size_t)(to - from);
  char *buf = malloc(llen + ind + car + 32), *p = buf;
  p += sprintf(p, "\n\n%s", bold);
  memcpy(p, ls, llen);
  p += llen;
  p += sprintf(p, "%s\n%s", plain, bold);
  for (size_t i = 0; i < ind; i++) *p++ = ls[i] == '\t' ? '\t' : ' ';
  memset(p, '^', car);
  p += car;
  sprintf(p, "%s", plain);
  const char *r = sp_str_dup(buf);
  free(buf);
  return r;
}

/* The builtin methods of an exception. */
/* #detailed_message: "msg (Class)" from #message, which a class of the
   program can define, as the runtime writes it (no class for an unnamed
   Class.new class, "unhandled exception" for an empty message); with
   highlight, in the bold and the underline of CRuby. A NameError carries
   the mark under its source (fx_mark_text). */
static const char *fx_exc_detailed(sp_RbVal r, sp_Exception *e, int hl) {
  SP_GC_ROOT_RBVAL(r);
  SP_GC_ROOT(e);
  const char *msg = sp_poly_to_s(fx_send(r, "message", 0, NULL));
  SP_GC_ROOT_STR(msg);
  const char *d = hl ? sp_exc_detailed_highlight(e, msg) : sp_exc_detailed_text(e, msg);
  SP_GC_ROOT_STR(d);
  const char *mark = fx_mark_text(e, hl);
  if (!mark) return d;
  SP_GC_ROOT_STR(mark);
  return sp_str_concat(d, mark);
}

/* #full_message from the detailed message d: the first frame of the
   backtrace, the message, then the other frames as "\tfrom" lines; with
   order: :bottom, the frames first, as a traceback. An exception with no
   backtrace names the line of the call, as CRuby does. */
static const char *fx_exc_full(sp_Exception *e, const char *d, int bottom) {
  SP_GC_ROOT(e);
  SP_GC_ROOT_STR(d);
  sp_StrArray *bt = e->backtrace && e->backtrace->len ? e->backtrace : NULL;
  const char *first;
  if (bt) {
    first = sp_StrArray_get(bt, 0);
  } else {
    const FxFrameStack *s = fx_fs();
    const pm_node_t *n = s->depth ? s->tr[s->depth - 1].at : NULL;
    first = n ? sp_sprintf("%s:%d:in 'full_message'", fx_source_of(n)->path, fx_line(n))
              : sp_sprintf("%s:in 'full_message'", L.path);
  }
  SP_GC_ROOT_STR(first);
  const char *s = sp_sprintf("%s: %s\n", first, d);
  SP_GC_ROOT_STR(s);
  if (!bt) return s;
  if (bottom) {
    const char *t = sp_sprintf("Traceback (most recent call last):\n");
    SP_GC_ROOT_STR(t);
    for (sp_int i = bt->len - 1; i >= 1; i--) {
      t = sp_sprintf("%s\t%d: from %s\n", t, (int)i, sp_StrArray_get(bt, i));
      SP_GC_ROOT_STR(t);
    }
    return sp_sprintf("%s%s", t, s);
  }
  for (sp_int i = 1; i < bt->len; i++) {
    s = sp_sprintf("%s\tfrom %s\n", s, sp_StrArray_get(bt, i));
    SP_GC_ROOT_STR(s);
  }
  return s;
}

static int fx_exc_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (!fx_is_exc(r)) return 0;
  sp_Exception *e = (sp_Exception *)r.v.p;
  if (argc == 0 && FX_IS("message")) {
    /* Exception#message calls to_s, which a class of the program can define */
    FxMethod *m = fx_find_method(fx_lookup_class(r), sp_sym_intern("to_s"));
    if (m && m->owner != L.object) { *out = fx_invoke(m, r, 0, NULL); return 1; }
    *out = sp_box_str_new(sp_str_dup(sp_exc_message(e)));
    return 1;
  }
  if (argc == 0 && FX_IS("to_s")) { *out = sp_box_str_new(sp_str_dup(sp_exc_message(e))); return 1; }
  if (argc == 0 && FX_IS("cause")) { *out = e->cause ? fx_exc_box(e->cause) : sp_box_nil(); return 1; }
  if (argc == 0 && FX_IS("exception")) { *out = r; return 1; }
  /* exception(msg): the exception itself for itself, else a clone with
     the message, as CRuby's exc_exception */
  if (argc == 1 && FX_IS("exception")) {
    if (argv[0].tag == SP_TAG_OBJ && argv[0].v.p == (void *)e) { *out = r; return 1; }
    sp_RbVal c = fx_send(r, "clone", 0, NULL);
    SP_GC_ROOT_RBVAL(c);
    fx_exc_set_msg((sp_Exception *)c.v.p, argv[0]);
    *out = c;
    return 1;
  }
  /* NameError#local_variables: the names of the locals at a bare name that
     is no local and no method (fx_call_self), else none. did_you_mean
     reads it. */
  if (argc == 0 && FX_IS("local_variables") && fx_exc_is(r, "NameError")) {
    int kept = e->has_key && !fx_exc_is(r, "NoMethodError") && fx_is_array_value(e->xkey);
    *out = kept ? e->xkey : sp_box_poly_array(sp_PolyArray_new());
    return 1;
  }
  /* LoadError#path: the feature of a require that loaded nothing, else nil */
  if (argc == 0 && FX_IS("path")) { *out = sp_exc_path_acc(e); return 1; }
  /* full_message(highlight:, order:) and detailed_message(highlight:) */
  int kw = argc == 1 && fx_is_hash(argv[0]) && argv[0].v.p == L.kwhash;
  if ((argc == 0 || kw) && (FX_IS("full_message") || FX_IS("detailed_message"))) {
    int hl = 0, bottom = 0;
    if (kw) {
      sp_PolyPolyHash *h = sp_poly_as_poly_poly_hash(argv[0]);
      hl = sp_poly_truthy(sp_PolyPolyHash_get(h, sp_box_sym(sp_sym_intern("highlight"))));
      sp_RbVal order = sp_PolyPolyHash_get(h, sp_box_sym(sp_sym_intern("order")));
      bottom = order.tag == SP_TAG_SYM && !strcmp(sp_sym_to_s((sp_sym)order.v.i), "bottom");
    }
    const char *d = NULL;
    SP_GC_ROOT_STR(d);
    /* full_message asks the detailed_message of the program, when the
       class or a module ahead of it defines one (did_you_mean), with
       highlight: as CRuby's rb_error_write does */
    FxMethod *dm = name[0] == 'f' ? fx_find_method(fx_lookup_class(r), sp_sym_intern("detailed_message")) : NULL;
    if (dm && dm->kind != FX_M_BUILTIN) {
      sp_PolyPolyHash *h = sp_PolyPolyHash_new();
      sp_RbVal hv = sp_box_obj(h, SP_BUILTIN_POLY_POLY_HASH);
      SP_GC_ROOT_RBVAL(hv);
      sp_PolyPolyHash_set(h, sp_box_sym(sp_sym_intern("highlight")), sp_box_bool(hl));
      L.kwhash = h;
      d = sp_poly_to_s(fx_invoke_b(dm, r, 1, &hv, NULL));
    } else {
      d = fx_exc_detailed(r, e, hl);
    }
    *out = sp_box_str_new(name[0] == 'd' ? d : fx_exc_full(e, d, bottom));
    return 1;
  }
  /* clone and dup: a copy with the ivars, as irb clones an exception to
     set a filtered backtrace on the copy */
  if (argc == 0 && (FX_IS("clone") || FX_IS("dup"))) {
    SP_GC_ROOT_RBVAL(r);
    sp_RbVal c = fx_exc_box(name[0] == 'c' ? sp_exc_clone(e) : sp_exc_dup(e));
    SP_GC_ROOT_RBVAL(c);
    fx_mark_copy(e, (sp_Exception *)c.v.p);
    sp_RbVal names = fx_send(r, "instance_variables", 0, NULL);
    SP_GC_ROOT_RBVAL(names);
    for (sp_int i = 0; i < sp_poly_length(names); i++) {
      sp_RbVal args[2] = { sp_poly_arr_get(names, i), sp_box_nil() };
      args[1] = fx_send(r, "instance_variable_get", 1, args);
      fx_send(c, "instance_variable_set", 2, args);
    }
    *out = c;
    return 1;
  }
  /* the backtrace that the landing of the raise attached (fx_bt_land) or
     that set_backtrace set, else nil. backtrace_locations is nil: Fluxpinel
     has no Thread::Backtrace::Location. */
  if (argc == 0 && (FX_IS("backtrace") || FX_IS("backtrace_locations"))) {
    *out = e->backtrace && name[9] == 0 ? sp_box_obj(e->backtrace, SP_BUILTIN_STR_ARRAY) : sp_box_nil();
    return 1;
  }
  /* set_backtrace(array | string | nil) */
  if (argc == 1 && FX_IS("set_backtrace")) {
    sp_RbVal a = argv[0];
    sp_StrArray *bt = NULL;
    if (fx_is_string(a)) {
      bt = sp_StrArray_new();
      SP_GC_ROOT(bt);
      fx_strs_push(bt, sp_poly_recv_s(a, name));
    } else if (a.tag != SP_TAG_NIL) {
      bt = sp_poly_as_str_array(a);
    }
    *out = sp_Exception_set_backtrace(e, bt);
    return 1;
  }
  /* the same class and message; two UncaughtThrowErrors are equal */
  if (argc == 1 && FX_IS("==")) {
    *out = sp_box_bool(fx_is_exc(argv[0]) && sp_exc_eq(e, (sp_Exception *)argv[0].v.p));
    return 1;
  }
  return 0;
}

/* Whether a rescue clause target (a class or a module) takes the exception
   ev of class cls. */
static int fx_exc_matches(const char *cls, sp_RbVal ev, sp_RbVal target) {
  FxClass *k = fx_class_value(target);
  if (!k) sp_raise_cls("TypeError", "class or module required for rescue clause");
  if (k->is_module) return fx_is_a(ev, k);
  return sp_exc_cls_matches(cls, k->name);
}

/* The text of the method name of the program on e, or NULL when its
   class does not define one. */
static const char *fx_exc_user_text(sp_Exception *e, const char *name) {
  SP_GC_ROOT(e);
  sp_RbVal v = fx_exc_box(e);
  SP_GC_ROOT_RBVAL(v);
  FxMethod *m = fx_find_method(fx_lookup_class(v), sp_sym_intern(name));
  if (!m || m->owner == L.object) return NULL;
  return sp_poly_to_s(fx_invoke(m, v, 0, NULL));
}

/* #to_s as #inspect renders it: the program's to_s, else the message.
   The runtime calls the hook from any thread (FX_HOOKED). */
static const char *fx_exc_to_s_hook(sp_Exception *e) {
  const char *s;
  FX_HOOKED(s = fx_exc_user_text(e, "to_s"));
  return s ? s : sp_exc_message(e);
}

/* #message for the report of an uncaught exception: the program's message
   or to_s, else NULL */
static const char *fx_exc_message_hook(sp_Exception *e) {
  const char *s;
  FX_HOOKED({
    s = fx_exc_user_text(e, "message");
    if (!s) s = fx_exc_user_text(e, "to_s");
  });
  return s;
}

static void fx_exc_hooks(void) {
  sp_user_exc_parent_fn = fx_exc_parent;
  sp_user_exc_to_s_fn = fx_exc_to_s_hook;
  sp_user_exc_message_fn = fx_exc_message_hook;
}

#undef FX_IS
