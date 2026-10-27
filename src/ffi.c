/* ffi.c -- the FFI of spinel at run time.

   spinel reads ffi_func and its kin at compile time and emits a direct C
   call for each function (docs/FFI.md of spinel). Fluxpinel meets the same
   declarations in a module body at run time: it opens each ffi_lib with
   dlopen, finds each function with dlsym at its first call, and calls it
   through libffi with the C types that the declaration names.

   A package of spinel binds a C function of the runtime or of its own
   object with native_func. Fluxpinel calls it the same way: the Makefile links
   the objects of the packages, and keeps each bound function of the
   runtime in the link (tools/native_link.rb).

   A declared function is a singleton method of the module (FX_M_FFI),
   so a call, respond_to? and method lookup find it as they find a def.

   A :ptr is an SP_BUILTIN_FOREIGN_PTR box, which the collector does not
   follow, and NULL is nil, as spinel compares a NULL :ptr equal to nil.

   Built with FX_FFI=0 (no libffi), a declaration is a gap. */

#include "fluxpinel.h"

#define FX_IS(s) (!strcmp(name, (s)))

#if FX_FFI
#include <dlfcn.h>
#include <ffi.h>

typedef enum {
  FX_FT_INT, FX_FT_UINT32, FX_FT_INT32, FX_FT_UINT16, FX_FT_INT16, FX_FT_UINT8, FX_FT_INT8,
  FX_FT_SIZE_T, FX_FT_LONG, FX_FT_INT64, FX_FT_UINT64, FX_FT_FLOAT, FX_FT_DOUBLE, FX_FT_BOOL,
  FX_FT_STR, FX_FT_BINSTR, FX_FT_PTR, FX_FT_FLOAT_ARRAY, FX_FT_INT_ARRAY, FX_FT_VOID,
  /* the types of native_func only */
  FX_FT_ANY, FX_FT_RTSTR, FX_FT_NRTSTR, FX_FT_CSTR, FX_FT_CBINSTR, FX_FT_SPINT, FX_FT_SPBOOL,
  /* and of native_method */
  FX_FT_TEXT, FX_FT_REGEXP, FX_FT_SELF
} FxFfiType;

/* The type specs of docs/FFI.md, and the names of the ffi gem for them. */
static const struct {
  const char *name;
  FxFfiType type;
} fx_ffi_types[] = {
  { "int", FX_FT_INT }, { "uint32", FX_FT_UINT32 }, { "int32", FX_FT_INT32 },
  { "uint16", FX_FT_UINT16 }, { "int16", FX_FT_INT16 }, { "uint8", FX_FT_UINT8 }, { "int8", FX_FT_INT8 },
  { "size_t", FX_FT_SIZE_T }, { "long", FX_FT_LONG }, { "float", FX_FT_FLOAT }, { "double", FX_FT_DOUBLE },
  { "bool", FX_FT_BOOL }, { "str", FX_FT_STR }, { "binstr", FX_FT_BINSTR }, { "ptr", FX_FT_PTR },
  { "float_array", FX_FT_FLOAT_ARRAY }, { "int_array", FX_FT_INT_ARRAY }, { "void", FX_FT_VOID },
  { "uint", FX_FT_UINT32 }, { "ulong", FX_FT_UINT64 }, { "int64", FX_FT_INT64 }, { "uint64", FX_FT_UINT64 },
  { "short", FX_FT_INT16 }, { "ushort", FX_FT_UINT16 }, { "char", FX_FT_INT8 }, { "uchar", FX_FT_UINT8 },
  { "string", FX_FT_STR }, { "pointer", FX_FT_PTR }, { "buffer_in", FX_FT_PTR },
  { "buffer_out", FX_FT_PTR }, { "buffer_inout", FX_FT_PTR },
};

/* The type specs of native_func, a binding to a C function of the runtime
   or of a package (native_c_type in codegen_util.c of spinel):
   - :any is an sp_RbVal, by value.
   - :string is a String of the runtime, and string? (nstring) is one or
     NULL for nil.
   - A :cstring or :cbinstr return is the buffer of the callee, which the
     caller copies (the bytes of a :cbinstr are sp_ffi_bin_len).
   - :int is an sp_int, :float a double and :bool an sp_bool.
   native_method adds :text (a String, or the #to_s of anything else),
   :regexp (the compiled pattern) and the return :self (the receiver). */
static const struct {
  const char *name;
  FxFfiType type;
} fx_native_types[] = {
  { "any", FX_FT_ANY }, { "string", FX_FT_RTSTR }, { "string?", FX_FT_NRTSTR },
  { "nstring", FX_FT_NRTSTR }, { "cstring", FX_FT_CSTR }, { "cbinstr", FX_FT_CBINSTR },
  { "int", FX_FT_SPINT }, { "float", FX_FT_DOUBLE }, { "bool", FX_FT_SPBOOL },
  { "ptr", FX_FT_PTR }, { "nil", FX_FT_VOID }, { "void", FX_FT_VOID },
  { "text", FX_FT_TEXT }, { "regexp", FX_FT_REGEXP }, { "self", FX_FT_SELF },
};

