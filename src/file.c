/* file.c -- the class methods of File on paths.

   spinel's codegen calls a function of the runtime for each one
   (emit_call_file_dir_time_arms of spinel). Fluxpinel calls the same functions
   here. A path argument takes the protocol of sp_poly_arg_path: a String,
   or an object with to_path or to_str (fx_hook_to_path).

   File.open and File.new make a File (an IO of the runtime). With a block,
   File.open closes it when the block ends. The methods of an open File
   are the wrappers of the runtime, and the prelude writes the ones with a
   block (IO#each_line and its kin). */

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
  /* File.readlines(path[, sep[, limit]], chomp:): the lines as IO#readlines
     reads them (sp_io_line_args) */
  if (FX_IS("readlines") && argc >= 1) {
    static const char *const keys[] = { "chomp", NULL };
    int chomp = 0;
    if (argc >= 2 && fx_is_kwargs(argv[argc - 1])) {
      if (!fx_kwargs_only(argv[argc - 1], keys)) return 0;
      chomp = sp_poly_truthy(sp_PolyPolyHash_get(sp_poly_as_poly_poly_hash(argv[argc - 1]),
                                                 sp_box_sym(sp_sym_intern("chomp"))));
      argc--;
    }
    sp_PolyArray *rest = sp_PolyArray_new();
    SP_GC_ROOT(rest);
    for (int i = 1; i < argc; i++) sp_PolyArray_push(rest, argv[i]);
    const char *sep;
    sp_int limit;
    sp_io_line_args(rest, &sep, &limit, "readlines");
    SP_GC_ROOT_STR(sep);
    sp_File *f = sp_File_open(sp_poly_arg_path(argv[0]), "r");
    SP_GC_ROOT(f);
    sp_StrArray *a = sp_StrArray_new();
    SP_GC_ROOT(a);
    for (const char *s; (s = sp_File_gets_sep(f, sep, limit, chomp)) != NULL;) sp_StrArray_push(a, s);
    sp_File_close(f);
    *out = sp_box_nullable_obj((void *)a, SP_BUILTIN_STR_ARRAY);
    return 1;
  }
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

/* File.open(path[, mode[, perm]]) and File.new: mode is a String, an
   Integer of flags or nil, and the keywords mode: and perm: can give
   them. The keywords of the encoding are taken and do nothing: the runtime
   has one encoding. Answers NULL for a keyword that this does not take. */
static sp_File *fx_file_open(int argc, sp_RbVal *argv) {
  static const char *const keys[] = { "mode", "perm", "encoding", "external_encoding", "internal_encoding",
                                       "binmode", "textmode", NULL };
  sp_RbVal kw = sp_box_nil();
  if (argc >= 2 && fx_is_kwargs(argv[argc - 1])) {
    kw = argv[--argc];
    if (!fx_kwargs_only(kw, keys)) return NULL;
  }
  const char *path = sp_poly_arg_path(argv[0]);
  SP_GC_ROOT_STR(path);
  sp_RbVal mode = argc >= 2 ? argv[1] : sp_box_nil(), perm = argc >= 3 ? argv[2] : sp_box_nil();
  if (kw.tag != SP_TAG_NIL) {
    sp_PolyPolyHash *h = sp_poly_as_poly_poly_hash(kw);
    sp_RbVal km = sp_box_sym(sp_sym_intern("mode")), kp = sp_box_sym(sp_sym_intern("perm"));
    if (sp_PolyPolyHash_has_key(h, km)) mode = sp_PolyPolyHash_get(h, km);
    if (sp_PolyPolyHash_has_key(h, kp)) perm = sp_PolyPolyHash_get(h, kp);
  }
  return sp_File_open_val(path, mode, sp_poly_arg_perm(perm));
}

/* File.open with a block: the block takes the File, and the File closes
   when the block ends, also by a raise or a jump. Answers the value of the
   block. */
static sp_RbVal fx_file_with(sp_File *f, const FxBlock *blk) {
  SP_GC_SAVE();
  SP_GC_ROOT(f);
  sp_RbVal v = sp_box_nil(), io = sp_box_obj(f, SP_BUILTIN_IO);
  SP_GC_ROOT_RBVAL(v);
  SP_GC_ROOT_RBVAL(io);
  const int depth = fx_fs()->depth;
  FX_TRY_PUSH();
  if (setjmp(sp_exc_stack[sp_exc_top - 1]) == 0) {
    v = fx_y1(blk, io);
    sp_exc_top--;
  } else {
    FX_TRY_LANDED(depth);
    if (!sp_File_closed_p(f)) sp_File_close(f);
    /* a throw or a proc return goes on to its target */
    if (sp_unwind_kind != SP_UNWIND_NONE) sp_unwind_resume();
    fx_reraise(fx_caught());
  }
  if (!sp_File_closed_p(f)) sp_File_close(f);
  return v;
}

