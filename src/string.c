/* string.c -- the methods of String that spinel emits from codegen code.

   As for Array (array.c), the generated table has no wrapper for most
   methods of String. On a receiver of unknown type, the AOT code calls a
   sp_poly_* helper, or reads the bytes (sp_poly_recv_s), runs the C
   function of the typed String and writes the new bytes back into the
   handle (sp_poly_str_become). Fluxpinel does the same here.

   Every String that the walker makes is a mutable handle (STRBUF). A
   String of the tag SP_TAG_STR comes from the runtime and is frozen, so
   a mutator raises FrozenError on it, as sp_str_check_mutable does.

   A bang method that has a plain form (upcase!, strip!, ...) runs the
   plain form and writes its result back. It answers nil when nothing
   changes, as CRuby does. The methods with a regexp are in re.c. */

#include "fluxpinel.h"

#define FX_IS(s) (!strcmp(name, (s)))

/* The bytes of a String to change: FrozenError for a frozen one. */
static const char *fx_str_mut(sp_RbVal r, const char *name) {
  const char *s = sp_poly_recv_s_mut(r, name);
  if (!sp_poly_is_strbuf(r)) {
    sp_str_check_mutable(s);
    fx_gap(sp_sprintf("String#%s on a String of the runtime", name));
  }
  return s;
}

/* The bang methods that answer the plain form of the name. */
static const char *const fx_str_bangs[] = {
  "upcase!", "downcase!", "capitalize!", "swapcase!", "strip!", "lstrip!", "rstrip!", "chomp!", "chop!",
  "squeeze!", "tr!", "tr_s!", "delete!", "delete_prefix!", "delete_suffix!", "succ!", "next!", "reverse!",
  "unicode_normalize!", NULL
};

static int fx_str_bang(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  int i = 0;
  while (fx_str_bangs[i] && strcmp(fx_str_bangs[i], name)) i++;
  if (!fx_str_bangs[i]) return 0;
  const char *s = fx_str_mut(r, name);
  SP_GC_ROOT(s);
  char plain[32];
  snprintf(plain, sizeof plain, "%.*s", (int)strlen(name) - 1, name);
  sp_RbVal t = fx_send(r, plain, argc, argv);
  SP_GC_ROOT_RBVAL(t);
  const char *ts = sp_poly_recv_s(t, name);
  int changed = !sp_str_eq(s, ts);
  if (changed) sp_poly_str_become(r, ts);
  /* succ!, next! and reverse! answer self in any case */
  int self = FX_IS("succ!") || FX_IS("next!") || FX_IS("reverse!");
  *out = changed || self ? r : sp_box_nil();
  return 1;
}

/* The arguments joined as << joins them, into a new String: concat and
   prepend read all of them before the change (s.concat(s)). */
static sp_RbVal fx_str_joined(int argc, sp_RbVal *argv) {
  sp_RbVal t = fx_new_string(NULL, 0);
  SP_GC_ROOT_RBVAL(t);
  for (int i = 0; i < argc; i++) sp_poly_shl(t, argv[i]);
  return t;
}

/* The name of an Encoding or of an encoding as a String, for
   force_encoding: 1 for binary, 0 for UTF-8 and US-ASCII, -1 else. */
static int fx_str_enc_mode(sp_RbVal e) {
  const char *n = sp_poly_to_s(e);
  if (!strcasecmp(n, "BINARY") || !strcasecmp(n, "ASCII-8BIT")) return 1;
  if (!strcasecmp(n, "UTF-8") || !strcasecmp(n, "US-ASCII") || !strcasecmp(n, "ASCII")) return 0;
  return -1;
}

/* to_i, and to_i(base) when base is not -1. In promote mode a value past
   the machine word is a Bignum, as the AOT code answers. */
static sp_RbVal fx_str_to_i(const char *s, sp_int base) {
#ifdef SP_INT_OVERFLOW_MODE_PROMOTE
  return sp_str_to_i_promote(s, base, 0);
#else
  return sp_box_int(base == -1 ? sp_str_to_i_cruby(s) : sp_str_to_i_base(s, base));
#endif
}

