/* global.c -- global variables and the IO methods of the standard streams.

   A global variable of the program lives in a table of Fluxpinel (L.globals),
   which the mark hook of object.c marks. A global that no code has
   assigned reads nil.

   A special global reads the state of the runtime, as the code that
   spinel emits does: the standard streams, $?, $0, $LOAD_PATH, ... An
   assignment to a writable special global goes into the table, and a read
   prefers the table. After `$stdout = obj`, Kernel#puts, #print and #p
   write to obj. */

#include "fluxpinel.h"

#define FX_IS(s) (!strcmp(name, (s)))

/* The name that an alias stands for. */
static const char *fx_gvar_canon(const char *name) {
  if (FX_IS("$-v") || FX_IS("$-w")) return "$VERBOSE";
  if (FX_IS("$-d")) return "$DEBUG";
  if (FX_IS("$PROGRAM_NAME")) return "$0";
  if (FX_IS("$:") || FX_IS("$-I")) return "$LOAD_PATH";
  if (FX_IS("$\"")) return "$LOADED_FEATURES";
  return name;
}

static FxConst *fx_gvar_slot(sp_sym s) {
  for (int i = 0; i < L.nglobals; i++)
    if (L.globals[i].name == s) return &L.globals[i];
  return NULL;
}

static sp_RbVal fx_io_box(sp_File *f) {
  return sp_box_obj(f, SP_BUILTIN_IO);
}

static int fx_is_io(sp_RbVal v) {
  return v.tag == SP_TAG_OBJ && v.cls_id == SP_BUILTIN_IO && v.v.p;
}

static sp_RbVal fx_gvar_get(const char *name) {
  name = fx_gvar_canon(name);
  FxConst *g = fx_gvar_slot(sp_sym_intern(name));
  if (g) return g->value;
  if (FX_IS("$stdout")) return fx_io_box(sp_io_stdout());
  if (FX_IS("$stderr")) return fx_io_box(sp_io_stderr());
  if (FX_IS("$stdin")) return fx_io_box(sp_io_stdin());
  if (FX_IS("$/")) return fx_new_string((const uint8_t *)"\n", 1);
  if (FX_IS("$?")) return sp_box_nullable_obj((void *)sp_last_process_status(), SP_BUILTIN_PROCESS_STATUS);
  if (FX_IS("$$")) return sp_box_int((sp_int)getpid());
  if (FX_IS("$0")) return fx_new_string((const uint8_t *)L.path, strlen(L.path));
  if (FX_IS("$LOAD_PATH")) return sp_box_nullable_obj((void *)sp_get_load_path(), SP_BUILTIN_STR_ARRAY);
  if (FX_IS("$LOADED_FEATURES"))
    return sp_box_nullable_obj((void *)sp_get_loaded_features(), SP_BUILTIN_STR_ARRAY);
  if (FX_IS("$VERBOSE") || FX_IS("$DEBUG")) return sp_box_bool(0);
  return sp_box_nil();
}

/* Whether v has a write method, as CRuby asks of $stdout and $stderr. */
static int fx_gvar_writable(sp_RbVal v) {
  if (fx_is_io(v)) return 1;
  if (fx_is_fx_object(v)) return fx_find_method(fx_lookup_class(v), sp_sym_intern("write")) != NULL;
  return fx_builtin_responds(v, "write");
}

static void fx_gvar_set(const char *name, sp_RbVal v) {
  name = fx_gvar_canon(name);
  if (FX_IS("$?") || FX_IS("$$") || FX_IS("$LOAD_PATH") || FX_IS("$LOADED_FEATURES"))
    sp_raise_cls("NameError", sp_sprintf("%s is a read-only variable", name));
  if ((FX_IS("$stdout") || FX_IS("$stderr")) && !fx_gvar_writable(v))
    sp_raise_cls("TypeError", sp_sprintf("%s must have write method, %s given", name,
                                         v.tag == SP_TAG_NIL ? "NilClass" : sp_poly_class_name(v)));
  sp_sym s = sp_sym_intern(name);
  FxConst *g = fx_gvar_slot(s);
  if (g) { g->value = v; return; }
  if (L.nglobals == L.globals_cap) {
    L.globals_cap = L.globals_cap ? L.globals_cap * 2 : 16;
    L.globals = realloc(L.globals, sizeof *L.globals * (size_t)L.globals_cap);
  }
  L.globals[L.nglobals++] = (FxConst){ s, v };
}

