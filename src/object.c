/* object.c -- classes, modules, objects and constants of Fluxpinel.

   A Fluxpinel class is a FxClass with an id at or above FX_CLS_BASE. Its
   instance is a FxObject on the spinel heap, boxed as SP_TAG_OBJ with
   that id. The runtime reaches Fluxpinel objects through its hooks for user
   objects (sp_obj_to_s_fn and the others), which fx_object_init sets. */

#include "fluxpinel.h"

/* ---- method tables ---- */

/* The generation of the method tables and of the ancestors: each change
   of either makes it one more, and so every entry of the lookup cache
   (fx_find_method) goes stale at once. */
static unsigned fx_methods_gen = 1;

static FxMethod *fx_table_get(const FxMethodTable *t, sp_sym name) {
  for (int i = 0; i < t->n; i++)
    if (t->items[i]->name == name) return t->items[i];
  return NULL;
}

static void fx_table_put(FxMethodTable *t, FxMethod *m) {
  fx_methods_gen++;
  for (int i = 0; i < t->n; i++)
    if (t->items[i]->name == m->name) { t->items[i] = m; return; }
  if (t->n == t->cap) {
    t->cap = t->cap ? t->cap * 2 : 8;
    t->items = realloc(t->items, sizeof *t->items * (size_t)t->cap);
  }
  t->items[t->n++] = m;
}

static FxMethod *fx_method_new(FxMethodKind kind, sp_sym name, const pm_def_node_t *def,
                                   sp_sym ivar, FxClass *owner) {
  FxMethod *m = calloc(1, sizeof *m);
  *m = (FxMethod){ .name = name, .kind = kind, .def = def, .ivar = ivar, .owner = owner };
  return m;
}

/* A copy of m with the visibility vis. */
static FxMethod *fx_method_copy(const FxMethod *m, int vis) {
  FxMethod *c = malloc(sizeof *c);
  *c = *m;
  c->vis = vis;
  return c;
}

/* The instance methods that are private whatever the scope says. */
static int fx_always_private(sp_sym name) {
  const char *n = sp_sym_to_s(name);
  return !strcmp(n, "initialize") || !strcmp(n, "initialize_copy") || !strcmp(n, "initialize_clone") ||
         !strcmp(n, "initialize_dup") || !strcmp(n, "respond_to_missing?");
}

/* the number of builtin classes that the program reopened with a def */
static int fx_reopened_builtins;

/* 1 after a require of a feature of the runtime (io/console, ...): it
   gives builtin classes methods that the table of responds.c lacks */
static int fx_feature_loaded;

static void fx_define_method(FxClass *c, FxMethod *m, int singleton) {
  if (!singleton && fx_always_private(m->name)) m->vis = FX_VIS_PRIVATE;
  fx_table_put(singleton ? &c->smethods : &c->methods, m);
  if (!singleton && c->cls_id < 0 && c != L.object) fx_reopened_builtins++;
  /* the hook of the class: method_added(name) */
  if (!singleton) {
    FxMethod *hook = fx_find_smethod(c, sp_sym_intern("method_added"));
    if (hook) {
      sp_RbVal arg = sp_box_sym(m->name);
      fx_invoke(hook, fx_class_box(c), 1, &arg);
    }
  }
}

/* alias and alias_method: a copy of the method from under the name to. A
   builtin method has no entry to copy, which is a gap. */
static void fx_alias(FxClass *k, int singleton, sp_sym to, sp_sym from) {
  FxMethod *m = singleton ? fx_find_smethod(k, from) : fx_find_method(k, from);
  if (!m) fx_gap(sp_sprintf("an alias of the builtin method %s", sp_sym_to_s(from)));
  FxMethod *c = fx_method_copy(m, m->vis);
  c->name = to;
  fx_define_method(k, c, singleton);
}

/* ---- classes ---- */

static void fx_push_class(FxClass ***arr, int *n, int *cap, FxClass *c) {
  fx_methods_gen++;   /* an include or an extend changes the ancestors */
  if (*n == *cap) {
    *cap = *cap ? *cap * 2 : 8;
    *arr = realloc(*arr, sizeof **arr * (size_t)*cap);
  }
  (*arr)[(*n)++] = c;
}

static FxClass *fx_class_alloc(sp_int cls_id, const char *name, int is_module,
                                   FxClass *super, FxClass *lexical) {
  FxClass *c = calloc(1, sizeof *c);
  c->cls_id = cls_id;
  c->inst_id = cls_id;
  c->name = name;
  c->is_module = is_module;
  c->super = super;
  c->lexical = lexical;
  return c;
}

/* The builtin hierarchy that Fluxpinel knows: superclass and mixins. A builtin
   class with no row is a class under Object. */
static const struct {
  const char *name, *super, *mixin;
  int is_module;
} fx_builtin_tree[] = {
  { "BasicObject", NULL, NULL, 0 },
  { "Object", "BasicObject", "Kernel", 0 },
  { "Kernel", NULL, NULL, 1 },
  { "Comparable", NULL, NULL, 1 },
  { "Enumerable", NULL, NULL, 1 },
  { "Math", NULL, NULL, 1 },
  { "Module", "Object", NULL, 0 },
  { "Class", "Module", NULL, 0 },
  { "Numeric", "Object", "Comparable", 0 },
  { "Integer", "Numeric", NULL, 0 },
  { "Float", "Numeric", NULL, 0 },
  { "Rational", "Numeric", NULL, 0 },
  { "Complex", "Numeric", NULL, 0 },
  { "String", "Object", "Comparable", 0 },
  { "Symbol", "Object", "Comparable", 0 },
  { "Array", "Object", "Enumerable", 0 },
  { "Hash", "Object", "Enumerable", 0 },
  { "Range", "Object", "Enumerable", 0 },
  { "Struct", "Object", "Enumerable", 0 },
  { "Time", "Object", "Comparable", 0 },
  { "Enumerator", "Object", "Enumerable", 0 },
  { "Exception", "Object", NULL, 0 },
  { "ScriptError", "Exception", NULL, 0 },
  { "NotImplementedError", "ScriptError", NULL, 0 },
  { "SyntaxError", "ScriptError", NULL, 0 },
  { "SecurityError", "Exception", NULL, 0 },
  { "SignalException", "Exception", NULL, 0 },
  { "Interrupt", "SignalException", NULL, 0 },
  { "SystemExit", "Exception", NULL, 0 },
  { "StandardError", "Exception", NULL, 0 },
  { "RuntimeError", "StandardError", NULL, 0 },
  { "FrozenError", "RuntimeError", NULL, 0 },
  { "ArgumentError", "StandardError", NULL, 0 },
  { "TypeError", "StandardError", NULL, 0 },
  { "NameError", "StandardError", NULL, 0 },
  { "NoMethodError", "NameError", NULL, 0 },
  { "ZeroDivisionError", "StandardError", NULL, 0 },
  { "IndexError", "StandardError", NULL, 0 },
  { "KeyError", "IndexError", NULL, 0 },
  { "StopIteration", "IndexError", NULL, 0 },
  { "RangeError", "StandardError", NULL, 0 },
  { "FloatDomainError", "RangeError", NULL, 0 },
  { "IOError", "StandardError", NULL, 0 },
  { "EOFError", "IOError", NULL, 0 },
  { "LocalJumpError", "StandardError", NULL, 0 },
  { "FiberError", "StandardError", NULL, 0 },
  { "ThreadError", "StandardError", NULL, 0 },
  { "RegexpError", "StandardError", NULL, 0 },
  { "EncodingError", "StandardError", NULL, 0 },
  { "NoMatchingPatternError", "StandardError", NULL, 0 },
  { "NoMatchingPatternKeyError", "NoMatchingPatternError", NULL, 0 },
  { "UncaughtThrowError", "ArgumentError", NULL, 0 },
};

/* Core classes of CRuby that spinel's class table does not name. A class
   body for one of them reopens the builtin in CRuby; Fluxpinel cannot. */
static int fx_unknown_builtin(const char *name) {
  static const char *const names[] = { "Random", "Encoding", "Method", "UnboundMethod", "Binding",
                                       "Mutex", "Queue", "SizedQueue", "ConditionVariable", "ObjectSpace",
                                       "Marshal", "Warning", "RubyVM", NULL };
  for (int i = 0; names[i]; i++)
    if (!strcmp(names[i], name)) return 1;
  return 0;
}

/* The runtime id of a builtin class name, or 0. */
static sp_int fx_builtin_id(const char *name) {
  for (int i = 0; i < FX_NBUILTIN_CLASSES; i++)
    if (!strcmp(fx_builtin_class_names[i], name)) return -100 - i;
  return 0;
}

static void fx_const_set(FxClass *c, sp_sym name, sp_RbVal v);   /* the constants section */

