/* object.c -- classes, modules, objects and constants of Lupe.

   A Lupe class is a LupeClass with an id at or above LUPE_CLS_BASE. Its
   instance is a LupeObject on the spinel heap, boxed as SP_TAG_OBJ with
   that id. The runtime reaches Lupe objects through its hooks for user
   objects (sp_obj_to_s_fn and the others), which lupe_object_init sets. */

#include "lupe.h"

/* ---- method tables ---- */

static LupeMethod *lupe_table_get(const LupeMethodTable *t, sp_sym name) {
  for (int i = 0; i < t->n; i++)
    if (t->items[i]->name == name) return t->items[i];
  return NULL;
}

static void lupe_table_put(LupeMethodTable *t, LupeMethod *m) {
  for (int i = 0; i < t->n; i++)
    if (t->items[i]->name == m->name) { t->items[i] = m; return; }
  if (t->n == t->cap) {
    t->cap = t->cap ? t->cap * 2 : 8;
    t->items = realloc(t->items, sizeof *t->items * (size_t)t->cap);
  }
  t->items[t->n++] = m;
}

static LupeMethod *lupe_method_new(LupeMethodKind kind, sp_sym name, const pm_def_node_t *def,
                                   sp_sym ivar, LupeClass *owner) {
  LupeMethod *m = calloc(1, sizeof *m);
  *m = (LupeMethod){ name, kind, def, ivar, owner };
  return m;
}

/* the number of builtin classes that the program reopened with a def */
static int lupe_reopened_builtins;

static void lupe_define_method(LupeClass *c, LupeMethod *m, int singleton) {
  lupe_table_put(singleton ? &c->smethods : &c->methods, m);
  if (!singleton && c->cls_id < 0 && c != L.object) lupe_reopened_builtins++;
  /* the hook of the class: method_added(name) */
  if (!singleton) {
    LupeMethod *hook = lupe_find_smethod(c, sp_sym_intern("method_added"));
    if (hook) {
      sp_RbVal arg = sp_box_sym(m->name);
      lupe_invoke(hook, lupe_class_box(c), 1, &arg);
    }
  }
}

/* ---- classes ---- */

static void lupe_push_class(LupeClass ***arr, int *n, int *cap, LupeClass *c) {
  if (*n == *cap) {
    *cap = *cap ? *cap * 2 : 8;
    *arr = realloc(*arr, sizeof **arr * (size_t)*cap);
  }
  (*arr)[(*n)++] = c;
}

static LupeClass *lupe_class_alloc(sp_int cls_id, const char *name, int is_module,
                                   LupeClass *super, LupeClass *lexical) {
  LupeClass *c = calloc(1, sizeof *c);
  c->cls_id = cls_id;
  c->inst_id = cls_id;
  c->name = name;
  c->is_module = is_module;
  c->super = super;
  c->lexical = lexical;
  return c;
}

/* The builtin hierarchy that Lupe knows: superclass and mixins. A builtin
   class with no row is a class under Object. */
