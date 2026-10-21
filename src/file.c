/* file.c -- the class methods of File on paths.

   spinel's codegen calls a function of the runtime for each one
   (emit_call_file_dir_time_arms of spinel). Fluxpinel calls the same functions
   here. A path argument takes the protocol of sp_poly_arg_path: a String,
   or an object with to_path or to_str (fx_hook_to_path).

   File.open and the other methods that make an IO are not here. */

#include "fluxpinel.h"

#define FX_IS(s) (!strcmp(name, (s)))

static int fx_is_kwargs(sp_RbVal v) { return fx_is_hash(v) && v.v.p == L.kwhash; }

/* The String of a keyword argument, or NULL when the call does not give
   it. */
static const char *fx_kwarg_str(sp_RbVal kw, const char *key, sp_RbVal *v) {
  sp_PolyPolyHash *h = sp_poly_as_poly_poly_hash(kw);
  sp_RbVal k = sp_box_sym(sp_sym_intern(key));
  if (!sp_PolyPolyHash_has_key(h, k)) return NULL;
  *v = sp_PolyPolyHash_get(h, k);
  return v->tag == SP_TAG_NIL ? NULL : sp_poly_arg_str_chk(*v);
}

/* Each key of the keyword arguments is one of keys. */
static int fx_kwargs_only(sp_RbVal kw, const char *const *keys) {
  sp_PolyPolyHash *h = sp_poly_as_poly_poly_hash(kw);
  for (sp_int i = 0; i < h->len; i++) {
    sp_RbVal k = h->keys[h->order[i]];
    int ok = 0;
    for (int j = 0; k.tag == SP_TAG_SYM && keys[j]; j++)
      if (!strcmp(sp_sym_to_s((sp_sym)k.v.i), keys[j])) ok = 1;
    if (!ok) return 0;
  }
  return 1;
}

/* File.write(path, data[, offset]) and File.write(path, data, mode:,
   perm:): the count of the bytes that it writes. */
static int fx_file_write(int argc, sp_RbVal *argv, sp_RbVal *out) {
  static const char *const keys[] = { "mode", "perm", NULL };
  const char *path = sp_poly_arg_path(argv[0]);
  SP_GC_ROOT_STR(path);
  const char *data = sp_poly_to_s(argv[1]);
  SP_GC_ROOT_STR(data);
  if (argc == 2 || argv[2].tag == SP_TAG_NIL) { *out = sp_box_int(sp_file_write(path, data)); return 1; }
  if (!fx_is_kwargs(argv[2])) {
    *out = sp_box_int(sp_file_write_at(path, data, sp_poly_arg_int_chk(argv[2])));
    return 1;
  }
  if (!fx_kwargs_only(argv[2], keys)) return 0;
  sp_RbVal mv = sp_box_nil(), pv = sp_box_nil();
  SP_GC_ROOT_RBVAL(mv);
  SP_GC_ROOT_RBVAL(pv);
  const char *mode = fx_kwarg_str(argv[2], "mode", &mv);
  SP_GC_ROOT_STR(mode);
  sp_PolyPolyHash *kw = sp_poly_as_poly_poly_hash(argv[2]);
  sp_RbVal pk = sp_box_sym(sp_sym_intern("perm"));
  if (sp_PolyPolyHash_has_key(kw, pk)) {
    pv = sp_PolyPolyHash_get(kw, pk);
    *out = sp_box_int(sp_file_write_perm(path, data, mode ? mode : "w", sp_poly_arg_perm(pv)));
  } else {
    *out = sp_box_int(mode ? sp_file_write_mode(path, data, mode) : sp_file_write(path, data));
  }
  return 1;
}

/* File.read(path[, length[, offset]]): a nil length reads to the end. */
static sp_RbVal fx_file_read(int bin, int argc, sp_RbVal *argv) {
  const char *path = sp_poly_arg_path(argv[0]);
  SP_GC_ROOT_STR(path);
  const char *s;
  if (argc == 1) {
    s = sp_file_read(path);
  } else {
    sp_oint n = sp_poly_arg_perm(argv[1]);
    s = argc == 3 ? sp_file_read_at_o(path, n, sp_poly_arg_int_chk(argv[2])) : sp_file_read_o(path, n);
  }
  return sp_box_str_new(bin ? sp_str_as_binary(s) : s);
}

