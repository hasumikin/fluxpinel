/* ffi.c -- the FFI of spinel at run time.

   spinel reads ffi_func and its kin at compile time and emits a direct C
   call for each function (docs/FFI.md of spinel). Lupe meets the same
   declarations in a module body at run time: it opens each ffi_lib with
   dlopen, finds each function with dlsym at its first call, and calls it
   through libffi with the C types that the declaration names.

   A package of spinel binds a C function of the runtime or of its own
   object with native_func. Lupe calls it the same way: the Makefile links
   the objects of the packages, and keeps each bound function of the
   runtime in the link (tools/native_link.rb).

   A declared function is a singleton method of the module (LUPE_M_FFI),
   so a call, respond_to? and method lookup find it as they find a def.

   A :ptr is an SP_BUILTIN_FOREIGN_PTR box, which the collector does not
   follow, and NULL is nil, as spinel compares a NULL :ptr equal to nil.

   Built with LUPE_FFI=0 (no libffi), a declaration is a gap. */

#include "lupe.h"

#define LUPE_IS(s) (!strcmp(name, (s)))

#if LUPE_FFI
#include <dlfcn.h>
#include <ffi.h>

typedef enum {
  LUPE_FT_INT, LUPE_FT_UINT32, LUPE_FT_INT32, LUPE_FT_UINT16, LUPE_FT_INT16, LUPE_FT_UINT8, LUPE_FT_INT8,
  LUPE_FT_SIZE_T, LUPE_FT_LONG, LUPE_FT_INT64, LUPE_FT_UINT64, LUPE_FT_FLOAT, LUPE_FT_DOUBLE, LUPE_FT_BOOL,
  LUPE_FT_STR, LUPE_FT_BINSTR, LUPE_FT_PTR, LUPE_FT_FLOAT_ARRAY, LUPE_FT_INT_ARRAY, LUPE_FT_VOID,
  /* the types of native_func only */
  LUPE_FT_ANY, LUPE_FT_RTSTR, LUPE_FT_NRTSTR, LUPE_FT_CSTR, LUPE_FT_CBINSTR, LUPE_FT_SPINT, LUPE_FT_SPBOOL,
  /* and of native_method */
  LUPE_FT_TEXT, LUPE_FT_REGEXP, LUPE_FT_SELF
} LupeFfiType;

/* The type specs of docs/FFI.md, and the names of the ffi gem for them. */
static const struct {
  const char *name;
  LupeFfiType type;
} lupe_ffi_types[] = {
  { "int", LUPE_FT_INT }, { "uint32", LUPE_FT_UINT32 }, { "int32", LUPE_FT_INT32 },
  { "uint16", LUPE_FT_UINT16 }, { "int16", LUPE_FT_INT16 }, { "uint8", LUPE_FT_UINT8 }, { "int8", LUPE_FT_INT8 },
  { "size_t", LUPE_FT_SIZE_T }, { "long", LUPE_FT_LONG }, { "float", LUPE_FT_FLOAT }, { "double", LUPE_FT_DOUBLE },
  { "bool", LUPE_FT_BOOL }, { "str", LUPE_FT_STR }, { "binstr", LUPE_FT_BINSTR }, { "ptr", LUPE_FT_PTR },
  { "float_array", LUPE_FT_FLOAT_ARRAY }, { "int_array", LUPE_FT_INT_ARRAY }, { "void", LUPE_FT_VOID },
  { "uint", LUPE_FT_UINT32 }, { "ulong", LUPE_FT_UINT64 }, { "int64", LUPE_FT_INT64 }, { "uint64", LUPE_FT_UINT64 },
  { "short", LUPE_FT_INT16 }, { "ushort", LUPE_FT_UINT16 }, { "char", LUPE_FT_INT8 }, { "uchar", LUPE_FT_UINT8 },
  { "string", LUPE_FT_STR }, { "pointer", LUPE_FT_PTR }, { "buffer_in", LUPE_FT_PTR },
  { "buffer_out", LUPE_FT_PTR }, { "buffer_inout", LUPE_FT_PTR },
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
  LupeFfiType type;
} lupe_native_types[] = {
  { "any", LUPE_FT_ANY }, { "string", LUPE_FT_RTSTR }, { "string?", LUPE_FT_NRTSTR },
  { "nstring", LUPE_FT_NRTSTR }, { "cstring", LUPE_FT_CSTR }, { "cbinstr", LUPE_FT_CBINSTR },
  { "int", LUPE_FT_SPINT }, { "float", LUPE_FT_DOUBLE }, { "bool", LUPE_FT_SPBOOL },
  { "ptr", LUPE_FT_PTR }, { "nil", LUPE_FT_VOID }, { "void", LUPE_FT_VOID },
  { "text", LUPE_FT_TEXT }, { "regexp", LUPE_FT_REGEXP }, { "self", LUPE_FT_SELF },
};

