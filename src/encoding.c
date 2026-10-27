/* encoding.c -- the class Encoding, its constants and its methods.

   An Encoding is an SP_TAG_ENCODING value of the runtime, which holds the
   name of the encoding. The runtime gives String#encoding, inspect, ==
   and hash. spinel's codegen gives the constants (codegen_expr.c) and a
   few methods; Fluxpinel gives them here. The runtime keeps one internal
   representation of a String (UTF-8 or ASCII-8BIT): an Encoding of
   another name does not change the bytes. */

#include "fluxpinel.h"

#define FX_IS(s) (!strcmp(name, (s)))

/* the names and aliases of the encodings, and the encoding each names */
static const char *const fx_enc_names[][2] = {
#include "sp_encoding_names.h"
  { NULL, NULL } };

/* Whether the encoding name n gives the constant c, as CRuby names them
   (set_encoding_const): n with "_" for each byte other than a letter or a
   digit, with an uppercase first letter when n starts with a lowercase
   one and has an uppercase one; or that in uppercase. A constant starts
   with an uppercase letter. */
static int fx_enc_const_of(const char *n, const char *c) {
  if (!(c[0] >= 'A' && c[0] <= 'Z')) return 0;
  int has_upper = 0, as = 1, up = 1;
  size_t j = 0;
  for (; n[j]; j++) {
    if (n[j] >= 'A' && n[j] <= 'Z') has_upper = 1;
    char ch = isalnum((unsigned char)n[j]) ? n[j] : '_';
    if ((j ? ch : (char)toupper((unsigned char)ch)) != c[j]) as = 0;
    if (toupper((unsigned char)ch) != c[j]) up = 0;
    if (!c[j]) return 0;
  }
  if (c[j]) return 0;
  /* a lowercase first letter stays only in the uppercase form */
  if (n[0] >= 'a' && n[0] <= 'z' && !has_upper) as = 0;
  return as || up;
}

/* The constant name of Encoding, at its first read: the Encoding of a
   name or an alias, or UNICODE_VERSION. Answers 0 for no such constant.
   The constants are made on demand, so a program that does not read them
   interns no names of them. */
static int fx_encoding_const(FxClass *enc, sp_sym name, sp_RbVal *out) {
  const char *c = sp_sym_to_s(name);
  sp_RbVal v = sp_box_nil();
  /* the Unicode version of the tables of the runtime (lib/regexp/unicase.h) */
  if (!strcmp(c, "UNICODE_VERSION")) v = fx_frozen_cstr("17.0.0");
  for (int i = 0; v.tag == SP_TAG_NIL && fx_enc_names[i][0]; i++)
    if (fx_enc_const_of(fx_enc_names[i][0], c)) v = sp_encoding_find(sp_box_str(fx_enc_names[i][0]));
  if (v.tag == SP_TAG_NIL) return 0;
  SP_GC_ROOT_RBVAL(v);
  fx_const_set(enc, name, v);
  *out = v;
  return 1;
}

/* CRuby's Encoding#dummy? and #ascii_compatible?: the encodings that answer
   true and false */
static int fx_enc_in(const char *n, const char *const *list) {
  for (int i = 0; list[i]; i++)
    if (!strcmp(n, list[i])) return 1;
  return 0;
}
static const char *const fx_enc_dummy[] = { "UTF-16", "UTF-32", "IBM037", "ISO-2022-JP", "ISO-2022-JP-2",
                                            "CP50220", "CP50221", "UTF-7", "ISO-2022-JP-KDDI", NULL };
static const char *const fx_enc_not_ascii[] = { "UTF-16BE", "UTF-16LE", "UTF-32BE", "UTF-32LE", NULL };

/* A method of an Encoding. Answers 1 and sets *out when it handles the
   call. */
static int fx_encoding_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (r.tag != SP_TAG_ENCODING) return 0;
  /* one Encoding for each encoding, as in CRuby: two values of one name
     are the same object, though the runtime may hold two copies of it */
  if (argc == 1 && FX_IS("equal?")) {
    *out = sp_box_bool(argv[0].tag == SP_TAG_ENCODING && sp_poly_eq(r, argv[0]));
    return 1;
  }
  if (argc) return 0;
  const char *n = r.v.s ? r.v.s : "";
  if (FX_IS("name") || FX_IS("to_s")) { *out = fx_frozen_cstr(n); return 1; }
  if (FX_IS("dummy?")) { *out = sp_box_bool(fx_enc_in(n, fx_enc_dummy)); return 1; }
  if (FX_IS("ascii_compatible?")) {
    *out = sp_box_bool(!fx_enc_in(n, fx_enc_dummy) && !fx_enc_in(n, fx_enc_not_ascii));
    return 1;
  }
  return 0;
}

/* A class method of Encoding. Answers 1 and sets *out when it handles the
   call. */
static int fx_encoding_class(const FxClass *k, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (k->cls_id >= 0 || strcmp(k->name, "Encoding")) return 0;
  if (FX_IS("find") && argc == 1) { *out = sp_encoding_find(argv[0]); return 1; }
  if (FX_IS("list") && argc == 0) { *out = sp_box_poly_array(sp_encoding_list()); return 1; }
  if (FX_IS("default_external") && argc == 0) { *out = sp_box_encoding(sp_encoding_utf8()); return 1; }
  if (FX_IS("default_internal") && argc == 0) { *out = sp_box_nil(); return 1; }
  /* an Encoding is made only by the runtime */
  if (FX_IS("new")) sp_raise_nomethod(sp_nomethod_msg(name, fx_class_box(k)));
  if (FX_IS("allocate")) sp_raise_cls("TypeError", "allocator undefined for Encoding");
  return 0;
}

#undef FX_IS