/* The FxClass of a builtin class name, made on first use, or NULL. */
static FxClass *fx_builtin(const char *name) {
  /* the top-level names of the thread classes */
  static const char *const alias[][2] = { { "Queue", "Thread::Queue" }, { "SizedQueue", "Thread::SizedQueue" },
                                          { "Mutex", "Thread::Mutex" },
                                          { "ConditionVariable", "Thread::ConditionVariable" } };
  for (size_t i = 0; i < sizeof alias / sizeof alias[0]; i++)
    if (!strcmp(name, alias[i][0])) name = alias[i][1];
  for (int i = 0; i < L.nbuiltins; i++)
    if (!strcmp(L.builtins[i]->name, name)) return L.builtins[i];
  sp_int id = fx_builtin_id(name);
  /* an exception class that only the runtime knows (LoadError, ...): an id
     of its own below the table, and the runtime's superclass */
  const char *exc_parent = id ? NULL : sp_exc_parent_of_name(name);
  /* the runtime takes any name under Errno; a class of Errno is one that
     has a number, as CRuby defines them (Errno::ENOENT::Errno) */
  sp_int errnum = strncmp(name, "Errno::", 7) ? -1 : sp_errno_num(name);
  if (errnum == 0) return NULL;
  /* two names of one number are one class (EWOULDBLOCK is EAGAIN) */
  if (errnum > 0 && strcmp(sp_errno_class_name((int)errnum), name)) return fx_builtin(sp_errno_class_name((int)errnum));
  if (exc_parent) {
    FxClass *c = fx_class_alloc(FX_EXC_CLS_BASE - L.nbuiltins, strdup(name), 0, NULL, NULL);
    fx_push_class(&L.builtins, &L.nbuiltins, &L.builtins_cap, c);
    c->super = fx_builtin(exc_parent);
    if (errnum > 0) fx_const_set(c, sp_sym_intern("Errno"), sp_box_int(errnum));
    return c;
  }
  /* a module that spinel's codegen answers by name (process.c), and Errno,
     whose classes (Errno::ENOENT) the runtime names: an id of its own below
     the table too */
  if (!id && (!strcmp(name, "Process") || !strcmp(name, "GC") || !strcmp(name, "Errno"))) {
    FxClass *c = fx_class_alloc(FX_EXC_CLS_BASE - L.nbuiltins, strdup(name), 1, NULL, NULL);
    fx_push_class(&L.builtins, &L.nbuiltins, &L.builtins_cap, c);
    if (name[0] == 'P') fx_process_consts(c);
    return c;
  }
  if (!id) return NULL;
  const char *super = "Object", *mixin = NULL;
  int is_module = 0;
  for (size_t i = 0; i < sizeof fx_builtin_tree / sizeof fx_builtin_tree[0]; i++) {
    if (strcmp(fx_builtin_tree[i].name, name)) continue;
    super = fx_builtin_tree[i].super;
    mixin = fx_builtin_tree[i].mixin;
    is_module = fx_builtin_tree[i].is_module;
  }
  FxClass *c = fx_class_alloc(id, fx_builtin_class_names[-100 - id], is_module, NULL, NULL);
  fx_push_class(&L.builtins, &L.nbuiltins, &L.builtins_cap, c);
  if (super) c->super = fx_builtin(super);
  if (mixin) fx_push_class(&c->includes, &c->nincludes, &c->includes_cap, fx_builtin(mixin));
  return c;
}

static FxClass *fx_class_by_id(sp_int cls_id) {
  if (cls_id >= FX_CLS_BASE && cls_id < FX_CLS_BASE + L.nclasses) return L.classes[cls_id - FX_CLS_BASE];
  int i = -100 - (int)cls_id;
  if (i >= 0 && i < FX_NBUILTIN_CLASSES) return fx_builtin(fx_builtin_class_names[i]);
  /* a class that only the runtime names, or a class of the kernel (host.c) */
  for (int j = 0; (cls_id <= FX_EXC_CLS_BASE || fx_kernel_cls(cls_id)) && j < L.nbuiltins; j++)
    if (L.builtins[j]->cls_id == cls_id) return L.builtins[j];
  return NULL;
}

static FxClass *fx_class_new(const char *name, int is_module, FxClass *super, FxClass *lexical) {
  FxClass *c = fx_class_alloc(FX_CLS_BASE + L.nclasses, name, is_module, super, lexical);
  fx_push_class(&L.classes, &L.nclasses, &L.classes_cap, c);
  return c;
}

static sp_RbVal fx_class_box(const FxClass *c) {
  return sp_box_class((sp_Class){ c->cls_id, NULL });
}

/* The class or module that a Class value names, or NULL. */
static FxClass *fx_class_value(sp_RbVal v) {
  if (v.tag != SP_TAG_CLASS) return NULL;
  if (v.cls_id == SP_CLASS_BY_NAME) return v.v.s ? fx_builtin(v.v.s) : NULL;
  return fx_class_by_id(v.cls_id);
}

static int fx_is_fx_object(sp_RbVal v) {
  return v.tag == SP_TAG_OBJ && v.cls_id >= FX_CLS_BASE && v.v.p;
}

/* An instance of a native class (native_struct): a C struct of a package,
   which is no FxObject. */
static int fx_is_native_obj(sp_RbVal v) {
  if (!fx_is_fx_object(v)) return 0;
  FxClass *c = fx_class_by_id(v.cls_id);
  return c && c->native;
}

/* The class of the program with this full name ("A::B"), or NULL. */
static FxClass *fx_class_named(const char *name) {
  for (int i = 0; i < L.nclasses; i++)
    if (!strcmp(L.classes[i]->name, name)) return L.classes[i];
  return NULL;
}

/* The class of any value. An exception names its class, which can be a
   class of the program. */
static FxClass *fx_class_of(sp_RbVal v) {
  if (fx_is_fx_object(v)) return fx_class_by_id(v.cls_id);
  const char *name = sp_poly_class_name(v);
  FxClass *c = fx_builtin(name);
  if (!c && v.tag == SP_TAG_OBJ && v.cls_id == SP_BUILTIN_EXCEPTION) c = fx_class_named(name);
  return c ? c : L.object;
}

/* The receiver in the words of CRuby: "main", "nil", "class C", "an
   instance of C". */
static const char *fx_recv_words(sp_RbVal self) {
  if (fx_is_fx_object(self) && self.v.p == L.main.v.p) return "main";
  if (self.tag == SP_TAG_NIL) return "nil";
  if (self.tag == SP_TAG_BOOL) return self.v.b ? "true" : "false";
  if (self.tag == SP_TAG_CLASS) {
    FxClass *k = fx_class_value(self);
    return sp_sprintf("%s %s", k && k->is_module ? "module" : "class", k ? k->name : sp_poly_inspect(self));
  }
  return sp_sprintf("an instance of %s", sp_poly_class_name(self));
}

/* The error of a call that missed for the reason why (FX_MISS_*):
   NameError for a bare name, else NoMethodError. */
static __attribute__((noreturn)) void fx_no_method(sp_RbVal self, const char *name, int why) {
  const char *on = fx_recv_words(self);
  if (why == FX_MISS_VCALL)
    sp_raise_cls("NameError", sp_sprintf("undefined local variable or method '%s' for %s", name, on));
  if (why == FX_MISS_PRIVATE || why == FX_MISS_PROTECTED)
    sp_raise_cls("NoMethodError", sp_sprintf("%s method '%s' called for %s",
                                             why == FX_MISS_PRIVATE ? "private" : "protected", name, on));
  sp_raise_cls("NoMethodError", sp_sprintf("undefined method '%s' for %s", name, on));
}

/* Whether name is a method of Object or Kernel in CRuby. Such a call does
   not miss in CRuby, so when Fluxpinel has no such builtin, the call is a
   gap of Fluxpinel and not one for method_missing. */
static int fx_object_method_name(const char *name) {
  static const char *const names[] = {
    "!", "!=", "!~", "<=>", "==", "===", "Array", "Complex",
    "Float", "Hash", "Integer", "Rational", "String", "__callee__", "__dir__", "__id__",
    "__method__", "__send__", "`", "abort", "at_exit", "autoload", "autoload?", "binding",
    "block_given?", "caller", "caller_locations", "catch", "class", "clone", "define_singleton_method", "display",
    "dup", "enum_for", "eql?", "equal?", "eval", "exec", "exit", "exit!",
    "extend", "fail", "fork", "format", "freeze", "frozen?", "gets", "global_variables",
    "hash", "inspect", "instance_eval", "instance_exec", "instance_of?", "instance_variable_defined?",
    "instance_variable_get", "instance_variable_set", "instance_variables", "is_a?", "iterator?", "itself",
    "kind_of?", "lambda", "load", "local_variables", "loop", "method", "methods", "nil?",
    "object_id", "open", "p", "pp", "print", "printf", "private_methods", "proc",
    "protected_methods", "public_method", "public_methods", "public_send", "putc", "puts", "raise", "rand",
    "readline", "readlines", "remove_instance_variable", "require", "require_relative", "respond_to?", "select",
    "send", "set_trace_func", "singleton_class", "singleton_method", "singleton_methods", "sleep", "spawn",
    "sprintf", "srand", "syscall", "system", "tap", "test", "then", "throw",
    "to_enum", "to_s", "trace_var", "trap", "untrace_var", "warn", "yield_self", NULL
  };
  for (int i = 0; names[i]; i++)
    if (!strcmp(name, names[i])) return 1;
  return 0;
}

static FxMethod *fx_find_hook(sp_RbVal r, const char *hook) {
  sp_sym s = sp_sym_intern(hook);
  FxClass *k = fx_class_value(r);
  FxMethod *m = k ? fx_find_smethod(k, s) : NULL;
  return m ? m : fx_find_method(fx_class_of(r), s);
}

/* The hook of a missing method: method_missing(:name, *args) of the
   receiver, with the block of the call. Answers 0 when the program
   defines no method_missing for the receiver. why (FX_MISS_*) is the
   reason of the miss, for the error of a super from the hook
   (L.mm_why). */
static int fx_method_missing(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                               int why, sp_RbVal *out) {
  FxMethod *m = fx_object_method_name(name) ? NULL : fx_find_hook(r, "method_missing");
  if (!m) return 0;
  SP_GC_SAVE();
  sp_PolyArray *a = sp_PolyArray_new();
  SP_GC_ROOT(a);
  sp_PolyArray_push(a, sp_box_sym(sp_sym_intern(name)));
  for (int i = 0; i < argc; i++) sp_PolyArray_push(a, argv[i]);
  int saved = L.mm_why;
  L.mm_why = why;
  *out = fx_invoke_b(m, r, (int)a->len, a->data, blk);
  L.mm_why = saved;
  return 1;
}

/* The Kernel methods that are private in CRuby. A call of one with a
   receiver is a private call, unless the receiver has a public method of
   the name (IO#puts, Array#select). */
static int fx_kernel_private(const char *name) {
  static const char *const names[] = {
    "Array", "Complex", "Float", "Hash", "Integer", "Rational", "String", "__callee__", "__dir__", "__method__",
    "`", "abort", "at_exit", "autoload", "autoload?", "binding", "block_given?", "caller", "caller_locations",
    "catch", "eval", "exec", "exit", "exit!", "fail", "fork", "format", "gets", "global_variables", "iterator?",
    "lambda", "load", "local_variables", "loop", "open", "p", "pp", "print", "printf", "proc", "putc", "puts",
    "raise", "rand", "readline", "readlines", "require", "require_relative", "select", "sleep", "spawn",
    "sprintf", "srand", "syscall", "system", "test", "throw", "trap", "warn", "initialize", "initialize_copy",
    "initialize_clone", "initialize_dup", "respond_to_missing?", "method_missing", NULL
  };
  for (int i = 0; names[i]; i++)
    if (!strcmp(name, names[i])) return 1;
  return 0;
}

