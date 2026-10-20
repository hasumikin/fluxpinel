/* ffi.c -- the FFI of spinel at run time.

   spinel reads ffi_func and its kin at compile time and emits a direct C
   call for each function (docs/FFI.md of spinel). Lupe meets the same
   declarations in a module body at run time: it opens each ffi_lib with
   dlopen, finds each function with dlsym at its first call, and calls it
   through libffi with the C types that the declaration names.

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
  LUPE_FT_STR, LUPE_FT_BINSTR, LUPE_FT_PTR, LUPE_FT_FLOAT_ARRAY, LUPE_FT_INT_ARRAY, LUPE_FT_VOID
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

static ffi_type *lupe_ffi_ctype(LupeFfiType t) {
  switch (t) {
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

enum { LUPE_FFI_FUNC, LUPE_FFI_BUFFER, LUPE_FFI_READ, LUPE_FFI_WRITE };

typedef struct LupeFfi {
  int kind;
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

/* The declarations of a module body. Answers 1 and sets *out when it
   handles the call. */
static int lupe_ffi_class(LupeClass *k, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (strncmp(name, "ffi_", 4) && !LUPE_IS("native_func")) return 0;
  *out = sp_box_nil();
  if (LUPE_IS("ffi_lib")) {
    for (int i = 0; i < argc; i++) lupe_ffi_open(sp_poly_recv_s(argv[i], name));
    return 1;
  }
  if (LUPE_IS("ffi_cflags")) return 1;
  if (LUPE_IS("ffi_const") && argc == 2 && argv[0].tag == SP_TAG_SYM) {
    lupe_const_set(k, (sp_sym)argv[0].v.i, argv[1]);
    return 1;
  }
  if (LUPE_IS("ffi_func") && argc >= 3 && argv[0].tag == SP_TAG_SYM && lupe_is_array(argv[1])) {
    if (argc == 4 && !lupe_is_kwhash(argv[3])) return 0;   /* blocking: has no effect on one worker */
    LupeFfi *x = lupe_ffi_new(LUPE_FFI_FUNC);
    x->name = sp_sym_to_s((sp_sym)argv[0].v.i);
    x->nargs = (int)sp_poly_length(argv[1]);
    x->args = calloc((size_t)x->nargs + 1, sizeof *x->args);
    x->atypes = calloc((size_t)x->nargs + 1, sizeof *x->atypes);
    for (int i = 0; i < x->nargs; i++) {
      sp_RbVal t = sp_poly_arr_get(argv[1], i);
      if (t.tag == SP_TAG_SYM && !strcmp(sp_sym_to_s((sp_sym)t.v.i), "..."))
        lupe_gap("a variadic FFI function");
      x->args[i] = lupe_ffi_type(t);
      x->atypes[i] = lupe_ffi_ctype(x->args[i]);
    }
    x->ret = lupe_ffi_type(argv[2]);
    if (ffi_prep_cif(&x->cif, FFI_DEFAULT_ABI, (unsigned)x->nargs, lupe_ffi_ctype(x->ret), x->atypes) != FFI_OK)
      sp_raise_cls("ArgumentError", sp_sprintf("ffi_func :%s: no call interface for its types", x->name));
    lupe_ffi_define(k, x->name, x);
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
  if (LUPE_IS("ffi_struct") || LUPE_IS("ffi_callback") || LUPE_IS("ffi_source") || LUPE_IS("native_func"))
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
} LupeFfiSlot;

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
  default: return sp_box_nil();
  }
}

static sp_RbVal lupe_ffi_call(LupeFfi *x, int argc, sp_RbVal *argv) {
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
  union { ffi_arg w; float f; double d; void *p; } r;
  sp_ffi_bin_len = 0;
  ffi_call(&x->cif, FFI_FN(x->fn), &r, values);
  for (int i = 0; i < x->nargs; i++) free(heap[i]);
  return lupe_ffi_ret(x->ret, &r);
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

static sp_RbVal lupe_ffi_invoke(const LupeFfi *cx, int argc, sp_RbVal *argv) {
  LupeFfi *x = (LupeFfi *)cx;
  switch (x->kind) {
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
  if (!strncmp(name, "ffi_", 4) || LUPE_IS("native_func")) lupe_gap(sp_sprintf("%s in a build with no FFI", name));
  return 0;
}

static sp_RbVal lupe_ffi_invoke(const struct LupeFfi *x, int argc, sp_RbVal *argv) {
  (void)x, (void)argc, (void)argv;
  lupe_gap("an FFI call in a build with no FFI");
}

#endif

#undef LUPE_IS
