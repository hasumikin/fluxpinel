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
static int64_t fx_utime_arg(sp_RbVal v, int32_t *nsec);

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
    sp_oint perm = sp_poly_arg_perm(pv);
    sp_int n;
    FX_UNLOCKED(n = sp_file_write_perm(path, data, mode ? mode : "w", perm));   /* an open of a FIFO waits */
    *out = sp_box_int(n);
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
    const char *path = sp_poly_arg_path(argv[0]);
    SP_GC_ROOT_STR(path);
    sp_File *f;
    FX_UNLOCKED_ROOT(f = sp_File_open(path, "r"), SP_GC_ROOT(f));   /* an open of a FIFO waits for its peer */
    SP_GC_ROOT(f);
    sp_StrArray *a = sp_StrArray_new();
    SP_GC_ROOT(a);
    for (const char *s;;) {
      FX_UNLOCKED_ROOT(s = sp_File_gets_sep(f, sep, limit, chomp), SP_GC_ROOT_STR(s));
      if (!s) break;
      sp_StrArray_push(a, s);
    }
    sp_File_close(f);
    *out = sp_box_nullable_obj((void *)a, SP_BUILTIN_STR_ARRAY);
    return 1;
  }
  if (FX_IS("delete") || FX_IS("unlink")) { *out = fx_file_delete(argc, argv); return 1; }
  if ((FX_IS("write") || FX_IS("binwrite")) && (argc == 2 || argc == 3)) return fx_file_write(argc, argv, out);
  /* File.read(path[, length[, offset]], mode:, encoding:, ...): a binary
     mode, or a BINARY encoding, reads as binread does; the other keywords
     of the encoding do nothing (UTF-8 is the text) */
  if ((FX_IS("read") || FX_IS("binread")) && argc >= 1 && argc <= 4) {
    static const char *const keys[] = { "mode", "encoding", "external_encoding", "internal_encoding", "binmode",
                                        NULL };
    int bin = name[0] == 'b';
    if (fx_is_kwargs(argv[argc - 1])) {
      if (!fx_kwargs_only(argv[argc - 1], keys)) return 0;
      sp_PolyPolyHash *h = sp_poly_as_poly_poly_hash(argv[argc - 1]);
      sp_RbVal mode = sp_PolyPolyHash_get(h, sp_box_sym(sp_sym_intern("mode")));
      sp_RbVal enc = sp_PolyPolyHash_get(h, sp_box_sym(sp_sym_intern("encoding")));
      SP_GC_ROOT_RBVAL(mode);
      SP_GC_ROOT_RBVAL(enc);
      if (mode.tag != SP_TAG_NIL && strchr(sp_poly_arg_str_chk(mode), 'b')) bin = 1;
      if (sp_poly_truthy(sp_PolyPolyHash_get(h, sp_box_sym(sp_sym_intern("binmode"))))) bin = 1;
      if (enc.tag != SP_TAG_NIL) {
        const char *e = fx_is_string(enc) ? sp_poly_arg_str_chk(enc) : sp_poly_to_s(enc);
        if (!strncasecmp(e, "BINARY", 6) || !strncasecmp(e, "ASCII-8BIT", 10)) bin = 1;
      }
      argc--;
    }
    if (argc > 3) return 0;
    *out = fx_file_read(bin, argc, argv);
    return 1;
  }
  /* the class methods that change a file or the process, through the
     runtime: umask, symlink and link, readlink, mkfifo, chmod and chown
     over every path (the count), utime and lutime (a Time or seconds),
     truncate */
  if (FX_IS("umask") && argc <= 1) {
    *out = sp_box_int(sp_file_umask(argc ? sp_poly_arg_int_chk(argv[0]) : 0, argc));
    return 1;
  }
  if ((FX_IS("symlink") || FX_IS("link")) && argc == 2) {
    const char *from = sp_poly_arg_path(argv[0]);
    SP_GC_ROOT_STR(from);
    const char *to = sp_poly_arg_path(argv[1]);
    SP_GC_ROOT_STR(to);
    *out = sp_box_int(name[0] == 's' ? sp_file_do_symlink(from, to) : sp_file_do_link(from, to));
    return 1;
  }
  if ((FX_IS("readlink") || FX_IS("mkfifo")) && argc >= 1 && argc <= (FX_IS("mkfifo") ? 2 : 1)) {
    const char *path = sp_poly_arg_path(argv[0]);
    SP_GC_ROOT_STR(path);
    if (FX_IS("readlink")) *out = sp_box_str_new(sp_file_readlink(path));
    else *out = sp_box_int(sp_file_mkfifo(path, argc == 2 ? sp_poly_arg_int_chk(argv[1]) : 0666));
    return 1;
  }
  if ((FX_IS("chmod") && argc >= 2) || (FX_IS("chown") && argc >= 3)) {
    int np = FX_IS("chmod") ? 1 : 2;
    sp_int a = argv[0].tag == SP_TAG_NIL ? -1 : sp_poly_arg_int_chk(argv[0]);
    sp_int b = np == 2 && argv[1].tag != SP_TAG_NIL ? sp_poly_arg_int_chk(argv[1]) : -1;
    for (int i = np; i < argc; i++) {
      const char *path = sp_poly_arg_path(argv[i]);
      SP_GC_ROOT_STR(path);
      if (np == 1) sp_file_chmod(a, path);
      else sp_file_chown(path, a, b);
    }
    *out = sp_box_int(argc - np);
    return 1;
  }
  if ((FX_IS("utime") || FX_IS("lutime")) && argc >= 3) {
    int32_t an = 0, mn = 0;
    int64_t as = fx_utime_arg(argv[0], &an), ms = fx_utime_arg(argv[1], &mn);
    for (int i = 2; i < argc; i++) {
      const char *path = sp_poly_arg_path(argv[i]);
      SP_GC_ROOT_STR(path);
      if (name[0] == 'l') sp_file_lutime_ns(as, an, ms, mn, path);
      else sp_file_utime_ns(as, an, ms, mn, path);
    }
    *out = sp_box_int(argc - 2);
    return 1;
  }
  if (FX_IS("truncate") && argc == 2) {
    const char *path = sp_poly_arg_path(argv[0]);
    SP_GC_ROOT_STR(path);
    *out = sp_box_int(sp_file_truncate(path, sp_poly_arg_int_chk(argv[1])));
    return 1;
  }
  static const char *const one[] = { "exist?", "directory?", "file?", "symlink?", "size", "basename", "dirname",
                                     "extname", "readable?", "writable?", "executable?", "mtime", "atime", "ctime",
                                     "path", "stat", "lstat", "ftype", NULL };
  int is_one = 0;
  for (int i = 0; argc == 1 && one[i]; i++)
    if (FX_IS(one[i])) is_one = 1;
  if (is_one) {
    const char *path = sp_poly_arg_path(argv[0]);
    SP_GC_ROOT_STR(path);
    if (FX_IS("exist?")) { *out = sp_box_bool(sp_file_exist(path)); return 1; }
    if (FX_IS("directory?")) { *out = sp_box_bool(sp_file_directory(path)); return 1; }
    if (FX_IS("file?")) { *out = sp_box_bool(!sp_file_directory(path) && sp_file_exist(path)); return 1; }
    if (FX_IS("symlink?")) { *out = sp_box_bool(sp_file_symlink(path)); return 1; }
    if (FX_IS("ftype")) { *out = sp_box_str_new(sp_file_ftype(path)); return 1; }
    if (FX_IS("size")) { *out = sp_box_int(sp_file_size(path)); return 1; }
    if (FX_IS("basename")) { *out = sp_box_str_new(sp_file_basename(path)); return 1; }
    if (FX_IS("dirname")) { *out = sp_box_str_new(sp_file_dirname(path)); return 1; }
    if (FX_IS("extname")) { *out = sp_box_str_new(sp_file_extname(path)); return 1; }
    /* File.path: the path of a String or of a value with to_path, as a new String */
    if (FX_IS("path")) { *out = sp_box_str_new(sp_str_dup(path)); return 1; }
    /* the times of a stat of the path (Errno::ENOENT for no file) */
    if (FX_IS("mtime")) { *out = sp_box_timev(sp_filev_mtime(path)); return 1; }
    if (FX_IS("atime")) { *out = sp_box_timev(sp_filev_atime(path)); return 1; }
    if (FX_IS("ctime")) { *out = sp_box_timev(sp_filev_ctime(path)); return 1; }
    /* File.stat and File.lstat: a handle of the runtime whose readers are
       in fx_stat_core (global.c) */
    if (FX_IS("stat")) { *out = sp_box_obj(sp_file_stat_handle(path), SP_BUILTIN_IO); return 1; }
    if (FX_IS("lstat")) { *out = sp_box_obj(sp_file_lstat_handle(path), SP_BUILTIN_IO); return 1; }
    /* the access of the process, as CRuby asks with access(2) */
    int mode = name[0] == 'r' ? R_OK : name[0] == 'w' ? W_OK : X_OK;
    *out = sp_box_bool(access(path, mode) == 0);
    return 1;
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
  sp_oint p = sp_poly_arg_perm(perm);
  sp_File *f;
  FX_UNLOCKED_ROOT(f = sp_File_open_val(path, mode, p), SP_GC_ROOT(f));   /* an open of a FIFO waits for its peer */
  return f;
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
  sp_File *f;
  FX_UNLOCKED_ROOT(f = sp_File_open_val(path, mode, sp_oint_nil()), SP_GC_ROOT(f));   /* an open of a FIFO waits for its peer */
  SP_GC_SAVE();
  SP_GC_ROOT(f);
  sp_RbVal line = sp_box_nil();
  SP_GC_ROOT_RBVAL(line);
  const int depth = fx_fs()->depth;
  FX_TRY_PUSH();
  if (setjmp(sp_exc_stack[sp_exc_top - 1]) == 0) {
    for (const char *s;;) {
      FX_UNLOCKED_ROOT(s = sp_File_gets_sep(f, sep, limit, chomp), SP_GC_ROOT_STR(s));
      if (!s) break;
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
         (fx_is_fx_object(v) && fx_find_method(fx_lookup_class(v), sp_sym_intern("to_path")));
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
    sp_int n;
    FX_UNLOCKED(n = sp_io_copy_stream(s, d));
    return n;
  }
  sp_RbVal data = sp_box_nil();
  SP_GC_ROOT_RBVAL(data);
  if (spath) {
    const char *path = sp_poly_arg_path(src);
    SP_GC_ROOT_STR(path);
    sp_File *in;
    FX_UNLOCKED_ROOT(in = sp_File_open(path, "rb"), SP_GC_ROOT(in));   /* an open of a FIFO waits for its peer */
    SP_GC_ROOT(in);
    const char *s;
    FX_UNLOCKED_ROOT(s = sp_File_read(in), SP_GC_ROOT_STR(s));
    sp_File_close(in);
    data = s ? sp_box_str_new(s) : sp_box_nil();
  } else if (fx_is_io(src)) {
    const char *s;
    FX_UNLOCKED_ROOT(s = sp_File_read((sp_File *)src.v.p), SP_GC_ROOT_STR(s));
    data = s ? sp_box_str_new(s) : sp_box_nil();
  } else {
    data = fx_send(src, "read", 0, NULL);
  }
  if (data.tag == SP_TAG_NIL) return 0;
  sp_int n = (sp_int)sp_str_byte_len(sp_poly_arg_str_chk(data));
  if (dpath) {
    const char *path = sp_poly_arg_path(dst);
    SP_GC_ROOT_STR(path);
    sp_File *out;
    FX_UNLOCKED_ROOT(out = sp_File_open(path, "wb"), SP_GC_ROOT(out));
    SP_GC_ROOT(out);
    FX_UNLOCKED(sp_File_write_poly(out, data));
    sp_File_close(out);
  } else if (fx_is_io(dst)) {
    FX_UNLOCKED(sp_File_write_poly((sp_File *)dst.v.p, data));
  } else {
    fx_send(dst, "write", 1, &data);
  }
  return n;
}

/* A class method of File on paths, or one that opens a File. Answers 1
   and sets *out when it handles the call. */
/* Dir.glob(pattern | [patterns], base:, flags:, sort:) { } and
   Dir[pattern, ...]: the matches of each pattern, in order, through the
   glob of the runtime (sp_dir_glob_keywords, which takes the keywords).
   With a block, each match goes to it and the answer is nil. */
static int fx_dir_glob(const char *name, int argc, sp_RbVal *argv, const FxBlock *blk, sp_RbVal *out) {
  int kw = argc > 0 && fx_is_kwargs(argv[argc - 1]);
  /* Dir.glob(pattern, flags): the flags as a second argument */
  int pos_flags = FX_IS("glob") && argc - kw == 2 && argv[1].tag == SP_TAG_INT;
  if (FX_IS("[]") ? kw || argc < 1 : argc - kw != 1 + pos_flags) return 0;
  static const char *const keys[] = { "base", "flags", "sort", NULL };
  sp_RbVal base = sp_box_nil(), flags = pos_flags ? argv[1] : sp_box_nil(), sort = sp_box_nil();
  int fg = pos_flags, sg = 0;
  if (pos_flags) { argv[1] = argv[argc - 1]; argc--; }
  if (kw) {
    if (!fx_kwargs_only(argv[argc - 1], keys)) return 0;
    sp_PolyPolyHash *h = sp_poly_as_poly_poly_hash(argv[argc - 1]);
    sp_RbVal kb = sp_box_sym(sp_sym_intern("base")), kf = sp_box_sym(sp_sym_intern("flags"));
    sp_RbVal ks = sp_box_sym(sp_sym_intern("sort"));
    if (sp_PolyPolyHash_has_key(h, kb)) base = sp_PolyPolyHash_get(h, kb);
    if (sp_PolyPolyHash_has_key(h, kf)) { fg = 1; flags = sp_PolyPolyHash_get(h, kf); }
    if ((sg = sp_PolyPolyHash_has_key(h, ks))) sort = sp_PolyPolyHash_get(h, ks);
    argc--;
  }
  SP_GC_ROOT_RBVAL(base);
  SP_GC_ROOT_RBVAL(flags);
  SP_GC_ROOT_RBVAL(sort);
  /* the patterns: the arguments of Dir[], or the one argument of glob,
     which can be an Array */
  sp_RbVal pats = argc == 1 && fx_is_array(argv[0]) ? argv[0] : sp_box_nil();
  SP_GC_ROOT_RBVAL(pats);
  sp_int n = pats.tag == SP_TAG_NIL ? argc : sp_poly_length(pats);
  sp_StrArray *a = sp_StrArray_new();
  SP_GC_ROOT(a);
  for (sp_int i = 0; i < n; i++) {
    sp_RbVal p = pats.tag == SP_TAG_NIL ? argv[i] : sp_poly_arr_get(pats, i);
    sp_StrArray *one = sp_dir_glob_keywords(p, base, flags, fg, sort, sg);
    SP_GC_ROOT(one);
    for (sp_int j = 0; one && j < one->len; j++) sp_StrArray_push(a, sp_StrArray_get(one, j));
  }
  if (!blk) { *out = sp_box_nullable_obj((void *)a, SP_BUILTIN_STR_ARRAY); return 1; }
  for (sp_int i = 0; i < a->len; i++) {
    fx_y1(blk, sp_box_str_new(sp_str_dup(sp_StrArray_get(a, i))));
    if (L.ctl != FX_CTL_NONE) break;
  }
  *out = sp_box_nil();
  return 1;
}

/* A time of File.utime: a Time's seconds and nanoseconds, or a number of
   seconds (a Float or a Rational carries a fraction). */
static int64_t fx_utime_arg(sp_RbVal v, int32_t *nsec) {
  if (v.tag == SP_TAG_OBJ && v.cls_id == SP_BUILTIN_TIME && v.v.p) {
    const sp_Time *t = (const sp_Time *)v.v.p;
    *nsec = t->tv_nsec;
    return t->tv_sec;
  }
  return sp_poly_time_sec_split(v, nsec);
}

/* A Dir handle as a value: nil for none. */
static sp_RbVal fx_dir_box(sp_Dir *d) { return sp_box_nullable_obj((void *)d, SP_BUILTIN_DIR); }

/* Dir.open with a block: the block takes the Dir, which closes when the
   block ends, also by a raise or a jump. Answers the value of the block. */
static sp_RbVal fx_dir_with(sp_Dir *d, const FxBlock *blk) {
  SP_GC_SAVE();
  SP_GC_ROOT(d);
  sp_RbVal v = sp_box_nil(), dir = fx_dir_box(d);
  SP_GC_ROOT_RBVAL(v);
  SP_GC_ROOT_RBVAL(dir);
  const int depth = fx_fs()->depth;
  FX_TRY_PUSH();
  if (setjmp(sp_exc_stack[sp_exc_top - 1]) == 0) {
    v = fx_y1(blk, dir);
    sp_exc_top--;
  } else {
    FX_TRY_LANDED(depth);
    if (d->dp) sp_Dir_close(d);
    if (sp_unwind_kind != SP_UNWIND_NONE) sp_unwind_resume();
    fx_reraise(fx_caught());
  }
  if (d->dp) sp_Dir_close(d);
  return v;
}

/* Dir.chdir(path) with a block: the block runs in the directory, which
   takes the path as its argument, and the working directory comes back
   when the block ends, also by a raise or a jump. Answers the value of the
   block. */
static sp_RbVal fx_dir_chdir_with(const char *path, const FxBlock *blk) {
  SP_GC_SAVE();
  const char *back = sp_dir_pwd();
  SP_GC_ROOT_STR(back);
  sp_RbVal v = sp_box_nil(), arg = sp_box_str_new(sp_str_dup(path));
  SP_GC_ROOT_RBVAL(v);
  SP_GC_ROOT_RBVAL(arg);
  sp_dir_chdir0(path);
  const int depth = fx_fs()->depth;
  FX_TRY_PUSH();
  if (setjmp(sp_exc_stack[sp_exc_top - 1]) == 0) {
    v = fx_y1(blk, arg);
    sp_exc_top--;
  } else {
    FX_TRY_LANDED(depth);
    sp_dir_chdir(back);
    if (sp_unwind_kind != SP_UNWIND_NONE) sp_unwind_resume();
    fx_reraise(fx_caught());
  }
  sp_dir_chdir(back);
  return v;
}

/* The methods of a Dir handle, as the runtime has them: read, the lists,
   the position, the path, close, dup, and each with a block; the methods
   of Enumerable go over the entries. */
static int fx_dir_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk, sp_RbVal *out) {
  if (r.tag != SP_TAG_OBJ || r.cls_id != SP_BUILTIN_DIR || !r.v.p) return 0;
  sp_Dir *d = (sp_Dir *)r.v.p;
  SP_GC_ROOT(d);
  int each = FX_IS("each") || FX_IS("each_entry") || FX_IS("each_child");
  if (blk && each && argc == 0) {
    const char *s = NULL;
    SP_GC_ROOT_STR(s);
    int children = name[5] == 'c';
    /* from the first entry, wherever a read left the stream, as CRuby's
       each rewinds first */
    sp_Dir_rewind(d);
    while ((s = sp_Dir_read(d)) != NULL) {
      if (children && (!strcmp(s, ".") || !strcmp(s, ".."))) continue;
      fx_y1(blk, sp_box_str_new(s));
      if (L.ctl != FX_CTL_NONE) break;
    }
    *out = r;
    return 1;
  }
  if (!blk && argc == 0) {
    if (FX_IS("read")) {
      const char *s = sp_Dir_read(d);
      *out = s ? sp_box_str_new(s) : sp_box_nil();
      return 1;
    }
    /* a new String each time, as CRuby answers it; nil for a Dir of for_fd */
    if (FX_IS("path") || FX_IS("to_path")) {
      const char *s = sp_Dir_path_copy(d);
      *out = s ? sp_box_str_new(s) : sp_box_nil();
      return 1;
    }
    if (FX_IS("inspect")) {
      const char *p = sp_Dir_path(d);
      *out = sp_box_str_new(sp_sprintf("#<Dir:%s>", p ? p : ""));
      return 1;
    }
    if (FX_IS("to_s")) { *out = sp_box_str_new(sp_Dir_to_s(d)); return 1; }
    if (FX_IS("close")) { *out = sp_Dir_close(d); return 1; }
    if (FX_IS("rewind")) { sp_Dir_rewind(d); *out = r; return 1; }
    if (FX_IS("tell") || FX_IS("pos")) { *out = sp_box_int(sp_Dir_tell(d)); return 1; }
    if (FX_IS("fileno")) { *out = sp_box_int(sp_Dir_fileno(d)); return 1; }
    if (FX_IS("children") || FX_IS("entries")) {
      *out = sp_box_nullable_obj((void *)sp_Dir_entries_h(d, name[0] == 'c'), SP_BUILTIN_STR_ARRAY);
      return 1;
    }
    if (FX_IS("dup") || FX_IS("clone")) { *out = fx_dir_box(sp_dir_dup_handle(d)); return 1; }
    if (each) {
      sp_RbVal items = sp_box_nullable_obj((void *)sp_Dir_entries_h(d, name[5] == 'c'), SP_BUILTIN_STR_ARRAY);
      SP_GC_ROOT_RBVAL(items);
      *out = sp_box_obj(sp_Enumerator_new_from_items(sp_poly_to_poly_array(items)), SP_BUILTIN_ENUMERATOR);
      return 1;
    }
  }
  if (!blk && argc == 1 && FX_IS("seek")) { sp_Dir_seek(d, sp_poly_arg_int_chk(argv[0])); *out = r; return 1; }
  if (!blk && argc == 1 && FX_IS("pos=")) { sp_Dir_seek(d, sp_poly_arg_int_chk(argv[0])); *out = argv[0]; return 1; }
  /* count, include?, map, sort and the other methods of Enumerable: over
     the entries, from the start, as the runtime's count and include? read
     them */
  if (fx_enumerable_name(name)) {
    sp_RbVal items = sp_box_nullable_obj((void *)sp_Dir_entries_h(d, 0), SP_BUILTIN_STR_ARRAY);
    SP_GC_ROOT_RBVAL(items);
    *out = fx_send_b(items, name, argc, argv, blk);
    return 1;
  }
  return 0;
}

