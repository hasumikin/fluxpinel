/* complex.c -- Kernel#Complex and the class methods of Complex.

   spinel's codegen gives each form a function of the runtime (Complex in
   src/codegen_call.c of spinel, and emit_complex_real_args). Fluxpinel
   calls the functions that it gives to a boxed argument. A Complex is an
   sp_Complex of the runtime: two doubles and a flag that says which part
   is a Float. The generated wrappers give its methods. */

#include "fluxpinel.h"

#define FX_IS(s) (!strcmp(name, (s)))

/* Kernel#Complex(a) and Complex(a, b), as codegen gives a boxed argument:
   a Complex combines as a + b*i, a String parses whole, and a real number
   is a part (sp_poly_complex_arg, sp_complex_convert2). With raise 0
   (exception: false), nil, a String that does not parse and a value with
   no conversion give nil. */
static sp_RbVal fx_kernel_complex(int argc, sp_RbVal *argv, int raise) {
  sp_RbVal a = argv[0], b = argc == 2 ? argv[1] : sp_box_int(0);
  if (!raise) {
    for (int i = 0; i < argc; i++) {
      sp_RbVal v = sp_rbval_str(argv[i]);
      int real = v.tag == SP_TAG_INT || v.tag == SP_TAG_BIGINT || v.tag == SP_TAG_FLT || sp_poly_is_rat_kind(v);
      if (!real && v.tag != SP_TAG_STR && !(v.tag == SP_TAG_OBJ && v.cls_id == SP_BUILTIN_COMPLEX)) return sp_box_nil();
    }
    sp_convert_soft = 1;
    sp_convert_failed = 0;
    int fa = 0, fb = 0;
    sp_Complex x = sp_poly_complex_arg(a, &fa, argc < 2);
    sp_Complex y = argc < 2 ? (sp_Complex){ 0, 0, 0 } : sp_poly_complex_arg(b, &fb, 0);
    sp_convert_soft = 0;
    return sp_convert_failed ? sp_box_nil() : sp_box_complex(sp_complex_convert2(x, fa, y, fb));
  }
  /* true and false name themselves, as emit_complex_bool_reject raises */
  if (a.tag == SP_TAG_BOOL || (argc == 2 && b.tag == SP_TAG_BOOL)) sp_complex_reject_bool(a, b, argc);
  if (argc == 1 && a.tag == SP_TAG_OBJ && a.cls_id == SP_BUILTIN_COMPLEX) return a;
  int fa = 0, fb = 0;
  sp_Complex x = sp_poly_complex_arg(a, &fa, argc < 2);
  sp_Complex y = argc < 2 ? (sp_Complex){ 0, 0, 0 } : sp_poly_complex_arg(b, &fb, 0);
  return sp_box_complex(sp_complex_convert2(x, fa, y, fb));
}

/* An argument of Complex.rect or Complex.polar: a real number, or a
   Complex with an imaginary part of zero (sp_complex_real_chk). The
   answer holds the value in re and the Float flag in SP_CPLX_RE_F. */
static sp_Complex fx_complex_real(sp_RbVal v) {
  if (v.tag == SP_TAG_OBJ && v.cls_id == SP_BUILTIN_COMPLEX && v.v.p) return sp_complex_real_chk(*(sp_Complex *)v.v.p);
  if (v.tag == SP_TAG_INT || v.tag == SP_TAG_BIGINT) return (sp_Complex){ sp_poly_to_f(v), 0, 0 };
  if (v.tag == SP_TAG_FLT || sp_poly_is_rat_kind(v)) return (sp_Complex){ sp_poly_to_f_with_rational(v), 0, SP_CPLX_RE_F };
  sp_raise_cls("TypeError", "not a real");
}

static int fx_is_complex(sp_RbVal v) { return v.tag == SP_TAG_OBJ && v.cls_id == SP_BUILTIN_COMPLEX && v.v.p; }

static int fx_is_real(sp_RbVal v) {
  return v.tag == SP_TAG_INT || v.tag == SP_TAG_BIGINT || v.tag == SP_TAG_FLT || sp_poly_is_rat_kind(v);
}

/* A real number as a Complex, as codegen lifts the receiver of
   `Integer op Complex`: the Float flag of a Float or a Rational. */
static sp_Complex fx_complex_lift(sp_RbVal v) {
  return (sp_Complex){ sp_poly_to_f_with_rational(v), 0, v.tag == SP_TAG_FLT || sp_poly_is_rat_kind(v) ? SP_CPLX_RE_F : 0 };
}

/* Complex#<=>: the order of the real parts when neither has an imaginary
   part, else nil, as in CRuby. */
static sp_RbVal fx_complex_cmp(sp_Complex a, sp_Complex b) {
  if (a.im != 0 || b.im != 0) return sp_box_nil();
  return sp_box_int(a.re < b.re ? -1 : a.re > b.re ? 1 : 0);
}

/* An operator of a real number with a Complex operand: the receiver
   lifted to a Complex, then the operation of two Complexes. Answers 1 and
   sets *out when it handles the call. */