/* sp_RbVal as libffi passes it: the tag, the class id and the union, which
   holds no member wider than 8 bytes. */
static ffi_type *fx_rbval_elements[] = { &ffi_type_sint32, &ffi_type_sint32, &ffi_type_sint64, NULL };
static ffi_type fx_rbval_type = { 0, 0, FFI_TYPE_STRUCT, fx_rbval_elements };

static ffi_type *fx_ffi_ctype(FxFfiType t) {
  switch (t) {
  case FX_FT_ANY: return &fx_rbval_type;
  case FX_FT_SPINT: return &ffi_type_sint64;
  case FX_FT_SPBOOL: return &ffi_type_uint8;
  case FX_FT_INT: case FX_FT_INT32: case FX_FT_BOOL: return &ffi_type_sint32;
  case FX_FT_UINT32: return &ffi_type_uint32;
  case FX_FT_INT16: return &ffi_type_sint16;
  case FX_FT_UINT16: return &ffi_type_uint16;
  case FX_FT_INT8: return &ffi_type_sint8;
  case FX_FT_UINT8: return &ffi_type_uint8;
  case FX_FT_LONG: return &ffi_type_slong;
  case FX_FT_SIZE_T: return &ffi_type_ulong;
  case FX_FT_INT64: return &ffi_type_sint64;
  case FX_FT_UINT64: return &ffi_type_uint64;
  case FX_FT_FLOAT: return &ffi_type_float;
  case FX_FT_DOUBLE: return &ffi_type_double;
  case FX_FT_VOID: return &ffi_type_void;
  default: return &ffi_type_pointer;
  }
}

enum { FX_FFI_FUNC, FX_FFI_BUFFER, FX_FFI_READ, FX_FFI_WRITE, FX_FFI_NATIVE };

typedef struct FxFfi {
  int kind;
  int native;             /* declared by native_func, native_new or native_method */
  int rest;               /* a native binding with a trailing :rest */
  /* FX_FFI_NATIVE: the bindings of one name of a native class, of which
     a call takes the one that fits its arguments */
  struct FxFfi **overloads;
  int noverloads;
  int ctor;               /* native_new: the first C argument is the class id */
  FxClass *cls;
  const char *name;       /* the C symbol of a function */
  void *fn;               /* found at the first call */
  int nargs;
  FxFfiType ret, *args;
  ffi_cif cif;
  ffi_type **atypes;
  void *buf;              /* FX_FFI_BUFFER */
  long offset;            /* FX_FFI_READ / WRITE */
  int width, sign;        /* the bytes of a field, and whether it sign-extends;
                             width 0 is a pointer */
} FxFfi;

static FxFfiType fx_ffi_type(sp_RbVal v) {
  if (v.tag == SP_TAG_SYM) {
    const char *s = sp_sym_to_s((sp_sym)v.v.i);
    for (size_t i = 0; i < sizeof fx_ffi_types / sizeof fx_ffi_types[0]; i++)
      if (!strcmp(fx_ffi_types[i].name, s)) return fx_ffi_types[i].type;
    fx_gap(sp_sprintf("the FFI type :%s", s));
  }
  sp_raise_cls("TypeError", sp_sprintf("%s is not an FFI type", sp_poly_inspect(v)));
}

static FxFfiType fx_native_type(sp_RbVal v) {
  if (v.tag == SP_TAG_SYM) {
    const char *s = sp_sym_to_s((sp_sym)v.v.i);
    for (size_t i = 0; i < sizeof fx_native_types / sizeof fx_native_types[0]; i++)
      if (!strcmp(fx_native_types[i].name, s)) return fx_native_types[i].type;
    fx_gap(sp_sprintf("the native_func type :%s", s));
  }
  sp_raise_cls("TypeError", sp_sprintf("%s is not a native_func type", sp_poly_inspect(v)));
}

static sp_RbVal fx_ptr_box(void *p) {
  return p ? sp_box_obj(p, SP_BUILTIN_FOREIGN_PTR) : sp_box_nil();
}

/* The address that a :ptr argument passes: a pointer, or NULL for nil. */
static void *fx_ptr_arg(sp_RbVal v) {
  if (v.tag == SP_TAG_NIL) return NULL;
  if (v.tag == SP_TAG_OBJ && v.cls_id == SP_BUILTIN_FOREIGN_PTR) return v.v.p;
  fx_gap(sp_sprintf("a %s as an FFI pointer", sp_poly_class_name(v)));
}