/* The object that Kernel#puts writes to, when the program has assigned
   $stdout another one than the standard output. */
static int fx_gvar_stdout(sp_RbVal *out) {
  FxConst *g = fx_gvar_slot(sp_sym_intern("$stdout"));
  if (!g || (fx_is_io(g->value) && g->value.v.p == (void *)sp_io_stdout())) return 0;
  *out = g->value;
  return 1;
}

/* The global variable nodes. */
static sp_RbVal fx_gvar_eval(FxFrame *f, const pm_node_t *n) {
  switch (PM_NODE_TYPE(n)) {
  case PM_GLOBAL_VARIABLE_READ_NODE:
    return fx_gvar_get(sp_sym_to_s(fx_sym(((const pm_global_variable_read_node_t *)n)->name)));
  case PM_GLOBAL_VARIABLE_WRITE_NODE: {
    const pm_global_variable_write_node_t *w = (const pm_global_variable_write_node_t *)n;
    sp_RbVal v = fx_eval(f, w->value);
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
    SP_GC_ROOT_RBVAL(v);
    fx_gvar_set(sp_sym_to_s(fx_sym(w->name)), v);
    return v;
  }
  case PM_GLOBAL_VARIABLE_OPERATOR_WRITE_NODE: {
    const pm_global_variable_operator_write_node_t *w = (const pm_global_variable_operator_write_node_t *)n;
    const char *name = sp_sym_to_s(fx_sym(w->name));
    /* $x OP= v reads $x before v runs, as CRuby does */
    sp_RbVal cur = fx_gvar_get(name);
    SP_GC_ROOT_RBVAL(cur);
    sp_RbVal v = fx_eval(f, w->value);
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
    v = fx_op(cur, w->binary_operator, v);
    SP_GC_ROOT_RBVAL(v);
    fx_gvar_set(name, v);
    return v;
  }
  case PM_GLOBAL_VARIABLE_OR_WRITE_NODE:
  case PM_GLOBAL_VARIABLE_AND_WRITE_NODE: {
    /* the two nodes have the same fields */
    const pm_global_variable_or_write_node_t *w = (const pm_global_variable_or_write_node_t *)n;
    const char *name = sp_sym_to_s(fx_sym(w->name));
    sp_RbVal cur = fx_gvar_get(name);
    int or = PM_NODE_TYPE_P(n, PM_GLOBAL_VARIABLE_OR_WRITE_NODE);
    if (sp_poly_truthy(cur) == or) return cur;
    sp_RbVal v = fx_eval(f, w->value);
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
    SP_GC_ROOT_RBVAL(v);
    fx_gvar_set(name, v);
    return v;
  }
  default:
    fx_unsupported(n, "node");
  }
}

/* STDIN, STDOUT and STDERR: the streams that $stdin and the others start as. */
/* ---- ENV ---- */

/* ENV: an Object whose methods read and write the environment of the
   process, as the ENV arms of spinel codegen do. Any other method reads a
   Hash snapshot of the environment, as spinel desugars it (sp_env_to_h).
   The constant ENV keeps the object alive. */
static sp_RbVal fx_env;

static int fx_is_env(sp_RbVal r) {
  return r.tag == SP_TAG_OBJ && r.v.p && r.v.p == fx_env.v.p;
}

static const char *fx_env_key(sp_RbVal k) {
  return sp_env_chk(sp_poly_arg_str_chk(k), 0);
}

static sp_RbVal fx_env_get(const char *k) {
  const char *v = getenv(k);
  return v ? sp_box_str(sp_env_str(v)) : sp_box_nil();
}

