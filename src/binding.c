/* binding.c -- Binding: the scope of a `binding` call, which Kernel#eval,
   Binding#eval and the local_variable methods read and write later
   (docs/design.md, section 3.3.1).

   A Binding keeps the heap frame of the call (fx_frame_heap), so the
   scope and the Binding share one set of variables, as a Proc does. Each
   Binding#eval runs its code in a new frame under the last one and keeps
   that frame: a later eval sees the locals of an earlier one, as the
   console does with its pieces (console.c). local_variable_set of a new
   name adds a frame of that one name. */

#include "fluxpinel.h"

typedef struct {
  sp_int cls_id;        /* first, as in an object of spinel */
  FxFrame *base;        /* the heap frame of the `binding` call */
  FxFrame *frame;       /* the frame that the next eval runs under: base, or the
                           frame of the last eval or local_variable_set */
  const char *path;     /* source_location */
  int line;
} FxBinding;

static FxClass *fx_binding_cls;

static void fx_binding_scan(void *p) {
  FxBinding *b = (FxBinding *)p;
  if (b->base) sp_gc_mark(b->base);
  if (b->frame) sp_gc_mark(b->frame);
}

/* The class Binding: a native class, whose instance is a C struct. */
static void fx_binding_init(void) {
  fx_binding_cls = fx_class_new("Binding", 0, L.object, L.object);
  fx_binding_cls->native = 1;
  fx_const_set(L.object, sp_sym_intern("Binding"), fx_class_box(fx_binding_cls));
}

static int fx_is_binding(sp_RbVal v) {
  return v.tag == SP_TAG_OBJ && fx_binding_cls && v.cls_id == fx_binding_cls->inst_id && v.v.p;
}

/* Kernel#binding at a call in the frame f, at path:line. */
static sp_RbVal fx_binding_new(FxFrame *f, const char *path, int line) {
  SP_GC_SAVE();
  FxFrame *h = fx_frame_heap(f);
  SP_GC_ROOT(h);
  FxBinding *b = (FxBinding *)sp_gc_alloc(sizeof *b, NULL, fx_binding_scan);
  b->cls_id = fx_binding_cls->inst_id;
  b->base = b->frame = h;
  b->path = path;
  b->line = line;
  return sp_box_obj(b, (int)b->cls_id);
}

/* The slot of the local variable name in the frames of the binding, the
   newest first, or NULL. The caller can store into the slot. */
static sp_RbVal *fx_binding_slot(FxBinding *b, sp_sym name) {
  for (FxFrame *p = b->frame; p; p = p->parent)
    for (size_t i = 0; i < p->names->size; i++)
      if (fx_sym(p->names->ids[i]) == name) {
        fx_frame_wb(p);
        return &p->locals[i];
      }
  return NULL;
}

/* Binding#local_variables: the names of the frames, the newest first, each
   once, as CRuby lists the locals of an eval before those of the scope. */
static sp_RbVal fx_binding_locals(FxBinding *b) {
  sp_PolyArray *a = sp_PolyArray_new();
  SP_GC_ROOT(a);
  for (FxFrame *p = b->frame; p; p = p->parent)
    for (size_t i = 0; i < p->names->size; i++) {
      sp_sym s = fx_sym(p->names->ids[i]);
      int seen = 0;
      for (sp_int j = 0; j < a->len && !seen; j++) seen = (sp_sym)a->data[j].v.i == s;
      if (!seen) sp_PolyArray_push(a, sp_box_sym(s));
    }
  return sp_box_poly_array(a);
}

/* local_variable_set of a name that no frame has: a frame of that one
   name under the last frame, as an eval of an assignment would add. The
   scope of the call does not see the name, as in CRuby. */
static void fx_binding_add(FxBinding *b, sp_sym name, sp_RbVal v) {
  SP_GC_SAVE();
  SP_GC_ROOT(b);
  SP_GC_ROOT_RBVAL(v);
  pm_constant_id_list_t *names = calloc(1, sizeof *names);   /* lives with the frame */
  pm_constant_id_list_init(names);
  const char *s = sp_sym_to_s(name);
  pm_constant_id_list_append(names, pm_constant_pool_insert_constant(&L.pool, (const uint8_t *)s, strlen(s)));
  FX_FRAME(top, names, b->base->self, b->base->cref);
  fx_frame_context(&top, b->base);
  top.parent = b->frame;
  top.locals[0] = v;
  FxFrame *h = fx_frame_heap(&top);
  sp_gc_wb(b);
  b->frame = h;
}

