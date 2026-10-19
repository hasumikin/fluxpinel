/* walk.c -- the AST evaluator of Lupe.

   The walker evaluates the prism AST directly. Every value is an sp_RbVal.
   A C local that holds a value across a call that can allocate is rooted
   with SP_GC_ROOT_RBVAL, as the code that spinel generates does. */

#include "lupe.h"

static int lupe_line(const pm_node_t *n) {
  return pm_newline_list_line_column(&L.parser.newline_list, n->location.start, L.parser.start_line).line;
}

static void lupe_unsupported(const pm_node_t *n, const char *what) {
  fprintf(stderr, "lupe: %s:%d: unsupported %s (%s)\n", L.path, lupe_line(n), what,
          pm_node_type_to_str(PM_NODE_TYPE(n)));
  exit(2);
}

/* A semantics that Lupe does not model yet, met at run time with no node
   at hand. It stops as lupe_unsupported does, so the oracle counts the
   test as a gap of Lupe, not as a wrong answer. */
static void lupe_gap(const char *what) {
  fprintf(stderr, "lupe: %s: unsupported %s (runtime)\n", L.path, what);
  exit(2);
}

static sp_sym lupe_sym(pm_constant_id_t id) {
  const pm_constant_t *c = pm_constant_pool_id_to_constant(&L.parser.constant_pool, id);
  return sp_sym_intern_n((const char *)c->start, c->length);
}

static sp_RbVal *lupe_local(LupeFrame *f, const pm_node_t *n, pm_constant_id_t name, uint32_t depth) {
  if (depth != 0) lupe_unsupported(n, "local variable of an outer scope");
  for (size_t i = 0; i < f->names->size; i++)
    if (f->names->ids[i] == name) return &f->locals[i];
  lupe_unsupported(n, "local variable with no slot");
}

/* Declares the slots of a frame on the C stack and roots each one, and
   sets the self and the lexical class of the scope. The roots stay until
   the enclosing SP_GC_SAVE scope ends. */