/* File.foreach(path[, sep[, limit]]) and IO.foreach with a block: each
   line of the file, read as it goes. The separator and the limit read as
   CRuby reads them (sp_io_line_args), and chomp: takes off the separator.
   mode: opens the file in that mode, and the keywords of the encoding do
   nothing. */
static int fx_file_foreach(int argc, sp_RbVal *argv, const FxBlock *blk, sp_RbVal *out) {
  static const char *const keys[] = { "chomp", "mode", "encoding", "external_encoding", "internal_encoding",
                                      NULL };
  int chomp = 0;
  sp_RbVal mode = sp_box_nil();
  if (argc >= 2 && fx_is_kwargs(argv[argc - 1])) {
    if (!fx_kwargs_only(argv[argc - 1], keys)) return 0;
    sp_PolyPolyHash *h = sp_poly_as_poly_poly_hash(argv[argc - 1]);
    sp_RbVal kc = sp_box_sym(sp_sym_intern("chomp")), km = sp_box_sym(sp_sym_intern("mode"));
    chomp = sp_PolyPolyHash_has_key(h, kc) && sp_poly_truthy(sp_PolyPolyHash_get(h, kc));
    if (sp_PolyPolyHash_has_key(h, km)) mode = sp_PolyPolyHash_get(h, km);
    argc--;
  }
  if (argc < 1) return 0;
  SP_GC_ROOT_RBVAL(mode);
  sp_PolyArray *rest = sp_PolyArray_new();
  SP_GC_ROOT(rest);
  for (int i = 1; i < argc; i++) sp_PolyArray_push(rest, argv[i]);
  const char *sep;
  sp_int limit;
  sp_io_line_args(rest, &sep, &limit, "foreach");
  SP_GC_ROOT_STR(sep);
  const char *path = sp_poly_arg_path(argv[0]);
  SP_GC_ROOT_STR(path);
  sp_File *f = sp_File_open_val(path, mode, sp_oint_nil());
  SP_GC_SAVE();
  SP_GC_ROOT(f);
  sp_RbVal line = sp_box_nil();
  SP_GC_ROOT_RBVAL(line);
  const int depth = fx_fs()->depth;
  FX_TRY_PUSH();
  if (setjmp(sp_exc_stack[sp_exc_top - 1]) == 0) {
    for (const char *s; (s = sp_File_gets_sep(f, sep, limit, chomp)) != NULL;) {
      line = sp_box_str_new(s);
      fx_y1(blk, line);
      if (L.ctl != FX_CTL_NONE) break;
    }
    sp_exc_top--;
  } else {
    FX_TRY_LANDED(depth);
    sp_File_close(f);
    if (sp_unwind_kind != SP_UNWIND_NONE) sp_unwind_resume();
    fx_reraise(fx_caught());
  }
  sp_File_close(f);
  *out = sp_box_nil();
  return 1;
}

/* IO.pipe with a block: the block takes the reader and the writer, and
   both close when the block ends, also by a raise or a jump. */
static sp_RbVal fx_io_pipe_with(const FxBlock *blk) {
  SP_GC_SAVE();
  sp_PolyArray *ends = sp_io_pipe();
  SP_GC_ROOT(ends);
  sp_RbVal v = sp_box_nil();
  SP_GC_ROOT_RBVAL(v);
  const int depth = fx_fs()->depth;
  FX_TRY_PUSH();
  if (setjmp(sp_exc_stack[sp_exc_top - 1]) == 0) {
    v = fx_yield(blk, 2, ends->data);
    sp_exc_top--;
  } else {
    FX_TRY_LANDED(depth);
    for (int i = 0; i < 2; i++)
      if (!sp_File_closed_p((sp_File *)ends->data[i].v.p)) sp_File_close((sp_File *)ends->data[i].v.p);
    if (sp_unwind_kind != SP_UNWIND_NONE) sp_unwind_resume();
    fx_reraise(fx_caught());
  }
  for (int i = 0; i < 2; i++)
    if (!sp_File_closed_p((sp_File *)ends->data[i].v.p)) sp_File_close((sp_File *)ends->data[i].v.p);
  return v;
}

/* Whether v names a path for IO.copy_stream: a String, or an object with
   to_path, as CRuby asks. */