/* File.delete(*paths): each path converts and is checked before the first
   unlink, as CRuby converts them all first. Answers the count. */
static sp_RbVal fx_file_delete(int argc, sp_RbVal *argv) {
  sp_PolyArray *paths = sp_PolyArray_new();
  SP_GC_ROOT(paths);
  for (int i = 0; i < argc; i++) sp_PolyArray_push(paths, sp_box_str(sp_poly_arg_path(argv[i])));
  for (sp_int i = 0; i < paths->len; i++) sp_file_path_check(paths->data[i].v.s);
  for (sp_int i = 0; i < paths->len; i++) sp_file_delete(paths->data[i].v.s);
  return sp_box_int(argc);
}

static int fx_file_fn(const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (FX_IS("join")) { *out = sp_box_str_new(sp_file_join_vals(argv, argc)); return 1; }
  if (FX_IS("delete") || FX_IS("unlink")) { *out = fx_file_delete(argc, argv); return 1; }
  if ((FX_IS("write") || FX_IS("binwrite")) && (argc == 2 || argc == 3)) return fx_file_write(argc, argv, out);
  if ((FX_IS("read") || FX_IS("binread")) && argc >= 1 && argc <= 3 && !fx_is_kwargs(argv[argc - 1])) {
    *out = fx_file_read(name[0] == 'b', argc, argv);
    return 1;
  }
  static const char *const one[] = { "exist?", "directory?", "file?", "size", "basename", "dirname", "extname",
                                     NULL };
  int is_one = 0;
  for (int i = 0; argc == 1 && one[i]; i++)
    if (FX_IS(one[i])) is_one = 1;
  if (is_one) {
    const char *path = sp_poly_arg_path(argv[0]);
    SP_GC_ROOT_STR(path);
    if (FX_IS("exist?")) { *out = sp_box_bool(sp_file_exist(path)); return 1; }
    if (FX_IS("directory?")) { *out = sp_box_bool(sp_file_directory(path)); return 1; }
    if (FX_IS("file?")) { *out = sp_box_bool(!sp_file_directory(path) && sp_file_exist(path)); return 1; }
    if (FX_IS("size")) { *out = sp_box_int(sp_file_size(path)); return 1; }
    if (FX_IS("basename")) { *out = sp_box_str_new(sp_file_basename(path)); return 1; }
    if (FX_IS("dirname")) { *out = sp_box_str_new(sp_file_dirname(path)); return 1; }
    if (FX_IS("extname")) { *out = sp_box_str_new(sp_file_extname(path)); return 1; }
  }
  if (FX_IS("basename") && argc == 2) {
    const char *path = sp_poly_arg_path(argv[0]);
    SP_GC_ROOT_STR(path);
    *out = sp_box_str_new(sp_file_basename2(path, sp_poly_arg_str_chk(argv[1])));
    return 1;
  }
  if ((FX_IS("expand_path") || FX_IS("absolute_path")) && (argc == 1 || argc == 2)) {
    const char *path = sp_poly_arg_path(argv[0]);
    SP_GC_ROOT_STR(path);
    *out = sp_box_str_new(sp_file_expand_path(path, argc == 2 ? sp_poly_arg_path(argv[1]) : NULL));
    return 1;
  }
  return 0;
}

/* A class method of File on paths. Answers 1 and sets *out when it
   handles the call. */
static int fx_file_class(const FxClass *k, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (k->cls_id >= 0 || strcmp(k->name, "File")) return 0;
  return fx_file_fn(name, argc, argv, out);
}

/* respond_to? on File: the methods above. */
static int fx_file_responds(sp_RbVal r, const char *name) {
  static const char *const names[] = { "join", "delete", "unlink", "write", "binwrite", "read", "binread",
                                       "exist?", "directory?", "file?", "size", "basename", "dirname",
                                       "extname", "expand_path", "absolute_path", NULL };
  FxClass *k = fx_class_value(r);
  if (!k || k->cls_id >= 0 || strcmp(k->name, "File")) return 0;
  for (int i = 0; names[i]; i++)
    if (FX_IS(names[i])) return 1;
  return 0;
}
