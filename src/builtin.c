/* builtin.c -- the builtin methods of Lupe.

   A call on a receiver tries, in order:

   1. the core methods below (operators, Array access, conversions),
      written by hand because spinel emits them from codegen code, not
      from bop_rows[];
   2. the sp_poly_* helpers that answer a method as a whole;
   3. the wrappers that tools/bop_gen.c generates from bop_rows[];
   4. NoMethodError.

   A call with no receiver that names no `def` goes to lupe_kernel. */

#include "lupe.h"

/* The family, variant and TyKind of a value, in the spelling of the
   generated table. Answers 0 for a value outside the table. */
static int lupe_kind_of(sp_RbVal v, const char **family, const char **variant) {
  const char *fam = NULL, *var = NULL;
  int kind = LUPE_TY_POLY;
  switch (v.tag) {
  case SP_TAG_INT: fam = var = "int"; kind = LUPE_TY_INT; break;
  case SP_TAG_FLT: fam = var = "float"; kind = LUPE_TY_FLOAT; break;
  case SP_TAG_STR: fam = var = "string"; kind = LUPE_TY_STRING; break;
  case SP_TAG_SYM: fam = var = "symbol"; kind = LUPE_TY_SYMBOL; break;
  case SP_TAG_BOOL: kind = LUPE_TY_BOOL; break;
  case SP_TAG_NIL: kind = LUPE_TY_NIL; break;
  case SP_TAG_OBJ:
    switch (v.cls_id) {
    case SP_BUILTIN_STRBUF: fam = var = "string"; kind = LUPE_TY_STRING; break;
    case SP_BUILTIN_POLY_ARRAY: fam = "ANY_ARRAY"; var = "poly_array"; kind = LUPE_TY_POLY_ARRAY; break;
    case SP_BUILTIN_INT_ARRAY: fam = "ANY_ARRAY"; var = "int_array"; kind = LUPE_TY_INT_ARRAY; break;
    case SP_BUILTIN_FLT_ARRAY: fam = "ANY_ARRAY"; var = "float_array"; kind = LUPE_TY_FLOAT_ARRAY; break;
    case SP_BUILTIN_STR_ARRAY: fam = "ANY_ARRAY"; var = "str_array"; kind = LUPE_TY_STR_ARRAY; break;
    default: break;
    }
    break;
  default: break;
  }
  if (family) *family = fam;
  if (variant) *variant = var;
  return kind;
}

static int lupe_kind_fits(unsigned long long mask, sp_RbVal v) {
  if (!mask) return 1;
  int k = lupe_kind_of(v, NULL, NULL);
  return (mask >> k & 1) || (mask >> LUPE_TY_POLY & 1);
}

/* The generated wrapper for this call, or NULL. */
static lupe_bop_fn lupe_bop_find(sp_RbVal recv, const char *name, int argc, sp_RbVal *argv) {
  const char *fam, *var;
  lupe_kind_of(recv, &fam, &var);
  if (!fam) return NULL;
  for (int i = 0; i < lupe_bop_count; i++) {
    const LupeBop *b = &lupe_bop_table[i];
    if (!b->fn || strcmp(b->name, name) || strcmp(b->recv, fam) || strcmp(b->variant, var)) continue;
    if (argc < b->argc_min || argc > b->argc_max) continue;
    if (argc > 0 && !lupe_kind_fits(b->arg0, argv[0])) continue;
    if (argc > 1 && !lupe_kind_fits(b->arg1, argv[1])) continue;
    return b->fn;
  }
  return NULL;
}

static int lupe_is_array(sp_RbVal v) {
  return v.tag == SP_TAG_OBJ && sp_poly_is_array_kind(v.cls_id);
}

/* A String has two forms: a literal (SP_TAG_STR) and a mutable handle. */
static int lupe_is_string(sp_RbVal v) {
  return v.tag == SP_TAG_STR || (v.tag == SP_TAG_OBJ && v.cls_id == SP_BUILTIN_STRBUF);
}

/* Methods that a runtime helper on boxed values answers as a whole. The
   helper checks the receiver kind and raises as the AOT code does. */
static const struct {
  const char *name;
  sp_RbVal (*fn1)(sp_RbVal);
  sp_RbVal (*fn2)(sp_RbVal, sp_RbVal);
} lupe_poly_helpers[] = {
  { "floor", sp_poly_floor, NULL },
  { "ceil", sp_poly_ceil, NULL },
  { "sum", sp_poly_sum, NULL },
  { "gcd", NULL, sp_poly_int_gcd },
};

static int lupe_poly_helper(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  for (size_t i = 0; i < sizeof lupe_poly_helpers / sizeof lupe_poly_helpers[0]; i++) {
    if (strcmp(lupe_poly_helpers[i].name, name)) continue;
    if (argc == 0 && lupe_poly_helpers[i].fn1) { *out = lupe_poly_helpers[i].fn1(r); return 1; }
    if (argc == 1 && lupe_poly_helpers[i].fn2) { *out = lupe_poly_helpers[i].fn2(r, argv[0]); return 1; }
  }
  return 0;
}

