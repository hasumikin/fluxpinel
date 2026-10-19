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

/* Declares the slots of a frame on the C stack and roots each one. The
   roots stay until the enclosing SP_GC_SAVE scope ends. */
#define LUPE_FRAME(f, namelist)                                                   \
  size_t f##_n = (namelist)->size;                                                \
  sp_RbVal f##_slots[f##_n ? f##_n : 1];                                           \
  for (size_t f##_i = 0; f##_i < f##_n; f##_i++) {                                 \
    f##_slots[f##_i] = sp_box_nil();                                               \
    _sp_gc_root_push((void **)((uintptr_t)&f##_slots[f##_i] | (uintptr_t)1));      \
  }                                                                               \
  LupeFrame f = { f##_slots, (namelist) }

/* ---- methods ---- */

static void lupe_define(const pm_def_node_t *def) {
  if (def->receiver) lupe_unsupported((const pm_node_t *)def, "singleton method");
  sp_sym name = lupe_sym(def->name);
  for (int i = 0; i < L.nmethods; i++)
    if (L.methods[i].name == name) { L.methods[i].def = def; return; }
  if (L.nmethods == L.methods_cap) {
    L.methods_cap = L.methods_cap ? L.methods_cap * 2 : 16;
    L.methods = realloc(L.methods, sizeof *L.methods * (size_t)L.methods_cap);
  }
  L.methods[L.nmethods++] = (LupeMethod){ name, def };
}

static const pm_def_node_t *lupe_find_method(sp_sym name) {
  for (int i = 0; i < L.nmethods; i++)
    if (L.methods[i].name == name) return L.methods[i].def;
  return NULL;
}

static sp_RbVal lupe_invoke(const pm_def_node_t *def, int argc, sp_RbVal *argv) {
  SP_GC_SAVE();
  const pm_parameters_node_t *ps = def->parameters;
  int nreq = 0;
  if (ps) {
    if (ps->optionals.size || ps->rest || ps->posts.size || ps->keywords.size ||
        ps->keyword_rest || ps->block)
      lupe_unsupported((const pm_node_t *)ps, "parameter kind");
    nreq = (int)ps->requireds.size;
  }
  if (argc != nreq)
    sp_raise_cls("ArgumentError", sp_sprintf("wrong number of arguments (given %d, expected %d)", argc, nreq));
  LUPE_FRAME(f, &def->locals);
  for (int i = 0; i < nreq; i++) {
    const pm_node_t *p = ps->requireds.nodes[i];
    if (!PM_NODE_TYPE_P(p, PM_REQUIRED_PARAMETER_NODE)) lupe_unsupported(p, "parameter pattern");
    *lupe_local(&f, p, ((const pm_required_parameter_node_t *)p)->name, 0) = argv[i];
  }
  sp_RbVal v = def->body ? lupe_eval(&f, def->body) : sp_box_nil();
  if (L.ctl == LUPE_CTL_RETURN) { v = L.ctl_val; L.ctl = LUPE_CTL_NONE; }
  return v;
}

/* ---- calls ---- */

static sp_RbVal lupe_call(LupeFrame *f, const pm_call_node_t *c) {
  SP_GC_SAVE();
  if (c->block) lupe_unsupported((const pm_node_t *)c, "block");
  const char *name = sp_sym_to_s(lupe_sym(c->name));
  sp_RbVal recv = sp_box_nil();
  SP_GC_ROOT_RBVAL(recv);
  if (c->receiver) recv = lupe_eval(f, c->receiver);
  if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
  int argc = c->arguments ? (int)c->arguments->arguments.size : 0;
  sp_RbVal argv[argc ? argc : 1];
  for (int i = 0; i < argc; i++) {
    const pm_node_t *a = c->arguments->arguments.nodes[i];
    if (PM_NODE_TYPE_P(a, PM_SPLAT_NODE) || PM_NODE_TYPE_P(a, PM_KEYWORD_HASH_NODE) ||
        PM_NODE_TYPE_P(a, PM_BLOCK_ARGUMENT_NODE))
      lupe_unsupported(a, "argument kind");
    argv[i] = sp_box_nil();
    _sp_gc_root_push((void **)((uintptr_t)&argv[i] | (uintptr_t)1));
  }
  for (int i = 0; i < argc; i++) {
    argv[i] = lupe_eval(f, c->arguments->arguments.nodes[i]);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
  }
  if (c->receiver) return lupe_send(recv, name, argc, argv);
  const pm_def_node_t *def = lupe_find_method(lupe_sym(c->name));
  if (def) return lupe_invoke(def, argc, argv);
  sp_RbVal out;
  if (lupe_kernel(name, argc, argv, &out)) return out;
  sp_raise_cls("NoMethodError", sp_sprintf("undefined method '%s' for main", name));
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
    lupe_define(d);
    return sp_box_sym(lupe_sym(d->name));
  }
  case PM_CALL_NODE:
    return lupe_call(f, (const pm_call_node_t *)n);
  default:
    lupe_unsupported(n, "node");
  }
}