/* The class methods of Dir on paths, through the runtime: the lists
   (children, entries, glob), the tests (exist?, empty?) and mkdir, rmdir
   and chdir; new, open and for_fd answer a handle. each_child and foreach
   yield the names, or answer an Enumerator over them. */
static int fx_dir_class(const char *name, int argc, sp_RbVal *argv, const FxBlock *blk, sp_RbVal *out) {
  if (FX_IS("glob") || FX_IS("[]")) return fx_dir_glob(name, argc, argv, blk, out);
  if (argc == 0 && !blk) {
    if (FX_IS("pwd") || FX_IS("getwd")) { *out = sp_box_str_new(sp_dir_pwd()); return 1; }
    if (FX_IS("home")) { *out = sp_box_str_new(sp_dir_home()); return 1; }
    /* Dir.chdir with no path: the home directory */
    if (FX_IS("chdir")) { *out = sp_box_int(sp_dir_chdir(sp_dir_home())); return 1; }
    return 0;
  }
  if (argc == 1 && !blk && (FX_IS("for_fd") || FX_IS("fchdir"))) {
    sp_int fd = sp_poly_arg_int_chk(argv[0]);
    *out = name[1] == 'o' ? fx_dir_box(sp_Dir_for_fd(fd)) : sp_box_int(sp_Dir_fchdir(fd));
    return 1;
  }
  /* the lists take encoding: (nil, an Encoding or a name), as the runtime
     reads it */
  static const char *const enc_keys[] = { "encoding", NULL };
  sp_RbVal enc = sp_box_nil();
  int has_enc = 0;
  int each = FX_IS("each_child") || FX_IS("foreach");
  if (argc == 2 && (each || FX_IS("children") || FX_IS("entries")) && fx_is_kwargs(argv[1]) &&
      fx_kwargs_only(argv[1], enc_keys)) {
    has_enc = 1;
    enc = sp_PolyPolyHash_get(sp_poly_as_poly_poly_hash(argv[1]), sp_box_sym(sp_sym_intern("encoding")));
    argc--;
  }
  SP_GC_ROOT_RBVAL(enc);
  if (argc < 1 || argc > 2 || (argc == 2 && !FX_IS("mkdir"))) return 0;
  if (blk && !each && !FX_IS("open") && !FX_IS("chdir")) return 0;
  const char *path = sp_poly_arg_path(argv[0]);
  SP_GC_ROOT_STR(path);
  /* Dir.new and Dir.open: a handle; open with a block closes it after */
  if (FX_IS("new") || FX_IS("open")) {
    sp_Dir *d = sp_Dir_new(path);
    SP_GC_ROOT(d);
    *out = blk ? fx_dir_with(d, blk) : fx_dir_box(d);
    return 1;
  }
  if (FX_IS("chdir") && blk) { *out = fx_dir_chdir_with(path, blk); return 1; }
  if (FX_IS("exist?")) { *out = sp_box_bool(sp_file_directory(path)); return 1; }
  if (FX_IS("empty?")) { *out = sp_box_bool(sp_dir_empty(path)); return 1; }
  if (FX_IS("home")) { *out = sp_box_str_new(sp_dir_home_user(path)); return 1; }
  if (FX_IS("mkdir")) {
    *out = sp_box_int(argc == 2 ? sp_dir_mkdir_mode(path, sp_poly_arg_int_chk(argv[1])) : sp_dir_mkdir(path));
    return 1;
  }
  if (FX_IS("rmdir") || FX_IS("delete") || FX_IS("unlink")) { *out = sp_box_int(sp_dir_rmdir(path)); return 1; }
  if (FX_IS("chdir")) { *out = sp_box_int(sp_dir_chdir(path)); return 1; }
  if (!FX_IS("children") && !FX_IS("entries") && !each) return 0;
  int children = FX_IS("children") || FX_IS("each_child");
  sp_StrArray *a = has_enc ? sp_dir_entries_enc(path, children, enc)
                   : children ? sp_dir_children(path) : sp_dir_entries(path);
  SP_GC_ROOT(a);
  sp_RbVal list = sp_box_nullable_obj((void *)a, SP_BUILTIN_STR_ARRAY);
  SP_GC_ROOT_RBVAL(list);
  if (!each) { *out = list; return 1; }
  if (!blk) {
    *out = sp_box_obj(sp_Enumerator_new_from_items(sp_poly_to_poly_array(list)), SP_BUILTIN_ENUMERATOR);
    return 1;
  }
  for (sp_int i = 0; i < a->len; i++) {
    fx_y1(blk, sp_box_str_new(sp_str_dup(sp_StrArray_get(a, i))));
    if (L.ctl != FX_CTL_NONE) break;
  }
  *out = sp_box_nil();
  return 1;
}

