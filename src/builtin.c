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
    case SP_BUILTIN_RANGE: fam = var = "range"; kind = LUPE_TY_RANGE; break;
    case SP_BUILTIN_EXCEPTION: fam = var = "exception"; break;
    case SP_BUILTIN_REGEX: fam = var = "regex"; kind = LUPE_TY_REGEX; break;
    case SP_BUILTIN_MATCHDATA: fam = var = "matchdata"; kind = LUPE_TY_MATCHDATA; break;
    case SP_BUILTIN_IO: fam = var = "io"; kind = LUPE_TY_IO; break;
    case SP_BUILTIN_STR_INT_HASH: fam = "ANY_HASH"; var = "str_int_hash"; kind = LUPE_TY_STR_INT_HASH; break;
    case SP_BUILTIN_STR_STR_HASH: fam = "ANY_HASH"; var = "str_str_hash"; kind = LUPE_TY_STR_STR_HASH; break;
    case SP_BUILTIN_INT_INT_HASH: fam = "ANY_HASH"; var = "int_int_hash"; kind = LUPE_TY_INT_INT_HASH; break;
    case SP_BUILTIN_INT_STR_HASH: fam = "ANY_HASH"; var = "int_str_hash"; kind = LUPE_TY_INT_STR_HASH; break;
    case SP_BUILTIN_SYM_POLY_HASH: fam = "ANY_HASH"; var = "sym_poly_hash"; kind = LUPE_TY_SYM_POLY_HASH; break;
    case SP_BUILTIN_STR_POLY_HASH: fam = "ANY_HASH"; var = "str_poly_hash"; kind = LUPE_TY_STR_POLY_HASH; break;
    case SP_BUILTIN_POLY_POLY_HASH:
      fam = "ANY_HASH";
      var = "poly_poly_hash";
      kind = LUPE_TY_POLY_POLY_HASH;
      break;
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

/* An Integer has two forms: a machine word and a Bignum. */
static int lupe_is_integer(sp_RbVal v) {
  return v.tag == SP_TAG_INT || v.tag == SP_TAG_BIGINT;
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
    /* String#to_s answers the String itself, so a change to it shows */
    if (!strcmp(name, "to_s")) { *out = lupe_is_string(r) ? r : sp_poly_to_s_box(r); return 1; }
    if (!strcmp(name, "inspect")) { *out = sp_box_str_new(sp_str_dup(sp_poly_inspect(r))); return 1; }
    /* the value that a Hash key hashes by; it differs from CRuby's numbers */
    if (!strcmp(name, "hash")) { *out = sp_box_int(sp_rbval_hash_key(r)); return 1; }
    if (!strcmp(name, "-@") && !lupe_is_string(r)) { *out = sp_poly_neg(r); return 1; }
    /* +str: the String itself when it can change, else a mutable copy */
    if (!strcmp(name, "+@") && lupe_is_string(r)) {
      if (r.tag == SP_TAG_OBJ && !sp_gc_is_frozen(r.v.p)) { *out = r; return 1; }
      const char *s = sp_poly_recv_s(r, name);
      *out = lupe_new_string((const uint8_t *)s, strlen(s));
      return 1;
    }
    if (!strcmp(name, "+@") && (lupe_is_integer(r) || r.tag == SP_TAG_FLT)) { *out = r; return 1; }
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
    /* the bit operators of Integer, which answer a Bignum too */
    if (lupe_is_integer(r)) {
      if (!strcmp(name, "&")) { *out = sp_poly_band(r, a); return 1; }
      if (!strcmp(name, "|")) { *out = sp_poly_bor(r, a); return 1; }
      if (!strcmp(name, "^")) { *out = sp_poly_bxor(r, a); return 1; }
      if (!strcmp(name, "<<")) { *out = sp_poly_shl(r, a); return 1; }
      if (!strcmp(name, ">>")) { *out = sp_poly_shr(r, a); return 1; }
    }
    /* str << other on a mutable String: the handle grows in place */
    if (r.tag == SP_TAG_OBJ && r.cls_id == SP_BUILTIN_STRBUF && lupe_is_string(a) &&
        (!strcmp(name, "<<") || !strcmp(name, "concat"))) {
      sp_String_append((sp_String *)r.v.p, sp_poly_recv_s(a, name));
      *out = r;
      return 1;
    }
    if (r.tag == SP_TAG_OBJ && r.cls_id == SP_BUILTIN_POLY_ARRAY &&
        (!strcmp(name, "<<") || !strcmp(name, "push"))) {
      sp_PolyArray_push((sp_PolyArray *)r.v.p, a);
      *out = r;
      return 1;
    }
    if (!strcmp(name, "<=>")) {
      sp_bool comparable = FALSE;
      sp_int c = sp_poly_cmp(r, a, &comparable);
      /* Object#<=>: 0 for the same object or an == one, else nil */
      if (comparable) *out = sp_box_int(c);
      else *out = sp_poly_truthy(sp_poly_eq_value(r, a, 0)) ? sp_box_int(0) : sp_box_nil();
      return 1;
    }
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
    return 0;
  }
  if (argc == 2 && lupe_is_array(r) && !strcmp(name, "[]=") && argv[0].tag == SP_TAG_INT) {
    sp_poly_arr_set(r, argv[0].v.i, argv[1]);
    *out = argv[1];
    return 1;
  }
  return 0;
}

