/* builtin.c -- the builtin methods of Fluxpinel.

   A call on a receiver tries, in order (fx_builtin_call):

   1. the methods of one kind of receiver (Hash, Array, String, ...);
   2. the methods that take a block (fx_iter), then the prelude;
   3. the core methods below (operators, conversions), written by hand
      because spinel emits them from codegen code, not from bop_rows[];
   4. the sp_poly_* helpers that answer a method as a whole;
   5. the wrappers that tools/bop_gen.c generates from bop_rows[];
   6. the methods of Object, then NoMethodError.

   A call with no receiver that names no `def` goes to fx_kernel. */

#include "fluxpinel.h"

#define FX_IS(s) (!strcmp(name, (s)))

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
    case SP_BUILTIN_FLOAT_RANGE: fam = var = "float_range"; kind = FX_TY_FLOAT_RANGE; break;
    case SP_BUILTIN_STR_RANGE: fam = var = "str_range"; kind = FX_TY_STR_RANGE; break;
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
    case SP_BUILTIN_TIME: fam = var = "time"; kind = FX_TY_TIME; break;
    case SP_BUILTIN_RATIONAL: fam = var = "rational"; kind = FX_TY_RATIONAL; break;
    case SP_BUILTIN_COMPLEX: fam = var = "complex"; kind = FX_TY_COMPLEX; break;
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
/* round, floor, ceil and truncate of an Integer or a Float with a digit
   count, and round with half:. The arms are the ones that codegen gives a
   typed receiver: sp_float_prec_op for a Float above the decimal point,
   which compensates the error of the scaled product as CRuby does, the
   exact sp_int_round for an Integer, and sp_float_round_half_v and
   sp_int_round_half_v for half:. sp_poly_round_n of a boxed number scales
   in double, so it answers 64.78199 for 64.781995.round(5) and drops the
   low digits of an Integer past 2**53; it serves the other cases
   (sp_poly_prec_n, a Float at or below the decimal point, a Bignum). A
   Float digit count reads as sp_poly_ndigits_f reads it. With no
   argument, a Float or a Rational rounds and truncates as sp_poly_round
   and sp_poly_truncate do (FloatDomainError for NaN and Infinity); to_i
   and to_int truncate. A Rational with a digit count goes to the helpers
   of a boxed number too. */
/* round, floor, ceil or truncate (op, SP_PREC_*) of a Bignum to n digits,
   exactly (sp_bigint_round_prec). It rounds a half away from zero, as
   CRuby does, so a negative Bignum rounds by its magnitude. */
static sp_RbVal fx_bigint_prec(sp_Bigint *b, sp_int n, int op) {
  sp_int_round_check_ndigits(n);
  if (n >= 0) return sp_box_bigint(b);
  int neg = sp_bigint_sign(b) < 0;
  if (op == SP_PREC_TRUNC) op = neg ? SP_PREC_CEIL : SP_PREC_FLOOR;
  if (op == SP_PREC_ROUND && neg) {
    sp_Bigint *a = sp_bigint_round_prec(sp_bigint_abs_v(b), n, SP_PREC_ROUND);
    SP_GC_ROOT(a);
    return sp_box_bigint(sp_bigint_sub(sp_bigint_new_int(0), a));
  }
  return sp_box_bigint(sp_bigint_round_prec(b, n, op));
}

static int fx_num_prec(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (argc == 0) {
    if (!strcmp(name, "round")) *out = sp_poly_round(r);
    else if (!strcmp(name, "truncate") || !strcmp(name, "to_i") || !strcmp(name, "to_int")) *out = sp_poly_truncate(r);
    else return 0;
    return 1;
  }
  int round = !strcmp(name, "round");
  int op = !strcmp(name, "floor") ? SP_PREC_FLOOR : !strcmp(name, "ceil") ? SP_PREC_CEIL
         : !strcmp(name, "truncate") ? SP_PREC_TRUNC : -1;
  if (!round && op < 0) return 0;
  sp_RbVal half = sp_box_nil();
  int has_half = 0;
  /* only round takes a keyword: floor, ceil and truncate read the Hash as
     the digit count, and raise for it below */
  if (round && fx_is_hash(argv[argc - 1]) && argv[argc - 1].v.p == L.kwhash) {
    sp_PolyPolyHash *h = sp_poly_as_poly_poly_hash(argv[argc - 1]);
    sp_RbVal k = sp_box_sym(sp_sym_intern("half"));
    /* a key other than half: ArgumentError, though Integer#round of
       CRuby lets it pass */
    sp_int unknown = h->len - (sp_PolyPolyHash_has_key(h, k) ? 1 : 0);
    if (!fx_is_integer(r) && unknown > 0) {
      const char *s = "";
      SP_GC_ROOT_STR(s);
      for (sp_int i = 0; i < h->len; i++) {
        sp_RbVal key = h->keys[h->order[i]];
        if (!sp_poly_eq(key, k)) s = sp_sprintf("%s%s%s", s, *s ? ", " : "", sp_poly_inspect(key));
      }
      sp_raise_cls("ArgumentError", sp_sprintf("unknown keyword%s: %s", unknown > 1 ? "s" : "", s));
    }
    has_half = sp_PolyPolyHash_has_key(h, k);
    if (has_half) half = sp_PolyPolyHash_get(h, k);
    argc--;
  }
  if (argc > 1) fx_arity_error(argc, 0, 1, 0);
  sp_int n = 0;
  /* a Rational takes an Integer count only (sp_poly_round_digits) */
  if (argc == 1)
    n = argv[0].tag == SP_TAG_FLT && !sp_poly_is_rat_kind(r) ? sp_poly_ndigits_f(r, argv[0].v.f)
                                                             : sp_poly_round_digits(r, argv[0]);
  if (r.tag == SP_TAG_BIGINT && !has_half) {
    *out = fx_bigint_prec((sp_Bigint *)r.v.p, n, round ? SP_PREC_ROUND : op);
  } else if (!round) {
    *out = sp_poly_prec_n(r, n, op);
  } else if (r.tag == SP_TAG_FLT && has_half) {
    *out = sp_float_round_half_v(r.v.f, n, half);
  } else if (r.tag == SP_TAG_FLT && n > 0) {
    /* a nonzero value that rounds to zero is +0.0, as in CRuby */
    double x = r.v.f, v = sp_float_prec_op(x, n, SP_PREC_ROUND);
    *out = sp_box_float(x != 0.0 && v == 0.0 ? 0.0 : v);
  } else if (r.tag == SP_TAG_INT && has_half) {
    sp_int_round_check_ndigits(n);
    *out = sp_box_int(sp_int_round_half_v(r.v.i, n, half));
  } else if (r.tag == SP_TAG_INT) {
    sp_int_round_check_ndigits(n);
    *out = n >= 0 ? r : sp_box_int(sp_int_round(r.v.i, n));
  } else {
    *out = has_half ? sp_poly_round_half(r, n, half) : sp_poly_round_n(r, n);
  }
  return 1;
}