#define LUPE_FRAME(f, namelist, self_, cref_)                                     \
  size_t f##_n = (namelist)->size;                                                \
  sp_RbVal f##_slots[f##_n ? f##_n : 1];                                           \
  for (size_t f##_i = 0; f##_i < f##_n; f##_i++) {                                 \
    f##_slots[f##_i] = sp_box_nil();                                               \
    _sp_gc_root_push((void **)((uintptr_t)&f##_slots[f##_i] | (uintptr_t)1));      \
  }                                                                               \
  LupeFrame f = { f##_slots, (namelist), (self_), (cref_), NULL, 0 }

/* ---- methods ---- */

static void lupe_define(LupeFrame *f, const pm_def_node_t *def) {
  sp_sym name = lupe_sym(def->name);
  if (def->receiver) {
    if (!PM_NODE_TYPE_P(def->receiver, PM_SELF_NODE) || f->self.tag != SP_TAG_CLASS)
      lupe_unsupported((const pm_node_t *)def, "singleton method on an object");
    LupeClass *k = lupe_class_value(f->self);
    lupe_define_method(k, lupe_method_new(LUPE_M_DEF, name, def, 0, k), 1);
    return;
  }
  LupeClass *k = f->cref;
  LupeMethod *m = lupe_method_new(LUPE_M_DEF, name, def, 0, k);
  /* 1: inside class << self; 2: after module_function */
  if (f->singleton_defs != 1) lupe_define_method(k, m, 0);
  if (f->singleton_defs) lupe_define_method(k, m, 1);
}

static __attribute__((noreturn)) void lupe_arity_error(int argc, int nreq, int nopt, int rest) {
  if (rest) sp_raise_cls("ArgumentError", sp_sprintf("wrong number of arguments (given %d, expected %d+)", argc, nreq));
  if (nopt)
    sp_raise_cls("ArgumentError",
                 sp_sprintf("wrong number of arguments (given %d, expected %d..%d)", argc, nreq, nreq + nopt));
  sp_raise_cls("ArgumentError", sp_sprintf("wrong number of arguments (given %d, expected %d)", argc, nreq));
}

/* Binds the arguments to the parameters: the required ones, the optional
   ones (a default is evaluated in the callee frame), *rest and the
   required ones after it. */
static void lupe_bind_params(LupeFrame *f, const pm_parameters_node_t *ps, int argc, sp_RbVal *argv) {
  if (!ps) {
    if (argc != 0) lupe_arity_error(argc, 0, 0, 0);
    return;
  }
  if (ps->keywords.size || ps->keyword_rest) lupe_unsupported((const pm_node_t *)ps, "keyword parameter");
  int npre = (int)ps->requireds.size, npost = (int)ps->posts.size, nopt = (int)ps->optionals.size;
  int rest = ps->rest != NULL;
  if (argc < npre + npost || (!rest && argc > npre + npost + nopt)) lupe_arity_error(argc, npre + npost, nopt, rest);
  int ai = 0;
  for (int i = 0; i < npre; i++) {
    const pm_node_t *p = ps->requireds.nodes[i];
    if (!PM_NODE_TYPE_P(p, PM_REQUIRED_PARAMETER_NODE)) lupe_unsupported(p, "parameter pattern");
    *lupe_local(f, p, ((const pm_required_parameter_node_t *)p)->name, 0) = argv[ai++];
  }
  int nfill = argc - npre - npost;
  for (int i = 0; i < nopt; i++) {
    const pm_optional_parameter_node_t *p = (const pm_optional_parameter_node_t *)ps->optionals.nodes[i];
    sp_RbVal *slot = lupe_local(f, (const pm_node_t *)p, p->name, 0);
    if (i < nfill) *slot = argv[ai++];
    else *slot = lupe_eval(f, p->value);
  }
  if (rest) {
    sp_PolyArray *a = sp_PolyArray_new();
    SP_GC_ROOT(a);
    while (ai < argc - npost) sp_PolyArray_push(a, argv[ai++]);
    const pm_node_t *r = ps->rest;
    if (!PM_NODE_TYPE_P(r, PM_REST_PARAMETER_NODE)) lupe_unsupported(r, "rest parameter");
    pm_constant_id_t rn = ((const pm_rest_parameter_node_t *)r)->name;
    if (rn) *lupe_local(f, r, rn, 0) = sp_box_poly_array(a);
  }
  for (int i = 0; i < npost; i++) {
    const pm_node_t *p = ps->posts.nodes[i];
    if (!PM_NODE_TYPE_P(p, PM_REQUIRED_PARAMETER_NODE)) lupe_unsupported(p, "parameter pattern");
    *lupe_local(f, p, ((const pm_required_parameter_node_t *)p)->name, 0) = argv[ai++];
  }
  if (ps->block) {
    pm_constant_id_t bn = ((const pm_block_parameter_node_t *)ps->block)->name;
    if (bn) *lupe_local(f, (const pm_node_t *)ps->block, bn, 0) = sp_box_nil();
  }
}

static sp_RbVal lupe_invoke(const LupeMethod *m, sp_RbVal self, int argc, sp_RbVal *argv) {
  if (m->kind == LUPE_M_READER) {
    if (argc != 0) lupe_arity_error(argc, 0, 0, 0);
    return lupe_ivar_get(self, m->ivar);
  }
  if (m->kind == LUPE_M_WRITER) {
    if (argc != 1) lupe_arity_error(argc, 1, 0, 0);
    lupe_ivar_set(self, m->ivar, argv[0]);
    return argv[0];
  }
  SP_GC_SAVE();
  sp_RbVal me = self;
  SP_GC_ROOT_RBVAL(me);
  const pm_def_node_t *def = m->def;
  LUPE_FRAME(f, &def->locals, me, m->owner);
  f.method = m;
  lupe_bind_params(&f, def->parameters, argc, argv);
  sp_RbVal v = def->body ? lupe_eval(&f, def->body) : sp_box_nil();
  if (L.ctl == LUPE_CTL_RETURN) { v = L.ctl_val; L.ctl = LUPE_CTL_NONE; }
  return v;
}

/* ---- calls ---- */

static int lupe_argc(const pm_arguments_node_t *args) {
  if (!args) return 0;
  for (size_t i = 0; i < args->arguments.size; i++) {
    const pm_node_t *a = args->arguments.nodes[i];
    if (PM_NODE_TYPE_P(a, PM_SPLAT_NODE) || PM_NODE_TYPE_P(a, PM_KEYWORD_HASH_NODE) ||
        PM_NODE_TYPE_P(a, PM_BLOCK_ARGUMENT_NODE) || PM_NODE_TYPE_P(a, PM_FORWARDING_ARGUMENTS_NODE))
      lupe_unsupported(a, "argument kind");
  }
  return (int)args->arguments.size;
}

/* Roots argv[0..argc) and evaluates the arguments into it. Answers 0 when
   a jump is pending. The roots stay until the caller's SP_GC_SAVE ends. */
static int lupe_args(LupeFrame *f, const pm_arguments_node_t *args, int argc, sp_RbVal *argv) {
  for (int i = 0; i < argc; i++) {
    argv[i] = sp_box_nil();
    _sp_gc_root_push((void **)((uintptr_t)&argv[i] | (uintptr_t)1));
  }
  for (int i = 0; i < argc; i++) {
    argv[i] = lupe_eval(f, args->arguments.nodes[i]);
    if (L.ctl != LUPE_CTL_NONE) return 0;
  }
  return 1;
}

static sp_RbVal lupe_call(LupeFrame *f, const pm_call_node_t *c) {
  SP_GC_SAVE();
  if (c->block) lupe_unsupported((const pm_node_t *)c, "block");
  const char *name = sp_sym_to_s(lupe_sym(c->name));
  sp_RbVal recv = sp_box_nil();
  SP_GC_ROOT_RBVAL(recv);
  if (c->receiver) {
    recv = lupe_eval(f, c->receiver);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    if (PM_NODE_FLAG_P((const pm_node_t *)c, PM_CALL_NODE_FLAGS_SAFE_NAVIGATION) && recv.tag == SP_TAG_NIL)
      return recv;
  }
  int argc = lupe_argc(c->arguments);
  sp_RbVal argv[argc ? argc : 1];
  if (!lupe_args(f, c->arguments, argc, argv)) return sp_box_nil();
  if (!c->receiver) return lupe_call_self(f, name, argc, argv);
  sp_RbVal v = lupe_send(recv, name, argc, argv);
  /* `obj.x = v` answers v, whatever x= returns */
  if (PM_NODE_FLAG_P((const pm_node_t *)c, PM_CALL_NODE_FLAGS_ATTRIBUTE_WRITE) && argc > 0) return argv[argc - 1];
  return v;
}

/* A call with no receiver: a method of self, then Kernel, then a builtin
   method of self. In a class body or a class method, self is the class. */
static sp_RbVal lupe_call_self(LupeFrame *f, const char *name, int argc, sp_RbVal *argv) {
  sp_sym s = sp_sym_intern(name);
  sp_RbVal self = f->self, out;
  if (self.tag == SP_TAG_CLASS) {
    LupeClass *k = lupe_class_value(self);
    LupeMethod *m = k ? lupe_find_smethod(k, s) : NULL;
    if (m) return lupe_invoke(m, self, argc, argv);
    if (lupe_class_core(f, self, name, argc, argv, &out)) return out;
  }
  LupeMethod *m = lupe_find_method(lupe_class_of(self), s);
  if (m) return lupe_invoke(m, self, argc, argv);
  if (lupe_kernel(name, argc, argv, &out)) return out;
  if (lupe_object_core(self, name, argc, argv, &out)) return out;
  if (!lupe_is_lupe_object(self) && self.tag != SP_TAG_CLASS) return lupe_send(self, name, argc, argv);
  lupe_trace_missing("Kernel", name, argc);
  if (lupe_is_lupe_object(self) && self.v.p == L.main.v.p)
    sp_raise_cls("NoMethodError", sp_sprintf("undefined method '%s' for main", name));
  sp_raise_cls("NoMethodError", sp_sprintf("undefined method '%s' for an instance of %s", name,
                                           sp_poly_class_name(self)));
}

/* The arguments that a bare super passes: the current values of the
   parameters, in order, with *rest spread. Answers the count; with argv
   NULL, it only counts. */
static int lupe_zsuper_args(LupeFrame *f, const pm_node_t *n, sp_RbVal *argv) {
  const pm_parameters_node_t *ps = f->method->def ? f->method->def->parameters : NULL;
  if (!ps) return 0;
  int k = 0;
  const pm_node_list_t *lists[3] = { &ps->requireds, &ps->optionals, NULL };
  for (int l = 0; lists[l]; l++)
    for (size_t i = 0; i < lists[l]->size; i++) {
      const pm_node_t *p = lists[l]->nodes[i];
      pm_constant_id_t name = PM_NODE_TYPE_P(p, PM_OPTIONAL_PARAMETER_NODE)
                                ? ((const pm_optional_parameter_node_t *)p)->name
                                : ((const pm_required_parameter_node_t *)p)->name;
      if (argv) argv[k] = *lupe_local(f, n, name, 0);
      k++;
    }
  if (ps->rest) {
    pm_constant_id_t rn = ((const pm_rest_parameter_node_t *)ps->rest)->name;
    if (!rn) lupe_unsupported(n, "bare super with an anonymous rest");
    sp_RbVal rest = *lupe_local(f, n, rn, 0);
    sp_int len = sp_poly_length(rest);
    for (sp_int i = 0; i < len; i++, k++)
      if (argv) argv[k] = sp_poly_arr_get(rest, i);
  }
  for (size_t i = 0; i < ps->posts.size; i++, k++)
    if (argv) argv[k] = *lupe_local(f, n, ((const pm_required_parameter_node_t *)ps->posts.nodes[i])->name, 0);
  return k;
}

/* super, and super with no arguments, which passes the parameters. */
static sp_RbVal lupe_super(LupeFrame *f, const pm_node_t *n, const pm_arguments_node_t *args, int forward) {
  SP_GC_SAVE();
  if (!f->method) lupe_unsupported(n, "super outside a method");
  int argc = forward ? lupe_zsuper_args(f, n, NULL) : lupe_argc(args);
  sp_RbVal argv[argc ? argc : 1];
  if (forward) {
    lupe_zsuper_args(f, n, argv);
    for (int i = 0; i < argc; i++) _sp_gc_root_push((void **)((uintptr_t)&argv[i] | (uintptr_t)1));
  } else if (!lupe_args(f, args, argc, argv)) {
    return sp_box_nil();
  }
  const LupeMethod *m = f->method;
  LupeMethod *s;
  if (f->self.tag == SP_TAG_CLASS && lupe_table_get(&m->owner->smethods, m->name) == m)
    s = m->owner->super ? lupe_find_smethod(m->owner->super, m->name) : NULL;
  else
    s = lupe_find_super(lupe_class_of(f->self), m);
  if (s) return lupe_invoke(s, f->self, argc, argv);
  const char *name = sp_sym_to_s(m->name);
  sp_RbVal out;
  if (!strcmp(name, "initialize")) return sp_box_nil();
  if (f->self.tag == SP_TAG_CLASS && lupe_class_core(NULL, f->self, name, argc, argv, &out)) return out;
  if (lupe_object_core(f->self, name, argc, argv, &out)) return out;
  lupe_trace_missing("super", name, argc);
  sp_raise_cls("NoMethodError", sp_sprintf("super: no superclass method '%s'", name));
}

/* ---- control flow ---- */

static sp_RbVal lupe_eval_stmts(LupeFrame *f, const pm_statements_node_t *s) {
  sp_RbVal v = sp_box_nil();
  if (!s) return v;
  for (size_t i = 0; i < s->body.size; i++) {
    v = lupe_eval(f, s->body.nodes[i]);
    if (L.ctl != LUPE_CTL_NONE) break;
  }
  return v;
}

static sp_RbVal lupe_while(LupeFrame *f, const pm_node_t *pred, const pm_statements_node_t *body, int until) {
  for (;;) {
    sp_RbVal c = lupe_eval(f, pred);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    if (sp_poly_truthy(c) == until) return sp_box_nil();
    lupe_eval_stmts(f, body);
    if (L.ctl == LUPE_CTL_BREAK) { L.ctl = LUPE_CTL_NONE; return L.ctl_val; }
    if (L.ctl == LUPE_CTL_NEXT) { L.ctl = LUPE_CTL_NONE; continue; }
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
  }
}

/* The value that return / break / next carry: nil, one value, or an Array. */
static sp_RbVal lupe_jump_value(LupeFrame *f, const pm_arguments_node_t *args) {
  if (!args || args->arguments.size == 0) return sp_box_nil();
  if (args->arguments.size == 1) return lupe_eval(f, args->arguments.nodes[0]);
  sp_PolyArray *a = sp_PolyArray_new();
  SP_GC_ROOT(a);
  for (size_t i = 0; i < args->arguments.size; i++) sp_PolyArray_push(a, lupe_eval(f, args->arguments.nodes[i]));
  return sp_box_poly_array(a);
}

static sp_RbVal lupe_jump(LupeFrame *f, LupeCtl kind, const pm_arguments_node_t *args) {
  sp_RbVal v = lupe_jump_value(f, args);
  if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
  L.ctl_val = v;
  L.ctl = kind;
  return sp_box_nil();
}

/* ---- literals ---- */

static sp_RbVal lupe_integer(const pm_node_t *n, const pm_integer_t *iv) {
  uint64_t u;
  if (!iv->values) u = iv->value;
  else if (iv->length == 2) u = (uint64_t)iv->values[1] << 32 | iv->values[0];
  else lupe_unsupported(n, "integer literal beyond 64 bits");
  if (u > (uint64_t)INT64_MAX + (iv->negative ? 1 : 0)) lupe_unsupported(n, "integer literal beyond 64 bits");
  return sp_box_int(iv->negative ? (sp_int)(0 - u) : (sp_int)u);
}

/* A new mutable String with the given bytes, as a literal makes one. */
static sp_RbVal lupe_new_string(const uint8_t *p, size_t len) {
  char *d = sp_str_alloc(len);
  if (len) memcpy(d, p, len);
  d[len] = 0;
  return sp_box_str_new(d);
}

static sp_RbVal lupe_interpolate(LupeFrame *f, const pm_node_list_t *parts) {
  sp_RbVal acc = lupe_new_string(NULL, 0), v = sp_box_nil();
  SP_GC_ROOT_RBVAL(acc);
  SP_GC_ROOT_RBVAL(v);
  for (size_t i = 0; i < parts->size; i++) {
    const pm_node_t *p = parts->nodes[i];
    if (PM_NODE_TYPE_P(p, PM_STRING_NODE)) {
      const pm_string_t *s = &((const pm_string_node_t *)p)->unescaped;
      v = lupe_new_string(pm_string_source(s), pm_string_length(s));
    } else if (PM_NODE_TYPE_P(p, PM_EMBEDDED_STATEMENTS_NODE)) {
      v = lupe_eval_stmts(f, ((const pm_embedded_statements_node_t *)p)->statements);
      if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
      v = sp_poly_to_s_box(v);
    } else {
      lupe_unsupported(p, "interpolation part");
    }
    acc = sp_poly_add(acc, v);
  }
  return acc;
}

static sp_RbVal lupe_array(LupeFrame *f, const pm_array_node_t *n) {
  sp_PolyArray *a = sp_PolyArray_new();
  SP_GC_ROOT(a);
  for (size_t i = 0; i < n->elements.size; i++) {
    const pm_node_t *e = n->elements.nodes[i];
    if (PM_NODE_TYPE_P(e, PM_SPLAT_NODE)) lupe_unsupported(e, "splat in an Array literal");
    sp_RbVal v = lupe_eval(f, e);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    sp_PolyArray_push(a, v);
  }
  return sp_box_poly_array(a);
}

/* ---- classes, modules and constants ---- */

/* The scope and the name that a constant path writes or defines. */
static void lupe_const_target(LupeFrame *f, const pm_node_t *path, LupeClass **scope, sp_sym *name) {
  if (PM_NODE_TYPE_P(path, PM_CONSTANT_READ_NODE)) {
    *scope = f->cref;
    *name = lupe_sym(((const pm_constant_read_node_t *)path)->name);
    return;
  }
  if (!PM_NODE_TYPE_P(path, PM_CONSTANT_PATH_NODE)) lupe_unsupported(path, "constant target");
  const pm_constant_path_node_t *cp = (const pm_constant_path_node_t *)path;
  *name = lupe_sym(cp->name);
  if (!cp->parent) { *scope = L.object; return; }
  sp_RbVal pv = lupe_eval(f, cp->parent);
  *scope = lupe_class_value(pv);
  if (!*scope) sp_raise_cls("TypeError", sp_sprintf("%s is not a class/module", sp_poly_inspect(pv)));
}

static sp_RbVal lupe_class_def(LupeFrame *f, const pm_node_t *path, const pm_node_t *super_node,
                               const pm_node_t *body, const pm_constant_id_list_t *locals, int is_module) {
  SP_GC_SAVE();
  LupeClass *scope;
  sp_sym name;
  lupe_const_target(f, path, &scope, &name);
  const char *cname = sp_sym_to_s(name);
  LupeClass *k = NULL;
  LupeConst *own = lupe_const_own(scope, name);
  if (own) {
    k = lupe_class_value(own->value);
    if (!k) sp_raise_cls("TypeError", sp_sprintf("%s is not a %s", cname, is_module ? "module" : "class"));
  } else if (scope == L.object) {
    k = lupe_builtin(cname);   /* reopen a builtin class */
    if (!k && lupe_unknown_builtin(cname)) lupe_gap(sp_sprintf("reopening of the builtin %s", cname));
  }
  if (!k) {
    LupeClass *super = NULL;
    if (!is_module) {
      super = L.object;
      if (super_node) {
        sp_RbVal sv = lupe_eval(f, super_node);
        if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
        super = lupe_class_value(sv);
        if (!super || super->is_module) sp_raise_cls("TypeError", "superclass must be an instance of Class");
      }
    }
    const char *full = scope == L.object ? cname : sp_sprintf("%s::%s", scope->name, cname);
    k = lupe_class_new(strdup(full), is_module, super, scope);
    lupe_const_set(scope, name, lupe_class_box(k));
    /* the hook of the superclass: inherited(subclass), before the body */
    LupeMethod *hook = super ? lupe_find_smethod(super, sp_sym_intern("inherited")) : NULL;
    if (hook) {
      sp_RbVal sub = lupe_class_box(k);
      lupe_invoke(hook, lupe_class_box(super), 1, &sub);
    }
  }
  if (!body) return sp_box_nil();
  LUPE_FRAME(cf, locals, lupe_class_box(k), k);
  return lupe_eval(&cf, body);
}

static sp_RbVal lupe_singleton_class(LupeFrame *f, const pm_singleton_class_node_t *n) {
  SP_GC_SAVE();
  sp_RbVal target = lupe_eval(f, n->expression);
  LupeClass *k = lupe_class_value(target);
  if (!k) lupe_unsupported((const pm_node_t *)n, "singleton class of an object");
  if (!n->body) return sp_box_nil();
  LUPE_FRAME(sf, &n->locals, target, k);
  sf.singleton_defs = 1;
  return lupe_eval(&sf, n->body);
}

static sp_RbVal lupe_const_read(LupeFrame *f, const pm_node_t *n) {
  sp_RbVal v;
  if (PM_NODE_TYPE_P(n, PM_CONSTANT_READ_NODE)) {
    sp_sym name = lupe_sym(((const pm_constant_read_node_t *)n)->name);
    if (!lupe_const_lookup(f->cref, name, &v)) lupe_const_missing(sp_sym_to_s(name));
    return v;
  }
  const pm_constant_path_node_t *cp = (const pm_constant_path_node_t *)n;
  sp_sym name = lupe_sym(cp->name);
  LupeClass *scope = L.object;
  if (cp->parent) {
    sp_RbVal pv = lupe_eval(f, cp->parent);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    scope = lupe_class_value(pv);
    if (!scope) sp_raise_cls("TypeError", sp_sprintf("%s is not a class/module", sp_poly_inspect(pv)));
  }
  if (!lupe_const_scoped(scope, name, &v)) lupe_const_missing(sp_sym_to_s(name));
  return v;
}

/* x op= v on a local, an ivar or an attribute: read, send op, write. */
static sp_RbVal lupe_op(sp_RbVal cur, pm_constant_id_t op, sp_RbVal v) {
  SP_GC_ROOT_RBVAL(cur);
  SP_GC_ROOT_RBVAL(v);
  return lupe_send(cur, sp_sym_to_s(lupe_sym(op)), 1, &v);
}

static sp_RbVal lupe_call_op_write(LupeFrame *f, const pm_call_operator_write_node_t *n) {
  SP_GC_SAVE();
  if (!n->receiver) lupe_unsupported((const pm_node_t *)n, "attribute op-assign with no receiver");
  sp_RbVal recv = lupe_eval(f, n->receiver);
  SP_GC_ROOT_RBVAL(recv);
  if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
  sp_RbVal cur = lupe_send(recv, sp_sym_to_s(lupe_sym(n->read_name)), 0, NULL);
  SP_GC_ROOT_RBVAL(cur);
  sp_RbVal v = lupe_eval(f, n->value);
  if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
  sp_RbVal nv = lupe_op(cur, n->binary_operator, v);
  SP_GC_ROOT_RBVAL(nv);
  lupe_send(recv, sp_sym_to_s(lupe_sym(n->write_name)), 1, &nv);
  return nv;
}

static sp_RbVal lupe_index_op_write(LupeFrame *f, const pm_index_operator_write_node_t *n) {
  SP_GC_SAVE();
  if (n->block) lupe_unsupported((const pm_node_t *)n, "block");
  sp_RbVal recv = lupe_eval(f, n->receiver);
  SP_GC_ROOT_RBVAL(recv);
  if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
  int argc = lupe_argc(n->arguments);
  sp_RbVal argv[argc + 1];
  if (!lupe_args(f, n->arguments, argc, argv)) return sp_box_nil();
  sp_RbVal cur = lupe_send(recv, "[]", argc, argv);
  SP_GC_ROOT_RBVAL(cur);
  sp_RbVal v = lupe_eval(f, n->value);
  if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
  argv[argc] = lupe_op(cur, n->binary_operator, v);
  _sp_gc_root_push((void **)((uintptr_t)&argv[argc] | (uintptr_t)1));
  lupe_send(recv, "[]=", argc + 1, argv);
  return argv[argc];
}

/* ---- the evaluator ---- */

static sp_RbVal lupe_eval(LupeFrame *f, const pm_node_t *n) {
  switch (PM_NODE_TYPE(n)) {
  case PM_STATEMENTS_NODE:
    return lupe_eval_stmts(f, (const pm_statements_node_t *)n);
  case PM_PARENTHESES_NODE: {
    const pm_node_t *b = ((const pm_parentheses_node_t *)n)->body;
    return b ? lupe_eval(f, b) : sp_box_nil();
  }
  case PM_INTEGER_NODE:
    return lupe_integer(n, &((const pm_integer_node_t *)n)->value);
  case PM_FLOAT_NODE:
    return sp_box_float(((const pm_float_node_t *)n)->value);
  case PM_STRING_NODE: {
    const pm_string_t *s = &((const pm_string_node_t *)n)->unescaped;
    return lupe_new_string(pm_string_source(s), pm_string_length(s));
  }
  case PM_INTERPOLATED_STRING_NODE:
    return lupe_interpolate(f, &((const pm_interpolated_string_node_t *)n)->parts);
  case PM_SYMBOL_NODE: {
    const pm_string_t *s = &((const pm_symbol_node_t *)n)->unescaped;
    return sp_box_sym(sp_sym_intern_n((const char *)pm_string_source(s), pm_string_length(s)));
  }
  case PM_TRUE_NODE: return sp_box_bool(1);
  case PM_FALSE_NODE: return sp_box_bool(0);
  case PM_NIL_NODE: return sp_box_nil();
  case PM_ARRAY_NODE:
    return lupe_array(f, (const pm_array_node_t *)n);
  case PM_LOCAL_VARIABLE_READ_NODE: {
    const pm_local_variable_read_node_t *r = (const pm_local_variable_read_node_t *)n;
    return *lupe_local(f, n, r->name, r->depth);
  }
  case PM_LOCAL_VARIABLE_WRITE_NODE: {
    const pm_local_variable_write_node_t *w = (const pm_local_variable_write_node_t *)n;
    sp_RbVal v = lupe_eval(f, w->value);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    *lupe_local(f, n, w->name, w->depth) = v;
    return v;
  }
  case PM_LOCAL_VARIABLE_OPERATOR_WRITE_NODE: {
    const pm_local_variable_operator_write_node_t *w = (const pm_local_variable_operator_write_node_t *)n;
    sp_RbVal *slot = lupe_local(f, n, w->name, w->depth);
    sp_RbVal v = lupe_eval(f, w->value);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    SP_GC_ROOT_RBVAL(v);
    *slot = lupe_send(*slot, sp_sym_to_s(lupe_sym(w->binary_operator)), 1, &v);
    return *slot;
  }
  case PM_AND_NODE: {
    const pm_and_node_t *a = (const pm_and_node_t *)n;
    sp_RbVal l = lupe_eval(f, a->left);
    if (L.ctl != LUPE_CTL_NONE || !sp_poly_truthy(l)) return l;
    return lupe_eval(f, a->right);
  }
  case PM_OR_NODE: {
    const pm_or_node_t *o = (const pm_or_node_t *)n;
    sp_RbVal l = lupe_eval(f, o->left);
    if (L.ctl != LUPE_CTL_NONE || sp_poly_truthy(l)) return l;
    return lupe_eval(f, o->right);
  }
  case PM_IF_NODE: {
    const pm_if_node_t *i = (const pm_if_node_t *)n;
    sp_RbVal c = lupe_eval(f, i->predicate);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    if (sp_poly_truthy(c)) return lupe_eval_stmts(f, i->statements);
    return i->subsequent ? lupe_eval(f, i->subsequent) : sp_box_nil();
  }
  case PM_UNLESS_NODE: {
    const pm_unless_node_t *u = (const pm_unless_node_t *)n;
    sp_RbVal c = lupe_eval(f, u->predicate);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    if (!sp_poly_truthy(c)) return lupe_eval_stmts(f, u->statements);
    return u->else_clause ? lupe_eval(f, (const pm_node_t *)u->else_clause) : sp_box_nil();
  }
  case PM_ELSE_NODE:
    return lupe_eval_stmts(f, ((const pm_else_node_t *)n)->statements);
  case PM_WHILE_NODE: {
    const pm_while_node_t *w = (const pm_while_node_t *)n;
    if (PM_NODE_FLAG_P(n, PM_LOOP_FLAGS_BEGIN_MODIFIER)) lupe_unsupported(n, "begin...end while");
    return lupe_while(f, w->predicate, w->statements, 0);
  }
  case PM_UNTIL_NODE: {
    const pm_until_node_t *u = (const pm_until_node_t *)n;
    if (PM_NODE_FLAG_P(n, PM_LOOP_FLAGS_BEGIN_MODIFIER)) lupe_unsupported(n, "begin...end until");
    return lupe_while(f, u->predicate, u->statements, 1);
  }
  case PM_RETURN_NODE:
    return lupe_jump(f, LUPE_CTL_RETURN, ((const pm_return_node_t *)n)->arguments);
  case PM_BREAK_NODE:
    return lupe_jump(f, LUPE_CTL_BREAK, ((const pm_break_node_t *)n)->arguments);
  case PM_NEXT_NODE:
    return lupe_jump(f, LUPE_CTL_NEXT, ((const pm_next_node_t *)n)->arguments);
  case PM_DEF_NODE: {
    const pm_def_node_t *d = (const pm_def_node_t *)n;
    lupe_define(f, d);
    return sp_box_sym(lupe_sym(d->name));
  }
  case PM_CALL_NODE:
    return lupe_call(f, (const pm_call_node_t *)n);
  case PM_CALL_OPERATOR_WRITE_NODE:
    return lupe_call_op_write(f, (const pm_call_operator_write_node_t *)n);
  case PM_INDEX_OPERATOR_WRITE_NODE:
    return lupe_index_op_write(f, (const pm_index_operator_write_node_t *)n);
  case PM_SUPER_NODE: {
    const pm_super_node_t *s = (const pm_super_node_t *)n;
    if (s->block) lupe_unsupported(n, "block");
    return lupe_super(f, n, s->arguments, 0);
  }
  case PM_FORWARDING_SUPER_NODE:
    if (((const pm_forwarding_super_node_t *)n)->block) lupe_unsupported(n, "block");
    return lupe_super(f, n, NULL, 1);
  case PM_SELF_NODE:
    return f->self;
  case PM_CLASS_NODE: {
    const pm_class_node_t *c = (const pm_class_node_t *)n;
    return lupe_class_def(f, c->constant_path, c->superclass, c->body, &c->locals, 0);
  }
  case PM_MODULE_NODE: {
    const pm_module_node_t *m = (const pm_module_node_t *)n;
    return lupe_class_def(f, m->constant_path, NULL, m->body, &m->locals, 1);
  }
  case PM_SINGLETON_CLASS_NODE:
    return lupe_singleton_class(f, (const pm_singleton_class_node_t *)n);
  case PM_CONSTANT_READ_NODE:
  case PM_CONSTANT_PATH_NODE:
    return lupe_const_read(f, n);
  case PM_CONSTANT_WRITE_NODE: {
    const pm_constant_write_node_t *w = (const pm_constant_write_node_t *)n;
    sp_RbVal v = lupe_eval(f, w->value);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    lupe_const_set(f->cref, lupe_sym(w->name), v);
    return v;
  }
  case PM_CONSTANT_PATH_WRITE_NODE: {
    const pm_constant_path_write_node_t *w = (const pm_constant_path_write_node_t *)n;
    LupeClass *scope;
    sp_sym name;
    lupe_const_target(f, (const pm_node_t *)w->target, &scope, &name);
    sp_RbVal v = lupe_eval(f, w->value);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    lupe_const_set(scope, name, v);
    return v;
  }
  case PM_INSTANCE_VARIABLE_READ_NODE:
    return lupe_ivar_get(f->self, lupe_sym(((const pm_instance_variable_read_node_t *)n)->name));
  case PM_INSTANCE_VARIABLE_WRITE_NODE: {
    const pm_instance_variable_write_node_t *w = (const pm_instance_variable_write_node_t *)n;
    sp_RbVal v = lupe_eval(f, w->value);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    lupe_ivar_set(f->self, lupe_sym(w->name), v);
    return v;
  }
  case PM_INSTANCE_VARIABLE_OPERATOR_WRITE_NODE: {
    const pm_instance_variable_operator_write_node_t *w = (const pm_instance_variable_operator_write_node_t *)n;
    sp_sym name = lupe_sym(w->name);
    sp_RbVal v = lupe_eval(f, w->value);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    sp_RbVal nv = lupe_op(lupe_ivar_get(f->self, name), w->binary_operator, v);
    lupe_ivar_set(f->self, name, nv);
    return nv;
  }
  case PM_INSTANCE_VARIABLE_OR_WRITE_NODE: {
    const pm_instance_variable_or_write_node_t *w = (const pm_instance_variable_or_write_node_t *)n;
    sp_sym name = lupe_sym(w->name);
    sp_RbVal cur = lupe_ivar_get(f->self, name);
    if (sp_poly_truthy(cur)) return cur;
    sp_RbVal v = lupe_eval(f, w->value);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    lupe_ivar_set(f->self, name, v);
    return v;
  }
  case PM_LOCAL_VARIABLE_OR_WRITE_NODE: {
    const pm_local_variable_or_write_node_t *w = (const pm_local_variable_or_write_node_t *)n;
    sp_RbVal *slot = lupe_local(f, n, w->name, w->depth);
    if (sp_poly_truthy(*slot)) return *slot;
    sp_RbVal v = lupe_eval(f, w->value);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    *slot = v;
    return v;
  }
  case PM_BEGIN_NODE: {
    const pm_begin_node_t *b = (const pm_begin_node_t *)n;
    if (b->rescue_clause || b->else_clause || b->ensure_clause) lupe_unsupported(n, "begin with rescue/ensure");
    return lupe_eval_stmts(f, b->statements);
  }
  default:
    lupe_unsupported(n, "node");
  }
}
