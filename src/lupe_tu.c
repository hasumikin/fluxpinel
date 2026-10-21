/* lupe_tu.c -- the definitions a spinel-generated translation unit supplies.

   libspinel_rt.a leaves some symbols to the generated TU: the symbol table,
   the class-name table, the user exception table and the init hooks. Lupe
   alone has no generated TU, so this file supplies a generic version of
   them. It knows only the builtin classes.

   The host build (LUPE_KERNEL) puts a kernel into the same TU: a program
   that spinel emits as a library (docs/design.md, section 3.6). The kernel
   supplies the symbol table, the class names and the user exception
   table, and sets the hooks of the runtime for its classes. Lupe keeps
   those hooks (lupe_kernel_hooks) and sets its own, which call them for a
   class of the kernel. LUPE_KERNEL names the init function of the kernel
   (spinel --ext-init NAME).

   spinel_rt.h declares these hooks static. Include this file into the one
   TU that uses the runtime; do not compile it on its own. */

#include "lupe_tu.h"

/* A class of the kernel, in the host build. */
static int lupe_kernel_cls(sp_int cls_id) { return cls_id >= 0 && cls_id < LUPE_CLS_BASE; }

/* The hooks that the kernel sets for its classes. All are NULL in a build
   with no kernel. */
static struct {
  const char *(*class_name)(sp_Class c);
  void (*mark)(void);
  const char *(*obj_cls_name)(int cls_id);
  const char *(*obj_to_s)(int cls_id, void *p);
  const char *(*obj_inspect)(int cls_id, void *p);
  int (*class_is_module)(sp_Class c);
  const char *(*exc_parent)(const char *name);
  sp_RbVal (*to_ary)(sp_RbVal v);
  sp_RbVal (*to_a)(sp_RbVal v);
  sp_int (*to_int)(int cls_id, void *p, int *ok);
  const char *(*to_str)(int cls_id, void *p);
  const char *(*to_path)(int cls_id, void *p);
  sp_user_binop_fn binop;
  sp_user_coerce_fn coerce;
  sp_obj_cmp_fn cmp;
} lupe_kernel_hooks;

#ifndef LUPE_KERNEL
/* ---- symbols: dynamic interning only (no symbols known at build time) ---- */

static const char *sp_dyn_syms0[SP_DYN_SYMS_MAX];
static const char **sp_dyn_syms = sp_dyn_syms0;
static int sp_dyn_cap = SP_DYN_SYMS_MAX;
static int sp_ndyn = 0;

static SP_NOINLINE SP_COLD void sp_dyn_syms_grow(void) {
  int nc = sp_dyn_cap > 0 ? sp_dyn_cap * 2 : 64;
  const char **np = (const char **)malloc(sizeof(*np) * (size_t)nc);
  if (!np) sp_raise_cls("NoMemoryError", "failed to grow the symbol table");
  memcpy(np, sp_dyn_syms, sizeof(*np) * (size_t)sp_ndyn);
  sp_dyn_syms = np;
  sp_dyn_cap = nc;
}

static void sp_mark_dyn_syms(void) {
  for (int i = 0; i < sp_ndyn; i++) sp_mark_string(sp_dyn_syms[i]);
}

static const char *sp_sym_to_s(sp_sym id) {
  if (id >= 0 && id < sp_ndyn) return sp_dyn_syms[id];
  return sp_str_empty;
}

/* s may be NULL when n is 0: prism gives no bytes for the empty symbol. */
static sp_sym sp_sym_intern_n(const char *s, size_t n) {
  for (int i = 0; i < sp_ndyn; i++) {
    const char *c = sp_dyn_syms[i];
    if (sp_str_byte_len(c) == n && (n == 0 || memcmp(c, s, n) == 0)) return (sp_sym)i;
  }
  if (sp_ndyn >= sp_dyn_cap) sp_dyn_syms_grow();
  char *d = sp_str_alloc_nogc(n);
  if (n) memcpy(d, s, n);
  sp_dyn_syms[sp_ndyn] = d;
  return (sp_sym)sp_ndyn++;
}

static sp_sym sp_sym_intern(const char *s) { return sp_sym_intern_n(s, s ? strlen(s) : 0); }
#endif

/* prism gives no bytes for the empty symbol, so s is NULL when n is 0. The
   intern of a kernel reads s[0], so the walker interns through this. */
static sp_sym lupe_sym_intern_n(const char *s, size_t n) { return sp_sym_intern_n(n ? s : "", n); }

/* ---- classes: the builtin ids -100 .. -180, in the order spinel emits ---- */