/* sp_RbVal as libffi passes it: the tag, the class id and the union, which
   holds no member wider than 8 bytes. */
static ffi_type *lupe_rbval_elements[] = { &ffi_type_sint32, &ffi_type_sint32, &ffi_type_sint64, NULL };
static ffi_type lupe_rbval_type = { 0, 0, FFI_TYPE_STRUCT, lupe_rbval_elements };

static ffi_type *lupe_ffi_ctype(LupeFfiType t) {
  switch (t) {
  case LUPE_FT_ANY: return &lupe_rbval_type;
  case LUPE_FT_SPINT: return &ffi_type_sint64;
  case LUPE_FT_SPBOOL: return &ffi_type_uint8;
  case LUPE_FT_INT: case LUPE_FT_INT32: case LUPE_FT_BOOL: return &ffi_type_sint32;
  case LUPE_FT_UINT32: return &ffi_type_uint32;
  case LUPE_FT_INT16: return &ffi_type_sint16;
  case LUPE_FT_UINT16: return &ffi_type_uint16;
  case LUPE_FT_INT8: return &ffi_type_sint8;
  case LUPE_FT_UINT8: return &ffi_type_uint8;
  case LUPE_FT_LONG: return &ffi_type_slong;
  case LUPE_FT_SIZE_T: return &ffi_type_ulong;
  case LUPE_FT_INT64: return &ffi_type_sint64;
  case LUPE_FT_UINT64: return &ffi_type_uint64;
  case LUPE_FT_FLOAT: return &ffi_type_float;
  case LUPE_FT_DOUBLE: return &ffi_type_double;
  case LUPE_FT_VOID: return &ffi_type_void;
  default: return &ffi_type_pointer;
  }
}

enum { LUPE_FFI_FUNC, LUPE_FFI_BUFFER, LUPE_FFI_READ, LUPE_FFI_WRITE, LUPE_FFI_NATIVE };

typedef struct LupeFfi {
  int kind;
  int native;             /* declared by native_func, native_new or native_method */
  int rest;               /* a native binding with a trailing :rest */
  /* LUPE_FFI_NATIVE: the bindings of one name of a native class, of which
     a call takes the one that fits its arguments */
  struct LupeFfi **overloads;
  int noverloads;
  int ctor;               /* native_new: the first C argument is the class id */
  LupeClass *cls;
  const char *name;       /* the C symbol of a function */
  void *fn;               /* found at the first call */
  int nargs;
  LupeFfiType ret, *args;
  ffi_cif cif;
  ffi_type **atypes;
  void *buf;              /* LUPE_FFI_BUFFER */
  long offset;            /* LUPE_FFI_READ / WRITE */
  int width, sign;        /* the bytes of a field, and whether it sign-extends;
                             width 0 is a pointer */
} LupeFfi;

static LupeFfiType lupe_ffi_type(sp_RbVal v) {
  if (v.tag == SP_TAG_SYM) {
    const char *s = sp_sym_to_s((sp_sym)v.v.i);
    for (size_t i = 0; i < sizeof lupe_ffi_types / sizeof lupe_ffi_types[0]; i++)
      if (!strcmp(lupe_ffi_types[i].name, s)) return lupe_ffi_types[i].type;
    lupe_gap(sp_sprintf("the FFI type :%s", s));
  }
  sp_raise_cls("TypeError", sp_sprintf("%s is not an FFI type", sp_poly_inspect(v)));
}

static LupeFfiType lupe_native_type(sp_RbVal v) {
  if (v.tag == SP_TAG_SYM) {
    const char *s = sp_sym_to_s((sp_sym)v.v.i);
    for (size_t i = 0; i < sizeof lupe_native_types / sizeof lupe_native_types[0]; i++)
      if (!strcmp(lupe_native_types[i].name, s)) return lupe_native_types[i].type;
    lupe_gap(sp_sprintf("the native_func type :%s", s));
  }
  sp_raise_cls("TypeError", sp_sprintf("%s is not a native_func type", sp_poly_inspect(v)));
}