/* ffi_lib "name": libname.so, or a versioned one when only that exists. */
static void fx_ffi_open(const char *lib) {
  if (strchr(lib, '/') || strstr(lib, ".so")) {
    if (dlopen(lib, RTLD_NOW | RTLD_GLOBAL)) return;
  } else {
    if (dlopen(sp_sprintf("lib%s.so", lib), RTLD_NOW | RTLD_GLOBAL)) return;
    for (int v = 0; v <= 9; v++)
      if (dlopen(sp_sprintf("lib%s.so.%d", lib, v), RTLD_NOW | RTLD_GLOBAL)) return;
  }
  sp_raise_cls("LoadError", sp_sprintf("could not open library '%s': %s", lib, dlerror()));
}

static FxFfi *fx_ffi_new(int kind) {
  FxFfi *x = calloc(1, sizeof *x);
  x->kind = kind;
  return x;
}

static void fx_ffi_define(FxClass *k, const char *name, FxFfi *x) {
  FxMethod *m = fx_method_new(FX_M_FFI, sp_sym_intern(name), NULL, 0, k);
  m->ffi = x;
  fx_define_method(k, m, 1);
}

/* ffi_read_u32 and its kin: the width and the sign of the suffix. */
static int fx_ffi_width(const char *suffix, int *width, int *sign) {
  if (!strcmp(suffix, "ptr")) { *width = 0; *sign = 0; return 1; }
  if ((suffix[0] != 'u' && suffix[0] != 'i') || !suffix[1]) return 0;
  int bits = atoi(suffix + 1);
  if (bits != 8 && bits != 16 && bits != 32 && bits != 64) return 0;
  *width = bits / 8;
  *sign = suffix[0] == 'i';
  return 1;
}

/* ---- native classes ----
   A package binds a C struct as a class (native_struct). Its C functions
   make an instance (native_new, given the class id first) and answer its
   methods (native_method, given the struct first). The struct starts with
   the class id, so an instance is an object of the class for the dispatch
   of Fluxpinel. The bindings of one name form one method (FX_FFI_NATIVE),
   and a call takes the binding that fits its arguments. */

/* The class that the declarations after native_struct bind. */
static FxClass *fx_native_cur;

/* The class of native_struct "A::B": the path from Object. A missing
   segment is made, a module but for the last. */
static FxClass *fx_native_class(const char *path) {
  FxClass *scope = L.object;
  for (const char *p = path;;) {
    const char *sep = strstr(p, "::");
    size_t n = sep ? (size_t)(sep - p) : strlen(p);
    sp_sym s = fx_sym_intern_n(p, n);
    sp_RbVal v;
    FxClass *c = NULL;
    if (fx_const_scoped(scope, s, &v) && !(c = fx_class_value(v)))
      sp_raise_cls("TypeError", sp_sprintf("%.*s is not a class/module", (int)n, p));
    if (!c) {
      const char *full = scope == L.object ? sp_sprintf("%.*s", (int)n, p)
                                           : sp_sprintf("%s::%.*s", scope->name, (int)n, p);
      c = fx_class_new(strdup(full), sep != NULL, sep ? NULL : L.object, scope);
      fx_const_set(scope, s, fx_class_box(c));
    }
    if (!sep) return c;
    scope = c;
    p = sep + 2;
  }
}

/* A binding of native_new [specs], "csym" (ctor) or of native_method
   :name, [specs], ret, "csym". A trailing :rest takes every further
   argument: the C function gets their count and an sp_RbVal array. */
static FxFfi *fx_native_binding(int ctor, sp_RbVal specs, sp_RbVal ret, const char *csym) {
  FxFfi *x = fx_ffi_new(FX_FFI_FUNC);
  x->native = 1;
  x->ctor = ctor;
  x->name = strdup(csym);
  int n = (int)sp_poly_length(specs);
  sp_RbVal last = n ? sp_poly_arr_get(specs, n - 1) : sp_box_nil();
  x->rest = last.tag == SP_TAG_SYM && !strcmp(sp_sym_to_s((sp_sym)last.v.i), "rest");
  x->nargs = n - x->rest;
  int nc = 1 + x->nargs + 2 * x->rest;   /* the class id or the struct first */
  x->args = calloc((size_t)x->nargs + 1, sizeof *x->args);
  x->atypes = calloc((size_t)nc + 1, sizeof *x->atypes);
  x->atypes[0] = ctor ? &ffi_type_sint64 : &ffi_type_pointer;
  for (int i = 0; i < x->nargs; i++) {
    x->args[i] = fx_native_type(sp_poly_arr_get(specs, i));
    x->atypes[1 + i] = fx_ffi_ctype(x->args[i]);
  }
  if (x->rest) {
    x->atypes[1 + x->nargs] = &ffi_type_sint64;
    x->atypes[2 + x->nargs] = &ffi_type_pointer;
  }
  x->ret = ctor ? FX_FT_PTR : fx_native_type(ret);
  ffi_type *rt = x->ret == FX_FT_SELF ? &ffi_type_pointer : fx_ffi_ctype(x->ret);
  if (ffi_prep_cif(&x->cif, FFI_DEFAULT_ABI, (unsigned)nc, rt, x->atypes) != FFI_OK)
    sp_raise_cls("ArgumentError", sp_sprintf("%s: no call interface for its types", csym));
  return x;
}