/* The core methods. Answers 1 and sets *out when it handles the call. */
static int lupe_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (argc == 0) {
    if (!strcmp(name, "!")) { *out = sp_box_bool(!sp_poly_truthy(r)); return 1; }
    if (!strcmp(name, "nil?")) { *out = sp_box_bool(r.tag == SP_TAG_NIL); return 1; }
    if (!strcmp(name, "to_s")) { *out = sp_poly_to_s_box(r); return 1; }
    if (!strcmp(name, "inspect")) { *out = sp_box_str_new(sp_str_dup(sp_poly_inspect(r))); return 1; }
    if (!strcmp(name, "-@")) { *out = sp_poly_neg(r); return 1; }
    if ((!strcmp(name, "length") || !strcmp(name, "size")) && (lupe_is_array(r) || lupe_is_string(r))) {
      *out = sp_box_int(sp_poly_length_m(r));
      return 1;
    }
    if (lupe_is_array(r) && !strcmp(name, "first")) { *out = sp_poly_first(r); return 1; }
    if (lupe_is_array(r) && !strcmp(name, "last")) { *out = sp_poly_last(r); return 1; }
    return 0;
  }
  if (argc == 1) {
    sp_RbVal a = argv[0];
    if (!strcmp(name, "+")) { *out = sp_poly_add(r, a); return 1; }
    if (!strcmp(name, "-")) { *out = sp_poly_sub(r, a); return 1; }
    if (!strcmp(name, "*")) { *out = sp_poly_mul(r, a); return 1; }
    if (!strcmp(name, "/")) { *out = sp_poly_div(r, a); return 1; }
    if (!strcmp(name, "%")) { *out = sp_poly_mod(r, a); return 1; }
    if (!strcmp(name, "**")) { *out = sp_poly_pow(r, a); return 1; }
    if (!strcmp(name, "==")) { *out = sp_poly_eq_value(r, a, 0); return 1; }
    if (!strcmp(name, "!=")) { *out = sp_poly_eq_value(r, a, 1); return 1; }
    if (!strcmp(name, "<") || !strcmp(name, "<=") || !strcmp(name, ">") || !strcmp(name, ">=")) {
      *out = sp_poly_relop_v(name, r, a);
      return 1;
    }
    if (lupe_is_array(r) && !strcmp(name, "[]") && a.tag == SP_TAG_INT) {
      *out = sp_poly_arr_get(r, a.v.i);
      return 1;
    }
    if (lupe_is_array(r) && !strcmp(name, "include?")) {
      *out = sp_box_bool(sp_PolyArray_include(sp_poly_to_poly_array(r), a));
      return 1;
    }
    if (lupe_is_string(r) && lupe_is_string(a) &&
        (!strcmp(name, "start_with?") || !strcmp(name, "end_with?"))) {
      const char *s = sp_poly_recv_s(r, name), *x = sp_poly_recv_s(a, name);
      *out = sp_box_bool(name[0] == 's' ? sp_str_start_with(s, x) : sp_str_end_with(s, x));
      return 1;
    }
    /* the String-separator form, as codegen emits it (codegen_call_recv.c) */
    if (lupe_is_string(r) && lupe_is_string(a) && !strcmp(name, "split")) {
      sp_StrArray *parts = sp_str_split_drop_trailing(sp_poly_recv_s(r, name), sp_poly_recv_s(a, name));
      *out = sp_box_nullable_obj((void *)parts, SP_BUILTIN_STR_ARRAY);
      return 1;
    }
    if (r.tag == SP_TAG_OBJ && r.cls_id == SP_BUILTIN_POLY_ARRAY &&
        (!strcmp(name, "<<") || !strcmp(name, "push"))) {
      sp_PolyArray_push((sp_PolyArray *)r.v.p, a);
      *out = r;
      return 1;
    }
    return 0;
  }
  if (argc == 2 && lupe_is_array(r) && !strcmp(name, "[]=") && argv[0].tag == SP_TAG_INT) {
    sp_poly_arr_set(r, argv[0].v.i, argv[1]);
    *out = argv[1];
    return 1;
  }
  return 0;
}

static sp_RbVal lupe_send(sp_RbVal recv, const char *name, int argc, sp_RbVal *argv) {
  sp_RbVal out;
  if (lupe_core(recv, name, argc, argv, &out)) return out;
  if (lupe_poly_helper(recv, name, argc, argv, &out)) return out;
  lupe_bop_fn fn = lupe_bop_find(recv, name, argc, argv);
  if (fn) return fn(recv, argv);
  sp_raise_cls("NoMethodError", sp_sprintf("undefined method '%s' for an instance of %s", name,
                                           sp_poly_class_name(recv)));
}

/* Kernel methods. Answers 1 and sets *out when it handles the call. */
static int lupe_kernel(const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (!strcmp(name, "puts")) {
    if (argc == 0) sp_puts_str_line(sp_str_empty);
    for (int i = 0; i < argc; i++) sp_poly_puts(argv[i]);
    *out = sp_box_nil();
    return 1;
  }
  if (!strcmp(name, "print")) {
    for (int i = 0; i < argc; i++) sp_poly_print(argv[i]);
    *out = sp_box_nil();
    return 1;
  }
  if (!strcmp(name, "p")) {
    for (int i = 0; i < argc; i++) sp_puts_str_line(sp_poly_inspect(argv[i]));
    if (argc == 0) *out = sp_box_nil();
    else if (argc == 1) *out = argv[0];
    else {
      sp_PolyArray *a = sp_PolyArray_new();
      for (int i = 0; i < argc; i++) sp_PolyArray_push(a, argv[i]);
      *out = sp_box_poly_array(a);
    }
    return 1;
  }
  return 0;
}
