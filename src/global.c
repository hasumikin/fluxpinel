/* global.c -- global variables and the IO methods of the standard streams.

   A global variable of the program lives in a table of Lupe (L.globals),
   which the mark hook of object.c marks. A global that no code has
   assigned reads nil.

   A special global reads the state of the runtime, as the code that
   spinel emits does: the standard streams, $?, $0, $LOAD_PATH, ... An
   assignment to a writable special global goes into the table, and a read
   prefers the table. After `$stdout = obj`, Kernel#puts, #print and #p
   write to obj. */

#include "lupe.h"

#define LUPE_IS(s) (!strcmp(name, (s)))

/* The name that an alias stands for. */
static const char *lupe_gvar_canon(const char *name) {
  if (LUPE_IS("$-v") || LUPE_IS("$-w")) return "$VERBOSE";
  if (LUPE_IS("$-d")) return "$DEBUG";
  if (LUPE_IS("$PROGRAM_NAME")) return "$0";
  if (LUPE_IS("$:") || LUPE_IS("$-I")) return "$LOAD_PATH";
  if (LUPE_IS("$\"")) return "$LOADED_FEATURES";
  return name;
}

static LupeConst *lupe_gvar_slot(sp_sym s) {
  for (int i = 0; i < L.nglobals; i++)
    if (L.globals[i].name == s) return &L.globals[i];
  return NULL;
}

static sp_RbVal lupe_io_box(sp_File *f) {
  return sp_box_obj(f, SP_BUILTIN_IO);
}

static int lupe_is_io(sp_RbVal v) {
  return v.tag == SP_TAG_OBJ && v.cls_id == SP_BUILTIN_IO && v.v.p;
}

static sp_RbVal lupe_gvar_get(const char *name) {
  name = lupe_gvar_canon(name);
  LupeConst *g = lupe_gvar_slot(sp_sym_intern(name));
  if (g) return g->value;
  if (LUPE_IS("$stdout")) return lupe_io_box(sp_io_stdout());
  if (LUPE_IS("$stderr")) return lupe_io_box(sp_io_stderr());
  if (LUPE_IS("$stdin")) return lupe_io_box(sp_io_stdin());
  if (LUPE_IS("$/")) return lupe_new_string((const uint8_t *)"\n", 1);
  if (LUPE_IS("$?")) return sp_box_nullable_obj((void *)sp_last_process_status(), SP_BUILTIN_PROCESS_STATUS);
  if (LUPE_IS("$$")) return sp_box_int((sp_int)getpid());
  if (LUPE_IS("$0")) return lupe_new_string((const uint8_t *)L.path, strlen(L.path));
  if (LUPE_IS("$LOAD_PATH")) return sp_box_nullable_obj((void *)sp_get_load_path(), SP_BUILTIN_STR_ARRAY);
  if (LUPE_IS("$LOADED_FEATURES"))
    return sp_box_nullable_obj((void *)sp_get_loaded_features(), SP_BUILTIN_STR_ARRAY);
  if (LUPE_IS("$VERBOSE") || LUPE_IS("$DEBUG")) return sp_box_bool(0);
  return sp_box_nil();
}

/* Whether v has a write method, as CRuby asks of $stdout and $stderr. */
static int lupe_gvar_writable(sp_RbVal v) {
  if (lupe_is_io(v)) return 1;
  if (lupe_is_lupe_object(v)) return lupe_find_method(lupe_class_of(v), sp_sym_intern("write")) != NULL;
  return lupe_builtin_responds(v, "write");
}

static void lupe_gvar_set(const char *name, sp_RbVal v) {
  name = lupe_gvar_canon(name);
  if (LUPE_IS("$?") || LUPE_IS("$$") || LUPE_IS("$LOAD_PATH") || LUPE_IS("$LOADED_FEATURES"))
    sp_raise_cls("NameError", sp_sprintf("%s is a read-only variable", name));
  if ((LUPE_IS("$stdout") || LUPE_IS("$stderr")) && !lupe_gvar_writable(v))
    sp_raise_cls("TypeError", sp_sprintf("%s must have write method, %s given", name,
                                         v.tag == SP_TAG_NIL ? "NilClass" : sp_poly_class_name(v)));
  sp_sym s = sp_sym_intern(name);
  LupeConst *g = lupe_gvar_slot(s);
  if (g) { g->value = v; return; }
  if (L.nglobals == L.globals_cap) {
    L.globals_cap = L.globals_cap ? L.globals_cap * 2 : 16;
    L.globals = realloc(L.globals, sizeof *L.globals * (size_t)L.globals_cap);
  }
  L.globals[L.nglobals++] = (LupeConst){ s, v };
}

