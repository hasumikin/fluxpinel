/* time.c -- the class methods of Time.

   spinel's codegen calls a function of the runtime for each form of
   Time.now, Time.at, Time.new and Time.utc (src/codegen_call_module.c and
   emit_time_civil_ctor of spinel). Fluxpinel calls the same functions here,
   with the arguments in boxes. A Time is an sp_Time of the runtime, and
   the generated wrappers give its readers. */

#include "fluxpinel.h"

#define FX_IS(s) (!strcmp(name, (s)))

/* The keyword `in:` of the call: answers 1, sets *zone and takes the
   keyword hash off *argc. Answers 0 when the call gives no keywords, and
   -1 when it gives other keywords. */
static int fx_time_in(int *argc, sp_RbVal *argv, sp_RbVal *zone) {
  if (*argc == 0 || !fx_is_hash(argv[*argc - 1]) || argv[*argc - 1].v.p != L.kwhash) return 0;
  sp_PolyPolyHash *h = sp_poly_as_poly_poly_hash(argv[*argc - 1]);
  sp_RbVal k = sp_box_sym(sp_sym_intern("in"));
  if (h->len != 1 || !sp_PolyPolyHash_has_key(h, k)) return -1;
  *zone = sp_PolyPolyHash_get(h, k);
  (*argc)--;
  return 1;
}

/* Field i of a civil form, as emit_time_civil_fields reads it: a String
   is its Integer (or a month name for the month), and nil is the default
   of the field. */
static int64_t fx_time_field(sp_RbVal v, int i) {
  if (fx_is_string(v)) {
    const char *s = sp_poly_arg_str_chk(v);
    return i == 1 ? sp_time_month_arg(s) : (int64_t)strtoll(s, NULL, 10);
  }
  return sp_poly_time_field(v, i);
}

/* Time.new, Time.utc, Time.gm, Time.local and Time.mktime with civil
   fields. A 7th positional argument is the zone for Time.new and the
   microseconds for the others. The fraction of a Float or Rational
   second goes to tv_nsec, as emit_time_civil_ctor sets it. */
static sp_Time *fx_time_civil(int is_utc, int is_new, int argc, sp_RbVal *argv, int has_zone, sp_RbVal zone) {
  int64_t f[6] = { 0, 1, 1, 0, 0, 0 };
  int32_t ns = 0;
  if (is_new && argc == 7) {
    has_zone = 1;
    zone = argv[6];
    argc = 6;
  }
  for (int i = 0; i < argc && i < 6; i++)
    f[i] = i == 5 && !fx_is_string(argv[5]) ? sp_poly_time_sec_split(argv[5], &ns) : fx_time_field(argv[i], i);
  sp_Time *t;
  if (has_zone) {
    /* the fields read in the zone; nil reads them in local time */
    int u = 0;
    int64_t off = zone.tag == SP_TAG_NIL ? 0 : sp_time_zone_val_off(zone, &u);
    t = zone.tag == SP_TAG_NIL ? sp_time_new(f[0], f[1], f[2], f[3], f[4], f[5])
                               : sp_time_new_off(f[0], f[1], f[2], f[3], f[4], f[5], u ? 0 : off);
    if (u) {
      t->is_utc = 1;
      t->utc_off = 0;
    }
  } else {
    t = is_utc ? sp_time_new_utc(f[0], f[1], f[2], f[3], f[4], f[5]) : sp_time_new(f[0], f[1], f[2], f[3], f[4], f[5]);
    if (argc == 7) {
      SP_GC_ROOT(t);
      sp_RbVal us = argv[6];
      t = us.tag == SP_TAG_INT ? sp_time_with_usec(t, us.v.i) : sp_time_with_usec_f(t, sp_poly_to_f_with_rational(us));
    }
  }
  if (ns) t->tv_nsec = ns;
  return t;
}

/* Time.at: the positional arguments as one list, which sp_time_at_args
   reads as codegen gives it a splat. */
static sp_Time *fx_time_at(int argc, sp_RbVal *argv) {
  sp_PolyArray *a = sp_PolyArray_new();
  SP_GC_ROOT(a);
  for (int i = 0; i < argc; i++) sp_PolyArray_push(a, argv[i]);
  return sp_time_at_args(sp_box_poly_array(a));
}

