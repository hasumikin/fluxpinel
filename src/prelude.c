/* prelude.c -- builtin methods with a block, written in Ruby.

   src/prelude.rb defines them; tools/prelude_gen.rb makes it a C string.
   fx_builtin_call asks fx_prelude_find for a builtin method that takes a
   block when no builtin of Fluxpinel takes it, before it stops. The first
   such call parses the prelude. A method of it is a method of the program
   for the walker, but no class of the program holds it: the program does
   not see it, and a call of a builtin method does not look for it. So the
   other calls keep their speed (a reopened builtin class slows each call
   on its values). */

static const char fx_prelude_src[] =
#include "prelude.inc"
    ;

typedef struct {
  const char *cls;   /* the builtin class of the receiver */
  FxMethod *m;
} FxPreludeMethod;

static struct {
  int parsed;
  FxPreludeMethod *items;
  int n, cap;
} fx_prelude;

static void fx_prelude_add(const char *cls, FxMethod *m) {
  if (fx_prelude.n == fx_prelude.cap) {
    fx_prelude.cap = fx_prelude.cap ? fx_prelude.cap * 2 : 32;
    fx_prelude.items = realloc(fx_prelude.items, sizeof *fx_prelude.items * (size_t)fx_prelude.cap);
  }
  fx_prelude.items[fx_prelude.n++] = (FxPreludeMethod){ cls, m };
}

/* Each `class C ... end` of the top level, and each def in it. */
static void fx_prelude_parse(void) {
  fx_prelude.parsed = 1;
  size_t len = sizeof fx_prelude_src - 1;
  char *buf = malloc(len + 1);
  memcpy(buf, fx_prelude_src, len + 1);
  FxSource *s;
  const char *err = fx_parse("<internal:fluxpinel>", "<internal:fluxpinel>", buf, len, &s);
  if (err) {
    fprintf(stderr, "fluxpinel: the prelude does not parse: %s\n", err);
    _exit(2);
  }
  const pm_statements_node_t *top = ((const pm_program_node_t *)s->root)->statements;
  for (size_t i = 0; top && i < top->body.size; i++) {
    const pm_node_t *n = top->body.nodes[i];
    if (!PM_NODE_TYPE_P(n, PM_CLASS_NODE)) continue;
    const pm_class_node_t *c = (const pm_class_node_t *)n;
    if (!PM_NODE_TYPE_P(c->constant_path, PM_CONSTANT_READ_NODE) || !c->body ||
        !PM_NODE_TYPE_P(c->body, PM_STATEMENTS_NODE))
      continue;
    const char *cls = sp_sym_to_s(fx_sym(((const pm_constant_read_node_t *)c->constant_path)->name));
    const pm_statements_node_t *body = (const pm_statements_node_t *)c->body;
    for (size_t j = 0; j < body->body.size; j++) {
      if (!PM_NODE_TYPE_P(body->body.nodes[j], PM_DEF_NODE)) continue;
      const pm_def_node_t *d = (const pm_def_node_t *)body->body.nodes[j];
      FxMethod *m = fx_method_new(FX_M_DEF, fx_sym(d->name), d, 0, L.object);
      m->lexical = L.object;
      fx_prelude_add(cls, m);
    }
  }
}

/* The method name of the prelude for the receiver r, or NULL. Numeric
   serves Integer and Float, and IO serves each IO of the runtime (a File
   too). */
static const FxMethod *fx_prelude_find(sp_RbVal r, const char *name) {
  if (!fx_prelude.parsed) fx_prelude_parse();
  const char *cls = sp_poly_class_name(r);
  int num = fx_is_integer(r) || r.tag == SP_TAG_FLT;
  int io = r.tag == SP_TAG_OBJ && r.cls_id == SP_BUILTIN_IO && r.v.p;
  sp_sym s = sp_sym_intern(name);
  for (int i = 0; i < fx_prelude.n; i++) {
    const FxPreludeMethod *p = &fx_prelude.items[i];
    if (p->m->name != s) continue;
    if (!strcmp(p->cls, cls) || (num && !strcmp(p->cls, "Numeric")) || (io && !strcmp(p->cls, "IO"))) return p->m;
  }
  return NULL;
}

/* `__fx_gap__ "what"`: the prelude stops at a case that it does not
   write. It is out of line, as fx_prelude_call is. */
static __attribute__((noinline, cold)) void fx_prelude_gap(const char *name, int argc, sp_RbVal *argv) {
  if (argc == 1 && !strcmp(name, "__fx_gap__")) fx_gap(sp_poly_arg_str_chk(argv[0]));
}

/* Whether the prelude has the method name for r, for respond_to?. */
static int fx_prelude_has(sp_RbVal r, const char *name) {
  return fx_prelude_find(r, name) != NULL;
}

/* Calls the method name of the prelude on r, when there is one. It is out
   of line: the inlining of fx_builtin_call stays as it was. */
static __attribute__((noinline, cold)) int fx_prelude_call(sp_RbVal r, const char *name, int argc,
                                                            sp_RbVal *argv, const FxBlock *blk, sp_RbVal *out) {
  const FxMethod *m = fx_prelude_find(r, name);
  if (!m) return 0;
  *out = fx_invoke_b(m, r, argc, argv, blk);
  return 1;
}