/* The object that Kernel#puts writes to, when the program has assigned
   $stdout another one than the standard output. */
static int lupe_gvar_stdout(sp_RbVal *out) {
  LupeConst *g = lupe_gvar_slot(sp_sym_intern("$stdout"));
  if (!g || (lupe_is_io(g->value) && g->value.v.p == (void *)sp_io_stdout())) return 0;
  *out = g->value;
  return 1;
}

/* The global variable nodes. */
static sp_RbVal lupe_gvar_eval(LupeFrame *f, const pm_node_t *n) {
  switch (PM_NODE_TYPE(n)) {
  case PM_GLOBAL_VARIABLE_READ_NODE:
    return lupe_gvar_get(sp_sym_to_s(lupe_sym(((const pm_global_variable_read_node_t *)n)->name)));
  case PM_GLOBAL_VARIABLE_WRITE_NODE: {
    const pm_global_variable_write_node_t *w = (const pm_global_variable_write_node_t *)n;
    sp_RbVal v = lupe_eval(f, w->value);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    SP_GC_ROOT_RBVAL(v);
    lupe_gvar_set(sp_sym_to_s(lupe_sym(w->name)), v);
    return v;
  }
  case PM_GLOBAL_VARIABLE_OPERATOR_WRITE_NODE: {
    const pm_global_variable_operator_write_node_t *w = (const pm_global_variable_operator_write_node_t *)n;
    const char *name = sp_sym_to_s(lupe_sym(w->name));
    sp_RbVal v = lupe_eval(f, w->value);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    v = lupe_op(lupe_gvar_get(name), w->binary_operator, v);
    SP_GC_ROOT_RBVAL(v);
    lupe_gvar_set(name, v);
    return v;
  }
  case PM_GLOBAL_VARIABLE_OR_WRITE_NODE:
  case PM_GLOBAL_VARIABLE_AND_WRITE_NODE: {
    /* the two nodes have the same fields */
    const pm_global_variable_or_write_node_t *w = (const pm_global_variable_or_write_node_t *)n;
    const char *name = sp_sym_to_s(lupe_sym(w->name));
    sp_RbVal cur = lupe_gvar_get(name);
    int or = PM_NODE_TYPE_P(n, PM_GLOBAL_VARIABLE_OR_WRITE_NODE);
    if (sp_poly_truthy(cur) == or) return cur;
    sp_RbVal v = lupe_eval(f, w->value);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    SP_GC_ROOT_RBVAL(v);
    lupe_gvar_set(name, v);
    return v;
  }
  default:
    lupe_unsupported(n, "node");
  }
}

/* ---- IO ---- */

/* The writes of an IO that spinel emits from codegen code. */
static int lupe_io_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (!lupe_is_io(r)) return 0;
  sp_File *io = (sp_File *)r.v.p;
  if (LUPE_IS("puts")) {
    if (argc == 0) sp_File_puts(io, "");
    for (int i = 0; i < argc; i++) sp_File_puts_val(io, argv[i]);
    *out = sp_box_nil();
    return 1;
  }
  if (LUPE_IS("print")) {
    for (int i = 0; i < argc; i++) sp_File_write_poly(io, argv[i]);
    *out = sp_box_nil();
    return 1;
  }
  if (LUPE_IS("write")) {
    sp_int n = 0;
    for (int i = 0; i < argc; i++) n += sp_File_write_poly(io, argv[i]);
    *out = sp_box_int(n);
    return 1;
  }
  if (LUPE_IS("<<") && argc == 1) {
    sp_File_write_poly(io, argv[0]);
    *out = r;
    return 1;
  }
  if (LUPE_IS("sync") && argc == 0) { *out = sp_box_bool(sp_File_sync_p(io)); return 1; }
  if (LUPE_IS("sync=") && argc == 1) {
    sp_File_set_sync(io, sp_poly_truthy(argv[0]));
    *out = argv[0];
    return 1;
  }
  return 0;
}

#undef LUPE_IS