static sp_RbVal lupe_ptr_box(void *p) {
  return p ? sp_box_obj(p, SP_BUILTIN_FOREIGN_PTR) : sp_box_nil();
}

/* The address that a :ptr argument passes: a pointer, or NULL for nil. */
static void *lupe_ptr_arg(sp_RbVal v) {
  if (v.tag == SP_TAG_NIL) return NULL;
  if (v.tag == SP_TAG_OBJ && v.cls_id == SP_BUILTIN_FOREIGN_PTR) return v.v.p;
  lupe_gap(sp_sprintf("a %s as an FFI pointer", sp_poly_class_name(v)));
}

/* ffi_lib "name": libname.so, or a versioned one when only that exists. */
static void lupe_ffi_open(const char *lib) {
  if (strchr(lib, '/') || strstr(lib, ".so")) {
    if (dlopen(lib, RTLD_NOW | RTLD_GLOBAL)) return;
  } else {
    if (dlopen(sp_sprintf("lib%s.so", lib), RTLD_NOW | RTLD_GLOBAL)) return;
    for (int v = 0; v <= 9; v++)
      if (dlopen(sp_sprintf("lib%s.so.%d", lib, v), RTLD_NOW | RTLD_GLOBAL)) return;
  }
  sp_raise_cls("LoadError", sp_sprintf("could not open library '%s': %s", lib, dlerror()));
}

static LupeFfi *lupe_ffi_new(int kind) {
  LupeFfi *x = calloc(1, sizeof *x);
  x->kind = kind;
  return x;
}

static void lupe_ffi_define(LupeClass *k, const char *name, LupeFfi *x) {
  LupeMethod *m = lupe_method_new(LUPE_M_FFI, sp_sym_intern(name), NULL, 0, k);
  m->ffi = x;
  lupe_define_method(k, m, 1);
}

/* ffi_read_u32 and its kin: the width and the sign of the suffix. */
static int lupe_ffi_width(const char *suffix, int *width, int *sign) {
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
   of Lupe. The bindings of one name form one method (LUPE_FFI_NATIVE),
   and a call takes the binding that fits its arguments. */

/* The class that the declarations after native_struct bind. */
static LupeClass *lupe_native_cur;

/* The class of native_struct "A::B": the path from Object. A missing
   segment is made, a module but for the last. */
static LupeClass *lupe_native_class(const char *path) {
  LupeClass *scope = L.object;
  for (const char *p = path;;) {
    const char *sep = strstr(p, "::");
    size_t n = sep ? (size_t)(sep - p) : strlen(p);
    sp_sym s = sp_sym_intern_n(p, n);
    sp_RbVal v;
    LupeClass *c = NULL;
    if (lupe_const_scoped(scope, s, &v) && !(c = lupe_class_value(v)))
      sp_raise_cls("TypeError", sp_sprintf("%.*s is not a class/module", (int)n, p));
    if (!c) {
      const char *full = scope == L.object ? sp_sprintf("%.*s", (int)n, p)
                                           : sp_sprintf("%s::%.*s", scope->name, (int)n, p);
      c = lupe_class_new(strdup(full), sep != NULL, sep ? NULL : L.object, scope);
      lupe_const_set(scope, s, lupe_class_box(c));
    }
    if (!sep) return c;
    scope = c;
    p = sep + 2;
  }
}

/* A binding of native_new [specs], "csym" (ctor) or of native_method
   :name, [specs], ret, "csym". A trailing :rest takes every further
   argument: the C function gets their count and an sp_RbVal array. */
static LupeFfi *lupe_native_binding(int ctor, sp_RbVal specs, sp_RbVal ret, const char *csym) {
  LupeFfi *x = lupe_ffi_new(LUPE_FFI_FUNC);
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
    x->args[i] = lupe_native_type(sp_poly_arr_get(specs, i));
    x->atypes[1 + i] = lupe_ffi_ctype(x->args[i]);
  }
  if (x->rest) {
    x->atypes[1 + x->nargs] = &ffi_type_sint64;
    x->atypes[2 + x->nargs] = &ffi_type_pointer;
  }
  x->ret = ctor ? LUPE_FT_PTR : lupe_native_type(ret);
  ffi_type *rt = x->ret == LUPE_FT_SELF ? &ffi_type_pointer : lupe_ffi_ctype(x->ret);
  if (ffi_prep_cif(&x->cif, FFI_DEFAULT_ABI, (unsigned)nc, rt, x->atypes) != FFI_OK)
    sp_raise_cls("ArgumentError", sp_sprintf("%s: no call interface for its types", csym));
  return x;
}