/* The ancestors of c, in method lookup order: c, its mixins (the last
   included first, each with its own mixins), then the same for the
   superclass. Answers the count. */
static int fx_ancestors(const FxClass *c, const FxClass **out, int cap) {
  int n = 0;
  for (const FxClass *k = c; k; k = k->super) {
    const FxClass *stack[64];
    int sp = 0;
    stack[sp++] = k;
    while (sp > 0) {
      const FxClass *m = stack[--sp];
      int seen = 0;
      for (int i = 0; i < n; i++) if (out[i] == m) seen = 1;
      if (seen) continue;
      if (n < cap) out[n++] = m;
      /* push in include order, so that the last included pops first */
      for (int i = 0; i < m->nincludes && sp < 64; i++) stack[sp++] = m->includes[i];
    }
  }
  return n;
}

static SP_NOINLINE FxMethod *fx_find_method_slow(const FxClass *c, sp_sym name) {
  const FxClass *anc[128];
  int n = fx_ancestors(c, anc, 128);
  for (int i = 0; i < n; i++) {
    FxMethod *m = fx_table_get(&anc[i]->methods, name);
    if (m) return m;
  }
  return NULL;
}

/* The lookup cache: one entry for each (class, name) slot, with the answer
   of the walk of the ancestors, a miss included. An entry of an older
   fx_methods_gen is stale. */
#define FX_MCACHE_SIZE 4096
static struct {
  const FxClass *c;
  sp_sym name;
  unsigned gen;
  FxMethod *m;
} fx_mcache[FX_MCACHE_SIZE];

static FxMethod *fx_find_method(const FxClass *c, sp_sym name) {
  size_t i = (((uintptr_t)c >> 4) ^ ((size_t)name * 2654435761u)) & (FX_MCACHE_SIZE - 1);
  if (fx_mcache[i].gen == fx_methods_gen && fx_mcache[i].c == c && fx_mcache[i].name == name)
    return fx_mcache[i].m;
  FxMethod *m = fx_find_method_slow(c, name);
  fx_mcache[i].c = c;
  fx_mcache[i].name = name;
  fx_mcache[i].gen = fx_methods_gen;
  fx_mcache[i].m = m;
  return m;
}

/* A class method: the singleton methods of c and its superclasses, and the
   methods of the modules that they extend. */
static FxMethod *fx_find_smethod(const FxClass *c, sp_sym name) {
  for (const FxClass *k = c; k; k = k->super) {
    FxMethod *m = fx_table_get(&k->smethods, name);
    if (m) return m;
    for (int i = k->nextends - 1; i >= 0; i--)
      if ((m = fx_find_method(k->extends[i], name))) return m;
  }
  return NULL;
}

/* The method that super calls from m, for a receiver of class c. */
static FxMethod *fx_find_super(const FxClass *c, const FxMethod *m) {
  const FxClass *anc[128];
  int n = fx_ancestors(c, anc, 128), i = 0;
  while (i < n && anc[i] != m->owner) i++;
  for (i++; i < n; i++) {
    FxMethod *s = fx_table_get(&anc[i]->methods, m->name);
    if (s) return s;
  }
  return NULL;
}

static int fx_is_a(sp_RbVal v, const FxClass *k) {
  const FxClass *anc[128];
  int n = fx_ancestors(fx_class_of(v), anc, 128);
  for (int i = 0; i < n; i++) if (anc[i] == k) return 1;
  return 0;
}

/* ---- constants ---- */

static FxConst *fx_const_own(const FxClass *c, sp_sym name) {
  for (int i = 0; i < c->nconsts; i++)
    if (c->consts[i].name == name) return &c->consts[i];
  return NULL;
}

static void fx_const_set(FxClass *c, sp_sym name, sp_RbVal v) {
  /* an anonymous class takes the name of the first constant that holds it */
  FxClass *anon = fx_class_value(v);
  if (anon && anon->anon) {
    anon->anon = 0;
    anon->name = c == L.object ? sp_sym_to_s(name) : strdup(sp_sprintf("%s::%s", c->name, sp_sym_to_s(name)));
  }
  FxConst *k = fx_const_own(c, name);
  if (k) { k->value = v; return; }
  if (c->nconsts == c->consts_cap) {
    c->consts_cap = c->consts_cap ? c->consts_cap * 2 : 8;
    c->consts = realloc(c->consts, sizeof *c->consts * (size_t)c->consts_cap);
  }
  c->consts[c->nconsts++] = (FxConst){ name, v };
}

/* ---- class variables ---- */

/* The class variable of c or of its ancestors, the nearest one first;
   *owner is the class that holds it. */
static FxConst *fx_cvar_find(const FxClass *c, sp_sym name, FxClass **owner) {
  const FxClass *anc[128];
  int n = fx_ancestors(c, anc, 128);
  for (int i = 0; i < n; i++)
    for (int j = 0; j < anc[i]->ncvars; j++)
      if (anc[i]->cvars[j].name == name) {
        if (owner) *owner = (FxClass *)anc[i];
        return &anc[i]->cvars[j];
      }
  return NULL;
}

/* The class of a class variable access: the lexical class. At the top
   level there is none, as in CRuby. */
static FxClass *fx_cvar_base(FxClass *cref) {
  if (!cref || cref == L.object) sp_raise_cls("RuntimeError", "class variable access from toplevel");
  return cref;
}

static void fx_cvar_check_name(const char *n) {
  if (!(n[0] == '@' && n[1] == '@' && n[2] && !isdigit((unsigned char)n[2])))
    sp_raise_cls("NameError", sp_sprintf("'%s' is not allowed as a class variable name", n));
}

static sp_RbVal fx_cvar_get(FxClass *c, sp_sym name) {
  FxConst *v = fx_cvar_find(c, name, NULL);
  if (!v)
    sp_raise_cls("NameError", sp_sprintf("uninitialized class variable %s in %s", sp_sym_to_s(name), c->name));
  return v->value;
}

/* Writes the class variable where an ancestor holds it, else in c. */
static void fx_cvar_set(FxClass *c, sp_sym name, sp_RbVal v) {
  if (c->frozen) sp_raise_cls("FrozenError", sp_sprintf("can't modify frozen #<Class:%s>", c->name));
  FxConst *k = fx_cvar_find(c, name, NULL);
  if (k) { k->value = v; return; }
  if (c->ncvars == c->cvars_cap) {
    c->cvars_cap = c->cvars_cap ? c->cvars_cap * 2 : 4;
    c->cvars = realloc(c->cvars, sizeof *c->cvars * (size_t)c->cvars_cap);
  }
  c->cvars[c->ncvars++] = (FxConst){ name, v };
}

/* class_variables: the names of c, then of its ancestors (inherit) */
static sp_RbVal fx_cvar_names(const FxClass *c, int inherit) {
  sp_PolyArray *a = sp_PolyArray_new();
  SP_GC_ROOT(a);
  const FxClass *anc[128];
  int n = inherit ? fx_ancestors(c, anc, 128) : 1;
  if (!inherit) anc[0] = c;
  for (int i = 0; i < n; i++)
    for (int j = 0; j < anc[i]->ncvars; j++) {
      sp_RbVal s = sp_box_sym(anc[i]->cvars[j].name);
      int seen = 0;
      for (sp_int k = 0; k < a->len; k++) seen |= a->data[k].v.i == s.v.i;
      if (!seen) sp_PolyArray_push(a, s);
    }
  return sp_box_poly_array(a);
}

/* A constant of scope or its ancestors, then a builtin class. With top,
   the lookup goes on to Object and its ancestors. */
static int fx_const_in(const FxClass *scope, sp_sym name, sp_RbVal *out, int top) {
  const FxClass *anc[128];
  int n = fx_ancestors(scope, anc, 128);
  for (int i = 0; i < n && (top || anc[i] != L.object); i++) {
    FxConst *k = fx_const_own(anc[i], name);
    if (k) { *out = k->value; return 1; }
  }
  if (top && (scope == L.object || scope->is_module)) {
    FxConst *k = fx_const_own(L.object, name);
    if (k) { *out = k->value; return 1; }
    FxClass *b = fx_builtin(sp_sym_to_s(name));
    if (b) { *out = fx_class_box(b); return 1; }
    if (fx_autoload(name)) return fx_const_in(scope, name, out, top);
  }
  /* a builtin class nested in a builtin one (Thread::Queue), or a class of
     the kernel nested in another one */
  if ((scope->cls_id < 0 || fx_kernel_cls(scope->cls_id)) && scope != L.object) {
    FxClass *b = fx_builtin(sp_sprintf("%s::%s", scope->name, sp_sym_to_s(name)));
    if (b) { *out = fx_class_box(b); return 1; }
  }
  return 0;
}

static int fx_const_scoped(const FxClass *scope, sp_sym name, sp_RbVal *out) {
  return fx_const_in(scope, name, out, 1);
}

/* A constant of a path (A::B), as CRuby reads it: the ancestors of A,
   but not Object and its ancestors unless A is Object. Thus M::String of
   a module M is a NameError, and so is a RUBY_ENGINE that only Object
   has. */
static int fx_const_path(const FxClass *scope, sp_sym name, sp_RbVal *out) {
  return fx_const_in(scope, name, out, scope == L.object);
}

/* A constant as a bare name reads it: the lexical scopes from cref out
   (but the top level), then the ancestors of cref, then Object. */
static int fx_const_lookup(const FxClass *cref, sp_sym name, sp_RbVal *out) {
  for (const FxClass *k = cref; k && k != L.object; k = k->lexical) {
    FxConst *c = fx_const_own(k, name);
    if (c) { *out = c->value; return 1; }
  }
  if (fx_const_scoped(cref, name, out)) return 1;
  return fx_const_scoped(L.object, name, out);
}

static __attribute__((noreturn)) void fx_const_missing(const char *name) {
  fx_trace_missing("const#", name, 0);
  sp_raise_cls("NameError", sp_sprintf("uninitialized constant %s", name));
}

/* The name of a constant of a path in a message: "A::B", or "B" in Object. */
static const char *fx_const_full(const FxClass *scope, sp_sym name) {
  return scope == L.object ? sp_sym_to_s(name) : sp_sprintf("%s::%s", scope->name, sp_sym_to_s(name));
}

/* ---- objects and instance variables ---- */