/* A Hash of the environment, in the order of environ. */
static sp_RbVal fx_env_hash(void) {
  sp_StrStrHash *s = sp_env_to_h();
  SP_GC_ROOT(s);
  sp_PolyPolyHash *h = sp_PolyPolyHash_new();
  SP_GC_ROOT(h);
  for (sp_int i = 0; i < s->len; i++)
    sp_PolyPolyHash_set(h, sp_box_str(s->order[i]), sp_box_str(sp_StrStrHash_get(s, s->order[i])));
  return fx_hash_box(h);
}

/* ENV.update, merge! and replace with a Hash. replace then deletes each
   variable that the Hash does not name. */
static void fx_env_update(sp_RbVal hv, int replace) {
  sp_PolyPolyHash *h = sp_poly_as_poly_poly_hash(hv);
  SP_GC_ROOT(h);
  for (sp_int i = 0; i < h->len; i++)
    sp_env_aset(fx_env_key(h->keys[h->order[i]]), h->vals[h->order[i]]);
  if (!replace) return;
  sp_StrStrHash *s = sp_env_to_h();
  SP_GC_ROOT(s);
  for (sp_int i = 0; i < s->len; i++)
    if (!sp_PolyPolyHash_has_key(h, sp_box_str(s->order[i]))) unsetenv(s->order[i]);
}

static int fx_env_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                         sp_RbVal *out) {
  if (!fx_is_env(r)) return 0;
  if (argc == 1 && FX_IS("[]")) { *out = fx_env_get(fx_env_key(argv[0])); return 1; }
  if ((argc == 1 || argc == 2) && FX_IS("fetch")) {
    *out = fx_env_get(fx_env_key(argv[0]));
    if (out->tag != SP_TAG_NIL) return 1;
    if (blk) { *out = fx_yield(blk, 1, argv); return 1; }
    if (argc == 2) { *out = argv[1]; return 1; }
    sp_exc_stage_recv(r);
    sp_exc_stage_key(argv[0]);
    sp_raise_cls("KeyError", sp_sprintf("key not found: %s", sp_poly_inspect(argv[0])));
  }
  if (argc == 2 && (FX_IS("[]=") || FX_IS("store"))) {
    sp_env_aset(fx_env_key(argv[0]), argv[1]);
    *out = argv[1];
    return 1;
  }
  if (argc == 1 && (FX_IS("key?") || FX_IS("has_key?") || FX_IS("include?") || FX_IS("member?"))) {
    *out = sp_box_bool(getenv(fx_env_key(argv[0])) != NULL);
    return 1;
  }
  if (argc == 1 && FX_IS("delete")) {
    const char *k = fx_env_key(argv[0]);
    SP_GC_ROOT_STR(k);
    *out = fx_env_get(k);
    if (out->tag == SP_TAG_NIL) {
      if (blk) *out = fx_yield(blk, 1, argv);
      return 1;
    }
    unsetenv(k);
    return 1;
  }
  if (argc == 1 && !blk && fx_is_hash(argv[0]) &&
      (FX_IS("update") || FX_IS("merge!") || FX_IS("replace"))) {
    fx_env_update(argv[0], FX_IS("replace"));
    *out = r;
    return 1;
  }
  if (argc == 0 && !blk) {
    if (FX_IS("to_s")) { *out = sp_box_str_new(sp_str_dup("ENV")); return 1; }
    if (FX_IS("size") || FX_IS("length")) { *out = sp_box_int(sp_env_size()); return 1; }
    if (FX_IS("empty?")) { *out = sp_box_bool(sp_env_size() == 0); return 1; }
    if (FX_IS("clear")) { sp_env_clear(); *out = r; return 1; }
  }
  /* what an Object answers for itself */
  if (FX_IS("class") || FX_IS("is_a?") || FX_IS("kind_of?") || FX_IS("instance_of?") ||
      FX_IS("equal?") || FX_IS("object_id") || FX_IS("nil?") || FX_IS("frozen?"))
    return 0;
  sp_RbVal snap = fx_env_hash();
  SP_GC_ROOT_RBVAL(snap);
  *out = fx_send_b(snap, name, argc, argv, blk);
  /* each and its kin answer ENV itself */
  if (!strncmp(name, "each", 4) && blk) *out = r;
  return 1;
}

