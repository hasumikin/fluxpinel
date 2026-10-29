/* responds.c -- respond_to? on a builtin value, from the public methods of
   the builtin classes of CRuby (tools/responds_gen.rb writes the table).

   A row gives the own public instance methods of a class, the own public
   methods of its singleton class, its superclass and its ancestors. The
   lists are sorted for strcmp. The answer is the answer of CRuby, also
   for a method that Fluxpinel lacks: a call of it then shows as a gap. */

#include "fluxpinel.h"

typedef struct {
  const char *name;
  int is_module;
  const char *super;                /* the superclass, or NULL */
  const char *const *inst;          /* the own public instance methods */
  const char *const *sing;          /* the own public singleton methods */
  const char *const *anc;           /* the ancestors after the class */
} FxRespondRow;

#include "responds.inc"

#define FX_RS_NROWS ((int)(sizeof fx_rs_rows / sizeof fx_rs_rows[0]))

static const FxRespondRow *fx_rs_row(const char *name) {
  int lo = 0, hi = FX_RS_NROWS - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2, c = strcmp(name, fx_rs_rows[mid].name);
    if (!c) return &fx_rs_rows[mid];
    if (c < 0) hi = mid - 1;
    else lo = mid + 1;
  }
  return NULL;
}

static int fx_rs_in(const char *const *list, const char *mn) {
  int n = 0;
  while (list[n]) n++;
  int lo = 0, hi = n - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2, c = strcmp(mn, list[mid]);
    if (!c) return 1;
    if (c < 0) hi = mid - 1;
    else lo = mid + 1;
  }
  return 0;
}

/* Whether an instance of the builtin class cls has the public method mn:
   of cls, or of an ancestor. */
static int fx_rs_instance(const char *cls, const char *mn) {
  const FxRespondRow *row = fx_rs_row(cls);
  if (!row) return 0;
  if (fx_rs_in(row->inst, mn)) return 1;
  for (int i = 0; row->anc[i]; i++) {
    const FxRespondRow *a = fx_rs_row(row->anc[i]);
    if (a && fx_rs_in(a->inst, mn)) return 1;
  }
  return 0;
}

/* The own public instance methods of the builtin class or module cls, as
   Symbols into a (fx_names_push); nothing for a class with no row. */
static void fx_rs_own_names(sp_PolyArray *a, const char *cls) {
  const FxRespondRow *row = fx_rs_row(cls);
  for (int i = 0; row && row->inst[i]; i++) fx_names_push(a, sp_sym_intern(row->inst[i]));
}

/* The own public singleton methods of cls (Integer.sqrt, File.open). */
static void fx_rs_singleton_names(sp_PolyArray *a, const char *cls) {
  const FxRespondRow *row = fx_rs_row(cls);
  for (int i = 0; row && row->sing[i]; i++) fx_names_push(a, sp_sym_intern(row->sing[i]));
}

/* Whether the builtin class of r has the public method mn of its own, not
   from an ancestor. */
static int fx_rs_own(sp_RbVal r, const char *mn) {
  const FxRespondRow *row = fx_rs_row(sp_poly_class_name(r));
  return row && fx_rs_in(row->inst, mn);
}

/* Whether the builtin class or module cls itself has the public method mn:
   a singleton method of it or of a superclass, or a method of Class (of
   Module for a module). */
static int fx_rs_singleton(const char *cls, const char *mn) {
  const FxRespondRow *row = fx_rs_row(cls);
  if (!row) return 0;
  int module = row->is_module;
  for (; row; row = row->super ? fx_rs_row(row->super) : NULL)
    if (fx_rs_in(row->sing, mn)) return 1;
  return fx_rs_instance(module ? "Module" : "Class", mn);
}

/* The nearest builtin class of c: c, or a superclass. */
static const FxClass *fx_rs_builtin_of(const FxClass *c) {
  while (c && c->cls_id >= FX_CLS_BASE) c = c->super;
  return c;
}

/* Whether the instances of k have the public method mn as a builtin method
   of CRuby, for method_defined?: of k itself when k is builtin and not
   inherit, else of the builtin modules that k includes and of its nearest
   builtin class. -1 when the table cannot tell (k has no row). */
static int fx_rs_defined(const FxClass *k, const char *mn, int inherit) {
  if (k->cls_id < FX_CLS_BASE) {
    const FxRespondRow *row = fx_rs_row(k->name);
    if (!row) return -1;
    return inherit ? fx_rs_instance(k->name, mn) : fx_rs_in(row->inst, mn);
  }
  if (!inherit) return 0;
  const FxClass *anc[128];
  int n = fx_ancestors(k, anc, 128);
  for (int i = 0; i < n; i++)
    if (anc[i]->cls_id < FX_CLS_BASE && anc[i]->is_module && fx_rs_instance(anc[i]->name, mn)) return 1;
  const FxClass *b = fx_rs_builtin_of(k);
  if (!b) return 0;
  return fx_rs_row(b->name) ? fx_rs_instance(b->name, mn) : -1;
}

/* Whether r has the public method mn as a builtin method of CRuby. A
   method of the program is not here: fx_respond_to looks it up first. */
static int fx_rs_responds(sp_RbVal r, const char *mn) {
  if (r.tag == SP_TAG_CLASS) {
    FxClass *k = fx_class_value(r);
    if (!k) return 0;
    if (k->is_module && k->cls_id >= FX_CLS_BASE) return fx_rs_instance("Module", mn);
    const FxClass *b = fx_rs_builtin_of(k);
    return b ? fx_rs_singleton(b->name, mn) : fx_rs_instance("Class", mn);
  }
  const char *name = sp_poly_class_name(r);
  if (!fx_is_fx_object(r) && fx_rs_row(name)) return fx_rs_instance(name, mn);
  /* a value of a class of the program (an object, or an exception): the
     builtin modules that its class includes, then the nearest builtin
     superclass */
  const FxClass *anc[128];
  int n = fx_ancestors(fx_class_of(r), anc, 128);
  for (int i = 0; i < n; i++)
    if (anc[i]->cls_id < FX_CLS_BASE && anc[i]->is_module && fx_rs_instance(anc[i]->name, mn)) return 1;
  const FxClass *b = fx_rs_builtin_of(fx_class_of(r));
  return b && fx_rs_instance(b->name, mn);
}

/* Whether CRuby can have the method mn on r: a public builtin method, a
   private method of Kernel, or any method after a require of a feature
   or of a value whose class has no row (the table cannot tell). A miss of
   such a method is a gap of Fluxpinel, not NoMethodError. */
static int fx_rs_known(sp_RbVal r, const char *mn) {
  if (fx_feature_loaded || fx_kernel_private(mn) || fx_rs_responds(r, mn)) return 1;
  return r.tag != SP_TAG_CLASS && !fx_is_fx_object(r) && !fx_rs_row(sp_poly_class_name(r));
}