/* Whether a builtin value answers a method, for respond_to?: a core
   method, a helper or a row of the generated table. It knows only the
   methods that Lupe can call, so it answers false for a method of CRuby
   that Lupe lacks. */
static int lupe_builtin_responds(sp_RbVal r, const char *name) {
  static const char *const any[] = { "!", "nil?", "to_s", "inspect", "==", "!=", "class", "is_a?", "kind_of?",
                                     "instance_of?", "respond_to?", "send", "frozen?", "freeze", "itself", NULL };
  static const char *const num[] = { "+", "-", "*", "/", "%", "**", "<", "<=", ">", ">=", "-@", NULL };
  static const char *const bits[] = { "&", "|", "^", "<<", ">>", NULL };
  static const char *const str[] = { "+", "*", "%", "length", "size", "start_with?", "end_with?", "split", NULL };
  static const char *const ary[] = { "length", "size", "first", "last", "include?", "[]", "[]=", "<<", "push", "+",
                                     NULL };
  /* the Enumerable methods of iter.c, and the methods of hash.c */
  static const char *const enm[] = { "each", "each_with_index", "each_with_object", "map", "collect", "flat_map",
                                     "select", "filter", "reject", "filter_map", "find", "detect", "count", "any?",
                                     "all?", "none?", "one?", "inject", "reduce", "sum", "min_by", "max_by",
                                     "sort_by", "sort", "min", "max", "partition", "group_by", "to_h", "to_a",
                                     "take_while", "drop_while", "each_slice", "each_cons", "reverse_each", NULL };
  static const char *const hsh[] = { "[]", "[]=", "store", "key?", "has_key?", "include?", "member?", "fetch",
                                     "delete", "keys", "values", "size", "length", "empty?", "each_pair",
                                     "each_key", "each_value", "merge", "merge!", "update", "transform_values",
                                     "transform_keys", "dig", "default", "invert", "key", "compact", NULL };
  const char *const *lists[6] = { any, NULL, NULL, NULL, NULL, NULL };
  int nl = 1;
  if (lupe_is_integer(r) || r.tag == SP_TAG_FLT) lists[nl++] = num;
  if (lupe_is_integer(r)) lists[nl++] = bits;
  if (lupe_is_string(r)) lists[nl++] = str;
  if (lupe_is_array(r)) lists[nl++] = ary;
  if (lupe_is_array(r) || lupe_is_range(r) || lupe_is_hash(r)) lists[nl++] = enm;
  if (lupe_is_hash(r)) lists[nl++] = hsh;
  for (int l = 0; l < nl; l++)
    for (int i = 0; lists[l][i]; i++)
      if (!strcmp(lists[l][i], name)) return 1;
  for (size_t i = 0; i < sizeof lupe_poly_helpers / sizeof lupe_poly_helpers[0]; i++)
    if (!strcmp(lupe_poly_helpers[i].name, name)) return 1;
  const char *fam, *var;
  lupe_kind_of(r, &fam, &var);
  if (!fam) return 0;
  for (int i = 0; i < lupe_bop_count; i++) {
    const LupeBop *b = &lupe_bop_table[i];
    if (b->fn && !strcmp(b->name, name) && !strcmp(b->recv, fam) && !strcmp(b->variant, var)) return 1;
  }
  return 0;
}

