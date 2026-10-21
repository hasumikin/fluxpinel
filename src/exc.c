/* exc.c -- exceptions in the walker.

   An exception is a spinel sp_Exception, boxed with SP_BUILTIN_EXCEPTION.
   It carries its class by name (cls_name). For a class of the program,
   the name is the LupeClass name, and the runtime asks Lupe for the
   superclass through sp_user_exc_parent_fn. Thus sp_exc_cls_matches and
   the uncaught-exception report see the classes of the program.

   walk.c holds begin/rescue/else/ensure, which push a handler of the
   runtime as the code that spinel emits does. */

#include "lupe.h"

#define LUPE_IS(s) (!strcmp(name, (s)))

static int lupe_is_exc(sp_RbVal v) {
  return v.tag == SP_TAG_OBJ && v.cls_id == SP_BUILTIN_EXCEPTION && v.v.p;
}

static sp_RbVal lupe_exc_box(sp_Exception *e) {
  return sp_box_obj(e, SP_BUILTIN_EXCEPTION);
}

/* Whether k is Exception or a subclass of it. */
static int lupe_is_exc_class(const LupeClass *k) {
  for (const LupeClass *c = k; c; c = c->super)
    if (c->cls_id < 0 && !strcmp(c->name, "Exception")) return 1;
  return 0;
}

/* The runtime asks for the superclass of a class of the program by name:
   a class of Lupe, else a class of the kernel. */
static const char *lupe_exc_parent(const char *name) {
  LupeClass *k = lupe_class_named(name);
  if (k && k->cls_id >= LUPE_CLS_BASE) return k->super ? k->super->name : NULL;
  return lupe_kernel_hooks.exc_parent ? lupe_kernel_hooks.exc_parent(name) : NULL;
}

/* The message of e: nil gives none (#message then answers the class name);
   any other value gives its to_s. */
static void lupe_exc_set_msg(sp_Exception *e, sp_RbVal m) {
  SP_GC_ROOT(e);
  const char *s = m.tag == SP_TAG_NIL ? NULL : sp_msg_heapify(sp_poly_to_s(m));
  sp_gc_wb(e);
  e->msg = s;
  e->msg_h = NULL;
}

/* Exception#initialize: an optional message. */
static void lupe_exc_init(sp_RbVal v, int argc, sp_RbVal *argv) {
  if (argc > 0 && argv[argc - 1].tag == SP_TAG_OBJ && argv[argc - 1].v.p == L.kwhash) argc--;
  /* the builtin classes with more arguments (NameError, SignalException,
     SystemExit, ...) keep them in fields that Lupe does not set yet */
  if (argc > 1) {
    static const char *const more[] = { "NameError", "KeyError", "SignalException", "SystemExit", "StopIteration",
                                        "FrozenError", "UncaughtThrowError", "NoMatchingPatternKeyError",
                                        "LocalJumpError", "SystemCallError", NULL };
    for (LupeClass *k = lupe_class_of(v); k; k = k->super)
      for (int i = 0; more[i]; i++)
        if (!strcmp(k->name, more[i])) lupe_gap(sp_sprintf("%s.new with %d arguments", k->name, argc));
    sp_raise_cls("ArgumentError", sp_sprintf("wrong number of arguments (given %d, expected 0..1)", argc));
  }
  if (argc >= 1) lupe_exc_set_msg((sp_Exception *)v.v.p, argv[0]);
}

/* k.new(*args) for an exception class k: the program's initialize if it
   has one, else the builtin one. */
static sp_RbVal lupe_exc_new(LupeClass *k, int argc, sp_RbVal *argv, const LupeBlock *blk) {
  sp_RbVal v = lupe_exc_box(sp_exc_new(k->name, NULL));
  SP_GC_ROOT_RBVAL(v);
  LupeMethod *init = lupe_find_method(k, sp_sym_intern("initialize"));
  if (init) lupe_invoke_b(init, v, argc, argv, blk);
  else lupe_exc_init(v, argc, argv);
  return v;
}