static int fx_real_complex_op(sp_RbVal r, const char *name, sp_RbVal c, sp_RbVal *out) {
  sp_Complex a = fx_complex_lift(r), b = *(sp_Complex *)c.v.p;
  if (FX_IS("+")) *out = sp_box_complex(sp_complex_add(a, b));
  else if (FX_IS("-")) *out = sp_box_complex(sp_complex_sub(a, b));
  else if (FX_IS("*")) *out = sp_box_complex(sp_complex_mul(a, b));
  else if (FX_IS("/") || FX_IS("quo") || FX_IS("fdiv")) *out = sp_box_complex(sp_complex_div(a, b));
  else if (FX_IS("**")) *out = sp_box_complex(sp_complex_pow_c(a, b));
  else if (FX_IS("==") || FX_IS("===")) *out = sp_box_bool(sp_complex_eq(a, b));
  else if (FX_IS("!=")) *out = sp_box_bool(!sp_complex_eq(a, b));
  else if (FX_IS("<=>")) *out = fx_complex_cmp(a, b);
  else return 0;
  return 1;
}

/* The methods of a Complex, and of a real number with a Complex operand,
   that the generic arms of fx_core and the wrappers answer wrongly. ==
   compares with any number (sp_complex_eq); a Complex has no order, so <,
   <=, >, >=, between? and clamp are NoMethodError, as in CRuby. Answers 1
   and sets *out when it handles the call. */
static int fx_complex_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (argc == 1 && fx_is_real(r) && fx_is_complex(argv[0])) return fx_real_complex_op(r, name, argv[0], out);
  if (!fx_is_complex(r)) return 0;
  if (argc == 1 && (FX_IS("==") || FX_IS("!="))) {
    sp_RbVal v = argv[0];
    int eq;
    if (fx_is_real(v) || fx_is_complex(v)) eq = sp_complex_eq(*(sp_Complex *)r.v.p, sp_poly_as_complex(v));
    /* any other value answers with its own ==, as Complex#== asks it */
    else eq = fx_is_fx_object(v) && sp_poly_truthy(fx_send(v, "==", 1, &r));
    *out = sp_box_bool(name[0] == '!' ? !eq : eq);
    return 1;
  }
  if (argc == 1 && FX_IS("<=>")) {
    sp_RbVal v = argv[0];
    *out = fx_is_real(v) || fx_is_complex(v) ? fx_complex_cmp(*(sp_Complex *)r.v.p, sp_poly_as_complex(v)) : sp_box_nil();
    return 1;
  }
  /* a Symbol operand names itself, as rb_num_coerce does */
  if (argc == 1 && argv[0].tag == SP_TAG_SYM &&
      (FX_IS("+") || FX_IS("-") || FX_IS("*") || FX_IS("/") || FX_IS("**") || FX_IS("quo") || FX_IS("fdiv")))
    sp_raise_cls("TypeError", sp_sprintf("%s can't be coerced into Complex", sp_poly_inspect(argv[0])));
  /* fdiv(real) divides each part in Floats; the generator skips that row */
  if (argc == 1 && FX_IS("fdiv") && fx_is_real(argv[0])) {
    *out = sp_box_complex(sp_complex_div_real(*(sp_Complex *)r.v.p, sp_poly_to_f_with_rational(argv[0])));
    return 1;
  }
  if (FX_IS("<") || FX_IS("<=") || FX_IS(">") || FX_IS(">=") || FX_IS("between?") || FX_IS("clamp"))
    sp_raise_nomethod(sp_nomethod_msg(name, r));
  return 0;
}

/* to_c of a real number and of nil: the number as the real part, with
   the Float flag of a Float or a Rational (a Rational is its Float). */
static int fx_to_c(sp_RbVal r, const char *name, int argc, sp_RbVal *out) {
  if (argc || !FX_IS("to_c")) return 0;
  if (r.tag == SP_TAG_NIL || r.tag == SP_TAG_INT || r.tag == SP_TAG_BIGINT) {
    *out = sp_box_complex((sp_Complex){ r.tag == SP_TAG_NIL ? 0.0 : sp_poly_to_f(r), 0, 0 });
    return 1;
  }
  if (r.tag == SP_TAG_FLT || sp_poly_is_rat_kind(r)) {
    *out = sp_box_complex((sp_Complex){ sp_poly_to_f_with_rational(r), 0, SP_CPLX_RE_F });
    return 1;
  }
  return 0;
}

/* A class method of Complex. Answers 1 and sets *out when it handles the
   call. */
static int fx_complex_class(const FxClass *k, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (k->cls_id >= 0 || strcmp(k->name, "Complex") || argc < 1 || argc > 2) return 0;
  int polar = FX_IS("polar");
  if (!polar && !FX_IS("rect") && !FX_IS("rectangular")) return 0;
  sp_Complex a = fx_complex_real(argv[0]), b = argc == 2 ? fx_complex_real(argv[1]) : (sp_Complex){ 0, 0, 0 };
  if (polar) {
    *out = sp_box_complex(sp_complex_polar(a.re, b.re, a.fl & SP_CPLX_RE_F));
  } else {
    unsigned char fl = (unsigned char)((a.fl & SP_CPLX_RE_F) | ((b.fl & SP_CPLX_RE_F) << 1));
    *out = sp_box_complex((sp_Complex){ a.re, b.re, fl });
  }
  return 1;
}
