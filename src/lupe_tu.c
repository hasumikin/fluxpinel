/* lupe_tu.c -- the definitions a spinel-generated translation unit supplies.

   libspinel_rt.a leaves some symbols to the generated TU: the symbol table,
   the class-name table, the user exception table and the init hooks. Lupe
   has no generated TU, so this file supplies a generic version of them. It
   knows only the builtin classes.

   spinel_rt.h declares these hooks static. Include this file into the one
   TU that uses the runtime; do not compile it on its own. */

#include "lupe_tu.h"

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

/* The walker names its own classes (ids >= 0) through this hook. */
static const char *(*lupe_class_name_hook)(sp_int cls_id);

static const char *sp_class_to_s(sp_Class c) {
  if (sp_class_nil_p(c)) return SPL("nil");
  if (c.name) return c.name;
  if (c.cls_id >= 0 && lupe_class_name_hook) return lupe_class_name_hook(c.cls_id);
  if (sp_class_sclass_p(c)) {
    sp_Class b = { sp_sclass_base_id(c.cls_id), NULL };
    return sp_sprintf(SPL("#<Class:%s>"), sp_class_display_fn ? sp_class_display_fn(b) : sp_class_to_s(b));
  }
  int i = -100 - (int)c.cls_id;
  if (i >= 0 && i < LUPE_NBUILTIN_CLASSES) return lupe_builtin_class_names[i];
  return sp_str_empty;
}

/* ---- exceptions: no user exception classes ---- */

const sp_int sp_exc_subclass_count = 0;
const sp_int sp_exc_subclass_ids[] = { 0 };

/* ---- init ---- */

/* The walker marks its own globals (constants, class ivars) here. */
static void (*lupe_mark_hook)(void);

static void sp_mark_user_globals(void) {
  sp_re_mark_globals();
  sp_mark_dyn_syms();
  if (lupe_mark_hook) lupe_mark_hook();
}

void lupe_tu_init(void) {
  sp_sym_name_fn = sp_sym_to_s;
  sp_class_name_fn = sp_class_to_s;
  sp_stack_overflow_raise_fn = sp_raise_stack_overflow;
  sp_stack_guard_init();
  sp_gc_mark_globals_hook = sp_mark_user_globals;
  sp_json_sym_intern_fn = sp_sym_intern;
}