/* A frozen String, as the String constants of CRuby are. */
static sp_RbVal fx_frozen_cstr(const char *s) {
  sp_RbVal v = fx_new_string((const uint8_t *)s, strlen(s));
  SP_GC_ROOT_RBVAL(v);
  return fx_send(v, "freeze", 0, NULL);
}

/* The constants of the builtin classes, with the values that spinel gives
   a program ("well-known module constants" of codegen_expr.c). */
static void fx_builtin_consts(void) {
  FxClass *fl = fx_builtin("Float");
  const struct { const char *name; double v; } floats[] = {
    { "MAX", DBL_MAX }, { "MIN", DBL_MIN }, { "EPSILON", DBL_EPSILON },
    { "INFINITY", 1.0 / 0.0 }, { "NAN", 0.0 / 0.0 },
  };
  for (size_t i = 0; i < sizeof floats / sizeof floats[0]; i++)
    fx_const_set(fl, sp_sym_intern(floats[i].name), sp_box_float(floats[i].v));
  const struct { const char *name; sp_int v; } float_ints[] = {
    { "DIG", DBL_DIG }, { "MANT_DIG", DBL_MANT_DIG }, { "RADIX", FLT_RADIX },
    { "MAX_EXP", DBL_MAX_EXP }, { "MIN_EXP", DBL_MIN_EXP },
    { "MAX_10_EXP", DBL_MAX_10_EXP }, { "MIN_10_EXP", DBL_MIN_10_EXP },
  };
  for (size_t i = 0; i < sizeof float_ints / sizeof float_ints[0]; i++)
    fx_const_set(fl, sp_sym_intern(float_ints[i].name), sp_box_int(float_ints[i].v));

  FxClass *math = fx_builtin("Math");
  fx_const_set(math, sp_sym_intern("PI"), sp_box_float(M_PI));
  fx_const_set(math, sp_sym_intern("E"), sp_box_float(M_E));

  /* IO#seek whence, in IO and in File, as spinel takes them of both
     (is_io_class_name): the File of Fluxpinel is no subclass of IO */
  FxClass *file = fx_builtin("File");
  FxClass *ios[] = { fx_builtin("IO"), file };
  const char *const seeks[] = { "SEEK_SET", "SEEK_CUR", "SEEK_END" };
  for (int k = 0; k < 2; k++)
    for (int i = 0; i < 3; i++) fx_const_set(ios[k], sp_sym_intern(seeks[i]), sp_box_int(i));

  fx_const_set(file, sp_sym_intern("SEPARATOR"), fx_frozen_cstr("/"));
  fx_const_set(file, sp_sym_intern("PATH_SEPARATOR"), fx_frozen_cstr(":"));
  fx_const_set(file, sp_sym_intern("NULL"), fx_frozen_cstr("/dev/null"));
  fx_const_set(file, sp_sym_intern("ALT_SEPARATOR"), sp_box_nil());
  /* the flags of open(2) and flock(2), and the flags of File.fnmatch with
     the values of CRuby (SP_FNM_*) */
  const struct { const char *name; sp_int v; } file_ints[] = {
    { "RDONLY", O_RDONLY }, { "WRONLY", O_WRONLY }, { "RDWR", O_RDWR },
    { "CREAT", O_CREAT }, { "EXCL", O_EXCL }, { "TRUNC", O_TRUNC },
    { "APPEND", O_APPEND }, { "NONBLOCK", O_NONBLOCK }, { "NOFOLLOW", O_NOFOLLOW },
    { "NOCTTY", O_NOCTTY }, { "SYNC", O_SYNC }, { "DSYNC", O_DSYNC }, { "BINARY", 0 },
    { "LOCK_SH", LOCK_SH }, { "LOCK_EX", LOCK_EX }, { "LOCK_UN", LOCK_UN }, { "LOCK_NB", LOCK_NB },
    { "FNM_NOESCAPE", SP_FNM_NOESCAPE }, { "FNM_PATHNAME", SP_FNM_PATHNAME },
    { "FNM_DOTMATCH", SP_FNM_DOTMATCH }, { "FNM_CASEFOLD", SP_FNM_CASEFOLD },
    { "FNM_EXTGLOB", SP_FNM_EXTGLOB }, { "FNM_SYSCASE", 0 }, { "FNM_SHORTNAME", 0 },
  };
  for (size_t i = 0; i < sizeof file_ints / sizeof file_ints[0]; i++)
    fx_const_set(file, sp_sym_intern(file_ints[i].name), sp_box_int(file_ints[i].v));

  /* the encoding bits that Regexp#options reports; re.c sets the others */
  FxClass *re = fx_builtin("Regexp");
  fx_const_set(re, sp_sym_intern("FIXEDENCODING"), sp_box_int(16));
  fx_const_set(re, sp_sym_intern("NOENCODING"), sp_box_int(32));
}