/* Adds a binding to the method of its name: an instance method, or the
   singleton new for a constructor. */
static void fx_native_add(FxClass *k, sp_sym name, FxFfi *b) {
  FxMethodTable *t = b->ctor ? &k->smethods : &k->methods;
  FxMethod *m = fx_table_get(t, name);
  FxFfi *set = m && m->kind == FX_M_FFI && m->ffi->kind == FX_FFI_NATIVE && m->ffi->cls == k ? m->ffi : NULL;
  if (!set) {
    set = fx_ffi_new(FX_FFI_NATIVE);
    set->ctor = b->ctor;
    set->cls = k;
    m = fx_method_new(FX_M_FFI, name, NULL, 0, k);
    m->ffi = set;
    fx_define_method(k, m, b->ctor);
  }
  set->overloads = realloc(set->overloads, sizeof *set->overloads * (size_t)(set->noverloads + 1));
  set->overloads[set->noverloads++] = b;
}

/* native_struct, native_new, native_method and their kin. Answers 1 when
   it handles the call. */
static int fx_native_decl(const char *name, int argc, sp_RbVal *argv) {
  if (FX_IS("native_struct") && argc >= 1 && fx_is_string(argv[0])) {
    /* one argument: the class that a .share.rb declares for */
    fx_native_cur = fx_native_class(sp_poly_recv_s(argv[0], name));
    fx_native_cur->native = 1;
    return 1;
  }
  /* a subclass of a native class is a gap at its first call; the share
     declarations matter only to the analysis of spinel */
  if (FX_IS("native_subclass") || FX_IS("native_share")) return 1;
  if (!fx_native_cur) return 0;
  if (FX_IS("native_new") && argc == 2 && fx_is_array(argv[0]) && fx_is_string(argv[1])) {
    fx_native_add(fx_native_cur, sp_sym_intern("new"),
                    fx_native_binding(1, argv[0], sp_box_nil(), sp_poly_recv_s(argv[1], name)));
    return 1;
  }
  if (FX_IS("native_method") && argc == 4 && argv[0].tag == SP_TAG_SYM && fx_is_array(argv[1]) &&
      fx_is_string(argv[3])) {
    fx_native_add(fx_native_cur, (sp_sym)argv[0].v.i,
                    fx_native_binding(0, argv[1], argv[2], sp_poly_recv_s(argv[3], name)));
    return 1;
  }
  return 0;
}

/* The declarations of a module body. Answers 1 and sets *out when it
   handles the call. */