/* LUPE_TRACE_MISSING=1 reports each method that Lupe cannot find, before
   it raises NoMethodError. The oracle (tools/oracle.rb) reads the report
   to tell a gap of Lupe from a NoMethodError that the program expects. */
static void lupe_trace_missing(const char *cls, const char *name, int argc) {
  static int on = -1;
  if (on < 0) { const char *e = getenv("LUPE_TRACE_MISSING"); on = e && *e && *e != '0'; }
  if (on) fprintf(stderr, "lupe: missing builtin %s#%s/%d\n", cls, name, argc);
}

/* A call on a receiver. A method that the program defines comes first: on
   a Lupe object, on a class (a class method), or on a reopened builtin
   class. A top-level def is a private method of Object, so an explicit
   receiver does not reach it. */
static sp_RbVal lupe_send(sp_RbVal recv, const char *name, int argc, sp_RbVal *argv) {
  return lupe_send_b(recv, name, argc, argv, NULL);
}

/* The builtin methods of a value, without the methods of the program:
   for a call, and for super from a method of the program. A builtin
   method that takes a block is one of lupe_iter; Lupe stops at any other
   one, since the generated wrappers would drop the block. */
static int lupe_builtin_call(sp_RbVal recv, const char *name, int argc, sp_RbVal *argv, const LupeBlock *blk,
                             sp_RbVal *out) {
  if (lupe_is_proc(recv) && lupe_proc_core(recv, name, argc, argv, out)) return 1;
  if (!blk && lupe_range_core(recv, name, argc, argv, out)) return 1;
  if (lupe_hash_core(recv, name, argc, argv, blk, out)) return 1;
  if (!blk && lupe_array_hash(recv, name, argc, out)) return 1;
  if (lupe_exc_core(recv, name, argc, argv, out)) return 1;
  if (lupe_re_call(recv, name, argc, argv, blk, out)) return 1;
  if (!blk && lupe_io_core(recv, name, argc, argv, out)) return 1;
  if (lupe_iter(recv, name, argc, argv, blk, out)) return 1;
  if (blk) {
    if (argc >= 1 && (!strcmp(name, "send") || !strcmp(name, "__send__") || !strcmp(name, "public_send"))) return 0;
    lupe_gap(sp_sprintf("block for the builtin %s#%s", sp_poly_class_name(recv), name));
  }
  if (lupe_core(recv, name, argc, argv, out)) return 1;
  if (lupe_poly_helper(recv, name, argc, argv, out)) return 1;
  lupe_bop_fn fn = lupe_bop_find(recv, name, argc, argv);
  if (fn) { *out = fn(recv, argv); return 1; }
  return lupe_object_core(recv, name, argc, argv, out);
}