static void fx_obj_scan(void *p) {
  FxObject *o = (FxObject *)p;
  if (o->iv) sp_gc_mark(o->iv);
}

static sp_RbVal fx_new_object(FxClass *c) {
  FxObject *o = (FxObject *)sp_gc_alloc(sizeof *o, NULL, fx_obj_scan);
  o->cls_id = c->inst_id;
  o->iv = NULL;
  return sp_box_obj(o, (int)c->inst_id);
}

/* The slot cache: one entry for each (class, name) slot. A slot of a class
   never moves, so an entry stays right; a miss of the list is not kept,
   since a later set adds the slot. */
#define FX_ICACHE_SIZE 4096
static struct {
  const FxClass *c;
  sp_sym name;
  int slot;
} fx_icache[FX_ICACHE_SIZE];

static int fx_ivar_slot(FxClass *c, sp_sym name, int create) {
  size_t h = (((uintptr_t)c >> 4) ^ ((size_t)name * 2654435761u)) & (FX_ICACHE_SIZE - 1);
  if (fx_icache[h].c == c && fx_icache[h].name == name) return fx_icache[h].slot;
  int slot = -1;
  for (int i = 0; i < c->nivars; i++)
    if (c->ivars[i] == name) { slot = i; break; }
  if (slot < 0) {
    if (!create) return -1;
    if (c->nivars == c->ivars_cap) {
      c->ivars_cap = c->ivars_cap ? c->ivars_cap * 2 : 8;
      c->ivars = realloc(c->ivars, sizeof *c->ivars * (size_t)c->ivars_cap);
    }
    c->ivars[c->nivars] = name;
    slot = c->nivars++;
  }
  fx_icache[h].c = c;
  fx_icache[h].name = name;
  fx_icache[h].slot = slot;
  return slot;
}

/* The ivars of a class value (class-level @x) live in a hidden object. */
static sp_PolyArray *fx_class_ivars;   /* [class value, object, ...]; marked by the hook */

/* An exception keeps its ivars in a hidden object too. The table holds
   the exception, so an exception with an ivar lives until the end. */
static sp_RbVal fx_ivar_holder(sp_RbVal self) {
  if (fx_is_fx_object(self)) return self;
  int exc = self.tag == SP_TAG_OBJ && self.cls_id == SP_BUILTIN_EXCEPTION && self.v.p;
  if (self.tag != SP_TAG_CLASS && !exc) return sp_box_nil();
  FxClass *c = exc ? NULL : fx_class_value(self);
  if (!c && !exc) return sp_box_nil();
  if (!fx_class_ivars) fx_class_ivars = sp_PolyArray_new();
  for (sp_int i = 0; i + 1 < fx_class_ivars->len; i += 2) {
    sp_RbVal k = fx_class_ivars->data[i];
    if (exc ? k.v.p == self.v.p : k.tag == SP_TAG_CLASS && k.cls_id == c->cls_id) return fx_class_ivars->data[i + 1];
  }
  sp_RbVal holder = fx_new_object(fx_builtin("Object"));
  SP_GC_ROOT_RBVAL(holder);
  sp_PolyArray_push(fx_class_ivars, self);
  sp_PolyArray_push(fx_class_ivars, holder);
  return holder;
}

/* ---- the ivars of an object of the kernel ----

   spinel writes a table for the default inspect into the kernel
   (sp_insp_tab): the field of each ivar and its kind. tools/ivar_gen.rb
   writes a box for each kind (fx_iv_box). An ivar that the object has not
   set reads as nil, and instance_variables leaves it out. The ivars that a
   computed instance_variable_set adds (sp_bivar) are not read. */

#ifdef FX_KERNEL_IVARS
/* The row of an object of the kernel, or NULL for any other value. */
static const sp_insp_cls *fx_kernel_ivrow(sp_RbVal v) {
  if (v.tag != SP_TAG_OBJ || !v.v.p || !fx_kernel_cls(v.cls_id)) return NULL;
  const sp_insp_cls *d = v.cls_id < FX_KERNEL_NINSP ? sp_insp_tab[v.cls_id] : NULL;
  if (!d) fx_gap("the ivars of an object of the kernel whose class defines inspect");
  return d;
}

static sp_RbVal fx_kernel_iv_value(const sp_insp_iv *v, char *o) {
  void *s = o + v->off;
  if (v->obj == 2) return sp_box_nullable_obj_dyn(*(void **)s, v->arg);
  if (v->obj) return *(void **)s ? sp_box_obj(*(void **)s, v->arg) : sp_box_nil();
  return fx_iv_box[v->arg](s, v->nb >= 0 ? (const uint8_t *)(o + v->nb) : NULL);
}

static sp_RbVal fx_kernel_ivar_get(const sp_insp_cls *d, sp_RbVal self, sp_sym name) {
  char *o = (char *)self.v.p;
  const char *nm = sp_sym_to_s(name);
  for (int i = 0; i < d->niv; i++) {
    const sp_insp_iv *v = &d->ivs[i];
    if (strcmp(sp_insp_pool + v->name, nm)) continue;
    return d->any1 && !sp_insp_set(v, o) ? sp_box_nil() : fx_kernel_iv_value(v, o);
  }
  return sp_box_nil();
}

/* the names of the ivars that the object has set, in the order of inspect */
static sp_RbVal fx_kernel_ivars(const sp_insp_cls *d, sp_RbVal self) {
  char *o = (char *)self.v.p;
  sp_PolyArray *a = sp_PolyArray_new();
  SP_GC_ROOT(a);
  int ix[d->niv > 0 ? d->niv : 1];
  int n = d->niv;
  if (d->ranked) n = d->order(o, ix);
  else for (int i = 0; i < n; i++) ix[i] = i;
  for (int k = 0; k < n; k++) {
    const sp_insp_iv *v = &d->ivs[ix[k]];
    if (d->any1 && !d->ranked && !sp_insp_set(v, o)) continue;
    sp_PolyArray_push(a, sp_box_sym(sp_sym_intern(sp_insp_pool + v->name)));
  }
  return sp_box_poly_array(a);
}
#endif

static sp_RbVal fx_ivar_get(sp_RbVal self, sp_sym name) {
#ifdef FX_KERNEL_IVARS
  const sp_insp_cls *d = fx_kernel_ivrow(self);
  if (d) return fx_kernel_ivar_get(d, self, name);
#endif
  sp_RbVal h = fx_ivar_holder(self);
  if (!fx_is_fx_object(h) || fx_is_native_obj(h)) return sp_box_nil();
  FxObject *o = (FxObject *)h.v.p;
  int slot = fx_ivar_slot(fx_class_by_id(o->cls_id), name, 0);
  if (slot < 0 || !o->iv || slot >= o->iv->len) return sp_box_nil();
  return o->iv->data[slot];
}

/* Whether self has the ivar: its slot is there, as instance_variables
   lists it. An ivar of a kernel object is there when it is not nil. */
static int fx_ivar_defined(sp_RbVal self, sp_sym name) {
#ifdef FX_KERNEL_IVARS
  const sp_insp_cls *d = fx_kernel_ivrow(self);
  if (d) return fx_kernel_ivar_get(d, self, name).tag != SP_TAG_NIL;
#endif
  sp_RbVal h = fx_ivar_holder(self);
  if (!fx_is_fx_object(h) || fx_is_native_obj(h)) return 0;
  FxObject *o = (FxObject *)h.v.p;
  int slot = fx_ivar_slot(fx_class_by_id(o->cls_id), name, 0);
  return slot >= 0 && o->iv && slot < o->iv->len;
}

static void fx_ivar_set(sp_RbVal self, sp_sym name, sp_RbVal v) {
  SP_GC_ROOT_RBVAL(self);
  SP_GC_ROOT_RBVAL(v);
  sp_RbVal h = fx_ivar_holder(self);
  if (!fx_is_fx_object(h) || fx_is_native_obj(h)) fx_gap(sp_sprintf("ivar on a %s", sp_poly_class_name(self)));
  SP_GC_ROOT_RBVAL(h);
  FxObject *o = (FxObject *)h.v.p;
  /* a frozen object: an ivar, an attr writer or a Struct member */
  if (fx_is_fx_object(self) && sp_gc_is_frozen(self.v.p)) {
    const char *msg = sp_sprintf("can't modify frozen %s: %s", fx_class_of(self)->name, sp_poly_inspect(self));
    sp_exc_stage_recv(self);
    sp_raise_cls("FrozenError", msg);
  }
  int slot = fx_ivar_slot(fx_class_by_id(o->cls_id), name, 1);
  if (!o->iv) {
    sp_PolyArray *iv = sp_PolyArray_new();
    sp_gc_wb(o);
    o->iv = iv;
  }
  while (o->iv->len <= slot) sp_PolyArray_push(o->iv, sp_box_nil());
  sp_PolyArray_set(o->iv, slot, v);
}

/* Keeps the slot of name in ic, when self is an object of the program
   that has the slot. Only such an object keeps its ivars in o->iv. */
static void fx_ivar_learn(sp_RbVal self, sp_sym name, FxIvarCache *ic) {
  if (!fx_is_fx_object(self) || fx_is_native_obj(self)) return;
  int slot = fx_ivar_slot(fx_class_by_id(self.cls_id), name, 0);
  if (slot < 0) return;
  ic->cls_id = self.cls_id;
  ic->slot = slot;
}

/* fx_ivar_get and fx_ivar_set with the cache ic of one site. A hit needs
   only the class id of the box: ic keeps the id of an object of the
   program, which is no native object. */
#define FX_IC_HIT(self, ic) ((self).tag == SP_TAG_OBJ && (self).cls_id == (ic)->cls_id && (self).cls_id >= FX_CLS_BASE && (self).v.p)

static sp_RbVal fx_ivar_get_c(sp_RbVal self, sp_sym name, FxIvarCache *ic) {
  if (FX_IC_HIT(self, ic)) {
    const FxObject *o = (const FxObject *)self.v.p;
    return o->iv && ic->slot < o->iv->len ? o->iv->data[ic->slot] : sp_box_nil();
  }
  sp_RbVal v = fx_ivar_get(self, name);
  fx_ivar_learn(self, name, ic);
  return v;
}