/* Adds a binding to the method of its name: an instance method, or the
   singleton new for a constructor. */
static void lupe_native_add(LupeClass *k, sp_sym name, LupeFfi *b) {
  LupeMethodTable *t = b->ctor ? &k->smethods : &k->methods;
  LupeMethod *m = lupe_table_get(t, name);
  LupeFfi *set = m && m->kind == LUPE_M_FFI && m->ffi->kind == LUPE_FFI_NATIVE && m->ffi->cls == k ? m->ffi : NULL;
  if (!set) {
    set = lupe_ffi_new(LUPE_FFI_NATIVE);
    set->ctor = b->ctor;
    set->cls = k;
    m = lupe_method_new(LUPE_M_FFI, name, NULL, 0, k);
    m->ffi = set;
    lupe_define_method(k, m, b->ctor);
  }
  set->overloads = realloc(set->overloads, sizeof *set->overloads * (size_t)(set->noverloads + 1));
  set->overloads[set->noverloads++] = b;
}

/* native_struct, native_new, native_method and their kin. Answers 1 when
   it handles the call. */
static int lupe_native_decl(const char *name, int argc, sp_RbVal *argv) {
  if (LUPE_IS("native_struct") && argc >= 1 && lupe_is_string(argv[0])) {
    /* one argument: the class that a .share.rb declares for */
    lupe_native_cur = lupe_native_class(sp_poly_recv_s(argv[0], name));
    lupe_native_cur->native = 1;
    return 1;
  }
  /* a subclass of a native class is a gap at its first call; the share
     declarations matter only to the analysis of spinel */
  if (LUPE_IS("native_subclass") || LUPE_IS("native_share")) return 1;
  if (!lupe_native_cur) return 0;
  if (LUPE_IS("native_new") && argc == 2 && lupe_is_array(argv[0]) && lupe_is_string(argv[1])) {
    lupe_native_add(lupe_native_cur, sp_sym_intern("new"),
                    lupe_native_binding(1, argv[0], sp_box_nil(), sp_poly_recv_s(argv[1], name)));
    return 1;
  }
  if (LUPE_IS("native_method") && argc == 4 && argv[0].tag == SP_TAG_SYM && lupe_is_array(argv[1]) &&
      lupe_is_string(argv[3])) {
    lupe_native_add(lupe_native_cur, (sp_sym)argv[0].v.i,
                    lupe_native_binding(0, argv[1], argv[2], sp_poly_recv_s(argv[3], name)));
    return 1;
  }
  return 0;
}

/* The declarations of a module body. Answers 1 and sets *out when it
   handles the call. */