/* Binding#eval(src, file, line), and Kernel#eval(src, binding, file,
   line): the code runs in a new frame under the last frame of the
   binding, with the self and the context of the `binding` call, and the
   binding keeps that frame. path is NULL for the default. */
static sp_RbVal fx_binding_eval(sp_RbVal bv, const char *src, const char *path, int line) {
  SP_GC_SAVE();
  SP_GC_ROOT_RBVAL(bv);
  FxBinding *b = (FxBinding *)bv.v.p;
  FxFrame *kept = NULL;
  sp_RbVal v = fx_eval_in(b->frame, b->base, src, path ? path : "(eval)", line, &kept);
  sp_gc_wb(b);
  b->frame = kept;
  return v;
}

static sp_RbVal fx_binding_copy(FxBinding *b) {
  SP_GC_ROOT(b);
  FxBinding *c = (FxBinding *)sp_gc_alloc(sizeof *c, NULL, fx_binding_scan);
  *c = *b;
  return sp_box_obj(c, (int)c->cls_id);
}

static __attribute__((noreturn)) void fx_binding_no_local(sp_RbVal bv, sp_sym name) {
  sp_raise_cls("NameError", sp_sprintf("local variable '%s' is not defined for %s", sp_sym_to_s(name),
                                       sp_poly_inspect(bv)));
}

/* The methods of a Binding. Answers 0 for any other call. */
static int fx_binding_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                             sp_RbVal *out) {
  if (blk) return 0;
  FxBinding *b = (FxBinding *)r.v.p;
  if (!strcmp(name, "eval")) {
    if (argc < 1 || argc > 3) fx_arity_error(argc, 1, 2, 0);
    const char *src = sp_poly_arg_str_chk(argv[0]);
    const char *path = argc >= 2 && argv[1].tag != SP_TAG_NIL ? sp_poly_arg_str_chk(argv[1]) : NULL;
    int line = argc >= 3 && argv[2].tag != SP_TAG_NIL ? (int)sp_poly_arg_int_chk(argv[2]) : 1;
    *out = fx_binding_eval(r, src, path, line);
    return 1;
  }
  if (argc == 0 && !strcmp(name, "local_variables")) {
    *out = fx_binding_locals(b);
    return 1;
  }
  if (argc == 1 && !strcmp(name, "local_variable_get")) {
    sp_sym s = sp_sym_intern(fx_name_arg(argv[0]));
    sp_RbVal *slot = fx_binding_slot(b, s);
    if (!slot) fx_binding_no_local(r, s);
    *out = *slot;
    return 1;
  }
  if (argc == 1 && !strcmp(name, "local_variable_defined?")) {
    *out = sp_box_bool(fx_binding_slot(b, sp_sym_intern(fx_name_arg(argv[0]))) != NULL);
    return 1;
  }
  if (argc == 2 && !strcmp(name, "local_variable_set")) {
    sp_sym s = sp_sym_intern(fx_name_arg(argv[0]));
    sp_RbVal *slot = fx_binding_slot(b, s);
    if (slot) *slot = argv[1];
    else fx_binding_add(b, s, argv[1]);
    *out = argv[1];
    return 1;
  }
  if (argc == 0 && !strcmp(name, "receiver")) {
    *out = b->base->self;
    return 1;
  }
  if (argc == 0 && !strcmp(name, "source_location")) {
    sp_PolyArray *a = sp_PolyArray_new();
    SP_GC_ROOT(a);
    sp_PolyArray_push(a, fx_new_string((const uint8_t *)b->path, strlen(b->path)));
    sp_PolyArray_push(a, sp_box_int(b->line));
    *out = sp_box_poly_array(a);
    return 1;
  }
  if (argc == 0 && (!strcmp(name, "dup") || !strcmp(name, "clone"))) {
    *out = fx_binding_copy(b);
    return 1;
  }
  return 0;
}

static int fx_binding_responds(const char *name) {
  static const char *const names[] = { "eval", "local_variables", "local_variable_get", "local_variable_set",
                                       "local_variable_defined?", "receiver", "source_location", "dup", "clone",
                                       NULL };
  for (int i = 0; names[i]; i++)
    if (!strcmp(name, names[i])) return 1;
  return 0;
}