/* The queries of a big Rational (a part past sp_int), which has no
   generated wrapper. The helpers of a boxed number answer them, as codegen
   calls them for a receiver of no single type. */
static int fx_brat_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  static const struct {
    const char *name;
    sp_bool (*p)(sp_RbVal);
    sp_RbVal (*fn)(sp_RbVal);
  } q[] = {
    { "finite?", sp_poly_finite_p, NULL }, { "zero?", sp_poly_zero_p, NULL },
    { "positive?", sp_poly_positive_p, NULL }, { "negative?", sp_poly_negative_p, NULL },
    { "real?", sp_poly_real_p, NULL }, { "integer?", sp_poly_integer_p, NULL },
    { "infinite?", NULL, sp_poly_infinite }, { "nonzero?", NULL, sp_poly_nonzero },
    { "abs", NULL, sp_poly_abs }, { "magnitude", NULL, sp_poly_abs }, { "abs2", NULL, sp_poly_abs2 },
    { "numerator", NULL, sp_poly_numerator }, { "denominator", NULL, sp_poly_denominator },
  };
  (void)argv;
  if (!sp_poly_is_brat(r) || argc) return 0;
  for (size_t i = 0; i < sizeof q / sizeof q[0]; i++)
    if (!strcmp(name, q[i].name)) {
      *out = q[i].p ? sp_box_bool(q[i].p(r)) : q[i].fn(r);
      return 1;
    }
  if (!strcmp(name, "to_f")) { *out = sp_box_float(sp_poly_to_f(r)); return 1; }
  return 0;
}

/* The methods of an Integer that no generated wrapper gives: the rows of
   an Integer that codegen emits inline (lcm, pow, between?, ...), and
   nearly all methods of a Bignum, which has no rows. The helpers of a
   boxed number answer them, as codegen calls them for a receiver of no
   single type (SP_NUM_ARM of spinel's codegen_poly_plan.c). */
/* Integer.sqrt: the integer square root, of a Bignum too (sp_bigint_isqrt).
   The argument converts as to_int does. Answers 1 and sets *out when it
   handles the call. */
static int fx_integer_class(const FxClass *k, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (k->cls_id >= 0 || strcmp(k->name, "Integer") || argc != 1 || !FX_IS("sqrt")) return 0;
  sp_RbVal v = argv[0];
  if (v.tag != SP_TAG_BIGINT) {
    *out = sp_box_int(sp_int_sqrt(sp_poly_arg_int_chk(v)));
    return 1;
  }
  if (sp_bigint_sign((sp_Bigint *)v.v.p) < 0)
    sp_raise_cls("Math::DomainError", "Numerical argument is out of domain - \"isqrt\"");
  *out = sp_box_bigint(sp_bigint_isqrt((sp_Bigint *)v.v.p));
  return 1;
}

/* Comparable of an Integer or a Float: between?, clamp(min, max) and
   clamp(range). A Range side may be open; an exclusive Range with an end
   cannot clamp. */
static int fx_num_cmp(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (!fx_is_integer(r) && r.tag != SP_TAG_FLT) return 0;
  if (argc == 2 && FX_IS("between?")) {
    *out = sp_box_bool(sp_poly_cmp_ck(r, argv[0]) >= 0 && sp_poly_cmp_ck(r, argv[1]) <= 0);
    return 1;
  }
  if (argc == 2 && FX_IS("clamp")) {
    *out = sp_poly_clamp(r, argv[0], argv[1]);
    return 1;
  }
  if (argc != 1 || !FX_IS("clamp")) return 0;
  sp_RbVal g = argv[0];
  if (g.tag != SP_TAG_OBJ || (g.cls_id != SP_BUILTIN_RANGE && g.cls_id != SP_BUILTIN_FLOAT_RANGE &&
                              g.cls_id != SP_BUILTIN_STR_RANGE))
    sp_raise_cls("TypeError", sp_sprintf("wrong argument type %s (expected Range)", sp_poly_class_name(g)));
  SP_GC_ROOT_RBVAL(r);
  SP_GC_ROOT_RBVAL(g);
  sp_RbVal lo = fx_send(g, "begin", 0, NULL);
  SP_GC_ROOT_RBVAL(lo);
  sp_RbVal hi = fx_send(g, "end", 0, NULL);
  SP_GC_ROOT_RBVAL(hi);
  if (hi.tag != SP_TAG_NIL && sp_poly_truthy(fx_send(g, "exclude_end?", 0, NULL)))
    sp_raise_cls("ArgumentError", "cannot clamp with an exclusive range");
  /* a side that is not a number names itself in the error of <=> */
  if (lo.tag != SP_TAG_NIL && !sp_poly_tower_p(lo)) sp_poly_cmp_ck(r, lo);
  if (hi.tag != SP_TAG_NIL && !sp_poly_tower_p(hi)) sp_poly_cmp_ck(r, hi);
  *out = sp_num_clamp_open(r, lo, hi);
  return 1;
}

/* The bits of an Integer from bit start: len of them, or all of them for a
   negative len, as CRuby's Integer#[] reads them, in Bignums. */
static sp_RbVal fx_int_bits(sp_RbVal r, sp_int start, sp_int len) {
  sp_Bigint *x = sp_poly_as_bigint(r);
  SP_GC_ROOT(x);
  sp_Bigint *s = start >= 0 ? sp_bigint_shr(x, start) : sp_bigint_shl(x, -start);
  SP_GC_ROOT(s);
  if (len < 0) return sp_box_bigint(s);
  sp_Bigint *m = sp_bigint_sub(sp_bigint_shl(sp_bigint_new_int(1), len), sp_bigint_new_int(1));
  SP_GC_ROOT(m);
  return sp_box_bigint(sp_bigint_and(s, m));
}