/* Time#to_a: [sec, min, hour, mday, mon, year, wday, yday, isdst, zone] */
static sp_RbVal fx_time_to_a(sp_Time *t) {
  sp_PolyArray *a = sp_PolyArray_new();
  SP_GC_ROOT(a);
  int64_t (*const fn[])(const sp_Time *) = { sp_time_sec, sp_time_min, sp_time_hour, sp_time_mday,
                                             sp_time_mon, sp_time_year, sp_time_wday, sp_time_yday };
  for (size_t i = 0; i < sizeof fn / sizeof fn[0]; i++) sp_PolyArray_push(a, sp_box_int(fn[i](t)));
  sp_PolyArray_push(a, sp_box_bool(sp_time_isdst(t) != 0));
  sp_PolyArray_push(a, sp_box_str_new(sp_str_dup(sp_time_zone(t))));
  return sp_box_poly_array(a);
}

/* The order of two Times for a comparison operator; the other operand
   must be a Time, as in CRuby. The message of CRuby (rb_cmperr) gives the
   inspect of an immediate value and the class of any other value. */
static int fx_time_cmp_op(sp_Time *t, sp_RbVal v) {
  if (v.tag != SP_TAG_OBJ || v.cls_id != SP_BUILTIN_TIME) {
    int imm = v.tag == SP_TAG_NIL || v.tag == SP_TAG_BOOL || v.tag == SP_TAG_INT || v.tag == SP_TAG_FLT ||
              v.tag == SP_TAG_SYM;
    sp_raise_cls("ArgumentError", sp_sprintf("comparison of Time with %s failed",
                                             imm ? sp_poly_inspect(v) : sp_poly_class_name(v)));
  }
  return sp_time_cmp(t, (sp_Time *)v.v.p);
}

/* An instance method of Time that has no generated wrapper, with the
   function that emit_value_recv_call and the operators of codegen call.
   Answers 1 and sets *out when it handles the call. */
static int fx_time_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (r.tag != SP_TAG_OBJ || r.cls_id != SP_BUILTIN_TIME) return 0;
  sp_Time *t = (sp_Time *)r.v.p;
  /* utc, gmtime and localtime change the receiver; the get* forms make a
     new Time */
  if (argc == 0 && (FX_IS("utc") || FX_IS("gmtime"))) {
    sp_time_utc_bang(t);
    *out = r;
    return 1;
  }
  if (argc == 0 && FX_IS("getutc")) { *out = sp_box_time(sp_time_utc(t)); return 1; }
  if (argc <= 1 && FX_IS("localtime")) {
    if (argc) sp_time_in_zone_v_bang(t, argv[0], 1); else sp_time_localtime_bang(t);
    *out = r;
    return 1;
  }
  if (argc <= 1 && FX_IS("getlocal")) {
    *out = sp_box_time(argc ? sp_time_in_zone_v(t, argv[0], 1) : sp_time_localtime(t));
    return 1;
  }
  if (argc == 1 && FX_IS("+")) { *out = sp_poly_add(r, argv[0]); return 1; }
  if (argc == 1 && FX_IS("-")) { *out = sp_poly_sub(r, argv[0]); return 1; }
  if (argc == 1 && FX_IS("<=>")) {
    sp_RbVal v = argv[0];
    *out = v.tag == SP_TAG_OBJ && v.cls_id == SP_BUILTIN_TIME ? sp_box_int(sp_time_cmp(t, (sp_Time *)v.v.p)) : sp_box_nil();
    return 1;
  }
  if (argc == 1 && (FX_IS("==") || FX_IS("eql?") || FX_IS("!="))) {
    sp_RbVal v = argv[0];
    int eq = v.tag == SP_TAG_OBJ && v.cls_id == SP_BUILTIN_TIME && sp_time_cmp(t, (sp_Time *)v.v.p) == 0;
    *out = sp_box_bool(name[0] == '!' ? !eq : eq);
    return 1;
  }
  if (argc == 1 && FX_IS("<")) { *out = sp_box_bool(fx_time_cmp_op(t, argv[0]) < 0); return 1; }
  if (argc == 1 && FX_IS("<=")) { *out = sp_box_bool(fx_time_cmp_op(t, argv[0]) <= 0); return 1; }
  if (argc == 1 && FX_IS(">")) { *out = sp_box_bool(fx_time_cmp_op(t, argv[0]) > 0); return 1; }
  if (argc == 1 && FX_IS(">=")) { *out = sp_box_bool(fx_time_cmp_op(t, argv[0]) >= 0); return 1; }
  /* Comparable#between? and #clamp, with the same operands */
  if (argc == 2 && FX_IS("between?")) {
    *out = sp_box_bool(fx_time_cmp_op(t, argv[0]) >= 0 && fx_time_cmp_op(t, argv[1]) <= 0);
    return 1;
  }
  if (argc == 2 && FX_IS("clamp")) {
    int lo = fx_time_cmp_op(t, argv[0]), hi = fx_time_cmp_op(t, argv[1]);
    if (sp_time_cmp((sp_Time *)argv[0].v.p, (sp_Time *)argv[1].v.p) > 0)
      sp_raise_cls("ArgumentError", "min argument must be less than or equal to max argument");
    *out = lo < 0 ? argv[0] : hi > 0 ? argv[1] : r;
    return 1;
  }
  if (argc == 0 && FX_IS("hash")) { *out = sp_box_int(sp_time_hash(t)); return 1; }
  /* the format is a String; the wrapper reads it with no check */
  if (argc == 1 && FX_IS("strftime")) {
    const char *f = sp_poly_arg_str_chk(argv[0]);
    *out = sp_box_str_new(sp_str_dup(sp_time_strftime(t, f)));
    return 1;
  }
  if (argc == 0 && FX_IS("to_a")) { *out = fx_time_to_a(t); return 1; }
  if (argc == 1 && FX_IS("deconstruct_keys")) {
    *out = sp_box_obj(sp_poly_deconstruct_keys(r, argv[0]), SP_BUILTIN_POLY_POLY_HASH);
    return 1;
  }
  const char *s = NULL;
  if (argc == 0 && FX_IS("iso8601")) s = sp_time_iso8601(t);
  else if (argc == 1 && FX_IS("iso8601")) s = sp_time_iso8601_frac(t, sp_poly_arg_int_chk(argv[0]));
  else if (argc == 0 && FX_IS("httpdate")) s = sp_time_httpdate(t);
  else if (argc == 0 && (FX_IS("rfc2822") || FX_IS("rfc822"))) s = sp_time_rfc2822(t);
  if (!s) return 0;
  *out = sp_box_str_new(sp_str_dup(s));
  return 1;
}