static int fx_copy_path(sp_RbVal v) {
  return fx_is_string(v) || (!fx_is_io(v) && fx_builtin_responds(v, "to_path")) ||
         (fx_is_fx_object(v) && fx_find_method(fx_class_of(v), sp_sym_intern("to_path")));
}

/* IO.copy_stream(src, dst): each side is a path, an IO, or an object that
   reads or writes (a StringIO). Two paths take sp_io_copy_stream of the
   runtime. Else the copy reads the rest of the source and writes it to
   the destination. Answers the count of the bytes. */
static sp_int fx_io_copy_stream(sp_RbVal src, sp_RbVal dst) {
  SP_GC_ROOT_RBVAL(src);
  SP_GC_ROOT_RBVAL(dst);
  int spath = fx_copy_path(src), dpath = fx_copy_path(dst);
  if (spath && dpath) {
    const char *s = sp_poly_arg_path(src);
    SP_GC_ROOT_STR(s);
    const char *d = sp_poly_arg_path(dst);
    SP_GC_ROOT_STR(d);
    return sp_io_copy_stream(s, d);
  }
  sp_RbVal data = sp_box_nil();
  SP_GC_ROOT_RBVAL(data);
  if (spath) {
    sp_File *in = sp_File_open(sp_poly_arg_path(src), "rb");
    SP_GC_ROOT(in);
    const char *s = sp_File_read(in);
    sp_File_close(in);
    data = s ? sp_box_str_new(s) : sp_box_nil();
  } else if (fx_is_io(src)) {
    const char *s = sp_File_read((sp_File *)src.v.p);
    data = s ? sp_box_str_new(s) : sp_box_nil();
  } else {
    data = fx_send(src, "read", 0, NULL);
  }
  if (data.tag == SP_TAG_NIL) return 0;
  sp_int n = (sp_int)sp_str_byte_len(sp_poly_arg_str_chk(data));
  if (dpath) {
    sp_File *out = sp_File_open(sp_poly_arg_path(dst), "wb");
    SP_GC_ROOT(out);
    sp_File_write_poly(out, data);
    sp_File_close(out);
  } else if (fx_is_io(dst)) {
    sp_File_write_poly((sp_File *)dst.v.p, data);
  } else {
    fx_send(dst, "write", 1, &data);
  }
  return n;
}

/* A class method of File on paths, or one that opens a File. Answers 1
   and sets *out when it handles the call. */
static int fx_file_class(const FxClass *k, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                         sp_RbVal *out) {
  if (k->cls_id >= 0) return 0;
  if (!strcmp(k->name, "IO")) {
    /* IO.foreach reads a path, as File.foreach does */
    if (FX_IS("foreach")) return blk && fx_file_foreach(argc, argv, blk, out);
    if (FX_IS("pipe") && argc == 0) {
      *out = blk ? fx_io_pipe_with(blk) : sp_box_poly_array(sp_io_pipe());
      return 1;
    }
    if (FX_IS("select") && argc >= 1 && argc <= 4 && !blk) {
      *out = fx_io_select(argc, argv);
      return 1;
    }
    if (FX_IS("copy_stream") && argc == 2 && !blk) {
      *out = sp_box_int(fx_io_copy_stream(argv[0], argv[1]));
      return 1;
    }
    return 0;
  }
  if (strcmp(k->name, "File")) return 0;
  if ((FX_IS("open") || FX_IS("new")) && argc >= 1 && argc <= 4) {
    sp_File *f = fx_file_open(argc, argv);
    if (!f) return 0;
    /* File.new takes no block, as CRuby only warns about one */
    *out = blk && FX_IS("open") ? fx_file_with(f, blk) : sp_box_obj(f, SP_BUILTIN_IO);
    return 1;
  }
  if (FX_IS("foreach") && blk) return fx_file_foreach(argc, argv, blk, out);
  if (blk) return 0;
  return fx_file_fn(name, argc, argv, out);
}

/* respond_to? on File: the methods above. */
static int fx_file_responds(sp_RbVal r, const char *name) {
  static const char *const names[] = { "open", "new", "foreach", "readlines", "join", "delete", "unlink",
                                       "write", "binwrite", "read", "binread",
                                       "exist?", "directory?", "file?", "size", "basename", "dirname",
                                       "extname", "expand_path", "absolute_path", NULL };
  FxClass *k = fx_class_value(r);
  if (!k || k->cls_id >= 0 || strcmp(k->name, "File")) return 0;
  for (int i = 0; names[i]; i++)
    if (FX_IS(names[i])) return 1;
  return 0;
}
