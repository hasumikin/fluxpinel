/* location.c -- Thread::Backtrace::Location: a frame of Kernel#caller_locations.

   A Location keeps what the line of Kernel#caller says of the frame
   (fx_bt_build): the path, the line and the label, and the base label
   beside them. The frames live on the C stack, which the call leaves, so
   a Location is made at the call and holds the strings. */

#include "fluxpinel.h"

typedef struct {
  sp_int cls_id;            /* first, as in an object of spinel */
  const char *path;         /* the path of the file, as the backtrace writes it */
  const char *label;        /* "block in A#m" (a heap String) */
  const char *base_label;   /* "m" (a heap String) */
  int line;                 /* 0 for a frame before its first statement */
} FxLocation;

static FxClass *fx_location_cls;

static void fx_location_scan(void *p) {
  FxLocation *l = (FxLocation *)p;
  sp_mark_string(l->label);
  sp_mark_string(l->base_label);
}

/* The class Thread::Backtrace::Location, under the module Thread::Backtrace:
   a native class, whose instance is a C struct. */
static void fx_location_init(void) {
  FxClass *thread = fx_builtin("Thread");
  FxClass *bt = fx_class_new("Thread::Backtrace", 1, NULL, thread);
  fx_const_set(thread, sp_sym_intern("Backtrace"), fx_class_box(bt));
  fx_location_cls = fx_class_new("Thread::Backtrace::Location", 0, L.object, bt);
  fx_location_cls->native = 1;
  fx_const_set(bt, sp_sym_intern("Location"), fx_class_box(fx_location_cls));
}

static int fx_is_location(sp_RbVal v) {
  return v.tag == SP_TAG_OBJ && fx_location_cls && v.cls_id == fx_location_cls->inst_id && v.v.p;
}

/* The base label of a frame, as CRuby gives it: the name of the method of
   a method frame or of a block in one, "<main>" for the main program and
   its blocks, the label of the others. A heap String. */
static const char *fx_bt_base_label(const FxTrace *t) {
  switch (t->kind) {
    case FX_TR_METHOD:
    case FX_TR_BLOCK: return sp_sprintf("%s", t->method ? sp_sym_to_s(t->method->name) : "<main>");
    case FX_TR_CLASS: return sp_sprintf("<class:%s>", t->cls);
    case FX_TR_MODULE: return sp_sprintf("<module:%s>", t->cls);
    case FX_TR_SCLASS: return sp_sprintf("singleton class");
    case FX_TR_BUILTIN: return sp_sprintf("%s", t->bname);
    default: return sp_sprintf("<main>");
  }
}

/* Kernel#caller_locations: the Locations of the frames of s below top, the
   innermost first, after the skip innermost ones, at most len of them
   (-1 for all), as fx_bt_build lists their lines. */
static sp_RbVal fx_locations_build(const FxFrameStack *s, int top, int skip, sp_int len) {
  SP_GC_SAVE();
  sp_PolyArray *a = sp_PolyArray_new();
  SP_GC_ROOT(a);
  for (int d = top - 1 - skip; d >= 0 && (len < 0 || a->len < len); d--) {
    SP_GC_SAVE();
    const FxTrace *t = &s->tr[d];
    const pm_node_t *n = t->at ? t->at : t->body;
    const char *label = fx_bt_label(t);
    SP_GC_ROOT_STR(label);
    const char *base = fx_bt_base_label(t);
    SP_GC_ROOT_STR(base);
    FxLocation *l = (FxLocation *)sp_gc_alloc(sizeof *l, NULL, fx_location_scan);
    l->cls_id = fx_location_cls->inst_id;
    l->label = label;
    l->base_label = base;
    l->path = n ? fx_source_of(n)->path : L.path;
    l->line = n ? fx_line(n) : 0;
    sp_PolyArray_push(a, sp_box_obj(l, (int)l->cls_id));
  }
  return sp_box_poly_array(a);
}

static sp_RbVal fx_location_str(const char *s) { return fx_new_string((const uint8_t *)s, strlen(s)); }

/* The methods of a Location. */
static int fx_location_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                            sp_RbVal *out) {
  (void)argv; (void)blk;
  if (argc != 0) return 0;
  FxLocation *l = (FxLocation *)r.v.p;
  SP_GC_ROOT(l);
  if (!strcmp(name, "path")) { *out = fx_location_str(l->path); return 1; }
  /* the main program's path, as given, from the working directory; a
     loaded file's path is its real path already */
  if (!strcmp(name, "absolute_path")) {
    const char *s = sp_file_expand_path(l->path, NULL);
    SP_GC_ROOT_STR(s);
    *out = fx_location_str(s);
    return 1;
  }
  if (!strcmp(name, "lineno")) { *out = sp_box_int(l->line); return 1; }
  if (!strcmp(name, "label")) { *out = fx_location_str(l->label); return 1; }
  if (!strcmp(name, "base_label")) { *out = fx_location_str(l->base_label); return 1; }
  if (!strcmp(name, "to_s") || !strcmp(name, "inspect")) {
    const char *s = l->line ? sp_sprintf("%s:%d:in '%s'", l->path, l->line, l->label)
                            : sp_sprintf("%s:in '%s'", l->path, l->label);
    SP_GC_ROOT_STR(s);
    *out = fx_location_str(s);
    if (name[0] == 'i') *out = fx_send(*out, "inspect", 0, NULL);
    return 1;
  }
  return 0;
}

static int fx_location_responds(const char *name) {
  static const char *const names[] = { "path", "absolute_path", "lineno", "label", "base_label", "to_s", "inspect",
                                       NULL };
  for (int i = 0; names[i]; i++)
    if (!strcmp(name, names[i])) return 1;
  return 0;
}