static int lupe_ffi_class(LupeClass *k, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (strncmp(name, "ffi_", 4) && strncmp(name, "native_", 7)) return 0;
  *out = sp_box_nil();
  if (LUPE_IS("ffi_lib")) {
    for (int i = 0; i < argc; i++) lupe_ffi_open(sp_poly_recv_s(argv[i], name));
    return 1;
  }
  /* what the build of spinel reads: the flags of the C compiler, and the
     objects of a package, which the Makefile links into Lupe */
  if (LUPE_IS("ffi_cflags") || LUPE_IS("native_lib") || LUPE_IS("native_obj") || LUPE_IS("native_obj_reflect"))
    return 1;
  if (LUPE_IS("ffi_const") && argc == 2 && argv[0].tag == SP_TAG_SYM) {
    lupe_const_set(k, (sp_sym)argv[0].v.i, argv[1]);
    return 1;
  }
  /* ffi_func :csym, [types], ret and native_func :name, [types], ret, "csym" */
  int native = LUPE_IS("native_func");
  if ((native || LUPE_IS("ffi_func")) && argc >= 3 && argv[0].tag == SP_TAG_SYM && lupe_is_array(argv[1])) {
    if (native ? argc != 4 || !lupe_is_string(argv[3]) : argc == 4 && !lupe_is_kwhash(argv[3]))
      return 0;   /* the blocking: of ffi_func has no effect on one worker */
    LupeFfiType (*type)(sp_RbVal) = native ? lupe_native_type : lupe_ffi_type;
    LupeFfi *x = lupe_ffi_new(LUPE_FFI_FUNC);
    x->native = native;
    x->name = native ? strdup(sp_poly_recv_s(argv[3], name)) : sp_sym_to_s((sp_sym)argv[0].v.i);
    x->nargs = (int)sp_poly_length(argv[1]);
    x->args = calloc((size_t)x->nargs + 1, sizeof *x->args);
    x->atypes = calloc((size_t)x->nargs + 1, sizeof *x->atypes);
    for (int i = 0; i < x->nargs; i++) {
      sp_RbVal t = sp_poly_arr_get(argv[1], i);
      if (t.tag == SP_TAG_SYM && !strcmp(sp_sym_to_s((sp_sym)t.v.i), "..."))
        lupe_gap("a variadic FFI function");
      x->args[i] = type(t);
      x->atypes[i] = lupe_ffi_ctype(x->args[i]);
    }
    x->ret = type(argv[2]);
    if (ffi_prep_cif(&x->cif, FFI_DEFAULT_ABI, (unsigned)x->nargs, lupe_ffi_ctype(x->ret), x->atypes) != FFI_OK)
      sp_raise_cls("ArgumentError", sp_sprintf("%s :%s: no call interface for its types", name, x->name));
    lupe_ffi_define(k, sp_sym_to_s((sp_sym)argv[0].v.i), x);
    *out = argv[0];
    return 1;
  }
  if (LUPE_IS("ffi_buffer") && argc == 2 && argv[0].tag == SP_TAG_SYM && argv[1].tag == SP_TAG_INT) {
    LupeFfi *x = lupe_ffi_new(LUPE_FFI_BUFFER);
    x->buf = calloc(1, argv[1].v.i > 0 ? (size_t)argv[1].v.i : 1);
    lupe_ffi_define(k, sp_sym_to_s((sp_sym)argv[0].v.i), x);
    *out = argv[0];
    return 1;
  }
  int write = !strncmp(name, "ffi_write_", 10);
  if ((write || !strncmp(name, "ffi_read_", 9)) && argc == 2 && argv[0].tag == SP_TAG_SYM &&
      argv[1].tag == SP_TAG_INT) {
    LupeFfi *x = lupe_ffi_new(write ? LUPE_FFI_WRITE : LUPE_FFI_READ);
    if (!lupe_ffi_width(name + (write ? 10 : 9), &x->width, &x->sign))
      sp_raise_cls("NoMethodError", sp_sprintf("undefined method '%s' for module %s", name, k->name));
    x->offset = (long)argv[1].v.i;
    lupe_ffi_define(k, sp_sym_to_s((sp_sym)argv[0].v.i), x);
    *out = argv[0];
    return 1;
  }
  if (lupe_native_decl(name, argc, argv)) return 1;
  if (LUPE_IS("ffi_struct") || LUPE_IS("ffi_callback") || LUPE_IS("ffi_source") || LUPE_IS("native_new") ||
      LUPE_IS("native_method"))
    lupe_gap(sp_sprintf("%s", name));
  return 0;
}

/* The integer that an argument of an integer type passes. */
static int64_t lupe_ffi_int(sp_RbVal v) {
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
} LupeFfiSlot;

/* The String that a :string argument of native_func passes. */
static const char *lupe_native_str(sp_RbVal v) {
  if (!lupe_is_string(v))
    sp_raise_cls("TypeError", sp_sprintf("no implicit conversion of %s into String",
                                         v.tag == SP_TAG_NIL ? "nil" : sp_poly_class_name(v)));
  return sp_poly_recv_s(v, "native_func");
}