static const char *const lupe_builtin_class_names[] = {
  "Integer", "Float", "String", "Symbol", "Array", "Hash", "Range", "Time",
  "Module", "Class", "NilClass", "TrueClass", "FalseClass", "Numeric",
  "Comparable", "Enumerable", "Object", "BasicObject", "Proc", "Kernel", "IO",
  "File", "Exception", "StandardError", "RuntimeError", "TypeError",
  "ArgumentError", "NameError", "NoMethodError", "StopIteration", "Math",
  "Complex", "IndexError", "KeyError", "RangeError", "FloatDomainError",
  "ZeroDivisionError", "FrozenError", "IOError", "LocalJumpError",
  "NotImplementedError", "ScriptError", "Rational", "Regexp", "Enumerator",
  "Struct", "Data", "SyntaxError", "SecurityError", "RegexpError",
  "EncodingError", "SignalException", "Interrupt", "ThreadError",
  "FiberError", "ClosedQueueError", "UncaughtThrowError",
  "NoMatchingPatternError", "NoMatchingPatternKeyError", "EOFError",
  "Math::DomainError", "SystemExit", "Signal", "Process::Status",
  "Process::Tms", "Dir", "BasicSocket", "IPSocket", "TCPSocket", "TCPServer",
  "UDPSocket", "UNIXSocket", "UNIXServer", "Socket", "Thread",
  "Thread::Mutex", "Thread::Queue", "Thread::SizedQueue",
  "Thread::ConditionVariable", "Fiber", "MatchData",
};
#define LUPE_NBUILTIN_CLASSES ((int)(sizeof lupe_builtin_class_names / sizeof lupe_builtin_class_names[0]))

/* The walker names its own classes (ids >= LUPE_CLS_BASE) through this
   hook. */
static const char *(*lupe_class_name_hook)(sp_int cls_id);

static const char *lupe_class_to_s(sp_Class c) {
  if (sp_class_nil_p(c)) return SPL("nil");
  if (c.name) return c.name;
  if (lupe_kernel_cls(c.cls_id) && lupe_kernel_hooks.class_name) return lupe_kernel_hooks.class_name(c);
  if (c.cls_id >= 0 && lupe_class_name_hook) return lupe_class_name_hook(c.cls_id);
  if (sp_class_sclass_p(c)) {
    sp_Class b = { sp_sclass_base_id(c.cls_id), NULL };
    return sp_sprintf(SPL("#<Class:%s>"), sp_class_display_fn ? sp_class_display_fn(b) : lupe_class_to_s(b));
  }
  int i = -100 - (int)c.cls_id;
  if (i >= 0 && i < LUPE_NBUILTIN_CLASSES) return lupe_builtin_class_names[i];
  /* an exception class that only the runtime names (LoadError): the walker
     gave it an id below the table */
  if (lupe_class_name_hook) return lupe_class_name_hook(c.cls_id);
  return sp_str_empty;
}

#ifdef LUPE_KERNEL
/* the name that the runtime asks for (kernel_prelude.h) */
const char *lupe_class_to_s_any(sp_Class c) { return lupe_class_to_s(c); }
#else
static const char *sp_class_to_s(sp_Class c) { return lupe_class_to_s(c); }

/* ---- exceptions: no user exception classes ---- */

const sp_int sp_exc_subclass_count = 0;
const sp_int sp_exc_subclass_ids[] = { 0 };
#endif

/* ---- init ---- */

/* The walker marks its own globals (constants, class ivars) here. */
static void (*lupe_mark_hook)(void);

/* The kernel marks the symbols and the regexp globals in the host build. */
static void lupe_mark_globals(void) {
  if (lupe_kernel_hooks.mark) lupe_kernel_hooks.mark();
#ifndef LUPE_KERNEL
  sp_re_mark_globals();
  sp_mark_dyn_syms();
#endif
  if (lupe_mark_hook) lupe_mark_hook();
}

void lupe_tu_init(void) {
#ifdef LUPE_KERNEL
  /* the kernel sets the symbol table, the stack guard and its hooks */
  LUPE_KERNEL();
  lupe_kernel_hooks.class_name = sp_class_name_fn;
  lupe_kernel_hooks.mark = sp_gc_mark_globals_hook;
  lupe_kernel_hooks.obj_cls_name = sp_obj_cls_name_fn;
  lupe_kernel_hooks.obj_to_s = sp_obj_to_s_fn;
  lupe_kernel_hooks.obj_inspect = sp_obj_inspect_fn;
  lupe_kernel_hooks.class_is_module = sp_class_is_module_fn;
  lupe_kernel_hooks.exc_parent = sp_user_exc_parent_fn;
  lupe_kernel_hooks.to_ary = sp_obj_to_ary_fn;
  lupe_kernel_hooks.to_a = sp_obj_to_a_fn;
  lupe_kernel_hooks.to_int = sp_obj_to_int_fn;
  lupe_kernel_hooks.to_str = sp_obj_to_str_fn;
  lupe_kernel_hooks.to_path = sp_obj_to_path_fn;
  lupe_kernel_hooks.binop = sp_user_binop_hook;
  lupe_kernel_hooks.coerce = sp_user_coerce_hook;
  lupe_kernel_hooks.cmp = sp_obj_cmp_hook;
#else
  sp_sym_name_fn = sp_sym_to_s;
  sp_stack_overflow_raise_fn = sp_raise_stack_overflow;
  sp_stack_guard_init();
  sp_json_sym_intern_fn = sp_sym_intern;
#endif
  sp_class_name_fn = lupe_class_to_s;
  sp_gc_mark_globals_hook = lupe_mark_globals;
}