/* A class method of Time. Answers 1 and sets *out when it handles the
   call. */
static int fx_time_class(const FxClass *k, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (k->cls_id >= 0 || strcmp(k->name, "Time")) return 0;
  int is_new = FX_IS("new"), is_utc = FX_IS("utc") || FX_IS("gm");
  if (!is_new && !is_utc && !FX_IS("now") && !FX_IS("at") && !FX_IS("local") && !FX_IS("mktime")) return 0;
  sp_RbVal zone = sp_box_nil();
  int has_zone = fx_time_in(&argc, argv, &zone);
  if (has_zone < 0 || (has_zone && !is_new && !FX_IS("now") && !FX_IS("at"))) return 0;
  sp_Time *t;
  if ((FX_IS("now") || is_new) && argc == 0) t = sp_time_now();
  else if (FX_IS("now")) return 0;
  else if (FX_IS("at")) {
    if (argc < 1 || argc > 3) return 0;
    t = fx_time_at(argc, argv);
  } else if (is_new && argc == 1 && fx_is_string(argv[0])) {
    t = sp_time_parse(sp_poly_arg_str_chk(argv[0]));
  } else if (!is_new && argc == 10) {
    /* sec, min, hour, day, mon, year, wday, yday, isdst, zone */
    int64_t f[6];
    for (int i = 0; i < 6; i++) f[i] = fx_time_field(argv[5 - i], i);
    t = is_utc ? sp_time_new_utc(f[0], f[1], f[2], f[3], f[4], f[5]) : sp_time_new(f[0], f[1], f[2], f[3], f[4], f[5]);
  } else if (argc >= 1 && argc <= 7) {
    *out = sp_box_time(fx_time_civil(is_utc, is_new, argc, argv, has_zone, zone));
    return 1;
  } else return 0;
  /* the instant of now or at, read in the zone of `in:` */
  if (has_zone) {
    SP_GC_ROOT(t);
    t = sp_time_in_zone_v(t, zone, 0);
  }
  *out = sp_box_time(t);
  return 1;
}