/* Integer#[](start, len) where the word arms of the wrappers do not reach:
   a Bignum receiver, or a start or a length that is not an Integer.
   Answers 1 and sets *out when it handles the call. */
static int fx_int_aref(sp_RbVal r, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (argc != 2 || (r.tag != SP_TAG_BIGINT && argv[0].tag == SP_TAG_INT && argv[1].tag == SP_TAG_INT)) return 0;
  sp_int start = sp_poly_arg_int_chk(argv[0]);
  if (argv[1].tag == SP_TAG_BIGINT) sp_raise_cls("RangeError", "shift width too big");
  *out = fx_int_bits(r, start, sp_poly_arg_int_chk(argv[1]));
  return 1;
}

static int fx_int_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (fx_num_cmp(r, name, argc, argv, out)) return 1;
  if (!fx_is_integer(r)) return 0;
  if (FX_IS("[]") && fx_int_aref(r, argc, argv, out)) return 1;
  sp_Bigint *b = r.tag == SP_TAG_BIGINT ? (sp_Bigint *)r.v.p : NULL;
  if (b && argc <= 1 && FX_IS("chr")) sp_raise_cls("RangeError", "bignum out of char range");
  if (argc == 0) {
    if (FX_IS("numerator")) *out = r;
    else if (FX_IS("denominator")) *out = sp_box_int(1);
    else if (!b) return 0;
    else if (FX_IS("abs") || FX_IS("magnitude")) *out = sp_poly_abs(r);
    else if (FX_IS("abs2")) *out = sp_poly_abs2(r);
    else if (FX_IS("nonzero?")) *out = sp_poly_nonzero(r);
    else if (FX_IS("infinite?")) *out = sp_poly_infinite(r);
    else if (FX_IS("bit_length")) *out = sp_box_int(sp_bigint_bit_length(b));
    else if (FX_IS("size")) *out = sp_box_int(sp_bigint_int_size(b));
    else if (FX_IS("succ") || FX_IS("next")) *out = sp_poly_add(r, sp_box_int(1));
    else if (FX_IS("pred")) *out = sp_poly_sub(r, sp_box_int(1));
    else if (FX_IS("to_f")) *out = sp_box_float(sp_bigint_to_double(b));
    else if (FX_IS("to_i") || FX_IS("to_int") || FX_IS("ord") || FX_IS("round") || FX_IS("floor") ||
             FX_IS("ceil") || FX_IS("truncate"))
      *out = r;
    else if (FX_IS("integer?") || FX_IS("finite?") || FX_IS("real?")) *out = sp_box_bool(1);
    /* a Bignum as a Complex: a real part, an angle of 0 or pi */
    else if (FX_IS("real") || FX_IS("conj") || FX_IS("conjugate")) *out = r;
    else if (FX_IS("imaginary") || FX_IS("imag")) *out = sp_box_int(0);
    else if (FX_IS("arg") || FX_IS("angle") || FX_IS("phase"))
      *out = sp_bigint_sign(b) < 0 ? sp_box_float(M_PI) : sp_box_int(0);
    else if (FX_IS("rectangular") || FX_IS("rect") || FX_IS("polar")) {
      SP_GC_ROOT_RBVAL(r);
      sp_PolyArray *pair = sp_PolyArray_new();
      SP_GC_ROOT(pair);
      int polar = name[0] == 'p';
      sp_PolyArray_push(pair, polar ? sp_poly_abs(r) : r);
      sp_PolyArray_push(pair, polar && sp_bigint_sign(b) < 0 ? sp_box_float(M_PI) : sp_box_int(0));
      *out = sp_box_poly_array(pair);
    }
    else if (FX_IS("zero?")) *out = sp_box_bool(sp_bigint_sign(b) == 0);
    else if (FX_IS("positive?")) *out = sp_box_bool(sp_bigint_sign(b) > 0);
    else if (FX_IS("negative?")) *out = sp_box_bool(sp_bigint_sign(b) < 0);
    else if (FX_IS("even?") || FX_IS("odd?")) *out = sp_box_bool(sp_bigint_even_p(b) == (name[0] == 'e'));
    else if (FX_IS("digits")) *out = sp_box_int_array(sp_poly_int_digits(r, 10));
    else return 0;
    return 1;
  }
  if (argc == 1) {
    sp_RbVal a = argv[0];
    /* gcd, lcm and gcdlcm take an Integer only, and compute in Bignums,
       so lcm does not overflow */
    if (FX_IS("gcd") || FX_IS("lcm") || FX_IS("gcdlcm")) {
      if (!fx_is_integer(a)) sp_raise_cls("TypeError", "not an integer");
      sp_Bigint *x = sp_poly_as_bigint(r);
      SP_GC_ROOT(x);
      sp_Bigint *y = sp_poly_as_bigint(a);
      SP_GC_ROOT(y);
      if (name[0] == 'l') {
        *out = sp_box_bigint(sp_bigint_lcm(x, y));
      } else if (name[3] == 0) {
        *out = sp_box_bigint(sp_bigint_gcd(x, y));
      } else {
        sp_PolyArray *pair = sp_PolyArray_new();
        SP_GC_ROOT(pair);
        sp_PolyArray_push(pair, sp_box_bigint(sp_bigint_gcd(x, y)));
        sp_PolyArray_push(pair, sp_box_bigint(sp_bigint_lcm(x, y)));
        *out = sp_box_poly_array(pair);
      }
      return 1;
    }
    /* ceildiv is -div(0 - other), as in CRuby, which gives its errors */
    if (FX_IS("ceildiv")) {
      *out = sp_poly_sub(sp_box_int(0), sp_poly_div_m(r, sp_poly_sub(sp_box_int(0), a)));
      return 1;
    }
    /* fdiv of a value that is not a real number: the error of a coerce */
    if (FX_IS("fdiv") && !fx_is_integer(a) && a.tag != SP_TAG_FLT && !sp_poly_is_rat_kind(a)) {
      *out = sp_poly_binop_bad("fdiv", r, a);
      return 1;
    }
    if (FX_IS("digits") && a.tag == SP_TAG_NIL) sp_raise_cls("TypeError", "no implicit conversion of nil into Integer");
    /* a Bignum base: the receiver is one digit, unless it is a Bignum */
    if (FX_IS("digits") && a.tag == SP_TAG_BIGINT && !b) {
      if (r.v.i < 0) sp_raise_cls("Math::DomainError", "out of domain");
      if (sp_bigint_sign((sp_Bigint *)a.v.p) > 0) {
        sp_IntArray *d = sp_IntArray_new();
        SP_GC_ROOT(d);
        sp_IntArray_push(d, r.v.i);
        *out = sp_box_int_array(d);
        return 1;
      }
    }
    if (FX_IS("pow")) *out = sp_poly_int_pow(r, a);
    else if (FX_IS("remainder")) *out = sp_poly_remainder(r, a);
    else if (FX_IS("allbits?")) *out = sp_box_bool(sp_poly_int_bits_test(r, a, 0));
    else if (FX_IS("anybits?")) *out = sp_box_bool(sp_poly_int_bits_test(r, a, 1));
    else if (FX_IS("nobits?")) *out = sp_box_bool(sp_poly_int_bits_test(r, a, 2));
    else if (FX_IS("coerce")) *out = sp_poly_coerce(r, a);
    else if (FX_IS("digits")) *out = sp_box_int_array(sp_poly_int_digits(r, sp_poly_arg_int_chk(a)));
    else if (FX_IS("fdiv") && (b || a.tag == SP_TAG_BIGINT)) *out = sp_box_float(sp_poly_fdiv(r, a));
    else if (!b) return 0;
    else if (FX_IS("to_s") || FX_IS("inspect")) {
      sp_int base = sp_poly_arg_int_chk(a);
      if (base < 2 || base > 36) sp_raise_cls("ArgumentError", sp_sprintf("invalid radix %lld", (long long)base));
      *out = sp_box_str(sp_bigint_to_s_base(b, base));
    } else {
      return 0;
    }
    return 1;
  }
  if (argc == 2) {
    /* a Bignum modulus bounds the answer, not the steps: run it in Bignums */
    if (FX_IS("pow") && argv[1].tag == SP_TAG_BIGINT) {
      sp_Bigint *x = sp_poly_as_bigint(r);
      SP_GC_ROOT(x);
      *out = sp_box_bigint(sp_bigint_powmod_arg(x, argv[0], (sp_Bigint *)argv[1].v.p));
      return 1;
    }
    if (!FX_IS("pow")) return 0;
    *out = sp_poly_int_powmod_recv(r, argv[0], argv[1]);
    return 1;
  }
  return 0;
}