static void lupe_ffi_arg(LupeFfiType t, sp_RbVal v, LupeFfiSlot *s, void **heap) {
  switch (t) {
  case LUPE_FT_INT: case LUPE_FT_INT32: s->i32 = (int32_t)lupe_ffi_int(v); break;
  case LUPE_FT_UINT32: s->u32 = (uint32_t)lupe_ffi_int(v); break;
  case LUPE_FT_INT16: s->i16 = (int16_t)lupe_ffi_int(v); break;
  case LUPE_FT_UINT16: s->u16 = (uint16_t)lupe_ffi_int(v); break;
  case LUPE_FT_INT8: s->i8 = (int8_t)lupe_ffi_int(v); break;
  case LUPE_FT_UINT8: s->u8 = (uint8_t)lupe_ffi_int(v); break;
  case LUPE_FT_LONG: s->l = (long)lupe_ffi_int(v); break;
  case LUPE_FT_SIZE_T: s->ul = (unsigned long)lupe_ffi_int(v); break;
  case LUPE_FT_INT64: s->i64 = lupe_ffi_int(v); break;
  case LUPE_FT_UINT64: s->u64 = (uint64_t)lupe_ffi_int(v); break;
  case LUPE_FT_BOOL: s->i32 = sp_poly_truthy(v) ? 1 : 0; break;
  case LUPE_FT_FLOAT: s->f = (float)sp_poly_to_f(v); break;
  case LUPE_FT_DOUBLE: s->d = sp_poly_to_f(v); break;
  case LUPE_FT_STR: case LUPE_FT_BINSTR:
    if (v.tag == SP_TAG_NIL) { s->p = NULL; break; }
    if (!lupe_is_string(v))
      sp_raise_cls("TypeError", sp_sprintf("no implicit conversion of %s into String", sp_poly_class_name(v)));
    s->p = sp_poly_recv_s(v, "ffi");
    break;
  case LUPE_FT_PTR: s->p = lupe_ptr_arg(v); break;
  case LUPE_FT_ANY: s->v = v; break;
  case LUPE_FT_RTSTR: s->p = lupe_native_str(v); break;
  case LUPE_FT_NRTSTR: s->p = v.tag == SP_TAG_NIL ? NULL : lupe_native_str(v); break;
  case LUPE_FT_SPINT: s->i64 = lupe_ffi_int(v); break;
  case LUPE_FT_SPBOOL: s->b = sp_poly_truthy(v); break;
  case LUPE_FT_TEXT: s->p = lupe_native_str(v); break;   /* the caller took #to_s of a non-String */
  case LUPE_FT_REGEXP:
    if (!lupe_is_regexp(v))
      sp_raise_cls("TypeError", sp_sprintf("wrong argument type %s (expected Regexp)", sp_poly_class_name(v)));
    s->p = v.v.p;
    break;
  case LUPE_FT_INT_ARRAY: case LUPE_FT_FLOAT_ARRAY: {
    if (!lupe_is_array(v)) sp_raise_cls("TypeError", sp_sprintf("no implicit conversion of %s into Array",
                                                                sp_poly_class_name(v)));
    sp_int n = sp_poly_length(v);
    /* the elements in C order; the C side reads them during the call only */
    if (t == LUPE_FT_INT_ARRAY) {
      int64_t *a = malloc(sizeof *a * (size_t)(n ? n : 1));
      for (sp_int i = 0; i < n; i++) a[i] = lupe_ffi_int(sp_poly_arr_get(v, i));
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
static sp_RbVal lupe_ffi_ret(LupeFfiType t, const void *r) {
  const ffi_arg w = *(const ffi_arg *)r;
  switch (t) {
  case LUPE_FT_INT: case LUPE_FT_INT32: return sp_box_int((int32_t)w);
  case LUPE_FT_UINT32: return sp_box_int((uint32_t)w);
  case LUPE_FT_INT16: return sp_box_int((int16_t)w);
  case LUPE_FT_UINT16: return sp_box_int((uint16_t)w);
  case LUPE_FT_INT8: return sp_box_int((int8_t)w);
  case LUPE_FT_UINT8: return sp_box_int((uint8_t)w);
  case LUPE_FT_LONG: case LUPE_FT_INT64: return sp_box_int((int64_t)w);
  case LUPE_FT_SIZE_T: case LUPE_FT_UINT64: return sp_box_int((sp_int)(uint64_t)w);
  case LUPE_FT_BOOL: return sp_box_bool((int32_t)w != 0);
  case LUPE_FT_FLOAT: return sp_box_float(*(const float *)r);
  case LUPE_FT_DOUBLE: return sp_box_float(*(const double *)r);
  case LUPE_FT_STR: {
    const char *s = *(const char *const *)r;
    return s ? lupe_new_string((const uint8_t *)s, strlen(s)) : sp_box_nil();
  }
  case LUPE_FT_BINSTR: {
    /* the callee set the byte count just before it returned */
    const char *s = *(const char *const *)r;
    return s ? lupe_new_string((const uint8_t *)s, (size_t)sp_ffi_bin_len) : sp_box_nil();
  }
  case LUPE_FT_PTR: return lupe_ptr_box(*(void *const *)r);
  case LUPE_FT_ANY: return *(const sp_RbVal *)r;
  case LUPE_FT_SPINT: return sp_box_int((sp_int)w);
  case LUPE_FT_SPBOOL: return sp_box_bool((uint8_t)w != 0);
  /* a String of the runtime, which the callee made */
  case LUPE_FT_RTSTR: case LUPE_FT_NRTSTR: return sp_box_str_new(*(const char *const *)r);
  case LUPE_FT_CSTR: return sp_box_str_new(sp_str_dup_external(*(const char *const *)r));
  case LUPE_FT_CBINSTR: {
    const char *s = *(const char *const *)r;
    return sp_box_str_new(sp_str_as_binary(sp_str_from_bytes(s, (size_t)(sp_ffi_bin_len < 0 ? 0 : sp_ffi_bin_len))));
  }
  default: return sp_box_nil();
  }
}

static sp_RbVal lupe_ffi_call(LupeFfi *x, int argc, sp_RbVal *argv) {
  /* AOT code passes the declared arguments of a native_func and drops the
     rest, but for JSON.parse(s, symbolize_names: true) (codegen_call_class.c) */
  int symbolize = 0;
  if (x->native && argc > x->nargs) {
    if (!strcmp(x->name, "sp_json_parse") && argc == 2 && lupe_is_kwhash(argv[1])) {
      sp_RbVal key = sp_box_sym(sp_sym_intern("symbolize_names"));
      symbolize = sp_poly_truthy(lupe_send(argv[1], "[]", 1, &key));
    }
    argc = x->nargs;
  }
  if (argc != x->nargs)
    sp_raise_cls("ArgumentError", sp_sprintf("wrong number of arguments (given %d, expected %d)", argc, x->nargs));
  if (!x->fn) {
    x->fn = dlsym(RTLD_DEFAULT, x->name);
    if (!x->fn) sp_raise_cls("NotImplementedError", sp_sprintf("FFI function %s not found", x->name));
  }
  LupeFfiSlot slots[x->nargs ? x->nargs : 1];
  void *values[x->nargs ? x->nargs : 1];
  void *heap[x->nargs ? x->nargs : 1];
  for (int i = 0; i < x->nargs; i++) {
    heap[i] = NULL;
    lupe_ffi_arg(x->args[i], argv[i], &slots[i], &heap[i]);
    values[i] = &slots[i];
  }
  union { ffi_arg w; float f; double d; void *p; sp_RbVal v; } r;
  sp_ffi_bin_len = 0;
  ffi_call(&x->cif, FFI_FN(x->fn), &r, values);
  for (int i = 0; i < x->nargs; i++) free(heap[i]);
  sp_RbVal v = lupe_ffi_ret(x->ret, &r);
  return symbolize ? sp_json_symbolize(v) : v;
}

/* Module.name(buf) of ffi_read_*, and Module.name(buf, v) of ffi_write_*. */
static sp_RbVal lupe_ffi_field(LupeFfi *x, int argc, sp_RbVal *argv) {
  int write = x->kind == LUPE_FFI_WRITE;
  if (argc != 1 + write)
    sp_raise_cls("ArgumentError", sp_sprintf("wrong number of arguments (given %d, expected %d)", argc, 1 + write));
  char *base = lupe_ptr_arg(argv[0]);
  if (!base) sp_raise_cls("ArgumentError", "NULL pointer");
  char *at = base + x->offset;
  if (x->width == 0) {
    if (write) { void *p = lupe_ptr_arg(argv[1]); memcpy(at, &p, sizeof p); return argv[1]; }
    void *p;
    memcpy(&p, at, sizeof p);
    return lupe_ptr_box(p);
  }
  if (write) {
    int64_t v = lupe_ffi_int(argv[1]);
    memcpy(at, &v, (size_t)x->width);   /* the low bytes, on a little-endian host */
    return argv[1];
  }
  uint64_t u = 0;
  memcpy(&u, at, (size_t)x->width);
  if (x->sign && x->width < 8 && (u >> (x->width * 8 - 1) & 1)) u |= ~(uint64_t)0 << (x->width * 8);
  return sp_box_int((sp_int)u);
}

/* Whether v is of the type t as is, with no conversion. */
static int lupe_native_fits(LupeFfiType t, sp_RbVal v) {
  switch (t) {
  case LUPE_FT_ANY: return 1;
  case LUPE_FT_RTSTR: case LUPE_FT_TEXT: return lupe_is_string(v);
  case LUPE_FT_NRTSTR: return v.tag == SP_TAG_NIL || lupe_is_string(v);
  case LUPE_FT_SPINT: return lupe_is_integer(v);
  case LUPE_FT_DOUBLE: return v.tag == SP_TAG_FLT;
  case LUPE_FT_SPBOOL: return v.tag == SP_TAG_BOOL;
  case LUPE_FT_REGEXP: return lupe_is_regexp(v);
  default: return 0;
  }
}

/* The binding of a native name that a call takes, as spinel picks it at
   compile time (comp_native_method_find_typed): of the bindings of the
   arity, the first that takes each argument as is, else the first of the
   arity, else one with a :rest that takes the count. NULL for none. */
static LupeFfi *lupe_native_pick(const LupeFfi *set, int argc, const sp_RbVal *argv) {
  LupeFfi *arity = NULL, *rest = NULL;
  for (int i = 0; i < set->noverloads; i++) {
    LupeFfi *o = set->overloads[i];
    if (o->rest) {
      if (argc >= o->nargs && !rest) rest = o;
      continue;
    }
    if (o->nargs != argc) continue;
    int fits = 1;
    for (int a = 0; a < argc && fits; a++) fits = lupe_native_fits(o->args[a], argv[a]);
    if (fits) return o;
    if (!arity) arity = o;
  }
  return arity ? arity : rest;
}

/* A call of a native name: Name.new(...) for a constructor, or a method of
   the instance self. */
static sp_RbVal lupe_native_call(const LupeFfi *set, sp_RbVal self, int argc, sp_RbVal *argv) {
  LupeFfi *x = lupe_native_pick(set, argc, argv);
  if (!x)
    sp_raise_cls("ArgumentError", sp_sprintf("wrong number of arguments (given %d, expected %d)", argc,
                                             set->overloads[0]->nargs));
  if (set->ctor ? lupe_class_value(self) != set->cls : self.tag != SP_TAG_OBJ || self.cls_id != set->cls->inst_id)
    lupe_gap(sp_sprintf("a subclass of the native class %s", set->cls->name));
  if (!x->fn) {
    x->fn = dlsym(RTLD_DEFAULT, x->name);
    if (!x->fn) sp_raise_cls("NotImplementedError", sp_sprintf("native function %s not found", x->name));
  }
  /* a :text argument that is no String passes its #to_s, kept here */
  sp_PolyArray *keep = sp_PolyArray_new();
  SP_GC_ROOT(keep);
  int nc = 1 + x->nargs + 2 * x->rest;
  LupeFfiSlot slots[nc];
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
    if (x->args[i] == LUPE_FT_TEXT && !lupe_is_string(v)) {
      v = lupe_send(v, "to_s", 0, NULL);
      sp_PolyArray_push(keep, v);
    }
    lupe_ffi_arg(x->args[i], v, &slots[1 + i], &heap[1 + i]);
  }
  if (x->rest) {
    slots[1 + x->nargs].i64 = argc - x->nargs;
    slots[2 + x->nargs].p = argv + x->nargs;
  }
  union { ffi_arg w; float f; double d; void *p; sp_RbVal v; } r;
  sp_ffi_bin_len = 0;
  ffi_call(&x->cif, FFI_FN(x->fn), &r, values);
  for (int i = 0; i < nc; i++) free(heap[i]);
  if (set->ctor) return sp_box_obj(r.p, (int)set->cls->inst_id);
  if (x->ret == LUPE_FT_SELF) return self;
  return lupe_ffi_ret(x->ret, &r);
}

static sp_RbVal lupe_ffi_invoke(const LupeFfi *cx, sp_RbVal self, int argc, sp_RbVal *argv) {
  LupeFfi *x = (LupeFfi *)cx;
  switch (x->kind) {
  case LUPE_FFI_NATIVE: return lupe_native_call(x, self, argc, argv);
  case LUPE_FFI_FUNC: return lupe_ffi_call(x, argc, argv);
  case LUPE_FFI_BUFFER:
    if (argc != 0) sp_raise_cls("ArgumentError", sp_sprintf("wrong number of arguments (given %d, expected 0)", argc));
    return lupe_ptr_box(x->buf);
  default: return lupe_ffi_field(x, argc, argv);
  }
}

#else

struct LupeFfi { int unused; };

static int lupe_ffi_class(LupeClass *k, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  (void)k, (void)argc, (void)argv, (void)out;
  if (!strncmp(name, "ffi_", 4) || !strncmp(name, "native_", 7))
    lupe_gap(sp_sprintf("%s in a build with no FFI", name));
  return 0;
}

static sp_RbVal lupe_ffi_invoke(const struct LupeFfi *x, sp_RbVal self, int argc, sp_RbVal *argv) {
  (void)x, (void)self, (void)argc, (void)argv;
  lupe_gap("an FFI call in a build with no FFI");
}

#endif

#undef LUPE_IS