static void fx_ivar_set_c(sp_RbVal self, sp_sym name, sp_RbVal v, FxIvarCache *ic) {
  if (FX_IC_HIT(self, ic)) {
    FxObject *o = (FxObject *)self.v.p;
    if (o->iv && ic->slot < o->iv->len && !sp_gc_is_frozen(o)) {
      sp_PolyArray_set(o->iv, ic->slot, v);
      return;
    }
  }
  SP_GC_ROOT_RBVAL(self);
  fx_ivar_set(self, name, v);
  fx_ivar_learn(self, name, ic);
}

/* ---- the runtime hooks ---- */

static const char *fx_hook_class_name(sp_int cls_id) {
  FxClass *c = fx_class_by_id(cls_id);
  return c ? c->name : sp_str_empty;
}

/* Each hook below calls the hook of the kernel for a class of the kernel
   (fx_tu.c). */
static const char *fx_hook_obj_cls_name(int cls_id) {
  if (fx_kernel_cls(cls_id) && fx_kernel_hooks.obj_cls_name)
    return fx_kernel_hooks.obj_cls_name(cls_id);
  return fx_hook_class_name(cls_id);
}

/* A user method on obj answers a String, or NULL when there is none. */
static const char *fx_user_string(sp_RbVal obj, const char *name) {
  FxMethod *m = fx_find_method(fx_class_of(obj), sp_sym_intern(name));
  if (!m) return NULL;
  sp_RbVal v = fx_invoke(m, obj, 0, NULL);
  return sp_poly_to_s(v);
}

/* Object#to_s and Object#inspect, in the format of CRuby and spinel. */
static const char *fx_default_to_s(int cls_id, void *p) {
  if (p == L.main.v.p) return SPL("main");
  if (fx_record_of(fx_class_by_id(cls_id))) return fx_record_inspect(sp_box_obj(p, cls_id));
  return sp_sprintf("#<%s:0x%016llx>", fx_hook_class_name(cls_id), (unsigned long long)(uintptr_t)p);
}

static const char *fx_default_inspect(int cls_id, void *p) {
  if (p == L.main.v.p) return SPL("main");
  FxClass *c = fx_class_by_id(cls_id);
  if (fx_record_of(c)) return fx_record_inspect(sp_box_obj(p, cls_id));
  if (c->native) return fx_default_to_s(cls_id, p);
  FxObject *o = (FxObject *)p;
  if (sp_poly_recur_seen(SP_POLY_RECUR_INSPECT, p, NULL))
    return sp_sprintf("#<%s:0x%016llx ...>", c->name, (unsigned long long)(uintptr_t)p);
  int mark = sp_poly_recur_push(SP_POLY_RECUR_INSPECT, p, NULL);
  sp_String *out = sp_String_new(sp_sprintf("#<%s:0x%016llx", c->name, (unsigned long long)(uintptr_t)p));
  SP_GC_ROOT(out);
  int first = 1;
  for (int i = 0; o->iv && i < c->nivars && i < o->iv->len; i++) {
    sp_String_append(out, first ? " " : ", ");
    sp_String_append(out, sp_sym_to_s(c->ivars[i]));
    sp_String_append(out, "=");
    sp_String_append(out, sp_poly_inspect(o->iv->data[i]));
    first = 0;
  }
  sp_String_append(out, ">");
  sp_poly_recur_pop(mark);
  return out->data;
}

static const char *fx_hook_obj_to_s(int cls_id, void *p) {
  if (fx_kernel_cls(cls_id) && fx_kernel_hooks.obj_to_s)
    return fx_kernel_hooks.obj_to_s(cls_id, p);
  sp_RbVal obj = sp_box_obj(p, cls_id);
  SP_GC_ROOT_RBVAL(obj);
  const char *s = p == L.main.v.p ? NULL : fx_user_string(obj, "to_s");
  return s ? s : fx_default_to_s(cls_id, p);
}

static const char *fx_hook_obj_inspect(int cls_id, void *p) {
  if (fx_kernel_cls(cls_id) && fx_kernel_hooks.obj_inspect)
    return fx_kernel_hooks.obj_inspect(cls_id, p);
  sp_RbVal obj = sp_box_obj(p, cls_id);
  SP_GC_ROOT_RBVAL(obj);
  const char *s = p == L.main.v.p ? NULL : fx_user_string(obj, "inspect");
  return s ? s : fx_default_inspect(cls_id, p);
}

/* The runtime asks a user object for an operator (==, <=>, + ...) through
   this hook, for example inside Array#include? or Array#sort. */
static __attribute__((noreturn)) void fx_cmp_failed(sp_RbVal a, sp_RbVal b) {
  sp_raise_cls("ArgumentError", sp_sprintf("comparison of %s with %s failed", sp_poly_class_name(a),
                                           b.tag == SP_TAG_NIL ? "nil" : sp_poly_class_name(b)));
}

/* Comparable on a Fluxpinel object: an operator that the class does not define
   answers from its <=>. Answers 0 when it does not apply. */
static int fx_comparable(const char *op, sp_RbVal a, sp_RbVal b, sp_RbVal *out) {
  int lt = !strcmp(op, "<"), le = !strcmp(op, "<="), gt = !strcmp(op, ">"), ge = !strcmp(op, ">=");
  int eq = !strcmp(op, "==");
  if (!(lt || le || gt || ge || eq)) return 0;
  FxClass *comparable = fx_builtin("Comparable");
  if (eq && !fx_is_a(a, comparable)) return 0;
  FxMethod *cmp = fx_find_method(fx_class_of(a), sp_sym_intern("<=>"));
  if (!cmp) return 0;
  sp_RbVal c = fx_invoke(cmp, a, 1, &b);
  if (c.tag != SP_TAG_INT) {
    if (eq) { *out = sp_box_bool(0); return 1; }
    fx_cmp_failed(a, b);
  }
  sp_int s = c.v.i;
  *out = sp_box_bool(lt ? s < 0 : le ? s <= 0 : gt ? s > 0 : ge ? s >= 0 : s == 0);
  return 1;
}

static sp_RbVal fx_hook_binop(const char *op, sp_RbVal a, sp_RbVal b, sp_bool *handled) {
  *handled = FALSE;
  if (a.tag == SP_TAG_OBJ && fx_kernel_cls(a.cls_id) && fx_kernel_hooks.binop)
    return fx_kernel_hooks.binop(op, a, b, handled);
  if (!fx_is_fx_object(a)) return sp_box_nil();
  SP_GC_ROOT_RBVAL(a);
  SP_GC_ROOT_RBVAL(b);
  FxMethod *m = fx_find_method(fx_class_of(a), sp_sym_intern(op));
  sp_RbVal out;
  /* a Struct or a Data compares by its members */
  if (!m && !strcmp(op, "==") && fx_record_of(fx_class_of(a))) {
    *handled = TRUE;
    return fx_send(a, op, 1, &b);
  }
  if (!m) {
    if (!fx_comparable(op, a, b, &out)) return sp_box_nil();
    *handled = TRUE;
    return out;
  }
  *handled = TRUE;
  return fx_invoke(m, a, 1, &b);
}

/* The coerce protocol: `5 * obj` is `x, y = obj.coerce(5); x * y`. */
static sp_RbVal fx_hook_coerce(const char *op, sp_RbVal num, sp_RbVal obj, sp_bool *handled) {
  *handled = FALSE;
  if (obj.tag == SP_TAG_OBJ && fx_kernel_cls(obj.cls_id) && fx_kernel_hooks.coerce)
    return fx_kernel_hooks.coerce(op, num, obj, handled);
  if (!fx_is_fx_object(obj)) return sp_box_nil();
  SP_GC_ROOT_RBVAL(num);
  SP_GC_ROOT_RBVAL(obj);
  FxMethod *m = fx_find_method(fx_class_of(obj), sp_sym_intern("coerce"));
  if (!m) return sp_box_nil();
  sp_RbVal pair = fx_invoke(m, obj, 1, &num);
  SP_GC_ROOT_RBVAL(pair);
  if (!(pair.tag == SP_TAG_OBJ && sp_poly_is_array_kind(pair.cls_id)) || sp_poly_length(pair) != 2)
    sp_raise_cls("TypeError", "coerce must return [x, y]");
  sp_RbVal x = sp_poly_arr_get(pair, 0), y = sp_poly_arr_get(pair, 1);
  SP_GC_ROOT_RBVAL(x);
  SP_GC_ROOT_RBVAL(y);
  *handled = TRUE;
  return fx_send(x, op, 1, &y);
}

static int fx_hook_class_is_module(sp_Class c) {
  if (fx_kernel_cls(c.cls_id) && fx_kernel_hooks.class_is_module)
    return fx_kernel_hooks.class_is_module(c);
  FxClass *k = fx_class_by_id(c.cls_id);
  return k && k->is_module;
}

static void fx_hook_mark(void) {
  for (int i = 0; i < L.nclasses; i++)
    for (int j = 0; j < L.classes[i]->nconsts; j++) sp_mark_rbval(L.classes[i]->consts[j].value);
  for (int i = 0; i < L.nbuiltins; i++)
    for (int j = 0; j < L.builtins[i]->nconsts; j++) sp_mark_rbval(L.builtins[i]->consts[j].value);
  for (int i = 0; i < L.nclasses; i++)
    for (int j = 0; j < L.classes[i]->ncvars; j++) sp_mark_rbval(L.classes[i]->cvars[j].value);
  for (int i = 0; i < L.nbuiltins; i++)
    for (int j = 0; j < L.builtins[i]->ncvars; j++) sp_mark_rbval(L.builtins[i]->cvars[j].value);
  if (fx_class_ivars) sp_gc_mark(fx_class_ivars);
  for (int i = 0; i < L.nglobals; i++) sp_mark_rbval(L.globals[i].value);
  for (int i = 0; i < L.nprocs; i++) sp_gc_mark(L.procs[i]);
}

static void fx_object_init(void) {
  L.object = fx_builtin("Object");
  /* An instance of Object (main, Object.new) carries an id in the Fluxpinel
     range, so that the runtime sees a user object and calls the hooks. */
  L.object->inst_id = FX_CLS_BASE + L.nclasses;
  fx_push_class(&L.classes, &L.nclasses, &L.classes_cap, L.object);
  fx_class_name_hook = fx_hook_class_name;
  fx_mark_hook = fx_hook_mark;
  sp_obj_cls_name_fn = fx_hook_obj_cls_name;
  sp_obj_to_s_fn = fx_hook_obj_to_s;
  sp_obj_inspect_fn = fx_hook_obj_inspect;
  sp_class_is_module_fn = fx_hook_class_is_module;
  SP_INSTALL_HOOK(sp_user_binop_hook, fx_hook_binop);
  sp_user_coerce_hook = fx_hook_coerce;
  L.main = fx_new_object(L.object);
  _sp_gc_root_push((void **)((uintptr_t)&L.main | (uintptr_t)1));
}