/* The same, with the block of the call. */
static sp_RbVal lupe_send_b(sp_RbVal recv, const char *name, int argc, sp_RbVal *argv, const LupeBlock *blk) {
  sp_RbVal out;
  if (lupe_is_lupe_object(recv)) {
    LupeMethod *m = lupe_find_method(lupe_class_of(recv), sp_sym_intern(name));
    if (m) return lupe_invoke_b(m, recv, argc, argv, blk);
  } else if (recv.tag == SP_TAG_CLASS) {
    LupeClass *k = lupe_class_value(recv);
    LupeMethod *m = k ? lupe_find_smethod(k, sp_sym_intern(name)) : NULL;
    if (m) return lupe_invoke_b(m, recv, argc, argv, blk);
    if (lupe_class_core(NULL, recv, name, argc, argv, blk, &out)) return out;
  } else if (lupe_reopened_builtins || lupe_is_exc(recv)) {
    /* a reopened builtin class, or an exception of a class of the program */
    LupeMethod *m = lupe_find_method(lupe_class_of(recv), sp_sym_intern(name));
    if (m && m->owner != L.object) return lupe_invoke_b(m, recv, argc, argv, blk);
  }
  if (lupe_builtin_call(recv, name, argc, argv, blk, &out)) return out;
  if (blk && argc >= 1 && (!strcmp(name, "send") || !strcmp(name, "__send__") || !strcmp(name, "public_send")))
    return lupe_send_b(recv, lupe_name_arg(argv[0]), argc - 1, argv + 1, blk);
  const char *cls = sp_poly_class_name(recv);
  lupe_trace_missing(cls, name, argc);
  sp_raise_cls("NoMethodError", sp_sprintf("undefined method '%s' for an instance of %s", name, cls));
}

/* p v to the object that $stdout holds: its write takes the inspect and a
   newline, or each of them when it is a write of the program, as CRuby
   calls a write of one argument. */
static void lupe_p_to(sp_RbVal so, sp_RbVal v) {
  sp_RbVal line[2] = { sp_box_nil(), sp_box_nil() };
  SP_GC_ROOT_RBVAL(line[0]);
  SP_GC_ROOT_RBVAL(line[1]);
  line[0] = sp_box_str_new(sp_str_dup(sp_poly_inspect(v)));
  line[1] = lupe_new_string((const uint8_t *)"\n", 1);
  if (lupe_is_lupe_object(so)) {
    lupe_send(so, "write", 1, &line[0]);
    lupe_send(so, "write", 1, &line[1]);
  } else {
    lupe_send(so, "write", 2, line);
  }
}

/* Kernel methods. Answers 1 and sets *out when it handles the call. */
static int lupe_kernel(const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  /* Integer(v), Integer(s, base) and exception: false */
  if (!strcmp(name, "Integer") && argc >= 1 && argc <= 3) {
    int raise = 1;
    if (argv[argc - 1].tag == SP_TAG_OBJ && argv[argc - 1].v.p == L.kwhash && sp_poly_is_hash_kind(argv[argc - 1].cls_id)) {
      sp_RbVal key = sp_box_sym(sp_sym_intern("exception"));
      sp_RbVal ex = lupe_send(argv[argc - 1], "[]", 1, &key);
      raise = ex.tag != SP_TAG_BOOL || ex.v.b;
      argc--;
    }
    sp_int base = argc == 2 && argv[1].tag == SP_TAG_INT ? argv[1].v.i : 0;
    *out = sp_kernel_Integer_val(argv[0], base, raise);
    return 1;
  }
  if (!strcmp(name, "Float") && argc == 1) {
    *out = sp_box_float(sp_poly_Float(argv[0]));
    return 1;
  }
  /* after `$stdout = obj`, puts, print and p write to obj */
  sp_RbVal so;
  if ((!strcmp(name, "puts") || !strcmp(name, "print")) && lupe_gvar_stdout(&so)) {
    lupe_send(so, name, argc, argv);
    *out = sp_box_nil();
    return 1;
  }
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
    if (lupe_gvar_stdout(&so)) {
      SP_GC_ROOT_RBVAL(so);
      for (int i = 0; i < argc; i++) lupe_p_to(so, argv[i]);
    } else {
      for (int i = 0; i < argc; i++) sp_puts_str_line(sp_poly_inspect(argv[i]));
    }
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