static int fx_ffi_class(FxClass *k, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (strncmp(name, "ffi_", 4) && strncmp(name, "native_", 7)) return 0;
  *out = sp_box_nil();
  if (FX_IS("ffi_lib")) {
    for (int i = 0; i < argc; i++) fx_ffi_open(sp_poly_recv_s(argv[i], name));
    return 1;
  }
  /* what the build of spinel reads: the flags of the C compiler, and the
     objects of a package, which the Makefile links into Fluxpinel */
  if (FX_IS("ffi_cflags") || FX_IS("native_lib") || FX_IS("native_obj") || FX_IS("native_obj_reflect"))
    return 1;
  if (FX_IS("ffi_const") && argc == 2 && argv[0].tag == SP_TAG_SYM) {
    fx_const_set(k, (sp_sym)argv[0].v.i, argv[1]);
    return 1;
  }
  /* ffi_func :csym, [types], ret and native_func :name, [types], ret, "csym" */
  int native = FX_IS("native_func");
  if ((native || FX_IS("ffi_func")) && argc >= 3 && argv[0].tag == SP_TAG_SYM && fx_is_array(argv[1])) {
    if (native ? argc != 4 || !fx_is_string(argv[3]) : argc == 4 && !fx_is_kwhash(argv[3]))
      return 0;   /* the blocking: of ffi_func has no effect on one worker */
    FxFfiType (*type)(sp_RbVal) = native ? fx_native_type : fx_ffi_type;
    FxFfi *x = fx_ffi_new(FX_FFI_FUNC);
    x->native = native;
    x->name = native ? strdup(sp_poly_recv_s(argv[3], name)) : sp_sym_to_s((sp_sym)argv[0].v.i);
    x->nargs = (int)sp_poly_length(argv[1]);
    x->args = calloc((size_t)x->nargs + 1, sizeof *x->args);
    x->atypes = calloc((size_t)x->nargs + 1, sizeof *x->atypes);
    for (int i = 0; i < x->nargs; i++) {
      sp_RbVal t = sp_poly_arr_get(argv[1], i);
      if (t.tag == SP_TAG_SYM && !strcmp(sp_sym_to_s((sp_sym)t.v.i), "..."))
        fx_gap("a variadic FFI function");
      x->args[i] = type(t);
      x->atypes[i] = fx_ffi_ctype(x->args[i]);
    }
    x->ret = type(argv[2]);
    if (ffi_prep_cif(&x->cif, FFI_DEFAULT_ABI, (unsigned)x->nargs, fx_ffi_ctype(x->ret), x->atypes) != FFI_OK)
      sp_raise_cls("ArgumentError", sp_sprintf("%s :%s: no call interface for its types", name, x->name));
    fx_ffi_define(k, sp_sym_to_s((sp_sym)argv[0].v.i), x);
    *out = argv[0];
    return 1;
  }
  if (FX_IS("ffi_buffer") && argc == 2 && argv[0].tag == SP_TAG_SYM && argv[1].tag == SP_TAG_INT) {
    FxFfi *x = fx_ffi_new(FX_FFI_BUFFER);
    x->buf = calloc(1, argv[1].v.i > 0 ? (size_t)argv[1].v.i : 1);
    fx_ffi_define(k, sp_sym_to_s((sp_sym)argv[0].v.i), x);
    *out = argv[0];
    return 1;
  }
  int write = !strncmp(name, "ffi_write_", 10);
  if ((write || !strncmp(name, "ffi_read_", 9)) && argc == 2 && argv[0].tag == SP_TAG_SYM &&
      argv[1].tag == SP_TAG_INT) {
    FxFfi *x = fx_ffi_new(write ? FX_FFI_WRITE : FX_FFI_READ);
    if (!fx_ffi_width(name + (write ? 10 : 9), &x->width, &x->sign))
      sp_raise_cls("NoMethodError", sp_sprintf("undefined method '%s' for module %s", name, k->name));
    x->offset = (long)argv[1].v.i;
    fx_ffi_define(k, sp_sym_to_s((sp_sym)argv[0].v.i), x);
    *out = argv[0];
    return 1;
  }
  if (fx_native_decl(name, argc, argv)) return 1;
  if (FX_IS("ffi_struct") || FX_IS("ffi_callback") || FX_IS("ffi_source") || FX_IS("native_new") ||
      FX_IS("native_method"))
    fx_gap(sp_sprintf("%s", name));
  return 0;
}

/* The integer that an argument of an integer type passes. */
static int64_t fx_ffi_int(sp_RbVal v) {
  if (v.tag == SP_TAG_INT) return v.v.i;
  if (v.tag == SP_TAG_FLT) return (int64_t)v.v.f;   /* truncates, as NUM2LONG does */
  if (v.tag == SP_TAG_NIL) sp_raise_cls("TypeError", "no implicit conversion from nil to integer");
  sp_raise_cls("TypeError", sp_sprintf("no implicit conversion of %s into Integer",
                                       v.tag == SP_TAG_BOOL ? (v.v.b ? "true" : "false") : sp_poly_class_name(v)));
}

/* One argument as libffi reads it. A slot is wide enough for any of them;
   an array argument keeps its C copy in *heap until the call ends. */
typedef union {
  int8_t i8;
  uint8_t u8;
  int16_t i16;
  uint16_t u16;
  int32_t i32;
  uint32_t u32;
  int64_t i64;
  uint64_t u64;
  long l;
  unsigned long ul;
  float f;
  double d;
  const void *p;
  bool b;
  sp_RbVal v;
} FxFfiSlot;

/* The String that a :string argument of native_func passes. */
static const char *fx_native_str(sp_RbVal v) {
  if (!fx_is_string(v))
    sp_raise_cls("TypeError", sp_sprintf("no implicit conversion of %s into String",
                                         v.tag == SP_TAG_NIL ? "nil" : sp_poly_class_name(v)));
  return sp_poly_recv_s(v, "native_func");
}