/* ---- builtin methods of every object ---- */

static const char *fx_name_arg(sp_RbVal v) {
  if (v.tag == SP_TAG_SYM) return sp_sym_to_s((sp_sym)v.v.i);
  return sp_poly_recv_s(v, "send");
}

/* respond_to?(name, include_all): a method of the program or a builtin
   method, else what respond_to_missing?(name, include_all) answers. A
   private or protected method answers include_all, and the hook does not
   run for it, as in CRuby. */
static sp_RbVal fx_respond_to(sp_RbVal r, sp_RbVal a, sp_RbVal all) {
  const char *mn = fx_name_arg(a);
  sp_sym s = sp_sym_intern(mn);
  FxMethod *pm = r.tag == SP_TAG_CLASS ? fx_find_smethod(fx_class_value(r), s) : NULL;
  if (!pm) pm = fx_find_method(fx_class_of(r), s);
  int yes = pm && pm->vis == FX_VIS_PUBLIC;
  if (!yes && !pm)
    yes = fx_builtin_responds(r, mn) || fx_record_responds(r, mn) || fx_process_responds(r, mn) ||
          fx_file_responds(r, mn) || fx_prelude_has(r, mn) || fx_rs_responds(r, mn);
  if (yes) return sp_box_bool(1);
  if (pm || fx_kernel_private(mn)) return sp_box_bool(sp_poly_truthy(all));
  FxMethod *m = fx_find_hook(r, "respond_to_missing?");
  if (!m) return sp_box_bool(0);
  SP_GC_SAVE();
  sp_RbVal args[2] = { sp_box_sym(s), all };
  SP_GC_ROOT_RBVAL(args[1]);
  return sp_box_bool(sp_poly_truthy(fx_invoke(m, r, 2, args)));
}

static int fx_object_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (argc == 2 && !strcmp(name, "respond_to?")) {
    *out = fx_respond_to(r, argv[0], argv[1]);
    return 1;
  }
  if (argc == 0) {
    if (!strcmp(name, "class")) {
      FxClass *k = r.tag == SP_TAG_CLASS ? fx_builtin(sp_poly_class_name(r)) : fx_class_of(r);
      *out = fx_class_box(k);
      return 1;
    }
    /* an immediate value is frozen, and so is a number on the heap (a
       Bignum, a Rational, a Complex); another heap value carries the flag,
       and a class keeps its own */
    if (!strcmp(name, "frozen?")) {
      int heap = r.tag == SP_TAG_OBJ && r.v.p && !sp_poly_is_rat_kind(r) && r.cls_id != SP_BUILTIN_COMPLEX;
      FxClass *k = fx_class_value(r);
      *out = sp_box_bool(k ? k->frozen : heap ? sp_gc_is_frozen(r.v.p) : 1);
      return 1;
    }
    if (!strcmp(name, "freeze")) {
      FxClass *k = fx_class_value(r);
      if (k) k->frozen = 1;
      else if (fx_is_fx_object(r)) sp_gc_freeze(r.v.p);
      *out = r;
      return 1;
    }
    if (!strcmp(name, "itself")) { *out = r; return 1; }
    /* the ids of CRuby for an Integer, nil, true and false; the identity
       of any other value */
    if (!strcmp(name, "object_id") || !strcmp(name, "__id__")) {
      if (r.tag == SP_TAG_INT) *out = sp_box_int(2 * r.v.i + 1);
      else if (r.tag == SP_TAG_NIL) *out = sp_box_int(4);
      else if (r.tag == SP_TAG_BOOL) *out = sp_box_int(r.v.b ? 20 : 0);
      else *out = sp_box_int((sp_int)(uintptr_t)sp_poly_identity_ptr(r));
      return 1;
    }
    if (!strcmp(name, "nil?")) { *out = sp_box_bool(r.tag == SP_TAG_NIL); return 1; }
    /* the defaults, also for super from a user to_s or inspect */
    if (fx_is_fx_object(r) && !strcmp(name, "to_s")) {
      *out = sp_box_str_new(sp_str_dup(fx_default_to_s(r.cls_id, r.v.p)));
      return 1;
    }
    if (fx_is_fx_object(r) && !strcmp(name, "inspect")) {
      *out = sp_box_str_new(sp_str_dup(fx_default_inspect(r.cls_id, r.v.p)));
      return 1;
    }
    if (!strcmp(name, "instance_variables")) {
#ifdef FX_KERNEL_IVARS
      const sp_insp_cls *d = fx_kernel_ivrow(r);
      if (d) { *out = fx_kernel_ivars(d, r); return 1; }
#endif
      sp_PolyArray *a = sp_PolyArray_new();
      SP_GC_ROOT(a);
      sp_RbVal h = fx_ivar_holder(r);
      if (fx_is_fx_object(h) && !fx_is_native_obj(h)) {
        FxObject *o = (FxObject *)h.v.p;
        FxClass *c = fx_class_by_id(o->cls_id);
        /* the members of a Struct have slots too, under names with no @ */
        for (int i = 0; o->iv && i < c->nivars && i < o->iv->len; i++)
          if (sp_sym_to_s(c->ivars[i])[0] == '@') sp_PolyArray_push(a, sp_box_sym(c->ivars[i]));
      }
      *out = sp_box_poly_array(a);
      return 1;
    }
  }
  if (argc == 1) {
    sp_RbVal a = argv[0];
    if (!strcmp(name, "instance_variable_defined?")) {
      *out = sp_box_bool(fx_ivar_defined(r, sp_sym_intern(fx_name_arg(a))));
      return 1;
    }
    if (!strcmp(name, "is_a?") || !strcmp(name, "kind_of?") || !strcmp(name, "instance_of?")) {
      FxClass *k = fx_class_value(a);
      if (!k) sp_raise_cls("TypeError", "class or module required");
      *out = sp_box_bool(name[0] == 'i' && name[1] == 'n' ? fx_class_of(r) == k : fx_is_a(r, k));
      return 1;
    }
    if (fx_is_fx_object(r) && (!strcmp(name, "==") || !strcmp(name, "equal?") || !strcmp(name, "eql?"))) {
      *out = sp_box_bool(fx_is_fx_object(a) && a.v.p == r.v.p);
      return 1;
    }
    /* equal?: the same object; an immediate value by its bits */
    if (!strcmp(name, "equal?")) {
      *out = sp_box_bool(r.tag == a.tag && r.cls_id == a.cls_id && r.v.i == a.v.i);
      return 1;
    }
    if (fx_is_fx_object(r) && !strcmp(name, "!=")) {
      *out = sp_box_bool(!sp_poly_truthy(fx_send(r, "==", 1, argv)));
      return 1;
    }
    if (!strcmp(name, "respond_to?")) {
      *out = fx_respond_to(r, a, sp_box_bool(0));
      return 1;
    }
    /* Object#===: the same object, or ==; Range, Regexp, Proc and a class
       answer their own before this */
    if (!strcmp(name, "===")) {
      int same = r.tag == a.tag && r.cls_id == a.cls_id && r.v.i == a.v.i;
      *out = sp_box_bool(same || sp_poly_truthy(fx_send(r, "==", 1, argv)));
      return 1;
    }
    if (fx_is_fx_object(r) && !strcmp(name, "extend")) fx_gap("extend on an object");
    /* method and public_method: a Method value (iter.c) */
    if (!strcmp(name, "method") || !strcmp(name, "public_method")) {
      if (fx_method_value_new(r, a, name[0] == 'p', out)) return 1;
      const char *on = r.tag == SP_TAG_CLASS ? sp_sprintf("#<Class:%s>", fx_class_value(r)->name)
                                            : fx_class_of(r)->name;
      sp_raise_cls("NameError", sp_sprintf("undefined method '%s' for class '%s'", fx_name_arg(a), on));
    }
    if (!strcmp(name, "instance_variable_get")) { *out = fx_ivar_get(r, sp_sym_intern(fx_name_arg(a))); return 1; }
  }
  /* Comparable#between? and #clamp from the operators, which answer from
     <=> (fx_comparable) */
  if (argc == 2 && fx_is_fx_object(r) && (!strcmp(name, "between?") || !strcmp(name, "clamp"))) {
    sp_RbVal lo = argv[0], hi = argv[1];
    int below = sp_poly_truthy(fx_send(r, "<", 1, &lo));
    int above = sp_poly_truthy(fx_send(r, ">", 1, &hi));
    if (name[0] == 'b') *out = sp_box_bool(!below && !above);
    else *out = below ? lo : above ? hi : r;
    return 1;
  }
  if (argc == 2 && !strcmp(name, "instance_variable_set")) {
    fx_ivar_set(r, sp_sym_intern(fx_name_arg(argv[0])), argv[1]);
    *out = argv[1];
    return 1;
  }
  if (argc >= 1 && (!strcmp(name, "send") || !strcmp(name, "__send__") || !strcmp(name, "public_send"))) {
    *out = fx_send_v(r, fx_name_arg(argv[0]), argc - 1, argv + 1, NULL, name[0] == 'p' ? 2 : 0, NULL);
    return 1;
  }
  return 0;
}

/* ---- builtin methods of a Class or Module value ---- */

/* The frame that holds the default visibility for a def in frame f: a
   block frame shares that of its scope. */
static FxFrame *fx_vis_frame(FxFrame *f) {
  while (f->parent && !f->vis_scope) f = f->parent;
  return f;
}

/* The visibility of a method that frame f defines. A definition through
   a receiver (f is NULL) is public. */
static int fx_scope_vis(FxFrame *f) {
  return f ? fx_vis_frame(f)->vis : FX_VIS_PUBLIC;
}

static void fx_define_vis(FxClass *k, FxMethod *m, int singleton, int vis) {
  m->vis = vis;
  fx_define_method(k, m, singleton);
}

