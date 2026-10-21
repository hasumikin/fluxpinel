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
  if (fx_is_fx_object(v)) return fx_find_method(fx_class_of(v), sp_sym_intern("write")) != NULL;
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
    sp_RbVal v = fx_eval(f, w->value);
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
    v = fx_op(fx_gvar_get(name), w->binary_operator, v);
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

static void fx_global_init(void) {
  fx_const_set(L.object, sp_sym_intern("STDIN"), fx_io_box(sp_io_stdin()));
  fx_const_set(L.object, sp_sym_intern("STDOUT"), fx_io_box(sp_io_stdout()));
  fx_const_set(L.object, sp_sym_intern("STDERR"), fx_io_box(sp_io_stderr()));
  fx_env = fx_new_object(L.object);
  fx_const_set(L.object, sp_sym_intern("ENV"), fx_env);
  /* ARGV: the arguments after the path, as frozen Strings (CRuby 4.0) */
  sp_PolyArray *argv = sp_PolyArray_new();
  SP_GC_ROOT(argv);
  for (int i = 0; i < L.nargs; i++) {
    sp_RbVal s = fx_new_string((const uint8_t *)L.args[i], strlen(L.args[i]));
    sp_PolyArray_push(argv, fx_send(s, "freeze", 0, NULL));
  }
  fx_const_set(L.object, sp_sym_intern("ARGV"), sp_box_poly_array(argv));
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
static int fx_io_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (!fx_is_io(r)) return 0;
  sp_File *io = (sp_File *)r.v.p;
  if (FX_IS("puts")) {
    if (argc == 0) sp_File_puts(io, "");
    for (int i = 0; i < argc; i++) sp_File_puts_val(io, argv[i]);
    *out = sp_box_nil();
    return 1;
  }
  if (FX_IS("print")) {
    for (int i = 0; i < argc; i++) sp_File_write_poly(io, argv[i]);
    *out = sp_box_nil();
    return 1;
  }
  if (FX_IS("write")) {
    sp_int n = 0;
    for (int i = 0; i < argc; i++) n += sp_File_write_poly(io, argv[i]);
    *out = sp_box_int(n);
    return 1;
  }
  if (FX_IS("<<") && argc == 1) {
    sp_File_write_poly(io, argv[0]);
    *out = r;
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
