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
  /* the builtin classes with more arguments (NameError, SignalException,
     SystemExit, ...) keep them in fields that Fluxpinel does not set yet */
  if (argc > 1) {
    static const char *const more[] = { "NameError", "KeyError", "SignalException", "SystemExit", "StopIteration",
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

/* The builtin methods of an exception. */
static int fx_exc_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (!fx_is_exc(r)) return 0;
  sp_Exception *e = (sp_Exception *)r.v.p;
  if (argc == 0 && FX_IS("message")) {
    /* Exception#message calls to_s, which a class of the program can define */
    FxMethod *m = fx_find_method(fx_class_of(r), sp_sym_intern("to_s"));
    if (m && m->owner != L.object) { *out = fx_invoke(m, r, 0, NULL); return 1; }
    *out = sp_box_str_new(sp_str_dup(sp_exc_message(e)));
    return 1;
  }
  if (argc == 0 && FX_IS("to_s")) { *out = sp_box_str_new(sp_str_dup(sp_exc_message(e))); return 1; }
  if (argc == 0 && FX_IS("cause")) { *out = e->cause ? fx_exc_box(e->cause) : sp_box_nil(); return 1; }
  if (argc == 0 && FX_IS("exception")) { *out = r; return 1; }
  if (argc == 0 && FX_IS("result")) { *out = sp_exc_result(e); return 1; }
  /* LoadError#path: the feature of a require that loaded nothing, else nil */
  if (argc == 0 && FX_IS("path")) { *out = sp_exc_path_acc(e); return 1; }
  if (argc == 0 && FX_IS("full_message")) {
    *out = sp_box_str_new(sp_sprintf("%s (%s)", sp_exc_message(e), e->cls_name));
    return 1;
  }
  /* "msg (Class)" from #message, which a class of the program can define,
     with no class for an unnamed Class.new class */
  if (argc == 0 && FX_IS("detailed_message")) {
    SP_GC_ROOT(e);
    const char *msg = sp_poly_to_s(fx_send(r, "message", 0, NULL));
    SP_GC_ROOT_STR(msg);
    *out = sp_box_str_new(sp_exc_detailed_text(e, msg));
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
  FxMethod *m = fx_find_method(fx_class_of(v), sp_sym_intern(name));
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