/* Float#fdiv and #remainder, which have no generated wrapper: the helpers
   of a boxed number, and the error of a coerce for a value that is not a
   real number. */
static int fx_float_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (r.tag != SP_TAG_FLT || argc != 1 || !(FX_IS("fdiv") || FX_IS("remainder"))) return 0;
  sp_RbVal a = argv[0];
  if (!fx_is_integer(a) && a.tag != SP_TAG_FLT && !sp_poly_is_rat_kind(a)) *out = sp_poly_binop_bad(name, r, a);
  else if (FX_IS("fdiv")) *out = sp_box_float(sp_poly_fdiv(r, a));
  else *out = sp_poly_remainder(r, a);
  return 1;
}

static int fx_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (fx_brat_core(r, name, argc, argv, out)) return 1;
  if (fx_int_core(r, name, argc, argv, out)) return 1;
  if (fx_float_core(r, name, argc, argv, out)) return 1;
  if (((argc >= 1 && fx_is_integer(r)) || r.tag == SP_TAG_FLT || sp_poly_is_rational(r) || sp_poly_is_brat(r)) &&
      fx_num_prec(r, name, argc, argv, out))
    return 1;
  /* quo, div, modulo, divmod, to_r and rationalize of a number (and to_r
     and rationalize of nil), as the helpers of a boxed number answer them */
  if (fx_is_integer(r) || r.tag == SP_TAG_FLT || sp_poly_is_rational(r) || sp_poly_is_brat(r) ||
      (r.tag == SP_TAG_NIL && strcmp(name, "quo"))) {
    if (argc == 1 && !strcmp(name, "quo")) { *out = sp_poly_quo(r, argv[0]); return 1; }
    /* div, modulo and divmod with an operand that no wrapper takes (a
       Rational) */
    if (argc == 1 && r.tag != SP_TAG_NIL) {
      if (!strcmp(name, "div")) { *out = sp_poly_div_m(r, argv[0]); return 1; }
      if (!strcmp(name, "modulo")) { *out = sp_poly_modulo(r, argv[0]); return 1; }
      if (!strcmp(name, "divmod")) { *out = sp_poly_divmod(r, argv[0]); return 1; }
    }
    /* a Bignum is a big Rational over 1, as codegen makes it */
    if (argc <= 1 && r.tag == SP_TAG_BIGINT && (!strcmp(name, "to_r") || !strcmp(name, "rationalize"))) {
      if (argc && !strcmp(name, "to_r")) return 0;
      *out = sp_box_brat((sp_Bigint *)r.v.p, sp_bigint_new_int(1));
      return 1;
    }
    if (argc == 0 && !strcmp(name, "to_r")) { *out = sp_poly_to_r_m(r); return 1; }
    if (argc <= 1 && !strcmp(name, "rationalize")) {
      *out = sp_poly_rationalize_m(r, argc, argc ? argv[0] : sp_box_nil());
      return 1;
    }
  }
  if (argc == 0) {
    if (!strcmp(name, "!")) { *out = sp_box_bool(!sp_poly_truthy(r)); return 1; }
    if (!strcmp(name, "nil?")) { *out = sp_box_bool(r.tag == SP_TAG_NIL); return 1; }
    /* String#to_s answers the String itself, so a change to it shows */
    if (!strcmp(name, "to_s")) { *out = fx_is_string(r) ? r : sp_poly_to_s_box(r); return 1; }
    if (!strcmp(name, "inspect")) { *out = sp_box_str_new(sp_str_dup(sp_poly_inspect(r))); return 1; }
    /* the value that a Hash key hashes by; it differs from CRuby's numbers */
    if (!strcmp(name, "hash")) { *out = sp_box_int(sp_rbval_hash_key(r)); return 1; }
    if (!strcmp(name, "-@") && !fx_is_string(r)) { *out = sp_poly_neg(r); return 1; }
    if (!strcmp(name, "~") && fx_is_integer(r)) {
      *out = r.tag == SP_TAG_INT ? sp_box_int(~r.v.i) : sp_box_bigint(sp_bigint_not((sp_Bigint *)r.v.p));
      return 1;
    }
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
      /* sp_poly_band and its pair cut a Bignum to a machine word */
      if ((r.tag == SP_TAG_BIGINT || a.tag == SP_TAG_BIGINT) && fx_is_integer(a)) {
        int op = !strcmp(name, "&") ? 0 : !strcmp(name, "|") ? 1 : !strcmp(name, "^") ? 2 : -1;
        if (op >= 0) { *out = sp_poly_bitop(r, a, op); return 1; }
      }
      if (!strcmp(name, "&")) { *out = sp_poly_band(r, a); return 1; }
      if (!strcmp(name, "|")) { *out = sp_poly_bor(r, a); return 1; }
      if (!strcmp(name, "^")) { *out = sp_poly_bxor(r, a); return 1; }
      if (!strcmp(name, "<<")) { *out = sp_poly_shl(r, a); return 1; }
      if (!strcmp(name, ">>")) { *out = sp_poly_shr(r, a); return 1; }
      /* n[i] and n[range]: a bit, or a bit field */
      if (!strcmp(name, "[]") && (fx_is_integer(a) || a.tag == SP_TAG_FLT || fx_is_range(a))) {
        *out = sp_poly_index_poly(r, a);
        return 1;
      }
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
  /* n[i, len]: the bit field of len bits from bit i */
  if (argc == 2 && r.tag == SP_TAG_INT && !strcmp(name, "[]") && argv[0].tag == SP_TAG_INT &&
      argv[1].tag == SP_TAG_INT) {
    *out = sp_box_int(sp_int_bit_range(r.v.i, argv[0].v.i, argv[1].v.i));
    return 1;
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
  /* io/console on an IO (global.c), once it is required */
  if (fx_console_loaded && fx_is_io(r) && (!strcmp(name, "winsize") || !strcmp(name, "winsize="))) return 1;
  static const char *const any[] = { "!", "nil?", "to_s", "inspect", "==", "!=", "class", "is_a?", "kind_of?",
                                     "instance_of?", "respond_to?", "send", "__send__", "public_send", "frozen?",
                                     "freeze", "itself", NULL };
  static const char *const num[] = { "+", "-", "*", "/", "%", "**", "<", "<=", ">", ">=", "-@", NULL };
  static const char *const bits[] = { "&", "|", "^", "<<", ">>", "[]", NULL };
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
  if (fx_is_binding(r) && fx_binding_responds(name)) return 1;
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

/* The names that fx_fast_call answers, as Symbols. */
static struct {
  int ready;
  sp_sym add, sub, mul, div, mod, lt, le, gt, ge, eq, ne, eqq, band, bor, bxor, shl, shr, aref, aset, push, size,
    length;
} fx_ops;

static SP_NOINLINE SP_COLD void fx_ops_init(void) {
  fx_ops.add = sp_sym_intern("+");
  fx_ops.sub = sp_sym_intern("-");
  fx_ops.mul = sp_sym_intern("*");
  fx_ops.div = sp_sym_intern("/");
  fx_ops.mod = sp_sym_intern("%");
  fx_ops.lt = sp_sym_intern("<");
  fx_ops.le = sp_sym_intern("<=");
  fx_ops.gt = sp_sym_intern(">");
  fx_ops.ge = sp_sym_intern(">=");
  fx_ops.eq = sp_sym_intern("==");
  fx_ops.ne = sp_sym_intern("!=");
  fx_ops.band = sp_sym_intern("&");
  fx_ops.bor = sp_sym_intern("|");
  fx_ops.bxor = sp_sym_intern("^");
  fx_ops.shl = sp_sym_intern("<<");
  fx_ops.shr = sp_sym_intern(">>");
  fx_ops.aref = sp_sym_intern("[]");
  fx_ops.aset = sp_sym_intern("[]=");
  fx_ops.eqq = sp_sym_intern("===");
  fx_ops.push = sp_sym_intern("<<");
  fx_ops.size = sp_sym_intern("size");
  fx_ops.length = sp_sym_intern("length");
  fx_ops.ready = 1;
}

/* The commonest calls, answered before the name walks the chain of
   fx_builtin_call:
   - the operators of two Integers or two Floats, and Integer#[]
   - == and === of two Symbols
   - Array#[] and #[]= with an Integer index, Array#<<, size and length
   - Hash#[]
   A call passes the name as a Symbol. The answers are the ones of that
   chain. Answers 0 for any other call, and for any call once the program
   reopens a builtin class. */
static int fx_fast_call(sp_RbVal r, sp_sym s, int argc, sp_RbVal *argv, sp_RbVal *out) {
  /* an instance of a subclass of Array or Hash can have its own [], << or size */
  if (fx_reopened_builtins || argc > 2 || fx_bsub_tail(r)) return 0;
  if (SP_UNLIKELY(!fx_ops.ready)) fx_ops_init();
  if (argc == 0) {
    if ((s == fx_ops.size || s == fx_ops.length) && r.tag == SP_TAG_OBJ && sp_poly_is_array_kind(r.cls_id) &&
        r.v.p) {
      *out = sp_box_int(sp_poly_length(r));
      return 1;
    }
    return 0;
  }
  sp_RbVal a = argv[0];
  if (argc == 1 && r.tag == SP_TAG_INT && a.tag == SP_TAG_INT) {
    sp_int x = r.v.i, y = a.v.i;
    if (s == fx_ops.aref) { *out = sp_box_int(sp_int_bit(x, y)); return 1; }
    if (s == fx_ops.eq || s == fx_ops.eqq) { *out = sp_box_bool(x == y); return 1; }
    if (s == fx_ops.add) { *out = sp_poly_add(r, a); return 1; }
    if (s == fx_ops.sub) { *out = sp_poly_sub(r, a); return 1; }
    if (s == fx_ops.band) { *out = sp_box_int(x & y); return 1; }
    if (s == fx_ops.bor) { *out = sp_box_int(x | y); return 1; }
    if (s == fx_ops.bxor) { *out = sp_box_int(x ^ y); return 1; }
    if (s == fx_ops.shl) { *out = sp_poly_shl(r, a); return 1; }
    if (s == fx_ops.shr) { *out = sp_poly_shr(r, a); return 1; }
    if (s == fx_ops.lt) { *out = sp_box_bool(x < y); return 1; }
    if (s == fx_ops.le) { *out = sp_box_bool(x <= y); return 1; }
    if (s == fx_ops.gt) { *out = sp_box_bool(x > y); return 1; }
    if (s == fx_ops.ge) { *out = sp_box_bool(x >= y); return 1; }
    if (s == fx_ops.ne) { *out = sp_box_bool(x != y); return 1; }
    if (s == fx_ops.mul) { *out = sp_poly_mul(r, a); return 1; }
    if (s == fx_ops.div) { *out = sp_poly_div(r, a); return 1; }
    if (s == fx_ops.mod) { *out = sp_poly_mod(r, a); return 1; }
    return 0;
  }
  if (argc == 1 && r.tag == SP_TAG_FLT && a.tag == SP_TAG_FLT) {
    sp_float x = r.v.f, y = a.v.f;
    if (s == fx_ops.add) { *out = sp_box_float(x + y); return 1; }
    if (s == fx_ops.sub) { *out = sp_box_float(x - y); return 1; }
    if (s == fx_ops.mul) { *out = sp_box_float(x * y); return 1; }
    if (s == fx_ops.div) { *out = sp_box_float(x / y); return 1; }
    if (s == fx_ops.lt) { *out = sp_box_bool(x < y); return 1; }
    if (s == fx_ops.le) { *out = sp_box_bool(x <= y); return 1; }
    if (s == fx_ops.gt) { *out = sp_box_bool(x > y); return 1; }
    if (s == fx_ops.ge) { *out = sp_box_bool(x >= y); return 1; }
    return 0;
  }
  if (argc == 1 && r.tag == SP_TAG_SYM && a.tag == SP_TAG_SYM && (s == fx_ops.eq || s == fx_ops.eqq)) {
    *out = sp_box_bool(r.v.i == a.v.i);
    return 1;
  }
  if (r.tag != SP_TAG_OBJ || !r.v.p) return 0;
  if (sp_poly_is_array_kind(r.cls_id)) {
    if (a.tag == SP_TAG_INT && argc == 1 && s == fx_ops.aref) { *out = sp_poly_arr_get_hash(r, a.v.i); return 1; }
    if (a.tag == SP_TAG_INT && argc == 2 && s == fx_ops.aset) {
      sp_poly_arr_set(r, a.v.i, argv[1]);
      *out = argv[1];
      return 1;
    }
    /* a frozen Array takes the chain, which raises */
    if (argc == 1 && s == fx_ops.push && r.cls_id == SP_BUILTIN_POLY_ARRAY && !sp_gc_is_frozen(r.v.p)) {
      sp_PolyArray_push((sp_PolyArray *)r.v.p, a);
      *out = r;
      return 1;
    }
    return 0;
  }
  if (argc == 1 && s == fx_ops.aref && r.cls_id == SP_BUILTIN_POLY_POLY_HASH) {
    *out = sp_PolyPolyHash_get((sp_PolyPolyHash *)r.v.p, a);
    return 1;
  }
  return 0;
}

/* v === s through fx_fast_call, for a when clause. */
static int fx_fast_eqq(sp_RbVal v, sp_RbVal s, sp_RbVal *out) {
  if (SP_UNLIKELY(!fx_ops.ready)) fx_ops_init();
  return fx_fast_call(v, fx_ops.eqq, 1, &s, out);
}

/* The builtin methods of a value, without the methods of the program:
   for a call, and for super from a method of the program. A builtin
   method that takes a block is one of fx_iter; Fluxpinel stops at any other
   one, since the generated wrappers would drop the block. */
static int fx_builtin_call(sp_RbVal recv, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                             sp_RbVal *out) {
  /* IO#printf and StringIO#printf: Kernel#printf to the receiver, which
     needs a format */
  if (!blk && !strcmp(name, "printf") &&
      ((recv.tag == SP_TAG_OBJ && recv.cls_id == SP_BUILTIN_IO) ||
       (fx_is_native_obj(recv) && !strcmp(fx_class_of(recv)->name, "StringIO")))) {
    if (argc == 0) sp_raise_cls("ArgumentError", "too few arguments");
    sp_PolyArray *a = sp_PolyArray_new();
    SP_GC_ROOT(a);
    sp_PolyArray_push(a, recv);
    for (int i = 0; i < argc; i++) sp_PolyArray_push(a, argv[i]);
    return fx_kernel(name, (int)a->len, a->data, out);
  }
  if (fx_singleton_core(recv, name, argc, argv, blk, out)) return 1;
  /* dup and clone of an instance of a subclass of Array or Hash keep its
     class; to_a and to_h are a plain Array or Hash of its elements; merge
     and compact of a Hash answer an instance of the class, as in CRuby */
  if (!blk && fx_hashsub_of(recv) && ((!strcmp(name, "merge") && argc >= 1) || (!strcmp(name, "compact") && argc == 0))) {
    SP_GC_ROOT_RBVAL(recv);
    sp_RbVal plain = fx_hash_box(fx_hash_dup((sp_PolyPolyHash *)recv.v.p));
    SP_GC_ROOT_RBVAL(plain);
    sp_RbVal r = fx_send(plain, name, argc, argv);
    SP_GC_ROOT_RBVAL(r);
    /* the entries of the result, the ivars of the receiver */
    sp_RbVal obj = fx_bsub_like(recv);
    SP_GC_ROOT_RBVAL(obj);
    fx_hashsub_fill(obj, r);
    *out = obj;
    return 1;
  }
  if (!blk && argc == 0 && fx_bsub_tail(recv)) {
    if (!strcmp(name, "dup") || !strcmp(name, "clone")) { *out = fx_bsub_copy(recv, name[0] == 'c'); return 1; }
    if (!strcmp(name, "to_h") && fx_hashsub_of(recv)) {
      *out = fx_hash_box(fx_hash_dup((sp_PolyPolyHash *)recv.v.p));
      return 1;
    }
    if (!strcmp(name, "to_a") && fx_arysub_of(recv)) {
      SP_GC_ROOT_RBVAL(recv);
      sp_PolyArray *a = sp_PolyArray_new();
      SP_GC_ROOT(a);
      sp_int n = sp_poly_length(recv);
      for (sp_int i = 0; i < n; i++) sp_PolyArray_push(a, sp_poly_arr_get(recv, i));
      *out = sp_box_poly_array(a);
      return 1;
    }
  }
  if (fx_eval_block(recv, name, argc, argv, blk, out)) return 1;
  if (fx_enum_core(recv, name, argc, argv, blk, out)) return 1;
  if (fx_env_core(recv, name, argc, argv, blk, out)) return 1;
  if (fx_record_core(recv, name, argc, argv, blk, out)) return 1;
  if (fx_is_proc(recv) && fx_proc_core(recv, name, argc, argv, out)) return 1;
  /* Symbol#to_proc: the lambda that &:sym passes */
  if (recv.tag == SP_TAG_SYM && argc == 0 && !blk && !strcmp(name, "to_proc")) {
    FxBlock b = { .sym = (sp_sym)recv.v.i, .lambda = 1 };
    *out = fx_block_proc(&b);
    return 1;
  }
  if (fx_is_method_value(recv) && fx_method_value_core(recv, name, argc, argv, blk, out)) return 1;
  if (fx_is_binding(recv) && fx_binding_core(recv, name, argc, argv, blk, out)) return 1;
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
  if (!blk && fx_time_core(recv, name, argc, argv, out)) return 1;
  if (!blk && fx_complex_core(recv, name, argc, argv, out)) return 1;
  if (!blk && fx_to_c(recv, name, argc, out)) return 1;
  if (!blk && fx_encoding_core(recv, name, argc, argv, out)) return 1;
  if (fx_iter(recv, name, argc, argv, blk, out)) return 1;
  if (blk) {
    if (argc >= 1 && (!strcmp(name, "send") || !strcmp(name, "__send__") || !strcmp(name, "public_send"))) return 0;
    if (fx_prelude_call(recv, name, argc, argv, blk, out)) return 1;
    /* a method of no builtin, with a hook that takes it */
    if (!fx_object_method_name(name) && fx_find_hook(recv, "method_missing")) return 0;
    /* a method that CRuby does not give the receiver either: the caller
       goes on as for a call with no block (a top-level def, else
       NoMethodError) */
    if (!fx_rs_known(recv, name)) return 0;
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
    m = fx_find_method(fx_lookup_class(recv), fx_name_sym(name));
  } else if (recv.tag == SP_TAG_CLASS) {
    FxClass *k = fx_class_value(recv);
    m = k ? fx_find_smethod(k, fx_name_sym(name)) : NULL;
    if (!m && fx_class_core(NULL, recv, name, argc, argv, blk, &out)) return out;
  } else if (fx_reopened_builtins || fx_is_exc(recv) || fx_bsub_tail(recv) || fx_singleton_of(recv, 0)) {
    /* a reopened builtin class, an exception of a class of the program, an
       instance of a subclass of Array or Hash, or a value with a singleton
       class */
    m = fx_find_method(fx_lookup_class(recv), fx_name_sym(name));
    if (m && (m->owner == L.object || fx_builtin_shadows(recv, m, name))) m = NULL;
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
      (m = fx_table_get(&L.object->methods, fx_name_sym(name)))) {
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
  fx_prelude_gap(name, argc, argv);
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

/* The keyword exception: of Integer(), Rational() and Complex(): 0 for
   false, else 1. A value that is not true or false is the ArgumentError
   of CRuby. */
static int fx_kw_exception(sp_RbVal kw) {
  sp_PolyPolyHash *h = sp_poly_as_poly_poly_hash(kw);
  sp_RbVal k = sp_box_sym(sp_sym_intern("exception"));
  if (!sp_PolyPolyHash_has_key(h, k)) return 1;
  sp_RbVal v = sp_PolyPolyHash_get(h, k);
  if (v.tag != SP_TAG_BOOL)
    sp_raise_cls("ArgumentError", sp_sprintf("expected true or false as exception: %s", sp_poly_inspect(v)));
  return v.v.b;
}

/* An argument of Rational(a, b), as codegen converts it (emit_rat_kernel_arg
   of spinel): a String parses strictly, and any other value converts as
   sp_poly_kernel_rational_arg converts it. */
static sp_Rational fx_rational_arg(sp_RbVal v) {
  if (fx_is_string(v)) return sp_str_to_r_strict(sp_poly_arg_str_chk(v));
  return sp_poly_kernel_rational_arg(v);
}

/* The parts of a big Rational: an Integer past sp_int, or one that fits. */
static sp_Bigint *fx_rational_big(sp_RbVal v) {
  return v.tag == SP_TAG_BIGINT ? (sp_Bigint *)v.v.p : sp_bigint_new_int(v.v.i);
}

/* Kernel#Rational. With raise 0 (exception: false), nil, a String that
   does not parse and a value with no conversion give nil. */
static sp_RbVal fx_kernel_rational(int argc, sp_RbVal *argv, int raise) {
  sp_RbVal a = argv[0], b = argc == 2 ? argv[1] : sp_box_int(1);
  if (!raise) {
    if (a.tag == SP_TAG_NIL || b.tag == SP_TAG_NIL || sp_poly_no_rational(a) || sp_poly_no_rational(b))
      return sp_box_nil();
    sp_convert_soft = 1;
    sp_convert_failed = 0;
    sp_Rational x = fx_rational_arg(a), y = fx_rational_arg(b);
    sp_convert_soft = 0;
    return sp_convert_failed ? sp_box_nil() : sp_box_rational(argc == 2 ? sp_rational_div(x, y) : x);
  }
  /* an Integer past sp_int makes a big Rational, as a literal of one does */
  if (a.tag == SP_TAG_BIGINT || b.tag == SP_TAG_BIGINT) {
    if ((a.tag != SP_TAG_INT && a.tag != SP_TAG_BIGINT) || (b.tag != SP_TAG_INT && b.tag != SP_TAG_BIGINT))
      fx_gap("Rational of a big Integer and a value that is not an Integer");
    if (b.tag == SP_TAG_INT && b.v.i == 0) sp_raise_cls("ZeroDivisionError", "divided by 0");
    sp_Bigint *n = fx_rational_big(a);
    SP_GC_ROOT(n);
    return sp_box_brat(n, fx_rational_big(b));
  }
  if (argc == 1) {
#ifdef SP_INT_OVERFLOW_MODE_PROMOTE
    /* a part past sp_int makes a big Rational */
    if (fx_is_string(a)) return sp_str_to_r_strict_v(sp_poly_arg_str_chk(a));
#else
    if (fx_is_string(a)) return sp_box_rational(sp_str_to_r_strict(sp_poly_arg_str_chk(a)));
#endif
    /* a Rational is the answer itself */
    if (sp_poly_is_rational(a)) return a;
    return sp_box_rational(sp_poly_kernel_rational1(a));
  }
  sp_Rational x = fx_rational_arg(a);
  return sp_box_rational(sp_rational_div(x, fx_rational_arg(b)));
}

/* Kernel methods. Answers 1 and sets *out when it handles the call. */
static int fx_kernel(const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (argc == 0 && !strcmp(name, "global_variables")) {
    *out = fx_global_names();
    return 1;
  }
  /* Kernel#select is IO.select */
  if (!strcmp(name, "select") && argc >= 1 && argc <= 4) {
    *out = fx_io_select(argc, argv);
    return 1;
  }
  /* warn(*msgs): each message on a line, as puts lays them out, in one
     write to $stderr; nothing when $VERBOSE is nil. uplevel: nil and
     category: are taken; an uplevel is a gap, as Fluxpinel keeps no
     locations of the calls */
  if (!strcmp(name, "warn")) {
    if (argc > 0 && fx_is_hash(argv[argc - 1]) && argv[argc - 1].v.p == L.kwhash) {
      sp_PolyPolyHash *h = sp_poly_as_poly_poly_hash(argv[argc - 1]);
      sp_RbVal up = sp_PolyPolyHash_get(h, sp_box_sym(sp_sym_intern("uplevel")));
      if (up.tag != SP_TAG_NIL) fx_gap("warn with uplevel:");
      argc--;
    }
    *out = sp_box_nil();
    if (argc == 0 || fx_gvar_get("$VERBOSE").tag == SP_TAG_NIL) return 1;
    sp_PolyArray *a = sp_PolyArray_new();
    SP_GC_ROOT(a);
    for (int i = 0; i < argc; i++) sp_PolyArray_push(a, argv[i]);
    sp_PolyArray *flat = sp_PolyArray_flatten(a);
    SP_GC_ROOT(flat);
    sp_String *text = sp_String_new("");
    SP_GC_ROOT(text);
    for (sp_int i = 0; i < flat->len; i++) {
      const char *s = sp_poly_to_s(flat->data[i]);
      SP_GC_ROOT_STR(s);
      size_t n = sp_str_byte_len(s);
      sp_String_append_n(text, s, n);
      if (!n || s[n - 1] != '\n') sp_String_append_n(text, "\n", 1);
    }
    sp_RbVal msg = sp_box_str_new(sp_String_cstr(text));
    SP_GC_ROOT_RBVAL(msg);
    fx_send(fx_gvar_get("$stderr"), "write", 1, &msg);
    return 1;
  }
  /* Integer(v), Integer(s, base) and exception: false */
  if (!strcmp(name, "Integer") && argc >= 1 && argc <= 3) {
    int raise = 1;
    if (argv[argc - 1].tag == SP_TAG_OBJ && argv[argc - 1].v.p == L.kwhash && sp_poly_is_hash_kind(argv[argc - 1].cls_id)) {
      raise = fx_kw_exception(argv[argc - 1]);
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
  /* Rational(a), Rational(a, b) and exception: false */
  if (!strcmp(name, "Rational") && argc >= 1 && argc <= 3) {
    int raise = 1;
    if (fx_is_hash(argv[argc - 1]) && argv[argc - 1].v.p == L.kwhash) {
      raise = fx_kw_exception(argv[argc - 1]);
      argc--;
    }
    if (argc < 1 || argc > 2) return 0;
    *out = fx_kernel_rational(argc, argv, raise);
    return 1;
  }
  /* Complex(a), Complex(a, b) and exception: false */
  if (!strcmp(name, "Complex") && argc >= 1 && argc <= 3) {
    int raise = 1;
    if (fx_is_hash(argv[argc - 1]) && argv[argc - 1].v.p == L.kwhash) {
      raise = fx_kw_exception(argv[argc - 1]);
      argc--;
    }
    if (argc < 1 || argc > 2) return 0;
    *out = fx_kernel_complex(argc, argv, raise);
    return 1;
  }
  /* format and sprintf: the format of String#% (sp_str_format_polyarr)
     over the rest of the arguments. printf writes it to $stdout, or to an
     IO that comes first. */
  if (((!strcmp(name, "format") || !strcmp(name, "sprintf")) && argc >= 1) || !strcmp(name, "printf")) {
    if (argc == 0) { *out = sp_box_nil(); return 1; }
    sp_RbVal io = sp_box_nil();
    if (name[0] == 'p' && argv[0].tag != SP_TAG_STR && !fx_is_string(argv[0])) {
      io = argv[0];
      argc--;
      argv++;
      if (argc == 0) { *out = sp_box_nil(); return 1; }
    }
    SP_GC_ROOT_RBVAL(io);
    const char *fmt = sp_poly_arg_str_chk(argv[0]);
    SP_GC_ROOT_STR(fmt);
    sp_PolyArray *rest = sp_PolyArray_new();
    SP_GC_ROOT(rest);
    for (int i = 1; i < argc; i++) sp_PolyArray_push(rest, argv[i]);
    sp_RbVal s = sp_box_str_new(sp_str_format_polyarr(fmt, rest));
    if (name[0] != 'p') {
      *out = s;
      return 1;
    }
    SP_GC_ROOT_RBVAL(s);
    sp_RbVal so;
    if (io.tag != SP_TAG_NIL) fx_send(io, "write", 1, &s);
    else if (fx_gvar_stdout(&so)) fx_send(so, "write", 1, &s);
    else FX_UNLOCKED(sp_poly_print(s));
    *out = sp_box_nil();
    return 1;
  }
  /* after `$stdout = obj`, puts, print and p write to obj */
  sp_RbVal so;
  if ((!strcmp(name, "puts") || !strcmp(name, "print")) && fx_gvar_stdout(&so)) {
    fx_send(so, name, argc, argv);
    *out = sp_box_nil();
    return 1;
  }
  /* a write of stdout parks the thread when stdout is a pipe or a
     terminal: the walker lock is given up for its time */
  if (!strcmp(name, "puts")) {
    if (argc == 0) FX_UNLOCKED(sp_puts_str_line(sp_str_empty));
    for (int i = 0; i < argc; i++) FX_UNLOCKED(sp_poly_puts(argv[i]));
    *out = sp_box_nil();
    return 1;
  }
  if (!strcmp(name, "print")) {
    for (int i = 0; i < argc; i++) FX_UNLOCKED(sp_poly_print(argv[i]));
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