static void fx_ffi_arg(FxFfiType t, sp_RbVal v, FxFfiSlot *s, void **heap) {
  switch (t) {
  case FX_FT_INT: case FX_FT_INT32: s->i32 = (int32_t)fx_ffi_int(v); break;
  case FX_FT_UINT32: s->u32 = (uint32_t)fx_ffi_int(v); break;
  case FX_FT_INT16: s->i16 = (int16_t)fx_ffi_int(v); break;
  case FX_FT_UINT16: s->u16 = (uint16_t)fx_ffi_int(v); break;
  case FX_FT_INT8: s->i8 = (int8_t)fx_ffi_int(v); break;
  case FX_FT_UINT8: s->u8 = (uint8_t)fx_ffi_int(v); break;
  case FX_FT_LONG: s->l = (long)fx_ffi_int(v); break;
  case FX_FT_SIZE_T: s->ul = (unsigned long)fx_ffi_int(v); break;
  case FX_FT_INT64: s->i64 = fx_ffi_int(v); break;
  case FX_FT_UINT64: s->u64 = (uint64_t)fx_ffi_int(v); break;
  case FX_FT_BOOL: s->i32 = sp_poly_truthy(v) ? 1 : 0; break;
  case FX_FT_FLOAT: s->f = (float)sp_poly_to_f(v); break;
  case FX_FT_DOUBLE: s->d = sp_poly_to_f(v); break;
  case FX_FT_STR: case FX_FT_BINSTR:
    if (v.tag == SP_TAG_NIL) { s->p = NULL; break; }
    if (!fx_is_string(v))
      sp_raise_cls("TypeError", sp_sprintf("no implicit conversion of %s into String", sp_poly_class_name(v)));
    s->p = sp_poly_recv_s(v, "ffi");
    break;
  case FX_FT_PTR: s->p = fx_ptr_arg(v); break;
  case FX_FT_ANY: s->v = v; break;
  case FX_FT_RTSTR: s->p = fx_native_str(v); break;
  case FX_FT_NRTSTR: s->p = v.tag == SP_TAG_NIL ? NULL : fx_native_str(v); break;
  case FX_FT_SPINT: s->i64 = fx_ffi_int(v); break;
  case FX_FT_SPBOOL: s->b = sp_poly_truthy(v); break;
  case FX_FT_TEXT: s->p = fx_native_str(v); break;   /* the caller took #to_s of a non-String */
  case FX_FT_REGEXP:
    if (!fx_is_regexp(v))
      sp_raise_cls("TypeError", sp_sprintf("wrong argument type %s (expected Regexp)", sp_poly_class_name(v)));
    s->p = v.v.p;
    break;
  case FX_FT_INT_ARRAY: case FX_FT_FLOAT_ARRAY: {
    if (!fx_is_array(v)) sp_raise_cls("TypeError", sp_sprintf("no implicit conversion of %s into Array",
                                                                sp_poly_class_name(v)));
    sp_int n = sp_poly_length(v);
    /* the elements in C order; the C side reads them during the call only */
    if (t == FX_FT_INT_ARRAY) {
      int64_t *a = malloc(sizeof *a * (size_t)(n ? n : 1));
      for (sp_int i = 0; i < n; i++) a[i] = fx_ffi_int(sp_poly_arr_get(v, i));
      *heap = a;
    } else {
      double *a = malloc(sizeof *a * (size_t)(n ? n : 1));
      for (sp_int i = 0; i < n; i++) a[i] = sp_poly_to_f(sp_poly_arr_get(v, i));
      *heap = a;
    }
    s->p = *heap;
    break;
  }
  default: s->p = NULL; break;
  }
}

/* The value of a return of type t, which libffi wrote into r. */
static sp_RbVal fx_ffi_ret(FxFfiType t, const void *r) {
  const ffi_arg w = *(const ffi_arg *)r;
  switch (t) {
  case FX_FT_INT: case FX_FT_INT32: return sp_box_int((int32_t)w);
  case FX_FT_UINT32: return sp_box_int((uint32_t)w);
  case FX_FT_INT16: return sp_box_int((int16_t)w);
  case FX_FT_UINT16: return sp_box_int((uint16_t)w);
  case FX_FT_INT8: return sp_box_int((int8_t)w);
  case FX_FT_UINT8: return sp_box_int((uint8_t)w);
  case FX_FT_LONG: case FX_FT_INT64: return sp_box_int((int64_t)w);
  case FX_FT_SIZE_T: case FX_FT_UINT64: return sp_box_int((sp_int)(uint64_t)w);
  case FX_FT_BOOL: return sp_box_bool((int32_t)w != 0);
  case FX_FT_FLOAT: return sp_box_float(*(const float *)r);
  case FX_FT_DOUBLE: return sp_box_float(*(const double *)r);
  case FX_FT_STR: {
    const char *s = *(const char *const *)r;
    return s ? fx_new_string((const uint8_t *)s, strlen(s)) : sp_box_nil();
  }
  case FX_FT_BINSTR: {
    /* the callee set the byte count just before it returned */
    const char *s = *(const char *const *)r;
    return s ? fx_new_string((const uint8_t *)s, (size_t)sp_ffi_bin_len) : sp_box_nil();
  }
  case FX_FT_PTR: return fx_ptr_box(*(void *const *)r);
  case FX_FT_ANY: return *(const sp_RbVal *)r;
  case FX_FT_SPINT: return sp_box_int((sp_int)w);
  case FX_FT_SPBOOL: return sp_box_bool((uint8_t)w != 0);
  /* a String of the runtime, which the callee made */
  case FX_FT_RTSTR: case FX_FT_NRTSTR: return sp_box_str_new(*(const char *const *)r);
  case FX_FT_CSTR: return sp_box_str_new(sp_str_dup_external(*(const char *const *)r));
  case FX_FT_CBINSTR: {
    const char *s = *(const char *const *)r;
    return sp_box_str_new(sp_str_as_binary(sp_str_from_bytes(s, (size_t)(sp_ffi_bin_len < 0 ? 0 : sp_ffi_bin_len))));
  }
  default: return sp_box_nil();
  }
}