static int fx_file_class(const FxClass *k, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                         sp_RbVal *out) {
  if (k->cls_id >= 0) return 0;
  if (!strcmp(k->name, "Dir")) return fx_dir_class(name, argc, argv, blk, out);
  if (!strcmp(k->name, "IO")) {
    /* IO.foreach reads a path, as File.foreach does */
    if (FX_IS("foreach")) return blk && fx_file_foreach(argc, argv, blk, out);
    /* IO.open(fd[, mode], **enc), IO.new and IO.for_fd: an IO on the
       descriptor. The standard descriptors stay open when it closes. The
       keywords of the encoding are taken and do nothing. */
    if ((FX_IS("open") || FX_IS("new") || FX_IS("for_fd")) && argc >= 1 && argc <= 3 && argv[0].tag == SP_TAG_INT) {
      int n = argc;
      if (fx_is_kwargs(argv[n - 1])) n--;
      if (n > 2) return 0;
      const char *mode = n == 2 ? sp_poly_arg_str_chk(argv[1]) : "r";
      int fd = (int)argv[0].v.i;
      sp_File *f = sp_io_fdopen_ex(fd, mode, fd > 2);
      if (fd <= 2) f->no_autoclose = 1;   /* a close, or the collector, flushes and leaves the descriptor */
      /* a terminal sees each write at once, as CRuby's IO flushes a write to
         a tty and as the runtime keeps stdout: stdio would hold the bytes of
         a line until its newline (Reline's rendering of IRB has none) */
      if (isatty(fd)) setvbuf(f->fp, NULL, _IONBF, 0);
      *out = blk && FX_IS("open") ? fx_file_with(f, blk) : sp_box_obj(f, SP_BUILTIN_IO);
      return 1;
    }
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
                                       "exist?", "directory?", "file?", "symlink?", "size", "basename", "dirname",
                                       "extname", "readable?", "writable?", "executable?", "mtime", "atime",
                                       "ctime", "path", "stat", "lstat", "expand_path", "absolute_path", NULL };
  FxClass *k = fx_class_value(r);
  if (!k || k->cls_id >= 0 || strcmp(k->name, "File")) return 0;
  for (int i = 0; names[i]; i++)
    if (FX_IS(names[i])) return 1;
  return 0;
}