/* attr_*; inside class << self, the accessors are class methods. */
static void fx_attr(FxClass *k, int reader, int writer, int singleton, int vis, int argc, sp_RbVal *argv) {
  for (int i = 0; i < argc; i++) {
    const char *n = fx_name_arg(argv[i]);
    sp_sym ivar = sp_sym_intern(sp_sprintf("@%s", n));
    if (reader) fx_define_vis(k, fx_method_new(FX_M_READER, sp_sym_intern(n), NULL, ivar, k), singleton, vis);
    if (writer)
      fx_define_vis(k, fx_method_new(FX_M_WRITER, sp_sym_intern(sp_sprintf("%s=", n)), NULL, ivar, k), singleton,
                      vis);
  }
}

/* Whether an ancestor of k is a builtin class or module other than
   Object, Kernel and BasicObject (Comparable, Exception, ...). */
static int fx_builtin_ancestor(const FxClass *k) {
  const FxClass *anc[128];
  int n = fx_ancestors(k, anc, 128);
  for (int i = 0; i < n; i++)
    if (anc[i]->cls_id < 0 && anc[i] != L.object && strcmp(anc[i]->name, "Kernel") &&
        strcmp(anc[i]->name, "BasicObject"))
      return 1;
  return 0;
}

/* Whether k has no builtin method of the name: a class or a module of
   the program whose ancestors are of the program, but Object, Kernel and
   BasicObject, and a name that those do not define. */
static int fx_no_builtin_method(const FxClass *k, const char *name) {
  if (k->cls_id < FX_CLS_BASE || k == L.object || k->record || k->native) return 0;
  if (fx_object_method_name(name) || fx_kernel_private(name)) return 0;
  return !fx_builtin_ancestor(k);
}

/* Sets the visibility of the method name of k, or of the singleton
   method: a method of an ancestor gets a copy in k, and a builtin method
   gets an entry (FX_M_BUILTIN). */
static void fx_set_vis(FxClass *k, int singleton, int vis, sp_sym name) {
  FxMethod *m = fx_table_get(singleton ? &k->smethods : &k->methods, name);
  if (m) { m->vis = vis; return; }
  m = singleton ? fx_find_smethod(k, name) : fx_find_method(k, name);
  if (m) { fx_define_method(k, fx_method_copy(m, vis), singleton); return; }
  const char *n = sp_sym_to_s(name);
  if (!singleton && fx_no_builtin_method(k, n))
    sp_raise_cls("NameError", sp_sprintf("undefined method '%s' for %s '%s'", n, k->is_module ? "module" : "class",
                                         k->name));
  fx_define_vis(k, fx_method_new(FX_M_BUILTIN, name, NULL, 0, k), singleton, vis);
}

/* The names of private :a, "b", [:c, :d] (what attr_* answers). */
static void fx_set_vis_args(FxClass *k, int singleton, int vis, int argc, sp_RbVal *argv) {
  for (int i = 0; i < argc; i++) {
    sp_RbVal a = argv[i];
    if (a.tag == SP_TAG_OBJ && sp_poly_is_array_kind(a.cls_id)) {
      SP_GC_ROOT_RBVAL(a);
      for (sp_int j = 0; j < sp_poly_length(a); j++) {
        sp_RbVal e = sp_poly_arr_get(a, j);
        fx_set_vis_args(k, singleton, vis, 1, &e);
      }
      continue;
    }
    if (a.tag != SP_TAG_SYM && !fx_is_string(a))
      sp_raise_cls("TypeError", sp_sprintf("%s is not a symbol nor a string", sp_poly_inspect(a)));
    fx_set_vis(k, singleton, vis, sp_sym_intern(fx_name_arg(a)));
  }
}

/* What private and its kin answer: nil, the argument, or the arguments. */
static sp_RbVal fx_vis_answer(int argc, sp_RbVal *argv) {
  if (argc == 0) return sp_box_nil();
  if (argc == 1) return argv[0];
  sp_PolyArray *a = sp_PolyArray_new();
  for (int i = 0; i < argc; i++) sp_PolyArray_push(a, argv[i]);
  return sp_box_poly_array(a);
}

/* The visibilities that a reflection method asks about, as a mask of
   1 << FX_VIS_*: method_defined? and instance_methods take public and
   protected, public_method_defined? only public, and so on. 0 when name
   is no such method. */
static int fx_vis_mask(const char *name) {
  static const struct { const char *prefix; int mask; } kinds[] = {
    { "public_", 1 << FX_VIS_PUBLIC }, { "private_", 1 << FX_VIS_PRIVATE },
    { "protected_", 1 << FX_VIS_PROTECTED }, { "", 1 << FX_VIS_PUBLIC | 1 << FX_VIS_PROTECTED }
  };
  for (size_t i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
    size_t n = strlen(kinds[i].prefix);
    if (strncmp(name, kinds[i].prefix, n)) continue;
    if (!strcmp(name + n, "method_defined?") || !strcmp(name + n, "instance_methods")) return kinds[i].mask;
  }
  return 0;
}

/* method_defined?(name, inherit = true) and its kin. A method of the
   program answers by its visibility. With no such method, a class of the
   program has the methods of Object and Kernel only; any other builtin
   method is not known here, which is a gap. */
static int fx_method_defined(FxClass *k, int mask, sp_sym name, int inherit) {
  FxMethod *m = inherit ? fx_find_method(k, name) : fx_table_get(&k->methods, name);
  if (m) return (mask >> m->vis) & 1;
  /* a record defines the accessors of its members in its table: a Data
     class has no writer */
  const FxClass *rec = fx_record_of(k);
  for (int i = 0; rec && i < rec->nmembers; i++) {
    const char *mn = sp_sym_to_s(rec->members[i]), *n = sp_sym_to_s(name);
    size_t len = strlen(mn);
    if (!strncmp(n, mn, len) && (!n[len] || !strcmp(n + len, "="))) return 0;
  }
  if (k->cls_id < FX_CLS_BASE || k->native || (inherit && fx_builtin_ancestor(k)))
    fx_gap(sp_sprintf("%s.method_defined? of a builtin method", k->name));
  /* a method of Object or Kernel, which a class inherits and a module
     does not */
  const char *n = sp_sym_to_s(name);
  if (!inherit || k->is_module || !(fx_object_method_name(n) || fx_kernel_private(n))) return 0;
  return (mask >> (fx_kernel_private(n) ? FX_VIS_PRIVATE : FX_VIS_PUBLIC)) & 1;
}

/* instance_methods(false) and its kin: the methods that k itself defines,
   in the order of definition. With inherit, the list takes the builtin
   methods of the ancestors too, which Fluxpinel does not know. */
static sp_RbVal fx_instance_methods(FxClass *k, int mask, int inherit) {
  if (inherit || k->cls_id < FX_CLS_BASE || k->native)
    fx_gap(sp_sprintf("%s.instance_methods with the builtin methods", k->name));
  sp_PolyArray *a = sp_PolyArray_new();
  SP_GC_ROOT(a);
  for (int i = 0; i < k->methods.n; i++)
    if ((mask >> k->methods.items[i]->vis) & 1) sp_PolyArray_push(a, sp_box_sym(k->methods.items[i]->name));
  return sp_box_poly_array(a);
}

/* define_method(name) { ... } and define_method(name, proc): a method whose
   body is the block (FX_M_PROC). Answers the name. */
static sp_RbVal fx_define_proc_method(FxClass *k, int singleton, int vis, int argc, sp_RbVal *argv,
                                        const FxBlock *blk) {
  if (argc != 1 && argc != 2) fx_arity_error(argc, 1, 1, 0);
  sp_sym name = sp_sym_intern(fx_name_arg(argv[0]));
  sp_RbVal body;
  if (argc == 2) {
    body = argv[1];
    if (!fx_is_proc(body)) {
      const char *cls = sp_poly_class_name(body);
      if (!strcmp(cls, "Method") || !strcmp(cls, "UnboundMethod")) fx_gap(sp_sprintf("define_method with a %s", cls));
      sp_raise_cls("TypeError", sp_sprintf("wrong argument type %s (expected Proc/Method/UnboundMethod)", cls));
    }
  } else {
    if (!blk) sp_raise_cls("ArgumentError", "tried to create Proc object without a block");
    body = fx_block_proc(blk);
  }
  if (L.nprocs == L.procs_cap) {
    L.procs_cap = L.procs_cap ? L.procs_cap * 2 : 16;
    L.procs = realloc(L.procs, sizeof *L.procs * (size_t)L.procs_cap);
  }
  L.procs[L.nprocs++] = body.v.p;
  FxMethod *m = fx_method_new(FX_M_PROC, name, NULL, 0, k);
  m->proc = body.v.p;
  fx_define_vis(k, m, singleton, vis);
  return sp_box_sym(name);
}