static sp_RbVal fx_ffi_call(FxFfi *x, int argc, sp_RbVal *argv) {
  /* AOT code passes the declared arguments of a native_func and drops the
     rest, but for JSON.parse(s, symbolize_names: true) (codegen_call_class.c) */
  int symbolize = 0;
  if (x->native && argc > x->nargs) {
    if (!strcmp(x->name, "sp_json_parse") && argc == 2 && fx_is_kwhash(argv[1])) {
      sp_RbVal key = sp_box_sym(sp_sym_intern("symbolize_names"));
      symbolize = sp_poly_truthy(fx_send(argv[1], "[]", 1, &key));
    }
    argc = x->nargs;
  }
  if (argc != x->nargs)
    sp_raise_cls("ArgumentError", sp_sprintf("wrong number of arguments (given %d, expected %d)", argc, x->nargs));
  if (!x->fn) {
    x->fn = dlsym(RTLD_DEFAULT, x->name);
    if (!x->fn) sp_raise_cls("NotImplementedError", sp_sprintf("FFI function %s not found", x->name));
  }
  FxFfiSlot slots[x->nargs ? x->nargs : 1];
  void *values[x->nargs ? x->nargs : 1];
  void *heap[x->nargs ? x->nargs : 1];
  for (int i = 0; i < x->nargs; i++) {
    heap[i] = NULL;
    fx_ffi_arg(x->args[i], argv[i], &slots[i], &heap[i]);
    values[i] = &slots[i];
  }
  union { ffi_arg w; float f; double d; void *p; sp_RbVal v; } r;
  sp_ffi_bin_len = 0;
  /* a C function can wait (a connect, a read of a socket): the walker lock
     is given up for the call */
  FX_UNLOCKED(ffi_call(&x->cif, FFI_FN(x->fn), &r, values));
  for (int i = 0; i < x->nargs; i++) free(heap[i]);
  sp_RbVal v = fx_ffi_ret(x->ret, &r);
  return symbolize ? sp_json_symbolize(v) : v;
}

/* Module.name(buf) of ffi_read_*, and Module.name(buf, v) of ffi_write_*. */
static sp_RbVal fx_ffi_field(FxFfi *x, int argc, sp_RbVal *argv) {
  int write = x->kind == FX_FFI_WRITE;
  if (argc != 1 + write)
    sp_raise_cls("ArgumentError", sp_sprintf("wrong number of arguments (given %d, expected %d)", argc, 1 + write));
  char *base = fx_ptr_arg(argv[0]);
  if (!base) sp_raise_cls("ArgumentError", "NULL pointer");
  char *at = base + x->offset;
  if (x->width == 0) {
    if (write) { void *p = fx_ptr_arg(argv[1]); memcpy(at, &p, sizeof p); return argv[1]; }
    void *p;
    memcpy(&p, at, sizeof p);
    return fx_ptr_box(p);
  }
  if (write) {
    int64_t v = fx_ffi_int(argv[1]);
    memcpy(at, &v, (size_t)x->width);   /* the low bytes, on a little-endian host */
    return argv[1];
  }
  uint64_t u = 0;
  memcpy(&u, at, (size_t)x->width);
  if (x->sign && x->width < 8 && (u >> (x->width * 8 - 1) & 1)) u |= ~(uint64_t)0 << (x->width * 8);
  return sp_box_int((sp_int)u);
}

/* Whether v is of the type t as is, with no conversion. */
static int fx_native_fits(FxFfiType t, sp_RbVal v) {
  switch (t) {
  case FX_FT_ANY: return 1;
  case FX_FT_RTSTR: case FX_FT_TEXT: return fx_is_string(v);
  case FX_FT_NRTSTR: return v.tag == SP_TAG_NIL || fx_is_string(v);
  case FX_FT_SPINT: return fx_is_integer(v);
  case FX_FT_DOUBLE: return v.tag == SP_TAG_FLT;
  case FX_FT_SPBOOL: return v.tag == SP_TAG_BOOL;
  case FX_FT_REGEXP: return fx_is_regexp(v);
  default: return 0;
  }
}