static sp_File *fx_hook_to_io(sp_RbVal v);   /* the IO methods below */

static void fx_global_init(void) {
  sp_user_to_io_hook = fx_hook_to_io;
  fx_const_set(L.object, sp_sym_intern("STDIN"), fx_io_box(sp_io_stdin()));
  fx_const_set(L.object, sp_sym_intern("STDOUT"), fx_io_box(sp_io_stdout()));
  fx_const_set(L.object, sp_sym_intern("STDERR"), fx_io_box(sp_io_stderr()));
  fx_env = fx_new_object(L.object);
  fx_const_set(L.object, sp_sym_intern("ENV"), fx_env);
  /* spinel loads the package of Set when a program names it, with no
     require (sp_require_preloaded of spinel_parse.c), and splices its
     builtins/*.rb for RbConfig and Gem, which CRuby has before the program
     (sp_splice_named_builtin) */
  fx_autoload_set(L.object, sp_sym_intern("Set"), "set");
  fx_autoload_set(L.object, sp_sym_intern("RbConfig"), FX_BUILTINS_DIR "/rbconfig.rb");
  fx_autoload_set(L.object, sp_sym_intern("Gem"), FX_BUILTINS_DIR "/gem.rb");
  /* ARGV: the arguments after the path, as frozen Strings (CRuby 4.0) */
  sp_PolyArray *argv = sp_PolyArray_new();
  SP_GC_ROOT(argv);
  for (int i = 0; i < L.nargs; i++) {
    sp_RbVal s = fx_new_string((const uint8_t *)L.args[i], strlen(L.args[i]));
    sp_PolyArray_push(argv, fx_send(s, "freeze", 0, NULL));
  }
  fx_const_set(L.object, sp_sym_intern("ARGV"), sp_box_poly_array(argv));
  /* the engine constants, frozen Strings with the values that spinel gives
     a program (emit_engine_const_str of codegen_expr.c), but the
     description of Fluxpinel itself */
  const char *const engine[][2] = {
    { "RUBY_VERSION", FX_RUBY_VERSION },
    { "RUBY_ENGINE", "spinel" },
    { "RUBY_ENGINE_VERSION", FX_RUBY_VERSION },
    { "RUBY_PLATFORM", SP_RUBY_ARCH "-" SP_RUBY_OS },
    { "RUBY_RELEASE_DATE", FX_RUBY_RELEASE_DATE },
    { "RUBY_REVISION", FX_RUBY_REVISION },
    { "RUBY_COPYRIGHT", FX_RUBY_COPYRIGHT },
    { "RUBY_DESCRIPTION", FX_DESCRIPTION },
  };
  for (size_t i = 0; i < sizeof engine / sizeof engine[0]; i++)
    fx_const_set(L.object, sp_sym_intern(engine[i][0]), fx_frozen_cstr(engine[i][1]));
  fx_const_set(L.object, sp_sym_intern("RUBY_PATCHLEVEL"), sp_box_int(0));
  fx_builtin_consts();
  /* the modules that a nonblocking error includes, which the class table
     of the runtime does not name */
  FxClass *io = fx_builtin("IO");
  const char *const waits[] = { "WaitReadable", "WaitWritable" };
  for (int i = 0; i < 2; i++) {
    FxClass *m = fx_class_new(strdup(sp_sprintf("IO::%s", waits[i])), 1, NULL, io);
    fx_const_set(io, sp_sym_intern(waits[i]), fx_class_box(m));
  }
}