static int fx_all_strings(int argc, sp_RbVal *argv) {
  for (int i = 0; i < argc; i++)
    if (!fx_is_string(argv[i])) return 0;
  return 1;
}

/* String.new and String.new(s), as the AOT code makes them: a new handle,
   ASCII-8BIT when empty and a copy of s otherwise. encoding: sets the
   encoding of the copy. capacity: has no visible effect. */
static int fx_string_class(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  FxClass *k = fx_class_value(r);
  if (!k || k->cls_id >= 0 || strcmp(k->name, "String") || !FX_IS("new")) return 0;
  sp_RbVal enc = sp_box_nil();
  SP_GC_ROOT_RBVAL(enc);
  if (argc > 0 && fx_is_hash(argv[argc - 1]) && argv[argc - 1].v.p == L.kwhash) {
    sp_PolyPolyHash *kw = sp_poly_as_poly_poly_hash(argv[--argc]);
    sp_RbVal key = sp_box_sym(sp_sym_intern("encoding"));
    if (sp_PolyPolyHash_has_key(kw, key)) enc = sp_PolyPolyHash_get(kw, key);
  }
  if (argc > 1) sp_raise_cls("ArgumentError", sp_sprintf("wrong number of arguments (given %d, expected 0..1)", argc));
  const char *s = argc ? sp_poly_arg_str_chk(argv[0]) : NULL;
  SP_GC_ROOT_STR(s);
  *out = sp_box_str_new(s ? sp_str_dup(s) : sp_str_empty_binary());
  if (enc.tag != SP_TAG_NIL && sp_poly_is_strbuf(*out))
    sp_String_force_encoding((sp_String *)out->v.p, fx_str_enc_mode(enc));
  return 1;
}

/* The methods of String with no block. Answers 1 and sets *out when it
   handles the call. */