static const struct {
  const char *name, *super, *mixin;
  int is_module;
} lupe_builtin_tree[] = {
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
   body for one of them reopens the builtin in CRuby; Lupe cannot. */
static int lupe_unknown_builtin(const char *name) {
  static const char *const names[] = { "Random", "Set", "Encoding", "Method", "UnboundMethod", "Binding",
                                       "Mutex", "Queue", "SizedQueue", "ConditionVariable", "ObjectSpace",
                                       "GC", "Process", "Marshal", "Warning", "RubyVM", NULL };
  for (int i = 0; names[i]; i++)
    if (!strcmp(names[i], name)) return 1;
  return 0;
}

/* The runtime id of a builtin class name, or 0. */
static sp_int lupe_builtin_id(const char *name) {
  for (int i = 0; i < LUPE_NBUILTIN_CLASSES; i++)
    if (!strcmp(lupe_builtin_class_names[i], name)) return -100 - i;
  return 0;
}

/* The LupeClass of a builtin class name, made on first use, or NULL. */
static LupeClass *lupe_builtin(const char *name) {
  /* the top-level names of the thread classes */
  static const char *const alias[][2] = { { "Queue", "Thread::Queue" }, { "SizedQueue", "Thread::SizedQueue" },
                                          { "Mutex", "Thread::Mutex" },
                                          { "ConditionVariable", "Thread::ConditionVariable" } };
  for (size_t i = 0; i < sizeof alias / sizeof alias[0]; i++)
    if (!strcmp(name, alias[i][0])) name = alias[i][1];
  for (int i = 0; i < L.nbuiltins; i++)
    if (!strcmp(L.builtins[i]->name, name)) return L.builtins[i];
  sp_int id = lupe_builtin_id(name);
  /* an exception class that only the runtime knows (LoadError, ...): an id
     of its own below the table, and the runtime's superclass */
  const char *exc_parent = id ? NULL : sp_exc_parent_of_name(name);
  if (exc_parent) {
    LupeClass *c = lupe_class_alloc(LUPE_EXC_CLS_BASE - L.nbuiltins, strdup(name), 0, NULL, NULL);
    lupe_push_class(&L.builtins, &L.nbuiltins, &L.builtins_cap, c);
    c->super = lupe_builtin(exc_parent);
    return c;
  }
  if (!id) return NULL;
  const char *super = "Object", *mixin = NULL;
  int is_module = 0;
  for (size_t i = 0; i < sizeof lupe_builtin_tree / sizeof lupe_builtin_tree[0]; i++) {
    if (strcmp(lupe_builtin_tree[i].name, name)) continue;
    super = lupe_builtin_tree[i].super;
    mixin = lupe_builtin_tree[i].mixin;
    is_module = lupe_builtin_tree[i].is_module;
  }
  LupeClass *c = lupe_class_alloc(id, lupe_builtin_class_names[-100 - id], is_module, NULL, NULL);
  lupe_push_class(&L.builtins, &L.nbuiltins, &L.builtins_cap, c);
  if (super) c->super = lupe_builtin(super);
  if (mixin) lupe_push_class(&c->includes, &c->nincludes, &c->includes_cap, lupe_builtin(mixin));
  return c;
}

static LupeClass *lupe_class_by_id(sp_int cls_id) {
  if (cls_id >= LUPE_CLS_BASE && cls_id < LUPE_CLS_BASE + L.nclasses) return L.classes[cls_id - LUPE_CLS_BASE];
  int i = -100 - (int)cls_id;
  if (i >= 0 && i < LUPE_NBUILTIN_CLASSES) return lupe_builtin(lupe_builtin_class_names[i]);
  for (int j = 0; cls_id <= LUPE_EXC_CLS_BASE && j < L.nbuiltins; j++)
    if (L.builtins[j]->cls_id == cls_id) return L.builtins[j];
  return NULL;
}

static LupeClass *lupe_class_new(const char *name, int is_module, LupeClass *super, LupeClass *lexical) {
  LupeClass *c = lupe_class_alloc(LUPE_CLS_BASE + L.nclasses, name, is_module, super, lexical);
  lupe_push_class(&L.classes, &L.nclasses, &L.classes_cap, c);
  return c;
}

static sp_RbVal lupe_class_box(const LupeClass *c) {
  return sp_box_class((sp_Class){ c->cls_id, NULL });
}

/* The class or module that a Class value names, or NULL. */
static LupeClass *lupe_class_value(sp_RbVal v) {
  if (v.tag != SP_TAG_CLASS) return NULL;
  if (v.cls_id == SP_CLASS_BY_NAME) return v.v.s ? lupe_builtin(v.v.s) : NULL;
  return lupe_class_by_id(v.cls_id);
}

static int lupe_is_lupe_object(sp_RbVal v) {
  return v.tag == SP_TAG_OBJ && v.cls_id >= LUPE_CLS_BASE && v.v.p;
}

/* The class of the program with this full name ("A::B"), or NULL. */
static LupeClass *lupe_class_named(const char *name) {
  for (int i = 0; i < L.nclasses; i++)
    if (!strcmp(L.classes[i]->name, name)) return L.classes[i];
  return NULL;
}

/* The class of any value. An exception names its class, which can be a
   class of the program. */
static LupeClass *lupe_class_of(sp_RbVal v) {
  if (lupe_is_lupe_object(v)) return lupe_class_by_id(v.cls_id);
  const char *name = sp_poly_class_name(v);
  LupeClass *c = lupe_builtin(name);
  if (!c && v.tag == SP_TAG_OBJ && v.cls_id == SP_BUILTIN_EXCEPTION) c = lupe_class_named(name);
  return c ? c : L.object;
}

/* The ancestors of c, in method lookup order: c, its mixins (the last
   included first, each with its own mixins), then the same for the
   superclass. Answers the count. */
static int lupe_ancestors(const LupeClass *c, const LupeClass **out, int cap) {
  int n = 0;
  for (const LupeClass *k = c; k; k = k->super) {
    const LupeClass *stack[64];
    int sp = 0;
    stack[sp++] = k;
    while (sp > 0) {
      const LupeClass *m = stack[--sp];
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

static LupeMethod *lupe_find_method(const LupeClass *c, sp_sym name) {
  const LupeClass *anc[128];
  int n = lupe_ancestors(c, anc, 128);
  for (int i = 0; i < n; i++) {
    LupeMethod *m = lupe_table_get(&anc[i]->methods, name);
    if (m) return m;
  }
  return NULL;
}

/* A class method: the singleton methods of c and its superclasses, and the
   methods of the modules that they extend. */
static LupeMethod *lupe_find_smethod(const LupeClass *c, sp_sym name) {
  for (const LupeClass *k = c; k; k = k->super) {
    LupeMethod *m = lupe_table_get(&k->smethods, name);
    if (m) return m;
    for (int i = k->nextends - 1; i >= 0; i--)
      if ((m = lupe_find_method(k->extends[i], name))) return m;
  }
  return NULL;
}

/* The method that super calls from m, for a receiver of class c. */
static LupeMethod *lupe_find_super(const LupeClass *c, const LupeMethod *m) {
  const LupeClass *anc[128];
  int n = lupe_ancestors(c, anc, 128), i = 0;
  while (i < n && anc[i] != m->owner) i++;
  for (i++; i < n; i++) {
    LupeMethod *s = lupe_table_get(&anc[i]->methods, m->name);
    if (s) return s;
  }
  return NULL;
}

static int lupe_is_a(sp_RbVal v, const LupeClass *k) {
  const LupeClass *anc[128];
  int n = lupe_ancestors(lupe_class_of(v), anc, 128);
  for (int i = 0; i < n; i++) if (anc[i] == k) return 1;
  return 0;
}

/* ---- constants ---- */

static LupeConst *lupe_const_own(const LupeClass *c, sp_sym name) {
  for (int i = 0; i < c->nconsts; i++)
    if (c->consts[i].name == name) return &c->consts[i];
  return NULL;
}

static void lupe_const_set(LupeClass *c, sp_sym name, sp_RbVal v) {
  /* an anonymous class takes the name of the first constant that holds it */
  LupeClass *anon = lupe_class_value(v);
  if (anon && anon->anon) {
    anon->anon = 0;
    anon->name = c == L.object ? sp_sym_to_s(name) : strdup(sp_sprintf("%s::%s", c->name, sp_sym_to_s(name)));
  }
  LupeConst *k = lupe_const_own(c, name);
  if (k) { k->value = v; return; }
  if (c->nconsts == c->consts_cap) {
    c->consts_cap = c->consts_cap ? c->consts_cap * 2 : 8;
    c->consts = realloc(c->consts, sizeof *c->consts * (size_t)c->consts_cap);
  }
  c->consts[c->nconsts++] = (LupeConst){ name, v };
}

/* A constant of scope or its ancestors, then a builtin class. */
static int lupe_const_scoped(const LupeClass *scope, sp_sym name, sp_RbVal *out) {
  const LupeClass *anc[128];
  int n = lupe_ancestors(scope, anc, 128);
  for (int i = 0; i < n; i++) {
    LupeConst *k = lupe_const_own(anc[i], name);
    if (k) { *out = k->value; return 1; }
  }
  if (scope == L.object || scope->is_module) {
    LupeConst *k = lupe_const_own(L.object, name);
    if (k) { *out = k->value; return 1; }
    LupeClass *b = lupe_builtin(sp_sym_to_s(name));
    if (b) { *out = lupe_class_box(b); return 1; }
  }
  /* a builtin class nested in a builtin one: Thread::Queue */
  if (scope->cls_id < 0 && scope != L.object) {
    LupeClass *b = lupe_builtin(sp_sprintf("%s::%s", scope->name, sp_sym_to_s(name)));
    if (b) { *out = lupe_class_box(b); return 1; }
  }
  return 0;
}

/* A constant as a bare name reads it: the lexical scopes from cref out
   (but the top level), then the ancestors of cref, then Object. */
static int lupe_const_lookup(const LupeClass *cref, sp_sym name, sp_RbVal *out) {
  for (const LupeClass *k = cref; k && k != L.object; k = k->lexical) {
    LupeConst *c = lupe_const_own(k, name);
    if (c) { *out = c->value; return 1; }
  }
  if (lupe_const_scoped(cref, name, out)) return 1;
  return lupe_const_scoped(L.object, name, out);
}

static __attribute__((noreturn)) void lupe_const_missing(const char *name) {
  lupe_trace_missing("const", name, 0);
  sp_raise_cls("NameError", sp_sprintf("uninitialized constant %s", name));
}

/* ---- objects and instance variables ---- */

static void lupe_obj_scan(void *p) {
  LupeObject *o = (LupeObject *)p;
  if (o->iv) sp_gc_mark(o->iv);
}

static sp_RbVal lupe_new_object(LupeClass *c) {
  LupeObject *o = (LupeObject *)sp_gc_alloc(sizeof *o, NULL, lupe_obj_scan);
  o->cls_id = c->inst_id;
  o->iv = NULL;
  return sp_box_obj(o, (int)c->inst_id);
}

static int lupe_ivar_slot(LupeClass *c, sp_sym name, int create) {
  for (int i = 0; i < c->nivars; i++)
    if (c->ivars[i] == name) return i;
  if (!create) return -1;
  if (c->nivars == c->ivars_cap) {
    c->ivars_cap = c->ivars_cap ? c->ivars_cap * 2 : 8;
    c->ivars = realloc(c->ivars, sizeof *c->ivars * (size_t)c->ivars_cap);
  }
  c->ivars[c->nivars] = name;
  return c->nivars++;
}

/* The ivars of a class value (class-level @x) live in a hidden object. */
static sp_PolyArray *lupe_class_ivars;   /* [class value, object, ...]; marked by the hook */

/* An exception keeps its ivars in a hidden object too. The table holds
   the exception, so an exception with an ivar lives until the end. */
static sp_RbVal lupe_ivar_holder(sp_RbVal self) {
  if (lupe_is_lupe_object(self)) return self;
  int exc = self.tag == SP_TAG_OBJ && self.cls_id == SP_BUILTIN_EXCEPTION && self.v.p;
  if (self.tag != SP_TAG_CLASS && !exc) return sp_box_nil();
  LupeClass *c = exc ? NULL : lupe_class_value(self);
  if (!c && !exc) return sp_box_nil();
  if (!lupe_class_ivars) lupe_class_ivars = sp_PolyArray_new();
  for (sp_int i = 0; i + 1 < lupe_class_ivars->len; i += 2) {
    sp_RbVal k = lupe_class_ivars->data[i];
    if (exc ? k.v.p == self.v.p : k.tag == SP_TAG_CLASS && k.cls_id == c->cls_id) return lupe_class_ivars->data[i + 1];
  }
  sp_RbVal holder = lupe_new_object(lupe_builtin("Object"));
  SP_GC_ROOT_RBVAL(holder);
  sp_PolyArray_push(lupe_class_ivars, self);
  sp_PolyArray_push(lupe_class_ivars, holder);
  return holder;
}

static sp_RbVal lupe_ivar_get(sp_RbVal self, sp_sym name) {
  sp_RbVal h = lupe_ivar_holder(self);
  if (!lupe_is_lupe_object(h)) return sp_box_nil();
  LupeObject *o = (LupeObject *)h.v.p;
  int slot = lupe_ivar_slot(lupe_class_by_id(o->cls_id), name, 0);
  if (slot < 0 || !o->iv || slot >= o->iv->len) return sp_box_nil();
  return o->iv->data[slot];
}

static void lupe_ivar_set(sp_RbVal self, sp_sym name, sp_RbVal v) {
  SP_GC_ROOT_RBVAL(self);
  SP_GC_ROOT_RBVAL(v);
  sp_RbVal h = lupe_ivar_holder(self);
  if (!lupe_is_lupe_object(h)) lupe_gap(sp_sprintf("ivar on a %s", sp_poly_class_name(self)));
  SP_GC_ROOT_RBVAL(h);
  LupeObject *o = (LupeObject *)h.v.p;
  /* a frozen object: an ivar, an attr writer or a Struct member */
  if (lupe_is_lupe_object(self) && sp_gc_is_frozen(self.v.p)) {
    const char *msg = sp_sprintf("can't modify frozen %s: %s", lupe_class_of(self)->name, sp_poly_inspect(self));
    sp_exc_stage_recv(self);
    sp_raise_cls("FrozenError", msg);
  }
  int slot = lupe_ivar_slot(lupe_class_by_id(o->cls_id), name, 1);
  if (!o->iv) {
    sp_PolyArray *iv = sp_PolyArray_new();
    sp_gc_wb(o);
    o->iv = iv;
  }
  while (o->iv->len <= slot) sp_PolyArray_push(o->iv, sp_box_nil());
  sp_PolyArray_set(o->iv, slot, v);
}

/* ---- the runtime hooks ---- */

static const char *lupe_hook_class_name(sp_int cls_id) {
  LupeClass *c = lupe_class_by_id(cls_id);
  return c ? c->name : sp_str_empty;
}

static const char *lupe_hook_obj_cls_name(int cls_id) {
  return lupe_hook_class_name(cls_id);
}

/* A user method on obj answers a String, or NULL when there is none. */
static const char *lupe_user_string(sp_RbVal obj, const char *name) {
  LupeMethod *m = lupe_find_method(lupe_class_of(obj), sp_sym_intern(name));
  if (!m) return NULL;
  sp_RbVal v = lupe_invoke(m, obj, 0, NULL);
  return sp_poly_to_s(v);
}

/* Object#to_s and Object#inspect, in the format of CRuby and spinel. */
static const char *lupe_default_to_s(int cls_id, void *p) {
  if (p == L.main.v.p) return SPL("main");
  if (lupe_record_of(lupe_class_by_id(cls_id))) return lupe_record_inspect(sp_box_obj(p, cls_id));
  return sp_sprintf("#<%s:0x%016llx>", lupe_hook_class_name(cls_id), (unsigned long long)(uintptr_t)p);
}

static const char *lupe_default_inspect(int cls_id, void *p) {
  if (p == L.main.v.p) return SPL("main");
  LupeClass *c = lupe_class_by_id(cls_id);
  if (lupe_record_of(c)) return lupe_record_inspect(sp_box_obj(p, cls_id));
  LupeObject *o = (LupeObject *)p;
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

static const char *lupe_hook_obj_to_s(int cls_id, void *p) {
  sp_RbVal obj = sp_box_obj(p, cls_id);
  SP_GC_ROOT_RBVAL(obj);
  const char *s = p == L.main.v.p ? NULL : lupe_user_string(obj, "to_s");
  return s ? s : lupe_default_to_s(cls_id, p);
}

static const char *lupe_hook_obj_inspect(int cls_id, void *p) {
  sp_RbVal obj = sp_box_obj(p, cls_id);
  SP_GC_ROOT_RBVAL(obj);
  const char *s = p == L.main.v.p ? NULL : lupe_user_string(obj, "inspect");
  return s ? s : lupe_default_inspect(cls_id, p);
}

/* The runtime asks a user object for an operator (==, <=>, + ...) through
   this hook, for example inside Array#include? or Array#sort. */
static __attribute__((noreturn)) void lupe_cmp_failed(sp_RbVal a, sp_RbVal b) {
  sp_raise_cls("ArgumentError", sp_sprintf("comparison of %s with %s failed", sp_poly_class_name(a),
                                           b.tag == SP_TAG_NIL ? "nil" : sp_poly_class_name(b)));
}

/* Comparable on a Lupe object: an operator that the class does not define
   answers from its <=>. Answers 0 when it does not apply. */
static int lupe_comparable(const char *op, sp_RbVal a, sp_RbVal b, sp_RbVal *out) {
  int lt = !strcmp(op, "<"), le = !strcmp(op, "<="), gt = !strcmp(op, ">"), ge = !strcmp(op, ">=");
  int eq = !strcmp(op, "==");
  if (!(lt || le || gt || ge || eq)) return 0;
  LupeClass *comparable = lupe_builtin("Comparable");
  if (eq && !lupe_is_a(a, comparable)) return 0;
  LupeMethod *cmp = lupe_find_method(lupe_class_of(a), sp_sym_intern("<=>"));
  if (!cmp) return 0;
  sp_RbVal c = lupe_invoke(cmp, a, 1, &b);
  if (c.tag != SP_TAG_INT) {
    if (eq) { *out = sp_box_bool(0); return 1; }
    lupe_cmp_failed(a, b);
  }
  sp_int s = c.v.i;
  *out = sp_box_bool(lt ? s < 0 : le ? s <= 0 : gt ? s > 0 : ge ? s >= 0 : s == 0);
  return 1;
}

static sp_RbVal lupe_hook_binop(const char *op, sp_RbVal a, sp_RbVal b, sp_bool *handled) {
  *handled = FALSE;
  if (!lupe_is_lupe_object(a)) return sp_box_nil();
  SP_GC_ROOT_RBVAL(a);
  SP_GC_ROOT_RBVAL(b);
  LupeMethod *m = lupe_find_method(lupe_class_of(a), sp_sym_intern(op));
  sp_RbVal out;
  /* a Struct or a Data compares by its members */
  if (!m && !strcmp(op, "==") && lupe_record_of(lupe_class_of(a))) {
    *handled = TRUE;
    return lupe_send(a, op, 1, &b);
  }
  if (!m) {
    if (!lupe_comparable(op, a, b, &out)) return sp_box_nil();
    *handled = TRUE;
    return out;
  }
  *handled = TRUE;
  return lupe_invoke(m, a, 1, &b);
}

/* The coerce protocol: `5 * obj` is `x, y = obj.coerce(5); x * y`. */
static sp_RbVal lupe_hook_coerce(const char *op, sp_RbVal num, sp_RbVal obj, sp_bool *handled) {
  *handled = FALSE;
  if (!lupe_is_lupe_object(obj)) return sp_box_nil();
  SP_GC_ROOT_RBVAL(num);
  SP_GC_ROOT_RBVAL(obj);
  LupeMethod *m = lupe_find_method(lupe_class_of(obj), sp_sym_intern("coerce"));
  if (!m) return sp_box_nil();
  sp_RbVal pair = lupe_invoke(m, obj, 1, &num);
  SP_GC_ROOT_RBVAL(pair);
  if (!(pair.tag == SP_TAG_OBJ && sp_poly_is_array_kind(pair.cls_id)) || sp_poly_length(pair) != 2)
    sp_raise_cls("TypeError", "coerce must return [x, y]");
  sp_RbVal x = sp_poly_arr_get(pair, 0), y = sp_poly_arr_get(pair, 1);
  SP_GC_ROOT_RBVAL(x);
  SP_GC_ROOT_RBVAL(y);
  *handled = TRUE;
  return lupe_send(x, op, 1, &y);
}

static int lupe_hook_class_is_module(sp_Class c) {
  LupeClass *k = lupe_class_by_id(c.cls_id);
  return k && k->is_module;
}

static void lupe_hook_mark(void) {
  for (int i = 0; i < L.nclasses; i++)
    for (int j = 0; j < L.classes[i]->nconsts; j++) sp_mark_rbval(L.classes[i]->consts[j].value);
  for (int i = 0; i < L.nbuiltins; i++)
    for (int j = 0; j < L.builtins[i]->nconsts; j++) sp_mark_rbval(L.builtins[i]->consts[j].value);
  if (lupe_class_ivars) sp_gc_mark(lupe_class_ivars);
  for (int i = 0; i < L.nglobals; i++) sp_mark_rbval(L.globals[i].value);
}

static void lupe_object_init(void) {
  L.object = lupe_builtin("Object");
  /* An instance of Object (main, Object.new) carries an id in the Lupe
     range, so that the runtime sees a user object and calls the hooks. */
  L.object->inst_id = LUPE_CLS_BASE + L.nclasses;
  lupe_push_class(&L.classes, &L.nclasses, &L.classes_cap, L.object);
  lupe_class_name_hook = lupe_hook_class_name;
  lupe_mark_hook = lupe_hook_mark;
  sp_obj_cls_name_fn = lupe_hook_obj_cls_name;
  sp_obj_to_s_fn = lupe_hook_obj_to_s;
  sp_obj_inspect_fn = lupe_hook_obj_inspect;
  sp_class_is_module_fn = lupe_hook_class_is_module;
  SP_INSTALL_HOOK(sp_user_binop_hook, lupe_hook_binop);
  sp_user_coerce_hook = lupe_hook_coerce;
  L.main = lupe_new_object(L.object);
  _sp_gc_root_push((void **)((uintptr_t)&L.main | (uintptr_t)1));
}

/* ---- builtin methods of every object ---- */

static const char *lupe_name_arg(sp_RbVal v) {
  if (v.tag == SP_TAG_SYM) return sp_sym_to_s((sp_sym)v.v.i);
  return sp_poly_recv_s(v, "send");
}

static int lupe_object_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (argc == 0) {
    if (!strcmp(name, "class")) {
      LupeClass *k = r.tag == SP_TAG_CLASS ? lupe_builtin(sp_poly_class_name(r)) : lupe_class_of(r);
      *out = lupe_class_box(k);
      return 1;
    }
    /* an immediate value is frozen; a heap value carries the flag, and a
       class keeps its own */
    if (!strcmp(name, "frozen?")) {
      int heap = r.tag == SP_TAG_OBJ && r.v.p;
      LupeClass *k = lupe_class_value(r);
      *out = sp_box_bool(k ? k->frozen : heap ? sp_gc_is_frozen(r.v.p) : 1);
      return 1;
    }
    if (!strcmp(name, "freeze")) {
      LupeClass *k = lupe_class_value(r);
      if (k) k->frozen = 1;
      else if (lupe_is_lupe_object(r)) sp_gc_freeze(r.v.p);
      *out = r;
      return 1;
    }
    if (!strcmp(name, "itself")) { *out = r; return 1; }
    if (!strcmp(name, "nil?")) { *out = sp_box_bool(r.tag == SP_TAG_NIL); return 1; }
    /* the defaults, also for super from a user to_s or inspect */
    if (lupe_is_lupe_object(r) && !strcmp(name, "to_s")) {
      *out = sp_box_str_new(sp_str_dup(lupe_default_to_s(r.cls_id, r.v.p)));
      return 1;
    }
    if (lupe_is_lupe_object(r) && !strcmp(name, "inspect")) {
      *out = sp_box_str_new(sp_str_dup(lupe_default_inspect(r.cls_id, r.v.p)));
      return 1;
    }
    if (!strcmp(name, "instance_variables")) {
      sp_PolyArray *a = sp_PolyArray_new();
      SP_GC_ROOT(a);
      sp_RbVal h = lupe_ivar_holder(r);
      if (lupe_is_lupe_object(h)) {
        LupeObject *o = (LupeObject *)h.v.p;
        LupeClass *c = lupe_class_by_id(o->cls_id);
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
    if (!strcmp(name, "is_a?") || !strcmp(name, "kind_of?") || !strcmp(name, "instance_of?")) {
      LupeClass *k = lupe_class_value(a);
      if (!k) sp_raise_cls("TypeError", "class or module required");
      *out = sp_box_bool(name[0] == 'i' && name[1] == 'n' ? lupe_class_of(r) == k : lupe_is_a(r, k));
      return 1;
    }
    if (lupe_is_lupe_object(r) && (!strcmp(name, "==") || !strcmp(name, "equal?") || !strcmp(name, "eql?"))) {
      *out = sp_box_bool(lupe_is_lupe_object(a) && a.v.p == r.v.p);
      return 1;
    }
    /* equal?: the same object; an immediate value by its bits */
    if (!strcmp(name, "equal?")) {
      *out = sp_box_bool(r.tag == a.tag && r.cls_id == a.cls_id && r.v.i == a.v.i);
      return 1;
    }
    if (lupe_is_lupe_object(r) && !strcmp(name, "!=")) {
      *out = sp_box_bool(!sp_poly_truthy(lupe_send(r, "==", 1, argv)));
      return 1;
    }
    if (!strcmp(name, "respond_to?")) {
      const char *mn = lupe_name_arg(a);
      sp_sym s = sp_sym_intern(mn);
      int yes = lupe_find_method(lupe_class_of(r), s) != NULL;
      if (!yes && r.tag == SP_TAG_CLASS) yes = lupe_find_smethod(lupe_class_value(r), s) != NULL;
      if (!yes) yes = lupe_builtin_responds(r, mn) || lupe_record_responds(r, mn);
      *out = sp_box_bool(yes);
      return 1;
    }
    if (lupe_is_lupe_object(r) && !strcmp(name, "extend")) lupe_gap("extend on an object");
    if (!strcmp(name, "instance_variable_get")) { *out = lupe_ivar_get(r, sp_sym_intern(lupe_name_arg(a))); return 1; }
  }
  /* Comparable#between? and #clamp from the operators, which answer from
     <=> (lupe_comparable) */
  if (argc == 2 && lupe_is_lupe_object(r) && (!strcmp(name, "between?") || !strcmp(name, "clamp"))) {
    sp_RbVal lo = argv[0], hi = argv[1];
    int below = sp_poly_truthy(lupe_send(r, "<", 1, &lo));
    int above = sp_poly_truthy(lupe_send(r, ">", 1, &hi));
    if (name[0] == 'b') *out = sp_box_bool(!below && !above);
    else *out = below ? lo : above ? hi : r;
    return 1;
  }
  if (argc == 2 && !strcmp(name, "instance_variable_set")) {
    lupe_ivar_set(r, sp_sym_intern(lupe_name_arg(argv[0])), argv[1]);
    *out = argv[1];
    return 1;
  }
  if (argc >= 1 && (!strcmp(name, "send") || !strcmp(name, "__send__") || !strcmp(name, "public_send"))) {
    *out = lupe_send(r, lupe_name_arg(argv[0]), argc - 1, argv + 1);
    return 1;
  }
  return 0;
}

/* ---- builtin methods of a Class or Module value ---- */

/* attr_*; inside class << self, the accessors are class methods. */
static void lupe_attr(LupeClass *k, int reader, int writer, int singleton, int argc, sp_RbVal *argv) {
  for (int i = 0; i < argc; i++) {
    const char *n = lupe_name_arg(argv[i]);
    sp_sym ivar = sp_sym_intern(sp_sprintf("@%s", n));
    if (reader) lupe_define_method(k, lupe_method_new(LUPE_M_READER, sp_sym_intern(n), NULL, ivar, k), singleton);
    if (writer)
      lupe_define_method(k, lupe_method_new(LUPE_M_WRITER, sp_sym_intern(sp_sprintf("%s=", n)), NULL, ivar, k),
                         singleton);
  }
}

static int lupe_class_core(LupeFrame *f, sp_RbVal r, const char *name, int argc, sp_RbVal *argv,
                           const LupeBlock *blk, sp_RbVal *out) {
  LupeClass *k = lupe_class_value(r);
  if (!k) return 0;
  /* Class.new, Module.new, Struct.new, Data.define and a record class */
  if (lupe_struct_class(f, k, name, argc, argv, blk, out)) return 1;
  /* ffi_func and its kin in a module body */
  if (!blk && lupe_ffi_class(k, name, argc, argv, out)) return 1;
  if (!strcmp(name, "new") || !strcmp(name, "allocate")) {
    if (k->is_module) return 0;
    /* an exception is a runtime sp_Exception, for a builtin class too */
    if (name[0] == 'n' && lupe_is_exc_class(k)) {
      *out = lupe_exc_new(k, argc, argv, blk);
      return 1;
    }
    /* a builtin class other than Object makes its own instances */
    if (k->cls_id < 0 && k != L.object) return 0;
    /* an instance of a subclass of Exception, Array, Struct ... needs the
       builtin part, which a LupeObject does not have */
    for (const LupeClass *s = k->super; s; s = s->super)
      if (s->cls_id < 0 && s != L.object && strcmp(s->name, "BasicObject"))
        lupe_gap(sp_sprintf("instance of a subclass of %s", s->name));
    sp_RbVal obj = lupe_new_object(k);
    SP_GC_ROOT_RBVAL(obj);
    if (name[0] == 'n') {
      LupeMethod *init = lupe_find_method(k, sp_sym_intern("initialize"));
      if (init) lupe_invoke_b(init, obj, argc, argv, blk);
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
    *out = k->super ? lupe_class_box(k->super) : sp_box_nil();
    return 1;
  }
  if (argc == 0 && !strcmp(name, "ancestors")) {
    const LupeClass *anc[128];
    int n = lupe_ancestors(k, anc, 128);
    sp_PolyArray *a = sp_PolyArray_new();
    SP_GC_ROOT(a);
    for (int i = 0; i < n; i++) sp_PolyArray_push(a, lupe_class_box(anc[i]));
    *out = sp_box_poly_array(a);
    return 1;
  }
  if (!strcmp(name, "attr_accessor") || !strcmp(name, "attr_reader") || !strcmp(name, "attr_writer")) {
    lupe_attr(k, name[5] != 'w', name[5] != 'r', f && f->singleton_defs == 1, argc, argv);
    sp_PolyArray *a = sp_PolyArray_new();
    for (int i = 0; i < argc; i++) sp_PolyArray_push(a, argv[i]);
    *out = sp_box_poly_array(a);
    return 1;
  }
  if (!strcmp(name, "include") || !strcmp(name, "extend")) {
    for (int i = argc - 1; i >= 0; i--) {
      SP_GC_ROOT_RBVAL(r);
      LupeClass *m = lupe_class_value(argv[i]);
      if (!m || !m->is_module) sp_raise_cls("TypeError", "wrong argument type (expected Module)");
      if (name[0] == 'i') lupe_push_class(&k->includes, &k->nincludes, &k->includes_cap, m);
      else lupe_push_class(&k->extends, &k->nextends, &k->extends_cap, m);
      /* the hook of the module: included(base) / extended(base) */
      LupeMethod *hook = lupe_table_get(&m->smethods, sp_sym_intern(name[0] == 'i' ? "included" : "extended"));
      if (hook) lupe_invoke(hook, argv[i], 1, &r);
    }
    *out = r;
    return 1;
  }
  /* Class ordering: k < o when o is an ancestor of k; nil when unrelated */
  if (argc == 1 && (!strcmp(name, "<") || !strcmp(name, "<=") || !strcmp(name, ">") || !strcmp(name, ">=") ||
                    !strcmp(name, "<=>"))) {
    LupeClass *o = lupe_class_value(argv[0]);
    if (!o && name[2] == '>') { *out = sp_box_nil(); return 1; }
    if (!o) sp_raise_cls("TypeError", "compared with non class/module");
    const LupeClass *anc[128];
    int below = 0, above = 0;
    int n = lupe_ancestors(k, anc, 128);
    for (int i = 1; i < n; i++) if (anc[i] == o) below = 1;
    n = lupe_ancestors(o, anc, 128);
    for (int i = 1; i < n; i++) if (anc[i] == k) above = 1;
    int strict = name[1] == 0;
    if (name[2] == '>') *out = k == o ? sp_box_int(0) : below ? sp_box_int(-1) : above ? sp_box_int(1) : sp_box_nil();
    else if (k == o) *out = sp_box_bool(!strict);
    else if (below || above) *out = sp_box_bool(name[0] == '<' ? below : above);
    else *out = sp_box_nil();
    return 1;
  }
  if (!strcmp(name, "module_function") && f) {
    if (argc == 0) { f->singleton_defs = 2; *out = sp_box_nil(); return 1; }
    for (int i = 0; i < argc; i++) {
      LupeMethod *m = lupe_table_get(&k->methods, sp_sym_intern(lupe_name_arg(argv[i])));
      if (m) lupe_define_method(k, m, 1);
    }
    *out = sp_box_nil();
    return 1;
  }
  /* visibility is not modeled: these answer as CRuby does and change nothing */
  if (!strcmp(name, "private") || !strcmp(name, "public") || !strcmp(name, "protected") ||
      !strcmp(name, "private_constant") || !strcmp(name, "public_constant") ||
      !strcmp(name, "private_class_method") || !strcmp(name, "public_class_method")) {
    *out = argc == 0 ? sp_box_nil() : argc == 1 ? argv[0] : sp_box_nil();
    return 1;
  }
  if (argc == 1 && !strcmp(name, "===")) { *out = sp_box_bool(lupe_is_a(argv[0], k)); return 1; }
  if (argc == 1 && (!strcmp(name, "==") || !strcmp(name, "equal?"))) {
    *out = sp_box_bool(lupe_class_value(argv[0]) == k);
    return 1;
  }
  if (argc == 1 && !strcmp(name, "method_defined?")) {
    *out = sp_box_bool(lupe_find_method(k, sp_sym_intern(lupe_name_arg(argv[0]))) != NULL);
    return 1;
  }
  if (argc == 1 && !strcmp(name, "const_get")) {
    sp_RbVal v;
    const char *cn = lupe_name_arg(argv[0]);
    if (!lupe_const_scoped(k, sp_sym_intern(cn), &v)) lupe_const_missing(cn);
    *out = v;
    return 1;
  }
  return 0;
}