/* ---- IO ---- */

/* The writes of an IO that spinel emits from codegen code. */
/* The IO of an object of the program for the runtime (sp_user_to_io_hook):
   the answer of its to_io, as IO.select and the IO methods ask of a
   wrapper. NULL for an object with no to_io. */
static sp_File *fx_hook_to_io_u(sp_RbVal v) {
  if (!fx_find_method(fx_lookup_class(v), sp_sym_intern("to_io"))) return NULL;
  sp_RbVal io = fx_send(v, "to_io", 0, NULL);
  return fx_is_io(io) ? (sp_File *)io.v.p : NULL;
}

static sp_File *fx_hook_to_io(sp_RbVal v) {
  if (!fx_is_fx_object(v) || fx_is_native_obj(v)) return NULL;
  sp_File *r;
  FX_HOOKED(r = fx_hook_to_io_u(v));
  return r;
}

/* IO.select and Kernel#select(read, write = nil, error = nil, timeout =
   nil), as codegen emits the boxed form (emit_io_select): each set nil or
   an Array, the timeout nil (no limit) or an interval. The interval reads
   as sp_io_wait_interval reads it, so a negative one is ArgumentError, as
   in CRuby, and does not mean "no limit". Answers [readers, writers,
   errors], or nil at the timeout. */
static sp_RbVal fx_io_select(int argc, sp_RbVal *argv) {
  sp_PolyArray *sets[3] = { NULL, NULL, NULL };
  for (int k = 0; k < 3 && k < argc; k++) {
    sp_RbVal v = argv[k];
    if (v.tag == SP_TAG_NIL) continue;
    if (v.tag == SP_TAG_OBJ && sp_poly_is_array_kind(v.cls_id)) sets[k] = sp_poly_to_poly_array(v);
    else sp_bm_arg_mismatch(v, "Array");
  }
  SP_GC_ROOT(sets[0]);
  SP_GC_ROOT(sets[1]);
  SP_GC_ROOT(sets[2]);
  double timeout = argc < 4 || argv[3].tag == SP_TAG_NIL ? -1.0 : sp_io_wait_interval(argv[3]);
  sp_RbVal r;
  FX_UNLOCKED(r = sp_io_select(sets[0], sets[1], sets[2], timeout));
  return r;
}