static int fx_string_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                            sp_RbVal *out) {
  if (r.tag == SP_TAG_CLASS) return blk ? 0 : fx_string_class(r, name, argc, argv, out);
  if (blk || !fx_is_string(r)) return 0;
  SP_GC_ROOT_RBVAL(r);
  if (fx_str_bang(r, name, argc, argv, out)) return 1;
  if (argc == 0) {
    if (FX_IS("dup")) { *out = sp_poly_dup(r, 0); return 1; }
    if (FX_IS("clone")) { *out = sp_poly_dup(r, 1); return 1; }
    /* -s: s itself when frozen, else a frozen copy */
    if (FX_IS("-@") || FX_IS("dedup")) {
      *out = r.tag == SP_TAG_STR || sp_poly_frozen(r) ? r : sp_poly_neg_chk(r);
      return 1;
    }
    if (FX_IS("freeze")) { *out = sp_poly_freeze(r); return 1; }
    /* a String of the runtime (SP_TAG_STR) is frozen */
    if (FX_IS("frozen?")) { *out = sp_box_bool(r.tag == SP_TAG_STR || sp_poly_frozen(r)); return 1; }
    if (FX_IS("object_id")) { *out = sp_box_int((sp_int)(uintptr_t)sp_poly_identity_ptr(r)); return 1; }
    if (FX_IS("to_i")) { *out = fx_str_to_i(sp_poly_recv_s(r, name), -1); return 1; }
    if (FX_IS("lines")) {
      *out = sp_box_nullable_obj(sp_str_lines(sp_poly_recv_s(r, name)), SP_BUILTIN_STR_ARRAY);
      return 1;
    }
    if (FX_IS("clear")) { fx_str_mut(r, name); *out = sp_poly_clear(r); return 1; }
    /* each_char and its kin with no block: an Enumerator, which keeps the
       name of the method for its inspect */
    const char *each = FX_IS("each_char") ? SPL("each_char") : FX_IS("each_byte") ? SPL("each_byte")
                     : FX_IS("each_line") ? SPL("each_line") : FX_IS("each_codepoint") ? SPL("each_codepoint")
                     : NULL;
    if (each) { *out = sp_box_obj(sp_poly_str_each_enum(r, each), SP_BUILTIN_ENUMERATOR); return 1; }
    if (FX_IS("scrub!")) {
      const char *s = fx_str_mut(r, name);
      const char *t = sp_str_scrub_bang(s, NULL);
      if (!sp_poly_str_is_own(r, t)) sp_poly_str_become(r, t);
      *out = r;
      return 1;
    }
  }
  if (argc == 1) {
    sp_RbVal a = argv[0];
    if (FX_IS("[]") || FX_IS("slice")) {
      *out = fx_is_string(a) ? sp_poly_get_str(r, sp_poly_recv_s(a, name)) : sp_poly_index_poly(r, a);
      return 1;
    }
    if (FX_IS("to_i")) { *out = fx_str_to_i(sp_poly_recv_s(r, name), sp_poly_arg_int_chk(a)); return 1; }
    /* the wrapper of the table reads an argument of any kind as a String */
    if (FX_IS("include?")) {
      const char *s = sp_poly_recv_s(r, name);
      SP_GC_ROOT(s);
      *out = sp_box_bool(sp_str_include(s, sp_poly_arg_str_chk(a)));
      return 1;
    }
    if (FX_IS("getbyte")) { *out = sp_poly_getbyte(r, sp_poly_arg_int_chk(a)); return 1; }
    if (FX_IS("unpack") || FX_IS("unpack1")) {
      sp_PolyArray *p = sp_str_unpack(sp_poly_recv_s(r, name), sp_poly_arg_str_chk(a));
      *out = name[6] ? sp_PolyArray_get(p, 0) : sp_box_poly_array(p);
      return 1;
    }
    /* a Regexp is re.c's; the helper raises TypeError for anything else
       that is no String */
    if (FX_IS("index") || FX_IS("rindex")) {
      *out = sp_box_oint(sp_poly_str_index_val(r, a, name[0] == 'r'));
      return 1;
    }
    if (fx_is_string(a) && (FX_IS("byteindex") || FX_IS("byterindex"))) {
      const char *s = sp_poly_recv_s(r, name), *x = sp_poly_recv_s(a, name);
      *out = sp_box_oint(name[4] == 'r' ? sp_str_byterindex(s, x) : sp_str_byteindex(s, x));
      return 1;
    }
    if (fx_is_string(a) && (FX_IS("partition") || FX_IS("rpartition"))) {
      const char *s = sp_poly_recv_s(r, name), *x = sp_poly_recv_s(a, name);
      *out = sp_box_nullable_obj(name[0] == 'r' ? sp_str_rpartition(s, x) : sp_str_partition(s, x),
                                 SP_BUILTIN_STR_ARRAY);
      return 1;
    }
    if (fx_is_string(a) && FX_IS("lines")) {
      *out = sp_box_nullable_obj(sp_str_lines_sep(sp_poly_recv_s(r, name), sp_poly_recv_s(a, name)),
                                 SP_BUILTIN_STR_ARRAY);
      return 1;
    }
    if (fx_is_string(a) && FX_IS("chomp")) {
      *out = sp_box_str_new(sp_str_chomp_sep(sp_poly_recv_s(r, name), sp_poly_recv_s(a, name)));
      return 1;
    }
    if (a.tag == SP_TAG_SYM && !strcmp(sp_sym_to_s((sp_sym)a.v.i), "ascii")) {
      const char *(*fn)(const char *) = FX_IS("upcase") ? sp_str_upcase_ascii
                                        : FX_IS("downcase") ? sp_str_downcase_ascii
                                        : FX_IS("capitalize") ? sp_str_capitalize_ascii
                                        : FX_IS("swapcase") ? sp_str_swapcase_ascii : NULL;
      if (fn) { *out = sp_poly_case_conv(r, fn, name); return 1; }
    }
    /* the frozen flag of the handle, without a read of its bytes, which
       would make a run of appends quadratic */
    if (FX_IS("<<")) {
      if (!sp_poly_is_strbuf(r)) fx_str_mut(r, name);
      if (sp_String_is_frozen((sp_String *)r.v.p)) sp_raise_frozen_str(sp_String_cstr((sp_String *)r.v.p));
      *out = sp_poly_shl(r, a);
      return 1;
    }
    if (FX_IS("replace")) { fx_str_mut(r, name); *out = sp_poly_replace_any(r, a); return 1; }
    if (FX_IS("force_encoding")) {
      /* a frozen String raises before a bad argument does, as in
         emit_strbuf_force_encoding_mode */
      if (sp_poly_is_strbuf(r)) {
        sp_String *h = (sp_String *)r.v.p;
        sp_String_force_encoding(h, sp_String_is_frozen(h) ? -1 : sp_force_encoding_mode(a));
      }
      /* a String of no handle takes the tag on its bytes, as
         emit_str_force_encoding does */
      else if (r.tag == SP_TAG_STR && r.v.s) {
        sp_str_check_mutable(r.v.s);
        int mode = sp_force_encoding_mode(a);
        if (mode == 1) sp_str_as_binary(r.v.s);
        else if (mode == 0) sp_str_as_text(r.v.s);
      }
      *out = r;
      return 1;
    }
    if (FX_IS("encode") && !(fx_is_hash(a) && a.v.p == L.kwhash)) {
      sp_RbVal nil = sp_box_nil();
      *out = sp_box_str_new(sp_str_encode(sp_poly_recv_s(r, name), 1, a, nil, nil, nil, nil));
      return 1;
    }
  }
  if (argc == 2) {
    sp_RbVal a = argv[0], b = argv[1];
    if ((FX_IS("[]") || FX_IS("slice")) && a.tag == SP_TAG_INT && b.tag == SP_TAG_INT) {
      *out = sp_poly_slice(r, a.v.i, b.v.i);
      return 1;
    }
    if (FX_IS("index") || FX_IS("rindex")) {
      *out = sp_box_oint(sp_poly_str_index_from_val(r, a, sp_poly_arg_int_chk(b), name[0] == 'r'));
      return 1;
    }
    if (fx_is_string(a) && (FX_IS("byteindex") || FX_IS("byterindex"))) {
      const char *s = sp_poly_recv_s(r, name), *x = sp_poly_recv_s(a, name);
      sp_int i = sp_poly_arg_int_chk(b);
      *out = sp_box_oint(name[4] == 'r' ? sp_str_byterindex_from(s, x, i) : sp_str_byteindex_from(s, x, i));
      return 1;
    }
    if (FX_IS("between?")) {
      sp_poly_recv_ck(r, name);
      *out = sp_box_bool(sp_poly_cmp_ck(r, a) >= 0 && sp_poly_cmp_ck(r, b) <= 0);
      return 1;
    }
    if (FX_IS("clamp")) { *out = sp_poly_clamp(r, a, b); return 1; }
    if (fx_is_string(a) && b.tag == SP_TAG_INT && FX_IS("split") && strcmp(sp_poly_recv_s(a, name), " ")) {
      sp_StrArray *p = sp_str_split_limit(sp_poly_recv_s(r, name), sp_poly_recv_s(a, name), b.v.i);
      *out = sp_box_nullable_obj(p, SP_BUILTIN_STR_ARRAY);
      return 1;
    }
    if (fx_is_string(b) && (FX_IS("center") || FX_IS("ljust") || FX_IS("rjust"))) {
      const char *s = sp_poly_recv_s(r, name), *pad = sp_poly_recv_s(b, name);
      sp_int w = sp_poly_arg_int_chk(a);
      *out = sp_box_str_new(FX_IS("center") ? sp_str_center2(s, w, pad)
                            : FX_IS("ljust") ? sp_str_ljust2(s, w, pad) : sp_str_rjust2(s, w, pad));
      return 1;
    }
    if (FX_IS("setbyte")) {
      const char *s = fx_str_mut(r, name);
      sp_int b8 = sp_poly_arg_int_chk(b);
      sp_poly_str_become(r, sp_str_setbyte_cow(s, sp_poly_arg_int_chk(a), b8));
      *out = b;
      return 1;
    }
    if (FX_IS("insert")) { fx_str_mut(r, name); *out = sp_poly_insert(r, sp_poly_arg_int_chk(a), b); return 1; }
    /* s[i] = v, s[range] = v and s["key"] = v */
    if (FX_IS("[]=") && (a.tag == SP_TAG_INT || fx_is_range(a) || fx_is_string(a))) {
      fx_str_mut(r, name);
      if (a.tag == SP_TAG_INT) sp_poly_arr_widen_and_set(r, a.v.i, b);
      else if (fx_is_range(a)) sp_poly_splice_range(r, *(sp_Range *)a.v.p, b);
      else sp_poly_str_aset_key(r, a, b);
      *out = b;
      return 1;
    }
  }
  if (argc == 3 && FX_IS("[]=")) {
    fx_str_mut(r, name);
    sp_poly_splice(r, sp_poly_arg_int_chk(argv[0]), sp_poly_arg_int_chk(argv[1]), argv[2]);
    *out = argv[2];
    return 1;
  }
  /* bytesplice(start, len, str) and bytesplice(start, len, str, sstart, slen) */
  if ((argc == 3 || argc == 5) && FX_IS("bytesplice") && argv[0].tag == SP_TAG_INT && argv[1].tag == SP_TAG_INT &&
      fx_is_string(argv[2])) {
    const char *s = fx_str_mut(r, name), *v = sp_poly_recv_s(argv[2], name);
    sp_int vb = argc == 5 ? sp_poly_arg_int_chk(argv[3]) : 0;
    sp_int vl = argc == 5 ? sp_poly_arg_int_chk(argv[4]) : (sp_int)sp_str_byte_len(v);
    sp_poly_str_become_enc(r, sp_str_bytesplice5(s, argv[0].v.i, argv[1].v.i, v, vb, vl));
    *out = r;
    return 1;
  }
  /* slice!(...): the part that [] answers, then that part removed */
  if (argc >= 1 && argc <= 2 && FX_IS("slice!")) {
    fx_str_mut(r, name);
    sp_RbVal part = fx_send(r, "[]", argc, argv);
    SP_GC_ROOT_RBVAL(part);
    if (part.tag != SP_TAG_NIL) {
      sp_RbVal args[3] = { argv[0], argc == 2 ? argv[1] : sp_box_nil(), fx_new_string(NULL, 0) };
      SP_GC_ROOT_RBVAL(args[2]);
      if (argc == 1) args[1] = args[2];
      fx_send(r, "[]=", argc + 1, args);
    }
    *out = part;
    return 1;
  }
  /* count, delete and squeeze raise TypeError for an argument that is no
     String; the wrapper of the table reads it as one */
  if (argc >= 1) {
    if (FX_IS("count")) { *out = sp_poly_str_setop_n(r, 0, argc, argv); return 1; }
    if (FX_IS("delete")) { *out = sp_poly_str_setop_n(r, 1, argc, argv); return 1; }
    if (FX_IS("squeeze")) { *out = sp_poly_str_setop_n(r, 2, argc, argv); return 1; }
  }
  if (argc >= 1 && fx_all_strings(argc, argv)) {
    if (FX_IS("start_with?") || FX_IS("end_with?")) {
      const char *s = sp_poly_recv_s(r, name);
      int hit = 0;
      for (int i = 0; i < argc && !hit; i++) {
        const char *x = sp_poly_recv_s(argv[i], name);
        hit = name[0] == 's' ? sp_str_start_with(s, x) : sp_str_end_with(s, x);
      }
      *out = sp_box_bool(hit);
      return 1;
    }
    if (FX_IS("append_as_bytes")) {
      const char *s = fx_str_mut(r, name);
      SP_GC_ROOT(s);
      for (int i = 0; i < argc; i++) s = sp_str_append_bytes(s, sp_poly_recv_s(argv[i], name));
      sp_poly_str_become(r, s);
      *out = r;
      return 1;
    }
  }
  if (FX_IS("concat") || FX_IS("prepend")) {
    const char *s = fx_str_mut(r, name);
    SP_GC_ROOT(s);
    sp_RbVal t = fx_str_joined(argc, argv);
    SP_GC_ROOT_RBVAL(t);
    const char *ts = sp_poly_recv_s(t, name);
    sp_poly_str_become(r, name[0] == 'c' ? sp_str_concat(s, ts) : sp_str_concat(ts, s));
    *out = r;
    return 1;
  }
  return 0;
}

#undef FX_IS