/* Kernel#raise and #fail. */
static int lupe_exc_kernel(const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (!LUPE_IS("raise") && !LUPE_IS("fail")) return 0;
  (void)out;
  /* raise ..., cause: x: the runtime takes it for the next raise */
  if (argc > 0 && argv[argc - 1].tag == SP_TAG_OBJ && argv[argc - 1].v.p == L.kwhash) {
    sp_PolyPolyHash *kw = sp_poly_as_poly_poly_hash(argv[--argc]);
    sp_RbVal key = sp_box_sym(sp_sym_intern("cause"));
    if (kw && sp_PolyPolyHash_has_key(kw, key)) {
      sp_RbVal c = sp_PolyPolyHash_get(kw, key);
      if (c.tag != SP_TAG_NIL && !lupe_is_exc(c)) sp_raise_cls("TypeError", "exception object expected");
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
  if (lupe_is_string(a)) {
    if (argc > 1) sp_raise_cls("TypeError", "exception class/object expected");
    sp_raise_cls("RuntimeError", sp_poly_recv_s(a, name));
  }
  if (lupe_is_exc(a)) {
    if (argc >= 2) lupe_exc_set_msg((sp_Exception *)a.v.p, argv[1]);
    sp_raise_exc((sp_Exception *)a.v.p);
  }
  LupeClass *k = lupe_class_value(a);
  if (!k || !lupe_is_exc_class(k)) sp_raise_cls("TypeError", "exception class/object expected");
  sp_RbVal e = lupe_send(a, "new", argc >= 2 ? 1 : 0, argv + 1);
  SP_GC_ROOT_RBVAL(e);
  if (!lupe_is_exc(e)) sp_raise_cls("TypeError", "exception object expected");
  sp_raise_exc((sp_Exception *)e.v.p);
  return 1;   /* not reached */
}

/* The builtin methods of an exception. */
static int lupe_exc_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (!lupe_is_exc(r)) return 0;
  sp_Exception *e = (sp_Exception *)r.v.p;
  if (argc == 0 && LUPE_IS("message")) {
    /* Exception#message calls to_s, which a class of the program can define */
    LupeMethod *m = lupe_find_method(lupe_class_of(r), sp_sym_intern("to_s"));
    if (m && m->owner != L.object) { *out = lupe_invoke(m, r, 0, NULL); return 1; }
    *out = sp_box_str_new(sp_str_dup(sp_exc_message(e)));
    return 1;
  }
  if (argc == 0 && LUPE_IS("to_s")) { *out = sp_box_str_new(sp_str_dup(sp_exc_message(e))); return 1; }
  if (argc == 0 && LUPE_IS("cause")) { *out = e->cause ? lupe_exc_box(e->cause) : sp_box_nil(); return 1; }
  if (argc == 0 && LUPE_IS("exception")) { *out = r; return 1; }
  if (argc == 0 && LUPE_IS("result")) { *out = sp_exc_result(e); return 1; }
  if (argc == 0 && LUPE_IS("full_message")) {
    *out = sp_box_str_new(sp_sprintf("%s (%s)", sp_exc_message(e), e->cls_name));
    return 1;
  }
  if (argc == 1 && LUPE_IS("==")) {
    sp_RbVal o = argv[0];
    *out = sp_box_bool(lupe_is_exc(o) && !strcmp(e->cls_name, ((sp_Exception *)o.v.p)->cls_name) &&
                       !strcmp(sp_exc_message(e), sp_exc_message((sp_Exception *)o.v.p)));
    return 1;
  }
  return 0;
}

/* Whether a rescue clause target (a class or a module) takes the exception
   ev of class cls. */
static int lupe_exc_matches(const char *cls, sp_RbVal ev, sp_RbVal target) {
  LupeClass *k = lupe_class_value(target);
  if (!k) sp_raise_cls("TypeError", "class or module required for rescue clause");
  if (k->is_module) return lupe_is_a(ev, k);
  return sp_exc_cls_matches(cls, k->name);
}

static void lupe_exc_hooks(void) {
  sp_user_exc_parent_fn = lupe_exc_parent;
}

#undef LUPE_IS