static int fx_io_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (!fx_is_io(r)) return 0;
  sp_File *io = (sp_File *)r.v.p;
  /* external_encoding: what set_encoding chose (ext_enc), else BINARY after
     binmode, else the default external encoding for a handle that only
     reads, and nil for one that can write, as CRuby answers. The runtime
     converts nothing, so internal_encoding is nil. */
  if (argc == 0 && FX_IS("external_encoding")) {
    const char *m = io->mode ? io->mode : "r";
    int writable = strchr(m, 'w') || strchr(m, 'a') || strchr(m, '+');
    if (io->ext_enc == 2 || (!io->ext_enc && sp_File_binmode_p(io))) *out = sp_box_encoding(sp_encoding_binary());
    else if (io->ext_enc == 1 || !writable) *out = sp_box_encoding(sp_encoding_utf8());
    else *out = sp_box_nil();
    return 1;
  }
  if (argc == 0 && FX_IS("internal_encoding")) { *out = sp_box_nil(); return 1; }
  /* io/console, once it is required: winsize and winsize=, from the
     terminal (Errno::ENOTTY for a pipe or a file, as CRuby raises) */
  if (fx_console_loaded && argc == 0 && FX_IS("winsize")) {
    *out = sp_box_int_array(sp_File_winsize(io));
    return 1;
  }
  if (fx_console_loaded && argc == 1 && FX_IS("winsize=")) {
    if (!fx_is_array(argv[0]) || (sp_poly_length(argv[0]) != 2 && sp_poly_length(argv[0]) != 4))
      sp_raise_cls("ArgumentError", sp_sprintf("wrong number of arguments (given %d, expected 2 or 4)",
                                               fx_is_array(argv[0]) ? (int)sp_poly_length(argv[0]) : 1));
    sp_File_set_winsize(io, sp_poly_as_int_array(argv[0]));
    *out = argv[0];
    return 1;
  }
  /* a read or a write of a socket, a pipe or a terminal parks the thread:
     the walker lock is given up for its time (FX_UNLOCKED) */
  if (FX_IS("puts")) {
    if (argc == 0) FX_UNLOCKED(sp_File_puts(io, ""));
    for (int i = 0; i < argc; i++) FX_UNLOCKED(sp_File_puts_val(io, argv[i]));
    *out = sp_box_nil();
    return 1;
  }
  if (FX_IS("print")) {
    for (int i = 0; i < argc; i++) FX_UNLOCKED(sp_File_write_poly(io, argv[i]));
    *out = sp_box_nil();
    return 1;
  }
  if (FX_IS("write")) {
    sp_int n = 0;
    for (int i = 0; i < argc; i++) FX_UNLOCKED(n += sp_File_write_poly(io, argv[i]));
    *out = sp_box_int(n);
    return 1;
  }
  if (FX_IS("<<") && argc == 1) {
    FX_UNLOCKED(sp_File_write_poly(io, argv[0]));
    *out = r;
    return 1;
  }
  /* read(length[, buffer]), gets(sep[, limit], chomp:), readline(...) and
     readlines(...): the arguments read as CRuby reads them
     (sp_io_read_splat, sp_io_line_args) */
  int kw = argc > 0 && fx_is_hash(argv[argc - 1]) && argv[argc - 1].v.p == L.kwhash;
  if (FX_IS("read") && argc >= 1 && !kw) {
    sp_PolyArray *a = sp_PolyArray_new();
    SP_GC_ROOT(a);
    for (int i = 0; i < argc; i++) sp_PolyArray_push(a, argv[i]);
    const char *s;
    FX_UNLOCKED(s = sp_io_read_splat(io, a));
    *out = s ? sp_box_str_new(s) : sp_box_nil();
    return 1;
  }
  /* wait_readable(t), wait_writable(t), wait_priority(t) and wait(t, kind),
     as codegen emits them (emit_io_wait): the IO when it is ready, nil at
     the timeout */
  if (!kw && argc <= 2 && (FX_IS("wait_readable") || FX_IS("wait_writable") || FX_IS("wait_priority") || FX_IS("wait"))) {
    if (argc == 2 && !FX_IS("wait")) return 0;
    sp_RbVal t = argc >= 1 ? argv[0] : sp_box_nil();
    if (FX_IS("wait_writable")) {
      sp_File *w;
      FX_UNLOCKED(w = sp_io_wait_writable_timeout(io, t));
      *out = sp_box_nullable_obj(w, SP_BUILTIN_IO);
      return 1;
    }
    int ev = FX_IS("wait_priority") ? 2 : 0;
    if (argc == 2) {
      if (argv[1].tag != SP_TAG_SYM) return 0;
      const char *k = sp_sym_to_s((sp_sym)argv[1].v.i);
      ev = !strcmp(k, "write") ? 1 : !strcmp(k, "priority") ? 2 : !strcmp(k, "read_write") ? 3 : 0;
    }
    int plain = FX_IS("wait") && argc > 0;
    FX_UNLOCKED(*out = sp_io_wait_timeout_value(io, t, ev, plain));
    return 1;
  }
  /* pread(len, offset), pwrite(str, offset), readpartial(n), sysread(n)
     and syswrite(str), with the checks of the runtime; the forms with an
     output buffer are not here */
  if (!kw && argc == 2 && FX_IS("pread")) {
    sp_int len = sp_poly_arg_int_chk(argv[0]), off = sp_poly_arg_int_chk(argv[1]);
    const char *s;
    FX_UNLOCKED(s = sp_File_pread_checked(io, len, off));
    *out = sp_box_str_new(s);
    return 1;
  }
  if (!kw && argc == 2 && FX_IS("pwrite")) {
    /* a String writes all of its bytes, NUL too, as IO#write does */
    int str = fx_is_string(argv[0]);
    const char *s = str ? sp_poly_arg_str_chk(argv[0]) : sp_poly_to_s(argv[0]);
    SP_GC_ROOT_STR(s);
    sp_int off = sp_poly_arg_int_chk(argv[1]), n;
    FX_UNLOCKED(n = str ? sp_File_pwrite_bin(io, s, off) : sp_File_pwrite(io, s, off));
    *out = sp_box_int(n);
    return 1;
  }
  if (!kw && argc == 1 && (FX_IS("readpartial") || FX_IS("sysread"))) {
    sp_int n = sp_poly_arg_int_chk(argv[0]);
    const char *s;
    FX_UNLOCKED(s = name[0] == 'r' ? sp_File_readpartial_checked(io, n) : sp_File_sysread_checked(io, n));
    *out = sp_box_str_new(s);
    return 1;
  }
  if (!kw && argc == 1 && FX_IS("syswrite")) {
    const char *s = sp_poly_to_s(argv[0]);
    SP_GC_ROOT_STR(s);
    sp_int n;
    FX_UNLOCKED(n = sp_File_syswrite(io, s, sp_str_byte_len(s)));
    *out = sp_box_int(n);
    return 1;
  }
  if ((FX_IS("gets") && argc >= 1) || FX_IS("readlines") || FX_IS("readline")) {
    int chomp = 0;
    if (kw) {
      sp_PolyPolyHash *h = sp_poly_as_poly_poly_hash(argv[argc - 1]);
      sp_RbVal kc = sp_box_sym(sp_sym_intern("chomp"));
      if (h->len != 1 || !sp_PolyPolyHash_has_key(h, kc)) return 0;
      chomp = sp_poly_truthy(sp_PolyPolyHash_get(h, kc));
      argc--;
    }
    sp_PolyArray *a = sp_PolyArray_new();
    SP_GC_ROOT(a);
    for (int i = 0; i < argc; i++) sp_PolyArray_push(a, argv[i]);
    const char *sep;
    if (FX_IS("gets")) {
      sp_oint lim = sp_io_gets_args(a, &sep);
      const char *s;
      FX_UNLOCKED(s = sp_File_gets_limit(io, sep, lim, chomp));
      *out = s ? sp_box_str_new(s) : sp_box_nil();
      return 1;
    }
    /* readline: gets, with EOFError at the end */
    if (FX_IS("readline")) {
      sp_oint lim = sp_io_gets_args(a, &sep);
      const char *s;
      FX_UNLOCKED(s = sp_File_readline_limit(io, sep, lim, chomp));
      *out = sp_box_str_new(s);
      return 1;
    }
    sp_int lim;
    sp_io_line_args(a, &sep, &lim, "readlines");
    SP_GC_ROOT_STR(sep);
    sp_StrArray *lines = sp_StrArray_new();
    SP_GC_ROOT(lines);
    for (const char *s;;) {
      FX_UNLOCKED(s = sp_File_gets_sep(io, sep, lim, chomp));
      if (!s) break;
      sp_StrArray_push(lines, s);
    }
    *out = sp_box_nullable_obj((void *)lines, SP_BUILTIN_STR_ARRAY);
    return 1;
  }
  if (FX_IS("sync") && argc == 0) { *out = sp_box_bool(sp_File_sync_p(io)); return 1; }
  if (FX_IS("sync=") && argc == 1) {
    sp_File_set_sync(io, sp_poly_truthy(argv[0]));
    *out = argv[0];
    return 1;
  }
  return 0;
}

#undef FX_IS