/* The binding of a native name that a call takes, as spinel picks it at
   compile time (comp_native_method_find_typed): of the bindings of the
   arity, the first that takes each argument as is, else the first of the
   arity, else one with a :rest that takes the count. NULL for none. */
static FxFfi *fx_native_pick(const FxFfi *set, int argc, const sp_RbVal *argv) {
  FxFfi *arity = NULL, *rest = NULL;
  for (int i = 0; i < set->noverloads; i++) {
    FxFfi *o = set->overloads[i];
    if (o->rest) {
      if (argc >= o->nargs && !rest) rest = o;
      continue;
    }
    if (o->nargs != argc) continue;
    int fits = 1;
    for (int a = 0; a < argc && fits; a++) fits = fx_native_fits(o->args[a], argv[a]);
    if (fits) return o;
    if (!arity) arity = o;
  }
  return arity ? arity : rest;
}

/* A call of a native name: Name.new(...) for a constructor, or a method of
   the instance self. */
static sp_RbVal fx_native_call(const FxFfi *set, sp_RbVal self, int argc, sp_RbVal *argv) {
  FxFfi *x = fx_native_pick(set, argc, argv);
  if (!x)
    sp_raise_cls("ArgumentError", sp_sprintf("wrong number of arguments (given %d, expected %d)", argc,
                                             set->overloads[0]->nargs));
  if (set->ctor ? fx_class_value(self) != set->cls : self.tag != SP_TAG_OBJ || self.cls_id != set->cls->inst_id)
    fx_gap(sp_sprintf("a subclass of the native class %s", set->cls->name));
  if (!x->fn) {
    x->fn = dlsym(RTLD_DEFAULT, x->name);
    if (!x->fn) sp_raise_cls("NotImplementedError", sp_sprintf("native function %s not found", x->name));
  }
  /* a :text argument that is no String passes its #to_s, kept here */
  sp_PolyArray *keep = sp_PolyArray_new();
  SP_GC_ROOT(keep);
  int nc = 1 + x->nargs + 2 * x->rest;
  FxFfiSlot slots[nc];
  void *values[nc];
  void *heap[nc];
  for (int i = 0; i < nc; i++) {
    heap[i] = NULL;
    values[i] = &slots[i];
  }
  if (set->ctor) slots[0].i64 = set->cls->inst_id;
  else slots[0].p = self.v.p;
  for (int i = 0; i < x->nargs; i++) {
    sp_RbVal v = argv[i];
    if (x->args[i] == FX_FT_TEXT && !fx_is_string(v)) {
      v = fx_send(v, "to_s", 0, NULL);
      sp_PolyArray_push(keep, v);
    }
    fx_ffi_arg(x->args[i], v, &slots[1 + i], &heap[1 + i]);
  }
  if (x->rest) {
    slots[1 + x->nargs].i64 = argc - x->nargs;
    slots[2 + x->nargs].p = argv + x->nargs;
  }
  union { ffi_arg w; float f; double d; void *p; sp_RbVal v; } r;
  sp_ffi_bin_len = 0;
  /* a C function can wait (a connect, a read of a socket): the walker lock
     is given up for the call */
  FX_UNLOCKED(ffi_call(&x->cif, FFI_FN(x->fn), &r, values));
  for (int i = 0; i < nc; i++) free(heap[i]);
  if (set->ctor) return sp_box_obj(r.p, (int)set->cls->inst_id);
  if (x->ret == FX_FT_SELF) return self;
  return fx_ffi_ret(x->ret, &r);
}

static sp_RbVal fx_ffi_invoke(const FxFfi *cx, sp_RbVal self, int argc, sp_RbVal *argv) {
  FxFfi *x = (FxFfi *)cx;
  switch (x->kind) {
  case FX_FFI_NATIVE: return fx_native_call(x, self, argc, argv);
  case FX_FFI_FUNC: return fx_ffi_call(x, argc, argv);
  case FX_FFI_BUFFER:
    if (argc != 0) sp_raise_cls("ArgumentError", sp_sprintf("wrong number of arguments (given %d, expected 0)", argc));
    return fx_ptr_box(x->buf);
  default: return fx_ffi_field(x, argc, argv);
  }
}

#else

struct FxFfi { int unused; };

static int fx_ffi_class(FxClass *k, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  (void)k, (void)argc, (void)argv, (void)out;
  if (!strncmp(name, "ffi_", 4) || !strncmp(name, "native_", 7))
    fx_gap(sp_sprintf("%s in a build with no FFI", name));
  return 0;
}

static sp_RbVal fx_ffi_invoke(const struct FxFfi *x, sp_RbVal self, int argc, sp_RbVal *argv) {
  (void)x, (void)self, (void)argc, (void)argv;
  fx_gap("an FFI call in a build with no FFI");
}

#endif

#undef FX_IS
