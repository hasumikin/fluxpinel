/* builtin.c -- the builtin methods of Fluxpinel.

   A call on a receiver tries, in order:

   1. the core methods below (operators, Array access, conversions),
      written by hand because spinel emits them from codegen code, not
      from bop_rows[];
   2. the sp_poly_* helpers that answer a method as a whole;
   3. the wrappers that tools/bop_gen.c generates from bop_rows[];
   4. NoMethodError.

   A call with no receiver that names no `def` goes to fx_kernel. */

#include "fluxpinel.h"

/* The family, variant and TyKind of a value, in the spelling of the
   generated table. Answers 0 for a value outside the table. */
static int fx_kind_of(sp_RbVal v, const char **family, const char **variant) {
  const char *fam = NULL, *var = NULL;
  int kind = FX_TY_POLY;
  switch (v.tag) {
  case SP_TAG_INT: fam = var = "int"; kind = FX_TY_INT; break;
  case SP_TAG_FLT: fam = var = "float"; kind = FX_TY_FLOAT; break;
  case SP_TAG_STR: fam = var = "string"; kind = FX_TY_STRING; break;
  case SP_TAG_SYM: fam = var = "symbol"; kind = FX_TY_SYMBOL; break;
  case SP_TAG_BOOL: kind = FX_TY_BOOL; break;
  case SP_TAG_NIL: kind = FX_TY_NIL; break;
  case SP_TAG_OBJ:
    switch (v.cls_id) {
    case SP_BUILTIN_STRBUF: fam = var = "string"; kind = FX_TY_STRING; break;
    case SP_BUILTIN_POLY_ARRAY: fam = "ANY_ARRAY"; var = "poly_array"; kind = FX_TY_POLY_ARRAY; break;
    case SP_BUILTIN_INT_ARRAY: fam = "ANY_ARRAY"; var = "int_array"; kind = FX_TY_INT_ARRAY; break;
    case SP_BUILTIN_FLT_ARRAY: fam = "ANY_ARRAY"; var = "float_array"; kind = FX_TY_FLOAT_ARRAY; break;
    case SP_BUILTIN_STR_ARRAY: fam = "ANY_ARRAY"; var = "str_array"; kind = FX_TY_STR_ARRAY; break;
    case SP_BUILTIN_RANGE: fam = var = "range"; kind = FX_TY_RANGE; break;
    case SP_BUILTIN_EXCEPTION: fam = var = "exception"; break;
    case SP_BUILTIN_REGEX: fam = var = "regex"; kind = FX_TY_REGEX; break;
    case SP_BUILTIN_MATCHDATA: fam = var = "matchdata"; kind = FX_TY_MATCHDATA; break;
    case SP_BUILTIN_IO: fam = var = "io"; kind = FX_TY_IO; break;
    case SP_BUILTIN_FIBER: fam = var = "fiber"; kind = FX_TY_FIBER; break;
    case SP_BUILTIN_THREAD: fam = var = "thread"; kind = FX_TY_THREAD; break;
    case SP_BUILTIN_ENUMERATOR: fam = var = "enumerator"; kind = FX_TY_ENUMERATOR; break;
    case SP_BUILTIN_QUEUE: fam = var = "queue"; kind = FX_TY_QUEUE; break;
    case SP_BUILTIN_MUTEX: fam = var = "mutex"; kind = FX_TY_MUTEX; break;
    case SP_BUILTIN_CONDVAR: fam = var = "condvar"; kind = FX_TY_CONDVAR; break;
    case SP_BUILTIN_TMS: fam = var = "tms"; break;
    case SP_BUILTIN_STR_INT_HASH: fam = "ANY_HASH"; var = "str_int_hash"; kind = FX_TY_STR_INT_HASH; break;
    case SP_BUILTIN_STR_STR_HASH: fam = "ANY_HASH"; var = "str_str_hash"; kind = FX_TY_STR_STR_HASH; break;
    case SP_BUILTIN_INT_INT_HASH: fam = "ANY_HASH"; var = "int_int_hash"; kind = FX_TY_INT_INT_HASH; break;
    case SP_BUILTIN_INT_STR_HASH: fam = "ANY_HASH"; var = "int_str_hash"; kind = FX_TY_INT_STR_HASH; break;
    case SP_BUILTIN_SYM_POLY_HASH: fam = "ANY_HASH"; var = "sym_poly_hash"; kind = FX_TY_SYM_POLY_HASH; break;
    case SP_BUILTIN_STR_POLY_HASH: fam = "ANY_HASH"; var = "str_poly_hash"; kind = FX_TY_STR_POLY_HASH; break;
    case SP_BUILTIN_POLY_POLY_HASH:
      fam = "ANY_HASH";
      var = "poly_poly_hash";
      kind = FX_TY_POLY_POLY_HASH;
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

static int fx_kind_fits(unsigned long long mask, sp_RbVal v) {
  if (!mask) return 1;
  int k = fx_kind_of(v, NULL, NULL);
  return (mask >> k & 1) || (mask >> FX_TY_POLY & 1);
}

/* The rows of the generated table with a wrapper, sorted by name. A
   lookup reads only the rows of one name. */
static const FxBop **fx_bop_index;
static int fx_bop_nindex;

/* By name, then in the order of the table: the first row that fits wins. */
static int fx_bop_cmp(const void *a, const void *b) {
  const FxBop *x = *(const FxBop *const *)a, *y = *(const FxBop *const *)b;
  int c = strcmp(x->name, y->name);
  return c ? c : (x > y) - (x < y);
}

/* Builds the index at the start, before a Thread can run a lookup. */
static void fx_bop_init(void) {
  fx_bop_index = malloc(sizeof *fx_bop_index * (fx_bop_count + 1));
  for (int i = 0; i < fx_bop_count; i++)
    if (fx_bop_table[i].fn) fx_bop_index[fx_bop_nindex++] = &fx_bop_table[i];
  qsort(fx_bop_index, fx_bop_nindex, sizeof *fx_bop_index, fx_bop_cmp);
}

/* The first row of the index with this name; *n is the count of them. */
static const FxBop **fx_bop_rows(const char *name, int *n) {
  int lo = 0, hi = fx_bop_nindex;
  while (lo < hi) {
    int mid = (lo + hi) / 2;
    if (strcmp(fx_bop_index[mid]->name, name) < 0) lo = mid + 1; else hi = mid;
  }
  int end = lo;
  while (end < fx_bop_nindex && !strcmp(fx_bop_index[end]->name, name)) end++;
  *n = end - lo;
  return fx_bop_index + lo;
}

/* The generated wrapper for this call, or NULL. */
static fx_bop_fn fx_bop_find(sp_RbVal recv, const char *name, int argc, sp_RbVal *argv) {
  const char *fam, *var;
  fx_kind_of(recv, &fam, &var);
  if (!fam) return NULL;
  int n;
  const FxBop **rows = fx_bop_rows(name, &n);
  for (int i = 0; i < n; i++) {
    const FxBop *b = rows[i];
    if (strcmp(b->recv, fam) || strcmp(b->variant, var)) continue;
    if (argc < b->argc_min || argc > b->argc_max) continue;
    if (argc > 0 && !fx_kind_fits(b->arg0, argv[0])) continue;
    if (argc > 1 && !fx_kind_fits(b->arg1, argv[1])) continue;
    return b->fn;
  }
  return NULL;
}

static int fx_is_array(sp_RbVal v) {
  return v.tag == SP_TAG_OBJ && sp_poly_is_array_kind(v.cls_id);
}

/* An Integer has two forms: a machine word and a Bignum. */
static int fx_is_integer(sp_RbVal v) {
  return v.tag == SP_TAG_INT || v.tag == SP_TAG_BIGINT;
}

/* A String has two forms: a literal (SP_TAG_STR) and a mutable handle. */
static int fx_is_string(sp_RbVal v) {
  return v.tag == SP_TAG_STR || (v.tag == SP_TAG_OBJ && v.cls_id == SP_BUILTIN_STRBUF);
}

/* Methods that a runtime helper on boxed values answers as a whole. The
   helper checks the receiver kind and raises as the AOT code does. */
static const struct {
  const char *name;
  sp_RbVal (*fn1)(sp_RbVal);
  sp_RbVal (*fn2)(sp_RbVal, sp_RbVal);
} fx_poly_helpers[] = {
  { "floor", sp_poly_floor, NULL },
  { "ceil", sp_poly_ceil, NULL },
  { "sum", sp_poly_sum, NULL },
  { "gcd", NULL, sp_poly_int_gcd },
};

static int fx_poly_helper(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  for (size_t i = 0; i < sizeof fx_poly_helpers / sizeof fx_poly_helpers[0]; i++) {
    if (strcmp(fx_poly_helpers[i].name, name)) continue;
    if (argc == 0 && fx_poly_helpers[i].fn1) { *out = fx_poly_helpers[i].fn1(r); return 1; }
    if (argc == 1 && fx_poly_helpers[i].fn2) { *out = fx_poly_helpers[i].fn2(r, argv[0]); return 1; }
  }
  return 0;
}

/* The core methods. Answers 1 and sets *out when it handles the call. */
static int fx_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (argc == 0) {
    if (!strcmp(name, "!")) { *out = sp_box_bool(!sp_poly_truthy(r)); return 1; }
    if (!strcmp(name, "nil?")) { *out = sp_box_bool(r.tag == SP_TAG_NIL); return 1; }
    /* String#to_s answers the String itself, so a change to it shows */
    if (!strcmp(name, "to_s")) { *out = fx_is_string(r) ? r : sp_poly_to_s_box(r); return 1; }
    if (!strcmp(name, "inspect")) { *out = sp_box_str_new(sp_str_dup(sp_poly_inspect(r))); return 1; }
    /* the value that a Hash key hashes by; it differs from CRuby's numbers */
    if (!strcmp(name, "hash")) { *out = sp_box_int(sp_rbval_hash_key(r)); return 1; }
    if (!strcmp(name, "-@") && !fx_is_string(r)) { *out = sp_poly_neg(r); return 1; }
    /* +str: the String itself when it can change, else a mutable copy */
    if (!strcmp(name, "+@") && fx_is_string(r)) {
      if (r.tag == SP_TAG_OBJ && !sp_gc_is_frozen(r.v.p)) { *out = r; return 1; }
      const char *s = sp_poly_recv_s(r, name);
      *out = fx_new_string((const uint8_t *)s, strlen(s));
      return 1;
    }
    if (!strcmp(name, "+@") && (fx_is_integer(r) || r.tag == SP_TAG_FLT)) { *out = r; return 1; }
    if ((!strcmp(name, "length") || !strcmp(name, "size")) && (fx_is_array(r) || fx_is_string(r))) {
      *out = sp_box_int(sp_poly_length_m(r));
      return 1;
    }
    if (fx_is_array(r) && !strcmp(name, "first")) { *out = sp_poly_first(r); return 1; }
    if (fx_is_array(r) && !strcmp(name, "last")) { *out = sp_poly_last(r); return 1; }
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
    if (fx_is_integer(r)) {
      if (!strcmp(name, "&")) { *out = sp_poly_band(r, a); return 1; }
      if (!strcmp(name, "|")) { *out = sp_poly_bor(r, a); return 1; }
      if (!strcmp(name, "^")) { *out = sp_poly_bxor(r, a); return 1; }
      if (!strcmp(name, "<<")) { *out = sp_poly_shl(r, a); return 1; }
      if (!strcmp(name, ">>")) { *out = sp_poly_shr(r, a); return 1; }
    }
    /* str << other on a mutable String: the handle grows in place */
    if (r.tag == SP_TAG_OBJ && r.cls_id == SP_BUILTIN_STRBUF && fx_is_string(a) &&
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
    if (!strcmp(name, "eql?")) { *out = sp_box_bool(sp_poly_eql(r, a)); return 1; }
    if (!strcmp(name, "<") || !strcmp(name, "<=") || !strcmp(name, ">") || !strcmp(name, ">=")) {
      *out = sp_poly_relop_v(name, r, a);
      return 1;
    }
    if (fx_is_array(r) && !strcmp(name, "[]") && a.tag == SP_TAG_INT) {
      *out = sp_poly_arr_get(r, a.v.i);
      return 1;
    }
    if (fx_is_array(r) && !strcmp(name, "include?")) {
      *out = sp_box_bool(sp_PolyArray_include(sp_poly_to_poly_array(r), a));
      return 1;
    }
    if (fx_is_string(r) && fx_is_string(a) &&
        (!strcmp(name, "start_with?") || !strcmp(name, "end_with?"))) {
      const char *s = sp_poly_recv_s(r, name), *x = sp_poly_recv_s(a, name);
      *out = sp_box_bool(name[0] == 's' ? sp_str_start_with(s, x) : sp_str_end_with(s, x));
      return 1;
    }
    /* the String-separator form, as codegen emits it (codegen_call_recv.c) */
    if (fx_is_string(r) && fx_is_string(a) && !strcmp(name, "split")) {
      sp_StrArray *parts = sp_str_split_drop_trailing(sp_poly_recv_s(r, name), sp_poly_recv_s(a, name));
      *out = sp_box_nullable_obj((void *)parts, SP_BUILTIN_STR_ARRAY);
      return 1;
    }
    return 0;
  }
  if (argc == 2 && fx_is_array(r) && !strcmp(name, "[]=") && argv[0].tag == SP_TAG_INT) {
    sp_poly_arr_set(r, argv[0].v.i, argv[1]);
    *out = argv[1];
    return 1;
  }
  return 0;
}

/* Whether a builtin value answers a method, for respond_to?: a core
   method, a helper or a row of the generated table. It knows only the
   methods that Fluxpinel can call, so it answers false for a method of CRuby
   that Fluxpinel lacks. */
static int fx_builtin_responds(sp_RbVal r, const char *name) {
  static const char *const any[] = { "!", "nil?", "to_s", "inspect", "==", "!=", "class", "is_a?", "kind_of?",
                                     "instance_of?", "respond_to?", "send", "__send__", "public_send", "frozen?",
                                     "freeze", "itself", NULL };
  static const char *const num[] = { "+", "-", "*", "/", "%", "**", "<", "<=", ">", ">=", "-@", NULL };
  static const char *const bits[] = { "&", "|", "^", "<<", ">>", NULL };
  /* and the methods of string.c */
  static const char *const str[] = { "+", "*", "%", "length", "size", "start_with?", "end_with?", "split", "dup",
                                     "clone", "-@", "dedup", "freeze", "frozen?", "to_i", "lines", "clear",
                                     "each_char", "each_byte", "each_line", "each_codepoint", "scrub!", "[]",
                                     "slice", "getbyte", "unpack", "unpack1", "index", "rindex", "byteindex",
                                     "byterindex", "partition", "rpartition", "chomp", "<<", "replace",
                                     "force_encoding", "encode", "between?", "clamp", "center", "ljust", "rjust",
                                     "setbyte", "insert", "[]=", "bytesplice", "slice!", "count", "delete",
                                     "squeeze", "append_as_bytes", "concat", "prepend", "upcase!", "downcase!",
                                     "capitalize!", "swapcase!", "strip!", "lstrip!", "rstrip!", "chomp!", "chop!",
                                     "squeeze!", "tr!", "tr_s!", "delete!", "delete_prefix!", "delete_suffix!",
                                     "succ!", "next!", "reverse!", NULL };
  /* and the methods of array.c */
  static const char *const ary[] = { "length", "size", "first", "last", "include?", "[]", "[]=", "<<", "push", "+",
                                     "empty?", "to_ary", "entries", "pop", "shift", "uniq", "flatten", "minmax",
                                     "transpose", "tally", "sort!", "compact!", "flatten!", "slice", "at",
                                     "member?", "index", "find_index", "rindex", "sample", "replace", "intersect?",
                                     "|", "&", "assoc", "rassoc", "delete", "fetch", "fill", "append", "unshift",
                                     "prepend", "insert", "concat", "union", "intersection", "difference", "dig",
                                     "values_at", "zip", "product", NULL };
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
  if (fx_is_integer(r) || r.tag == SP_TAG_FLT) lists[nl++] = num;
  if (fx_is_integer(r)) lists[nl++] = bits;
  if (fx_is_string(r)) lists[nl++] = str;
  if (fx_is_array(r)) lists[nl++] = ary;
  if (fx_is_array(r) || fx_is_range(r) || fx_is_hash(r)) lists[nl++] = enm;
  if (fx_is_hash(r)) lists[nl++] = hsh;
  for (int l = 0; l < nl; l++)
    for (int i = 0; lists[l][i]; i++)
      if (!strcmp(lists[l][i], name)) return 1;
  for (size_t i = 0; i < sizeof fx_poly_helpers / sizeof fx_poly_helpers[0]; i++)
    if (!strcmp(fx_poly_helpers[i].name, name)) return 1;
  const char *fam, *var;
  fx_kind_of(r, &fam, &var);
  if (!fam) return 0;
  int n;
  const FxBop **rows = fx_bop_rows(name, &n);
  for (int i = 0; i < n; i++)
    if (!strcmp(rows[i]->recv, fam) && !strcmp(rows[i]->variant, var)) return 1;
  return 0;
}

/* FX_TRACE_MISSING=1 reports each method that Fluxpinel cannot find, before
   it raises NoMethodError. The oracle (tools/oracle.rb) reads the report
   to tell a gap of Fluxpinel from a NoMethodError that the program expects.
   The owner ends with its separator: "Array#" for an instance method,
   "Array." for a method of the class Array. */
static void fx_trace_missing(const char *owner, const char *name, int argc) {
  static int on = -1;
  if (on < 0) { const char *e = getenv("FX_TRACE_MISSING"); on = e && *e && *e != '0'; }
  if (on) fprintf(stderr, "fluxpinel: missing builtin %s%s/%d\n", owner, name, argc);
}

/* A call on a receiver. A method that the program defines comes first: on
   a Fluxpinel object, on a class (a class method), or on a reopened builtin
   class. A top-level def is a private method of Object, so an explicit
   receiver does not reach it. */
static sp_RbVal fx_send(sp_RbVal recv, const char *name, int argc, sp_RbVal *argv) {
  return fx_send_b(recv, name, argc, argv, NULL);
}

/* The builtin methods of a value, without the methods of the program:
   for a call, and for super from a method of the program. A builtin
   method that takes a block is one of fx_iter; Fluxpinel stops at any other
   one, since the generated wrappers would drop the block. */
static int fx_builtin_call(sp_RbVal recv, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                             sp_RbVal *out) {
  if (fx_env_core(recv, name, argc, argv, blk, out)) return 1;
  if (fx_record_core(recv, name, argc, argv, blk, out)) return 1;
  if (fx_is_proc(recv) && fx_proc_core(recv, name, argc, argv, out)) return 1;
  if (!blk && fx_range_core(recv, name, argc, argv, out)) return 1;
  if (fx_hash_core(recv, name, argc, argv, blk, out)) return 1;
  if (!blk && fx_array_hash(recv, name, argc, out)) return 1;
  /* Array#to_a: the Array itself (spinel types it away, with no wrapper) */
  if (!blk && argc == 0 && fx_is_array(recv) && !strcmp(name, "to_a")) { *out = recv; return 1; }
  /* Array#join: to_s of each element, and a nested Array joined in turn */
  if (!blk && argc <= 1 && fx_is_array(recv) && !strcmp(name, "join")) {
    const char *sep = argc && argv[0].tag != SP_TAG_NIL ? sp_poly_arg_str_chk(argv[0]) : "";
    SP_GC_ROOT_STR(sep);
    *out = sp_box_str_new(sp_poly_join(recv, sep));
    return 1;
  }
  if (fx_array_core(recv, name, argc, argv, blk, out)) return 1;
  if (fx_exc_core(recv, name, argc, argv, out)) return 1;
  if (fx_re_call(recv, name, argc, argv, blk, out)) return 1;
  if (fx_string_core(recv, name, argc, argv, blk, out)) return 1;
  if (fx_fiber_core(recv, name, argc, argv, blk, out)) return 1;
  if (!blk && fx_io_core(recv, name, argc, argv, out)) return 1;
  if (fx_iter(recv, name, argc, argv, blk, out)) return 1;
  if (blk) {
    if (argc >= 1 && (!strcmp(name, "send") || !strcmp(name, "__send__") || !strcmp(name, "public_send"))) return 0;
    /* a method of no builtin, with a hook that takes it */
    if (!fx_object_method_name(name) && fx_find_hook(recv, "method_missing")) return 0;
    fx_gap(sp_sprintf("block for the builtin %s#%s", sp_poly_class_name(recv), name));
  }
  if (fx_core(recv, name, argc, argv, out)) return 1;
  if (fx_poly_helper(recv, name, argc, argv, out)) return 1;
  fx_bop_fn fn = fx_bop_find(recv, name, argc, argv);
  if (fn) { *out = fn(recv, argv); return 1; }
  return fx_object_core(recv, name, argc, argv, out);
}

/* The same, with the block of the call. */
static sp_RbVal fx_send_b(sp_RbVal recv, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk) {
  return fx_send_v(recv, name, argc, argv, blk, 0, NULL);
}

/* Whether a builtin method of the receiver r has the name in CRuby, by
   the modules ahead of Object: Kernel, Comparable and Enumerable. A miss
   of such a name is a gap of Fluxpinel, which a top-level def must not
   take. */
static int fx_builtin_module_name(sp_RbVal r, const char *name) {
  static const char *const comparable[] = { "between?", "clamp", "<", "<=", ">", ">=", "==", NULL };
  static const char *const enumerable[] = {
    "all?", "any?", "chain", "chunk", "chunk_while", "collect", "collect_concat", "compact", "count", "cycle",
    "detect", "drop", "drop_while", "each_cons", "each_entry", "each_slice", "each_with_index",
    "each_with_object", "entries", "filter", "filter_map", "find", "find_all", "find_index", "first", "flat_map",
    "grep", "grep_v", "group_by", "include?", "inject", "lazy", "map", "max", "max_by", "member?", "min",
    "min_by", "minmax", "minmax_by", "none?", "one?", "partition", "reduce", "reject", "reverse_each", "select",
    "slice_after", "slice_before", "slice_when", "sort", "sort_by", "sum", "take", "take_while", "tally", "to_a",
    "to_h", "to_set", "uniq", "zip", NULL
  };
  if (fx_object_method_name(name)) return 1;
  const char *const *lists[2] = { comparable, enumerable };
  const char *mods[2] = { "Comparable", "Enumerable" };
  for (int l = 0; l < 2; l++)
    for (int i = 0; lists[l][i]; i++)
      if (!strcmp(name, lists[l][i])) return fx_is_a(r, fx_builtin(mods[l]));
  return 0;
}

/* Why a call with check (fx_send_v) cannot reach m, or 0 when it can. */
static int fx_vis_denied(const FxMethod *m, int check, const sp_RbVal *caller) {
  if (!check || m->vis == FX_VIS_PUBLIC) return 0;
  if (m->vis == FX_VIS_PRIVATE) return FX_MISS_PRIVATE;
  return check == 1 && caller && fx_is_a(*caller, m->owner) ? 0 : FX_MISS_PROTECTED;
}

/* A call that cannot reach the method for the reason why goes to the
   method_missing hook, as in CRuby, else raises. */
static sp_RbVal fx_denied(sp_RbVal recv, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk, int why) {
  sp_RbVal out;
  if (fx_method_missing(recv, name, argc, argv, blk, why, &out)) return out;
  fx_no_method(recv, name, why);
}

/* The same, with the visibility of the method. check is 0 for a call
   that reaches any method (send, a call from the runtime), 1 for a call
   with a receiver, from *caller, and 2 for public_send. */
static sp_RbVal fx_send_v(sp_RbVal recv, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                            int check, const sp_RbVal *caller) {
  sp_RbVal out;
  FxMethod *m = NULL;
  if (fx_is_fx_object(recv)) {
    m = fx_find_method(fx_class_of(recv), sp_sym_intern(name));
  } else if (recv.tag == SP_TAG_CLASS) {
    FxClass *k = fx_class_value(recv);
    m = k ? fx_find_smethod(k, sp_sym_intern(name)) : NULL;
    if (!m && fx_class_core(NULL, recv, name, argc, argv, blk, &out)) return out;
  } else if (fx_reopened_builtins || fx_is_exc(recv)) {
    /* a reopened builtin class, or an exception of a class of the program */
    m = fx_find_method(fx_class_of(recv), sp_sym_intern(name));
    if (m && m->owner == L.object) m = NULL;
  }
  if (m) {
    int why = fx_vis_denied(m, check, caller);
    if (why) return fx_denied(recv, name, argc, argv, blk, why);
    return fx_invoke_b(m, recv, argc, argv, blk);
  }
  if (fx_builtin_call(recv, name, argc, argv, blk, &out)) return out;
  if (blk && argc >= 1 && (!strcmp(name, "send") || !strcmp(name, "__send__") || !strcmp(name, "public_send")))
    return fx_send_v(recv, fx_name_arg(argv[0]), argc - 1, argv + 1, blk, name[0] == 'p' ? 2 : 0, NULL);
  /* a method of Object (a top-level def) after the builtin methods of
     the receiver, which come first in its ancestors; a builtin class can
     have a class method of the name that Fluxpinel lacks (Time.at) */
  FxClass *rc = recv.tag == SP_TAG_CLASS ? fx_class_value(recv) : NULL;
  if (!fx_is_fx_object(recv) && !(rc && rc->cls_id < FX_CLS_BASE) && !fx_builtin_module_name(recv, name) &&
      (m = fx_table_get(&L.object->methods, sp_sym_intern(name)))) {
    int why = fx_vis_denied(m, check, caller);
    if (why) return fx_denied(recv, name, argc, argv, blk, why);
    return fx_invoke_b(m, recv, argc, argv, blk);
  }
  /* a private method of Kernel, on a receiver whose class has no public
     builtin of the name; on any other one, the miss is a gap of Fluxpinel
     (File.open, Array#select) */
  int plain = recv.tag == SP_TAG_INT || recv.tag == SP_TAG_FLT || recv.tag == SP_TAG_NIL || recv.tag == SP_TAG_BOOL ||
              (fx_is_fx_object(recv) && !fx_is_native_obj(recv) && !fx_record_of(fx_class_of(recv)));
  if (check && plain && fx_kernel_private(name)) return fx_denied(recv, name, argc, argv, blk, FX_MISS_PRIVATE);
  if (fx_method_missing(recv, name, argc, argv, blk, FX_MISS_METHOD, &out)) return out;
  FxClass *rk = recv.tag == SP_TAG_CLASS ? fx_class_value(recv) : NULL;
  fx_trace_missing(rk ? sp_sprintf("%s.", rk->name) : sp_sprintf("%s#", sp_poly_class_name(recv)), name, argc);
  fx_no_method(recv, name, FX_MISS_METHOD);
}

/* p v to the object that $stdout holds: its write takes the inspect and a
   newline, or each of them when it is a write of the program, as CRuby
   calls a write of one argument. */
static void fx_p_to(sp_RbVal so, sp_RbVal v) {
  sp_RbVal line[2] = { sp_box_nil(), sp_box_nil() };
  SP_GC_ROOT_RBVAL(line[0]);
  SP_GC_ROOT_RBVAL(line[1]);
  line[0] = sp_box_str_new(sp_str_dup(sp_poly_inspect(v)));
  line[1] = fx_new_string((const uint8_t *)"\n", 1);
  if (fx_is_fx_object(so)) {
    fx_send(so, "write", 1, &line[0]);
    fx_send(so, "write", 1, &line[1]);
  } else {
    fx_send(so, "write", 2, line);
  }
}

/* Kernel methods. Answers 1 and sets *out when it handles the call. */
static int fx_kernel(const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  /* Integer(v), Integer(s, base) and exception: false */
  if (!strcmp(name, "Integer") && argc >= 1 && argc <= 3) {
    int raise = 1;
    if (argv[argc - 1].tag == SP_TAG_OBJ && argv[argc - 1].v.p == L.kwhash && sp_poly_is_hash_kind(argv[argc - 1].cls_id)) {
      sp_RbVal key = sp_box_sym(sp_sym_intern("exception"));
      sp_RbVal ex = fx_send(argv[argc - 1], "[]", 1, &key);
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
  if ((!strcmp(name, "puts") || !strcmp(name, "print")) && fx_gvar_stdout(&so)) {
    fx_send(so, name, argc, argv);
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
    if (fx_gvar_stdout(&so)) {
      SP_GC_ROOT_RBVAL(so);
      for (int i = 0; i < argc; i++) fx_p_to(so, argv[i]);
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