static int fx_class_core(FxFrame *f, sp_RbVal r, const char *name, int argc, sp_RbVal *argv,
                           const FxBlock *blk, sp_RbVal *out) {
  FxClass *k = fx_class_value(r);
  if (!k) return 0;
  /* Class.new, Module.new, Struct.new, Data.define and a record class */
  if (fx_struct_class(f, k, name, argc, argv, blk, out)) return 1;
  /* ffi_func and its kin in a module body */
  if (!blk && fx_ffi_class(k, name, argc, argv, out)) return 1;
  /* the module functions of Process and GC */
  if (!blk && fx_process_class(k, name, argc, argv, out)) return 1;
  /* the class methods of File on paths */
  if (fx_file_class(k, name, argc, argv, blk, out)) return 1;
  /* Time.now, Time.at, Time.new and the civil forms */
  if (!blk && fx_time_class(k, name, argc, argv, out)) return 1;
  /* Complex.rect and Complex.polar */
  if (!blk && fx_complex_class(k, name, argc, argv, out)) return 1;
  /* Integer.sqrt */
  if (!blk && fx_integer_class(k, name, argc, argv, out)) return 1;
  if (!strcmp(name, "new") || !strcmp(name, "allocate")) {
    if (k->is_module) return 0;
    /* an exception is a runtime sp_Exception, for a builtin class too */
    if (name[0] == 'n' && fx_is_exc_class(k)) {
      *out = fx_exc_new(k, argc, argv, blk);
      return 1;
    }
    /* a builtin class other than Object makes its own instances */
    if (k->cls_id < 0 && k != L.object) return 0;
    /* the constructor of a class of the kernel is not an entry */
    if (fx_kernel_cls(k->cls_id)) fx_gap(sp_sprintf("%s.%s of a class of the kernel", k->name, name));
    /* an instance of a subclass of Exception, Array, Struct ... needs the
       builtin part, which a FxObject does not have */
    for (const FxClass *s = k->super; s; s = s->super)
      if (s->cls_id < 0 && s != L.object && strcmp(s->name, "BasicObject"))
        fx_gap(sp_sprintf("instance of a subclass of %s", s->name));
    sp_RbVal obj = fx_new_object(k);
    SP_GC_ROOT_RBVAL(obj);
    if (name[0] == 'n') {
      FxMethod *init = fx_find_method(k, sp_sym_intern("initialize"));
      if (init) fx_invoke_b(init, obj, argc, argv, blk);
      else if (argc > 0)
        sp_raise_cls("ArgumentError", sp_sprintf("wrong number of arguments (given %d, expected 0)", argc));
    }
    *out = obj;
    return 1;
  }
  if (argc == 0 && (!strcmp(name, "name") || !strcmp(name, "to_s") || !strcmp(name, "inspect"))) {
    *out = sp_box_str_new(sp_str_dup(k->name));
    return 1;
  }
  if (argc == 0 && !strcmp(name, "superclass")) {
    *out = k->super ? fx_class_box(k->super) : sp_box_nil();
    return 1;
  }
  if (argc == 0 && !strcmp(name, "ancestors")) {
    const FxClass *anc[128];
    int n = fx_ancestors(k, anc, 128);
    sp_PolyArray *a = sp_PolyArray_new();
    SP_GC_ROOT(a);
    for (int i = 0; i < n; i++) sp_PolyArray_push(a, fx_class_box(anc[i]));
    *out = sp_box_poly_array(a);
    return 1;
  }
  if (!strcmp(name, "attr_accessor") || !strcmp(name, "attr_reader") || !strcmp(name, "attr_writer")) {
    int reader = name[5] != 'w', writer = name[5] != 'r';
    fx_attr(k, reader, writer, f && f->singleton_defs == 1, fx_scope_vis(f), argc, argv);
    /* the names of the methods: [:x, :x=] for attr_accessor :x */
    sp_PolyArray *a = sp_PolyArray_new();
    SP_GC_ROOT(a);
    for (int i = 0; i < argc; i++) {
      const char *n = fx_name_arg(argv[i]);
      if (reader) sp_PolyArray_push(a, sp_box_sym(sp_sym_intern(n)));
      if (writer) sp_PolyArray_push(a, sp_box_sym(sp_sym_intern(sp_sprintf("%s=", n))));
    }
    *out = sp_box_poly_array(a);
    return 1;
  }
  if (!strcmp(name, "alias_method") && argc == 2) {
    sp_sym s[2];
    for (int i = 0; i < 2; i++) {
      if (argv[i].tag != SP_TAG_SYM && !fx_is_string(argv[i]))
        sp_raise_cls("TypeError", sp_sprintf("%s is not a symbol nor a string", sp_poly_inspect(argv[i])));
      s[i] = argv[i].tag == SP_TAG_SYM ? (sp_sym)argv[i].v.i : sp_sym_intern(sp_poly_recv_s(argv[i], name));
    }
    fx_alias(k, f && f->singleton_defs == 1, s[0], s[1]);
    *out = sp_box_sym(s[0]);
    return 1;
  }
  if (!strcmp(name, "define_method") || !strcmp(name, "define_singleton_method")) {
    int own = name[7] == 's';
    *out = fx_define_proc_method(k, own || (f && f->singleton_defs == 1), own ? FX_VIS_PUBLIC : fx_scope_vis(f),
                                 argc, argv, blk);
    return 1;
  }
  if (!strcmp(name, "include") || !strcmp(name, "extend")) {
    for (int i = argc - 1; i >= 0; i--) {
      SP_GC_ROOT_RBVAL(r);
      FxClass *m = fx_class_value(argv[i]);
      if (!m || !m->is_module) sp_raise_cls("TypeError", "wrong argument type (expected Module)");
      if (name[0] == 'i') fx_push_class(&k->includes, &k->nincludes, &k->includes_cap, m);
      else fx_push_class(&k->extends, &k->nextends, &k->extends_cap, m);
      /* a module in a builtin class reopens it, as a def does */
      if (name[0] == 'i' && k->cls_id < 0 && k != L.object) fx_reopened_builtins++;
      /* the hook of the module: included(base) / extended(base) */
      FxMethod *hook = fx_table_get(&m->smethods, sp_sym_intern(name[0] == 'i' ? "included" : "extended"));
      if (hook) fx_invoke(hook, argv[i], 1, &r);
    }
    *out = r;
    return 1;
  }
  /* Class ordering: k < o when o is an ancestor of k; nil when unrelated */
  if (argc == 1 && (!strcmp(name, "<") || !strcmp(name, "<=") || !strcmp(name, ">") || !strcmp(name, ">=") ||
                    !strcmp(name, "<=>"))) {
    FxClass *o = fx_class_value(argv[0]);
    if (!o && name[2] == '>') { *out = sp_box_nil(); return 1; }
    if (!o) sp_raise_cls("TypeError", "compared with non class/module");
    const FxClass *anc[128];
    int below = 0, above = 0;
    int n = fx_ancestors(k, anc, 128);
    for (int i = 1; i < n; i++) if (anc[i] == o) below = 1;
    n = fx_ancestors(o, anc, 128);
    for (int i = 1; i < n; i++) if (anc[i] == k) above = 1;
    int strict = name[1] == 0;
    if (name[2] == '>') *out = k == o ? sp_box_int(0) : below ? sp_box_int(-1) : above ? sp_box_int(1) : sp_box_nil();
    else if (k == o) *out = sp_box_bool(!strict);
    else if (below || above) *out = sp_box_bool(name[0] == '<' ? below : above);
    else *out = sp_box_nil();
    return 1;
  }
  if (!strcmp(name, "module_function") && f && k->is_module) {
    if (argc == 0) { f->singleton_defs = 2; *out = sp_box_nil(); return 1; }
    /* a public copy for the module, and the instance method turns private */
    for (int i = 0; i < argc; i++) {
      FxMethod *m = fx_table_get(&k->methods, sp_sym_intern(fx_name_arg(argv[i])));
      if (!m) continue;
      fx_define_method(k, fx_method_copy(m, FX_VIS_PUBLIC), 1);
      m->vis = FX_VIS_PRIVATE;
    }
    *out = fx_vis_answer(argc, argv);
    return 1;
  }
  /* private, public and protected: with no names, the default of the scope
     for the definitions after it, which also ends module_function */
  if (!strcmp(name, "private") || !strcmp(name, "public") || !strcmp(name, "protected") ||
      !strcmp(name, "private_class_method") || !strcmp(name, "public_class_method")) {
    int vis = name[1] == 'u' ? FX_VIS_PUBLIC : name[2] == 'i' ? FX_VIS_PRIVATE : FX_VIS_PROTECTED;
    int cm = strchr(name, '_') != NULL;
    if (argc == 0 && !cm && f) {
      fx_vis_frame(f)->vis = vis;
      if (f->singleton_defs == 2) f->singleton_defs = 0;
    }
    fx_set_vis_args(k, cm || (f && f->singleton_defs == 1), vis, argc, argv);
    *out = cm ? r : fx_vis_answer(argc, argv);
    return 1;
  }
  /* constants have no visibility here: these answer as CRuby does */
  if (!strcmp(name, "private_constant") || !strcmp(name, "public_constant")) {
    *out = r;
    return 1;
  }
  if (argc == 1 && !strcmp(name, "===")) { *out = sp_box_bool(fx_is_a(argv[0], k)); return 1; }
  if (argc == 1 && (!strcmp(name, "==") || !strcmp(name, "equal?"))) {
    *out = sp_box_bool(fx_class_value(argv[0]) == k);
    return 1;
  }
  /* method_defined?(name, inherit = true), instance_methods(inherit = true)
     and their kin for one visibility */
  int mask = fx_vis_mask(name);
  if (mask && name[strlen(name) - 1] == '?' && (argc == 1 || argc == 2)) {
    int inherit = argc == 1 || sp_poly_truthy(argv[1]);
    *out = sp_box_bool(fx_method_defined(k, mask, sp_sym_intern(fx_name_arg(argv[0])), inherit));
    return 1;
  }
  if (mask && name[strlen(name) - 1] == 's' && argc <= 1) {
    *out = fx_instance_methods(k, mask, argc == 0 || sp_poly_truthy(argv[0]));
    return 1;
  }
  /* the reflection of class variables */
  if (argc == 1 && (!strcmp(name, "class_variable_get") || !strcmp(name, "class_variable_defined?") ||
                    !strcmp(name, "remove_class_variable"))) {
    const char *cn = fx_name_arg(argv[0]);
    fx_cvar_check_name(cn);
    sp_sym s = sp_sym_intern(cn);
    if (!strcmp(name, "class_variable_defined?")) { *out = sp_box_bool(fx_cvar_find(k, s, NULL) != NULL); return 1; }
    if (!strcmp(name, "class_variable_get")) { *out = fx_cvar_get(k, s); return 1; }
    for (int i = 0; i < k->ncvars; i++)
      if (k->cvars[i].name == s) {
        *out = k->cvars[i].value;
        memmove(&k->cvars[i], &k->cvars[i + 1], sizeof *k->cvars * (size_t)(k->ncvars - i - 1));
        k->ncvars--;
        return 1;
      }
    sp_raise_cls("NameError", sp_sprintf("class variable %s not defined for %s", cn, k->name));
  }
  if (argc == 2 && !strcmp(name, "class_variable_set")) {
    const char *cn = fx_name_arg(argv[0]);
    fx_cvar_check_name(cn);
    fx_cvar_set(k, sp_sym_intern(cn), argv[1]);
    *out = argv[1];
    return 1;
  }
  if (argc <= 1 && !strcmp(name, "class_variables")) {
    *out = fx_cvar_names(k, argc == 0 || sp_poly_truthy(argv[0]));
    return 1;
  }
  if (argc == 1 && !strcmp(name, "const_get")) {
    sp_RbVal v;
    const char *cn = fx_name_arg(argv[0]);
    if (!fx_const_scoped(k, sp_sym_intern(cn), &v)) fx_const_missing(cn);
    *out = v;
    return 1;
  }
  return 0;
}
