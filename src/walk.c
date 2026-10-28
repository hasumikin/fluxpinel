/* walk.c -- the AST evaluator of Fluxpinel.

   The walker evaluates the prism AST directly. Every value is an sp_RbVal.
   A C local that holds a value across a call that can allocate is rooted
   with SP_GC_ROOT_RBVAL, as the code that spinel generates does. */

#include "fluxpinel.h"

/* The source file that holds node n, by the bytes it points into. */
static FxSource *fx_source_of(const pm_node_t *n) {
  for (int i = L.nsources - 1; i >= 0; i--) {
    FxSource *s = L.sources[i];
    if (n->location.start >= s->start && n->location.start <= s->end) return s;
  }
  return L.sources[0];
}

static int fx_line(const pm_node_t *n) {
  const pm_parser_t *p = &fx_source_of(n)->parser;
  return pm_newline_list_line_column(&p->newline_list, n->location.start, p->start_line).line;
}

static void fx_unsupported(const pm_node_t *n, const char *what) {
  fprintf(stderr, "fluxpinel: %s:%d: unsupported %s (%s)\n", fx_source_of(n)->path, fx_line(n), what,
          pm_node_type_to_str(PM_NODE_TYPE(n)));
  exit(2);
}

/* A semantics that Fluxpinel does not model yet, met at run time with no node
   at hand. It stops as fx_unsupported does, so the oracle counts the
   test as a gap of Fluxpinel, not as a wrong answer. */
static void fx_gap(const char *what) {
  fprintf(stderr, "fluxpinel: %s: unsupported %s (runtime)\n", L.path, what);
  /* a gap can come on a green thread: end at once, without the exit
     handlers of the runtime, which expect the main thread */
  fflush(stdout);
  fflush(stderr);
  _exit(2);
}

/* The Symbol of each constant id of L.pool, plus 1 (0: not interned yet).
   Every file shares the pool, so one table serves them all. It grows when
   a file adds constants. */
static struct {
  sp_sym *syms;
  size_t cap;
} fx_sym_cache;

static SP_NOINLINE sp_sym fx_sym_slow(pm_constant_id_t id) {
  if (id >= fx_sym_cache.cap) {
    size_t cap = fx_sym_cache.cap ? fx_sym_cache.cap : 1024;
    while (cap <= id) cap *= 2;
    fx_sym_cache.syms = realloc(fx_sym_cache.syms, sizeof *fx_sym_cache.syms * cap);
    memset(fx_sym_cache.syms + fx_sym_cache.cap, 0, sizeof *fx_sym_cache.syms * (cap - fx_sym_cache.cap));
    fx_sym_cache.cap = cap;
  }
  const pm_constant_t *c = pm_constant_pool_id_to_constant(&L.pool, id);
  sp_sym s = fx_sym_intern_n((const char *)c->start, c->length);
  fx_sym_cache.syms[id] = s + 1;
  return s;
}

static inline sp_sym fx_sym(pm_constant_id_t id) {
  if (id < fx_sym_cache.cap && fx_sym_cache.syms[id]) return fx_sym_cache.syms[id] - 1;
  return fx_sym_slow(id);
}

/* The Symbol of a name. Most names are the text of a Symbol itself
   (sp_sym_to_s), which never moves or goes: a cache keyed by that pointer
   answers them with no intern. Another text, such as a formatted one,
   takes the intern each time and stays out of the cache. */
static struct {
  const char *p;
  sp_sym s;
} fx_name_cache[1024];

static inline sp_sym fx_name_sym(const char *name) {
  size_t h = ((uintptr_t)name >> 3) & 1023;
  if (fx_name_cache[h].p == name) return fx_name_cache[h].s;
  sp_sym s = sp_sym_intern(name);
  if (sp_sym_to_s(s) == name) {
    fx_name_cache[h].p = name;
    fx_name_cache[h].s = s;
  }
  return s;
}

/* The write barrier of a frame whose slots are on the heap (FxEnv): the
   collector is generational, and an env that is old already must join the
   remembered set before a slot of it takes a young value. */
static void fx_frame_wb(const FxFrame *f) {
  if (f->heap) sp_gc_wb((void *)f);
  else if (f->env) sp_gc_wb(f->env);
}

/* The slot of a local variable. depth counts the block scopes between the
   reference and the scope that owns the variable. The caller can store
   into the slot, so the frame takes the write barrier. */
static sp_RbVal *fx_local(FxFrame *f, const pm_node_t *n, pm_constant_id_t name, uint32_t depth) {
  for (uint32_t d = 0; d < depth; d++)
    if (!(f = f->parent)) fx_unsupported(n, "local variable of an outer scope");
  fx_frame_wb(f);
  for (size_t i = 0; i < f->names->size; i++)
    if (f->names->ids[i] == name) return &f->locals[i];
  fx_unsupported(n, "local variable with no slot");
}

/* Declares the slots of a frame on the C stack, and sets the self and the
   lexical class of the scope. Three slots follow the locals: `it`, the
   anonymous *rest and the anonymous **kwrest. The slots are one root
   frame of the runtime (SP_GC_ROOT_FRAME): its header sits at the end of
   element 0, and the slots follow from element 1, so that one push roots
   them all. The heap copy of the frame (env) is a root too. The roots stay
   until the enclosing SP_GC_SAVE scope ends. */
_Static_assert(sizeof(sp_gc_frame_hdr) <= sizeof(sp_RbVal), "a frame header fits in one slot");
#define FX_FRAME(f, namelist, self_, cref_)                                     \
  size_t f##_n = (namelist)->size;                                                \
  sp_RbVal f##_buf[f##_n + 4];                                                    \
  sp_RbVal *f##_slots = &f##_buf[1];                                              \
  sp_gc_frame_hdr *f##_hdr = (sp_gc_frame_hdr *)((char *)f##_slots - sizeof(sp_gc_frame_hdr)); \
  f##_hdr->nv = (int)(f##_n + 3);                                                 \
  f##_hdr->np = 0;                                                                \
  for (size_t f##_i = 0; f##_i < f##_n + 3; f##_i++) f##_slots[f##_i] = sp_box_nil(); \
  SP_GC_ROOT_FRAME(*f##_hdr);                                                     \
  FxFrame f __attribute__((cleanup(fx_frame_exit))) = {                       \
    .locals = f##_slots, .names = (namelist), .self = (self_), .cref = (cref_),   \
    .it = &f##_slots[f##_n], .anon = &f##_slots[f##_n + 1], .serial = ++L.serial, \
    .depth = fx_frame_enter(L.serial), .fs = fx_fs() };                       \
  _sp_gc_root_push((void **)&f.env)

/* The frame stacks tell whether the home frame of a block is live. They
   are on the heap, so a check never reads the stack memory of a frame
   that has ended. */

static FxFrameStack *fx_fs_new(const void *fiber) {
  FxFrameStack *s = calloc(1, sizeof *s);
  s->fiber = fiber;
  return s;
}

/* The table slot of a fiber, in a table with open addressing. */
static int fx_fs_slot(const void *fiber) {
  int mask = L.fstacks_cap - 1, i = (int)(((uintptr_t)fiber >> 4) & (uintptr_t)mask);
  while (L.fstacks[i].fiber && L.fstacks[i].fiber != fiber) i = (i + 1) & mask;
  return i;
}

static void fx_fs_put(const void *fiber, FxFrameStack *s) {
  if (2 * (L.nfstacks + 1) > L.fstacks_cap) {
    int ocap = L.fstacks_cap;
    __typeof__(L.fstacks) old = L.fstacks;
    L.fstacks_cap = ocap ? ocap * 2 : 64;
    L.fstacks = calloc((size_t)L.fstacks_cap, sizeof *L.fstacks);
    for (int i = 0; i < ocap; i++)
      if (old[i].fiber) L.fstacks[fx_fs_slot(old[i].fiber)] = old[i];
    free(old);
  }
  int i = fx_fs_slot(fiber);
  if (!L.fstacks[i].fiber) L.nfstacks++;
  L.fstacks[i].fiber = fiber;
  L.fstacks[i].fs = s;
}

/* The frame stack of the running fiber. A fiber that Fluxpinel started got one
   in fx_fiber_body; any other is a root fiber (the main program, or a
   root that the scheduler adopted), whose frames are the main ones. */
static FxFrameStack *fx_fs(void) {
  const void *cur = sp_fiber_current;
  if (L.fs->fiber == cur) return L.fs;
  int i = L.fstacks_cap ? fx_fs_slot(cur) : 0;
  if (L.fstacks_cap && L.fstacks[i].fiber) {
    L.fs = L.fstacks[i].fs;
  } else {
    /* a fiber that Fluxpinel did not start: a thread of the kernel that
       calls into the walker. Its frames are its own, since two such
       threads interleave in the walker as the threads of the walker do. */
    L.fs = fx_fs_new(cur);
    fx_fs_put(cur, L.fs);
  }
  return L.fs;
}

static void fx_fs_init(void) {
  L.main_fs = L.fs = fx_fs_new(sp_fiber_current);
  fx_fs_put(sp_fiber_current, L.main_fs);
}

static int fx_frame_enter(uint64_t serial) {
  FxFrameStack *s = fx_fs();
  if (s->depth == s->cap) {
    s->cap = s->cap ? s->cap * 2 : 256;
    s->serial = realloc(s->serial, sizeof *s->serial * (size_t)s->cap);
  }
  s->serial[s->depth] = serial;
  return s->depth++;
}

/* A frame that ends pops its entry. A raise skips this cleanup; the
   rescue that lands cuts the stack back to its own depth. */
static void fx_frame_exit(FxFrame *f) {
  f->fs->depth = f->depth;
}

static int fx_frame_live(const FxBlock *b) {
  return b->depth < b->fs->depth && b->fs->serial[b->depth] == b->serial;
}

/* Whether the frame on the C stack of a heap frame f is live. */
static int fx_env_live(const FxFrame *f) {
  return f->depth < f->fs->depth && f->fs->serial[f->depth] == f->serial;
}

static void fx_env_scan(void *p) {
  FxEnv *e = (FxEnv *)p;
  for (size_t i = 0; i < e->nslots; i++) sp_mark_rbval(e->slots[i]);
  sp_mark_rbval(e->frame.self);
  if (e->frame.parent) sp_gc_mark(e->frame.parent);
  if (e->frame.block && e->frame.block->proc) sp_gc_mark(e->frame.block->proc);
}

static sp_RbVal fx_block_proc(const FxBlock *b);

/* The heap copy of the frame f, which a Proc keeps (FxEnv). The first
   call for a frame on the C stack copies its slots to the heap, and the
   frame runs on with the heap slots. The parent frames move too, since a
   block reads their locals, and so does the block of the frame, which
   yield calls: a block literal of the caller becomes a Proc. */
static FxFrame *fx_frame_heap(FxFrame *f) {
  if (f->heap) return f;
  if (f->env) return &f->env->frame;
  size_t n = f->names->size + 3;
  FxEnv *e = (FxEnv *)sp_gc_alloc(sizeof *e + sizeof(sp_RbVal) * n, NULL, fx_env_scan);
  memcpy(e->slots, f->locals, sizeof(sp_RbVal) * n);
  e->nslots = n;
  e->frame = *f;
  e->frame.locals = e->slots;
  e->frame.it = &e->slots[n - 3];
  e->frame.anon = &e->slots[n - 2];
  e->frame.heap = 1;
  e->frame.env = NULL;
  e->frame.parent = NULL;
  e->frame.block = NULL;
  e->stack = f;
  f->env = e;   /* the root of e */
  f->locals = e->slots;
  f->it = e->frame.it;
  f->anon = e->frame.anon;
  /* each can collect, and e can be old by the store */
  if (f->parent) {
    FxFrame *parent = fx_frame_heap(f->parent);
    sp_gc_wb(e);
    e->frame.parent = parent;
  }
  if (f->block) {
    const FxBlock *blk = f->block->proc ? f->block : &((FxProc *)fx_block_proc(f->block).v.p)->b;
    sp_gc_wb(e);
    e->frame.block = blk;
  }
  return &e->frame;
}

/* ---- methods ---- */

static void fx_define(FxFrame *f, const pm_def_node_t *def) {
  sp_sym name = fx_sym(def->name);
  if (def->receiver) {
    if (!PM_NODE_TYPE_P(def->receiver, PM_SELF_NODE) || f->self.tag != SP_TAG_CLASS)
      fx_unsupported((const pm_node_t *)def, "singleton method on an object");
    FxClass *k = fx_class_value(f->self);
    FxMethod *sm = fx_method_new(FX_M_DEF, name, def, 0, k);
    sm->lexical = f->cref;
    fx_define_method(k, sm, 1);
    return;
  }
  if (f->singleton_defs == 3) fx_unsupported((const pm_node_t *)def, "def in instance_eval of an object");
  FxClass *k = f->definee ? f->definee : f->cref;
  FxMethod *m = fx_method_new(FX_M_DEF, name, def, 0, k);
  m->lexical = f->cref;
  /* after module_function: a private instance method and a public copy
     for the module */
  if (f->singleton_defs == 2) {
    fx_define_vis(k, m, 0, FX_VIS_PRIVATE);
    fx_define_method(k, fx_method_copy(m, FX_VIS_PUBLIC), 1);
    return;
  }
  /* 1: inside class << self; 4: instance_eval of a class */
  fx_define_vis(k, m, f->singleton_defs == 1 || f->singleton_defs == 4, fx_scope_vis(f));
}

/* kws is "; required keyword: k" or "" (fx_required_keywords) */
static __attribute__((noreturn)) void fx_arity_error_kw(int argc, int nreq, int nopt, int rest, const char *kws) {
  if (rest)
    sp_raise_cls("ArgumentError", sp_sprintf("wrong number of arguments (given %d, expected %d+%s)", argc, nreq, kws));
  if (nopt)
    sp_raise_cls("ArgumentError", sp_sprintf("wrong number of arguments (given %d, expected %d..%d%s)", argc, nreq,
                                             nreq + nopt, kws));
  sp_raise_cls("ArgumentError", sp_sprintf("wrong number of arguments (given %d, expected %d%s)", argc, nreq, kws));
}

static __attribute__((noreturn)) void fx_arity_error(int argc, int nreq, int nopt, int rest) {
  fx_arity_error_kw(argc, nreq, nopt, rest, "");
}

/* The tail that CRuby adds to an arity error when the method has required
   keywords: all of them, given or not. */
static const char *fx_required_keywords(const pm_parameters_node_t *ps) {
  sp_String *s = sp_String_new("");
  SP_GC_ROOT(s);
  int n = 0;
  for (size_t i = 0; i < ps->keywords.size; i++) {
    const pm_node_t *p = ps->keywords.nodes[i];
    if (!PM_NODE_TYPE_P(p, PM_REQUIRED_KEYWORD_PARAMETER_NODE)) continue;
    sp_String_append(s, n++ ? ", " : "");
    sp_String_append(s, sp_sym_to_s(fx_sym(((const pm_required_keyword_parameter_node_t *)p)->name)));
  }
  if (!n) return "";
  return sp_sprintf("; required keyword%s: %s", n > 1 ? "s" : "", s->data);
}

static int fx_is_array_value(sp_RbVal v) {
  return v.tag == SP_TAG_OBJ && sp_poly_is_array_kind(v.cls_id);
}

/* Binds one value to a required parameter or to a pattern `(a, *b, c)`.
   A pattern takes the elements of an Array; any other value is one element. */
static void fx_bind_target(FxFrame *f, const pm_node_t *p, sp_RbVal v) {
  if (PM_NODE_TYPE_P(p, PM_REQUIRED_PARAMETER_NODE)) {
    /* a repeated `_x` takes its argument, but the name reads the first one */
    if (PM_NODE_FLAG_P(p, PM_PARAMETER_FLAGS_REPEATED_PARAMETER)) return;
    *fx_local(f, p, ((const pm_required_parameter_node_t *)p)->name, 0) = v;
    return;
  }
  if (!PM_NODE_TYPE_P(p, PM_MULTI_TARGET_NODE)) fx_unsupported(p, "parameter pattern");
  SP_GC_ROOT_RBVAL(v);
  const pm_multi_target_node_t *mt = (const pm_multi_target_node_t *)p;
  sp_int len = fx_is_array_value(v) ? sp_poly_length(v) : 1;
  sp_int nl = (sp_int)mt->lefts.size, nr = (sp_int)mt->rights.size;
#define FX_ELT(i) (fx_is_array_value(v) ? sp_poly_arr_get(v, (i)) : (i) == 0 ? v : sp_box_nil())
  for (sp_int i = 0; i < nl; i++) fx_bind_target(f, mt->lefts.nodes[i], i < len ? FX_ELT(i) : sp_box_nil());
  sp_int rstart = len - nr > nl ? len - nr : nl;
  if (mt->rest && PM_NODE_TYPE_P(mt->rest, PM_SPLAT_NODE)) {
    const pm_node_t *e = ((const pm_splat_node_t *)mt->rest)->expression;
    if (e) {
      sp_PolyArray *a = sp_PolyArray_new();
      SP_GC_ROOT(a);
      for (sp_int i = nl; i < rstart; i++) sp_PolyArray_push(a, FX_ELT(i));
      fx_bind_target(f, e, sp_box_poly_array(a));
    }
  }
  for (sp_int i = 0; i < nr; i++)
    fx_bind_target(f, mt->rights.nodes[i], rstart + i < len ? FX_ELT(rstart + i) : sp_box_nil());
#undef FX_ELT
}

static sp_RbVal fx_block_proc(const FxBlock *b);

/* ": a, :b" for an ArgumentError message. */
static const char *fx_key_list(sp_PolyArray *keys) {
  sp_String *s = sp_String_new("");
  SP_GC_ROOT(s);
  for (sp_int i = 0; i < keys->len; i++) {
    if (i) sp_String_append(s, ", ");
    sp_String_append(s, sp_poly_inspect(keys->data[i]));
  }
  return s->data;
}

/* Binds the keyword parameters from the Hash kw (nil for none): each
   keyword, then **rest. A missing required keyword and an unknown key
   raise ArgumentError, as CRuby does. */
static void fx_bind_keywords(FxFrame *f, const pm_parameters_node_t *ps, sp_RbVal kw) {
  sp_PolyPolyHash *h = kw.tag == SP_TAG_NIL ? NULL : sp_poly_as_poly_poly_hash(kw);
  SP_GC_ROOT(h);
  sp_PolyArray *missing = sp_PolyArray_new(), *used = NULL;
  SP_GC_ROOT(missing);
  SP_GC_ROOT(used);
  used = sp_PolyArray_new();
  for (size_t i = 0; i < ps->keywords.size; i++) {
    const pm_node_t *p = ps->keywords.nodes[i];
    int req = PM_NODE_TYPE_P(p, PM_REQUIRED_KEYWORD_PARAMETER_NODE);
    pm_constant_id_t name = req ? ((const pm_required_keyword_parameter_node_t *)p)->name
                                : ((const pm_optional_keyword_parameter_node_t *)p)->name;
    sp_RbVal key = sp_box_sym(fx_sym(name));
    if (h && sp_PolyPolyHash_has_key(h, key)) {
      *fx_local(f, p, name, 0) = sp_PolyPolyHash_get(h, key);
      sp_PolyArray_push(used, key);
    } else if (req) {
      sp_PolyArray_push(missing, key);
    } else {
      /* the default can make a Proc, which moves the slots: look up after */
      sp_RbVal v = fx_eval(f, ((const pm_optional_keyword_parameter_node_t *)p)->value);
      *fx_local(f, p, name, 0) = v;
    }
  }
  if (missing->len)
    sp_raise_cls("ArgumentError", sp_sprintf("missing keyword%s: %s", missing->len > 1 ? "s" : "",
                                             fx_key_list(missing)));
  sp_PolyPolyHash *rest = sp_PolyPolyHash_new();
  SP_GC_ROOT(rest);
  for (sp_int i = 0; h && i < h->len; i++) {
    sp_RbVal k = h->keys[h->order[i]];
    if (!sp_PolyArray_include(used, k)) sp_PolyPolyHash_set(rest, k, h->vals[h->order[i]]);
  }
  const pm_node_t *kr = ps->keyword_rest;
  if (kr && PM_NODE_TYPE_P(kr, PM_KEYWORD_REST_PARAMETER_NODE)) {
    pm_constant_id_t rn = ((const pm_keyword_rest_parameter_node_t *)kr)->name;
    fx_frame_wb(f);   /* a default above can have moved the slots */
    *(rn ? fx_local(f, kr, rn, 0) : &f->anon[1]) = sp_box_obj(rest, SP_BUILTIN_POLY_POLY_HASH);
    return;
  }
  if (kr && PM_NODE_TYPE_P(kr, PM_FORWARDING_PARAMETER_NODE)) {
    fx_frame_wb(f);
    f->anon[1] = sp_box_obj(rest, SP_BUILTIN_POLY_POLY_HASH);
    return;
  }
  if (rest->len) {
    sp_PolyArray *unknown = sp_PolyArray_new();
    SP_GC_ROOT(unknown);
    for (sp_int i = 0; i < rest->len; i++) sp_PolyArray_push(unknown, rest->keys[rest->order[i]]);
    sp_raise_cls("ArgumentError", sp_sprintf("unknown keyword%s: %s", unknown->len > 1 ? "s" : "",
                                             fx_key_list(unknown)));
  }
}

/* Binds the arguments to the parameters: the required ones, the optional
   ones (a default is evaluated in the callee frame), *rest, the required
   ones after it and &block. With loose set (a block that is not a lambda),
   a missing argument is nil and an extra one is dropped. */
static void fx_bind_list(FxFrame *f, const pm_parameters_node_t *ps, int argc, sp_RbVal *argv, int loose) {
  const void *kwp = L.kwhash;
  L.kwhash = NULL;
  if (!ps) {
    if (argc != 0 && !loose) fx_arity_error(argc, 0, 0, 0);
    return;
  }
  /* keyword parameters (and `...`) take the keyword Hash of the call, the
     last argument; a Hash literal in that place stays positional */
  const pm_node_t *kr = ps->keyword_rest;
  int fwd = kr && PM_NODE_TYPE_P(kr, PM_FORWARDING_PARAMETER_NODE);
  int haskw = ps->keywords.size || fwd || (kr && PM_NODE_TYPE_P(kr, PM_KEYWORD_REST_PARAMETER_NODE));
  sp_RbVal kw = sp_box_nil();
  SP_GC_ROOT_RBVAL(kw);
  if (haskw && argc > 0 && argv[argc - 1].tag == SP_TAG_OBJ && argv[argc - 1].v.p == kwp) kw = argv[--argc];
  int npre = (int)ps->requireds.size, npost = (int)ps->posts.size, nopt = (int)ps->optionals.size;
  int rest = ps->rest != NULL || fwd;
  if (!loose && (argc < npre + npost || (!rest && argc > npre + npost + nopt)))
    fx_arity_error_kw(argc, npre + npost, nopt, rest, fx_required_keywords(ps));
  /* a block drops the extra arguments from the end */
  if (loose && !rest && argc > npre + nopt + npost) argc = npre + nopt + npost;
  int ai = 0;
  for (int i = 0; i < npre; i++, ai++) fx_bind_target(f, ps->requireds.nodes[i], ai < argc ? argv[ai] : sp_box_nil());
  int nfill = argc - npre - npost;
  for (int i = 0; i < nopt; i++) {
    const pm_optional_parameter_node_t *p = (const pm_optional_parameter_node_t *)ps->optionals.nodes[i];
    /* the default can make a Proc, which moves the slots: look up after */
    sp_RbVal v = i < nfill ? argv[ai++] : fx_eval(f, p->value);
    *fx_local(f, (const pm_node_t *)p, p->name, 0) = v;
  }
  if (fwd || (ps->rest && PM_NODE_TYPE_P(ps->rest, PM_REST_PARAMETER_NODE))) {
    sp_PolyArray *a = sp_PolyArray_new();
    SP_GC_ROOT(a);
    while (ai < argc - npost) sp_PolyArray_push(a, argv[ai++]);
    pm_constant_id_t rn = ps->rest ? ((const pm_rest_parameter_node_t *)ps->rest)->name : 0;
    fx_frame_wb(f);   /* a default above can have moved the slots */
    *(rn ? fx_local(f, ps->rest, rn, 0) : &f->anon[0]) = sp_box_poly_array(a);
  } else if (ps->rest && !PM_NODE_TYPE_P(ps->rest, PM_IMPLICIT_REST_NODE)) {
    fx_unsupported(ps->rest, "rest parameter");
  }
  if (ai < argc - npost) ai = argc - npost;
  for (int i = 0; i < npost; i++, ai++) fx_bind_target(f, ps->posts.nodes[i], ai < argc ? argv[ai] : sp_box_nil());
  if (haskw) fx_bind_keywords(f, ps, kw);
  if (ps->block) {
    pm_constant_id_t bn = ((const pm_block_parameter_node_t *)ps->block)->name;
    if (bn) *fx_local(f, (const pm_node_t *)ps->block, bn, 0) = f->block ? fx_block_proc(f->block) : sp_box_nil();
  }
}

static void fx_bind_params(FxFrame *f, const pm_parameters_node_t *ps, int argc, sp_RbVal *argv) {
  fx_bind_list(f, ps, argc, argv, 0);
}

/* The slot of the numbered parameter _i (1..9) of a block frame. */
static sp_RbVal *fx_numbered(FxFrame *f, const pm_node_t *n, int i) {
  char name[3] = { '_', (char)('0' + i), 0 };
  pm_constant_id_t id = pm_constant_pool_find(&L.pool, (const uint8_t *)name, 2);
  if (!id) fx_unsupported(n, "numbered parameter");
  return fx_local(f, n, id, 0);
}

/* Binds the arguments of a block call. A block spreads one Array argument
   over two or more parameters. A lambda does so only when a method yields
   to it (`h.map(&l)`), not when Proc#call calls it (strict). */
static void fx_bind_block(FxFrame *f, const pm_node_t *params, int argc, sp_RbVal *argv, int lambda,
                            int strict) {
  SP_GC_SAVE();
  if (!params) {
    if (lambda && argc != 0) fx_arity_error(argc, 0, 0, 0);
    return;
  }
  if (PM_NODE_TYPE_P(params, PM_IT_PARAMETERS_NODE)) {
    if (lambda && argc != 1) fx_arity_error(argc, 1, 0, 0);
    *f->it = argc ? argv[0] : sp_box_nil();
    return;
  }
  int many;
  const pm_parameters_node_t *ps = NULL;
  if (PM_NODE_TYPE_P(params, PM_NUMBERED_PARAMETERS_NODE)) {
    many = ((const pm_numbered_parameters_node_t *)params)->maximum;
  } else {
    ps = ((const pm_block_parameters_node_t *)params)->parameters;
    if (!ps) {
      if (lambda && argc != 0) fx_arity_error(argc, 0, 0, 0);
      return;
    }
    int n = (int)(ps->requireds.size + ps->optionals.size + ps->posts.size);
    many = n >= 2 || (n >= 1 && ps->rest) ? 2 : n;
  }
  int spread = !(lambda && strict) && argc == 1 && many >= 2 && fx_is_array_value(argv[0]);
  int len = spread ? (int)sp_poly_length(argv[0]) : 0;
  sp_RbVal elts[len ? len : 1];
  if (spread) {
    sp_RbVal a = argv[0];
    SP_GC_ROOT_RBVAL(a);
    for (int i = 0; i < len; i++) {
      elts[i] = sp_poly_arr_get(a, i);
      _sp_gc_root_push((void **)((uintptr_t)&elts[i] | (uintptr_t)1));
    }
    argc = len;
    argv = elts;
  }
  if (ps) {
    fx_bind_list(f, ps, argc, argv, !lambda);
    return;
  }
  if (lambda && argc != many) fx_arity_error(argc, many, 0, 0);
  for (int i = 0; i < many; i++) *fx_numbered(f, params, i + 1) = i < argc ? argv[i] : sp_box_nil();
}

static sp_RbVal fx_yield_in(const FxBlock *b, int argc, sp_RbVal *argv, int strict, const sp_RbVal *self,
                              FxClass *definee, int sdefs, const FxMethod *m, const FxBlock *mblk);

static sp_RbVal fx_invoke(const FxMethod *m, sp_RbVal self, int argc, sp_RbVal *argv) {
  return fx_invoke_b(m, self, argc, argv, NULL);
}

static sp_RbVal fx_invoke_b(const FxMethod *m, sp_RbVal self, int argc, sp_RbVal *argv, const FxBlock *blk) {
  /* an accessor keeps the slot of its ivar in the method, which is heap
     memory that the callers see as const */
  if (m->kind == FX_M_READER) {
    if (argc != 0) fx_arity_error(argc, 0, 0, 0);
    return fx_ivar_get_c(self, m->ivar, (FxIvarCache *)&m->ic);
  }
  if (m->kind == FX_M_WRITER) {
    if (argc != 1) fx_arity_error(argc, 1, 0, 0);
    fx_ivar_set_c(self, m->ivar, argv[0], (FxIvarCache *)&m->ic);
    return argv[0];
  }
  if (m->kind == FX_M_FFI) return fx_ffi_invoke(m->ffi, self, argc, argv);
  if (m->kind == FX_M_ENTRY) return fx_entry_invoke(m->entry, argc, argv);
  if (m->kind == FX_M_BUILTIN) {
    /* as super does: an object of the program takes the defaults of
       Object, which do not call the hooks */
    sp_RbVal out;
    if (self.tag == SP_TAG_CLASS && fx_class_core(NULL, self, sp_sym_to_s(m->name), argc, argv, blk, &out))
      return out;
    if (fx_is_fx_object(self) ? fx_record_core(self, sp_sym_to_s(m->name), argc, argv, blk, &out) ||
                                    fx_object_core(self, sp_sym_to_s(m->name), argc, argv, &out)
                              : fx_builtin_call(self, sp_sym_to_s(m->name), argc, argv, blk, &out))
      return out;
    fx_no_method(self, sp_sym_to_s(m->name), FX_MISS_METHOD);
  }
  SP_GC_SAVE();
  sp_RbVal me = self;
  SP_GC_ROOT_RBVAL(me);
  if (m->kind == FX_M_PROC) {
    /* the body is a lambda: a strict arity, and a return ends the method */
    FxBlock b = ((FxProc *)m->proc)->b;
    b.lambda = 1;
    return fx_yield_in(&b, argc, argv, 1, &me, NULL, 0, m, blk);
  }
  const pm_def_node_t *def = m->def;
  /* $~ is local to the method: the caller's registers come back on exit */
  sp_re_frame rf;
  int rx = fx_re_frame_enter(m, &rf);
  FX_FRAME(f, &def->locals, me, m->lexical ? m->lexical : m->owner);
  f.method = m;
  f.block = blk;
  fx_bind_params(&f, def->parameters, argc, argv);
  sp_RbVal v = def->body ? fx_eval(&f, def->body) : sp_box_nil();
  if (L.ctl == FX_CTL_RETURN && L.ctl_target == &f) { v = L.ctl_val; L.ctl = FX_CTL_NONE; }
  if (rx) sp_re_frame_pop(&rf);
  return v;
}

/* ---- blocks ---- */

static void fx_block_parts(const pm_node_t *n, const pm_constant_id_list_t **locals, const pm_node_t **params,
                             const pm_node_t **body) {
  if (PM_NODE_TYPE_P(n, PM_LAMBDA_NODE)) {
    const pm_lambda_node_t *l = (const pm_lambda_node_t *)n;
    *locals = &l->locals, *params = l->parameters, *body = l->body;
  } else {
    const pm_block_node_t *b = (const pm_block_node_t *)n;
    *locals = &b->locals, *params = b->parameters, *body = b->body;
  }
}

/* Calls a block with the arguments. A next ends the block call; a break
   ends the call that passed the block; a return ends the method that
   holds the block, or the lambda. strict is set for Proc#call. self
   replaces that of the home frame when it is set; the caller roots
   *self. definee, when it is set, replaces the def target of the home
   frame, with sdefs for its singleton_defs (the body of Class.new,
   class_eval and instance_eval); constants stay those of the home. m is
   set for the body of a method that define_method made: the block is the
   body, super starts from m, and mblk is the block of the call. */
static sp_RbVal fx_yield_in(const FxBlock *b, int argc, sp_RbVal *argv, int strict, const sp_RbVal *self,
                              FxClass *definee, int sdefs, const FxMethod *m, const FxBlock *mblk) {
  if (b->cfn) return b->cfn(b->ctx, argc, argv);
  if (!b->node) {
    if (self) fx_gap("a Symbol block as a class body");
    if (argc < 1) sp_raise_cls("ArgumentError", "no receiver given");
    return fx_send(argv[0], sp_sym_to_s(b->sym), argc - 1, argv + 1);
  }
  /* a home on the heap outlives its scope; one on the C stack does not */
  FxFrame *h = b->home;
  int live = fx_frame_live(b);
  if (!live && !h->heap) fx_gap("a call of a block whose scope has returned");
  SP_GC_SAVE();
  const pm_constant_id_list_t *locals;
  const pm_node_t *params, *body;
  fx_block_parts(b->node, &locals, &params, &body);
  if (SP_UNLIKELY(SP_SAFEPOINT_POLL())) fx_safepoint();
  FX_FRAME(bf, locals, self ? *self : h->self, h->cref);
  bf.definee = definee ? definee : h->definee;
  bf.singleton_defs = definee ? sdefs : h->singleton_defs;
  bf.method = m ? m : h->method;
  bf.vis_scope = definee || m;
  bf.parent = h;
  bf.block = m ? mblk : h->block;
  bf.lambda = b->lambda;
  fx_bind_block(&bf, params, argc, argv, b->lambda, strict);
  sp_RbVal v;
  for (;;) {
    v = body ? fx_eval(&bf, body) : sp_box_nil();
    if (L.ctl != FX_CTL_REDO) break;
    L.ctl = FX_CTL_NONE;   /* the body again, with the same arguments */
  }
  switch (L.ctl) {
  case FX_CTL_NEXT:
    L.ctl = FX_CTL_NONE;
    return L.ctl_val;
  case FX_CTL_BREAK:
    if (b->lambda) { L.ctl = FX_CTL_NONE; return L.ctl_val; }
    /* the call that a break leaves is in the home scope */
    if (!b->site || !live) {
      L.ctl = FX_CTL_NONE;
      sp_raise_cls("LocalJumpError", "break from proc-closure");
    }
    L.ctl = FX_CTL_BREAK_BLOCK;
    L.ctl_target = b->site;
    return sp_box_nil();
  case FX_CTL_RETURN:
    if (L.ctl_target == &bf) { L.ctl = FX_CTL_NONE; return L.ctl_val; }
    return v;
  default:
    return v;
  }
}

static sp_RbVal fx_yield_x(const FxBlock *b, int argc, sp_RbVal *argv, int strict) {
  return fx_yield_in(b, argc, argv, strict, NULL, NULL, 0, NULL, NULL);
}

static sp_RbVal fx_yield(const FxBlock *b, int argc, sp_RbVal *argv) {
  return fx_yield_x(b, argc, argv, 0);
}

/* Calls a block with self and the definee replaced: a def in it defines a
   method of k. */
static sp_RbVal fx_yield_under(const FxBlock *b, sp_RbVal self, FxClass *k, int argc, sp_RbVal *argv) {
  SP_GC_ROOT_RBVAL(self);
  return fx_yield_in(b, argc, argv, 0, &self, k, 0, NULL, NULL);
}

/* instance_eval and instance_exec with a block: self is r. A def in the
   block defines a singleton method of r: a class method for a class, and
   a gap for any other object. class_eval, module_eval and their _exec
   kin: self is the class, and a def defines an instance method. The
   _exec forms pass their arguments, the others pass r. Answers 0 for
   any other call. */
static int fx_eval_block(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                           sp_RbVal *out) {
  int inst = !strcmp(name, "instance_eval") || !strcmp(name, "instance_exec");
  int mod = !strcmp(name, "class_eval") || !strcmp(name, "module_eval") || !strcmp(name, "class_exec") ||
            !strcmp(name, "module_exec");
  FxClass *k = fx_class_value(r);
  if (!(inst || (mod && k))) return 0;
  int exec = strstr(name, "_exec") != NULL;
  if (!exec && argc > 0) fx_gap(sp_sprintf("%s of a String", name));
  if (!blk && exec) sp_raise_cls("LocalJumpError", "no block given");
  if (!blk) sp_raise_cls("ArgumentError", "wrong number of arguments (given 0, expected 1..3)");
  SP_GC_SAVE();
  sp_RbVal self = r;
  SP_GC_ROOT_RBVAL(self);
  FxClass *definee = k ? k : fx_class_of(r);
  int sdefs = mod ? 0 : k ? 4 : 3;
  *out = fx_yield_in(blk, exec ? argc : 1, exec ? argv : &self, 0, &self, definee, sdefs, NULL, NULL);
  return 1;
}

static int fx_is_proc(sp_RbVal v) {
  return v.tag == SP_TAG_OBJ && v.cls_id == SP_BUILTIN_PROC && v.v.p;
}

static void fx_proc_scan(void *p) {
  const FxBlock *b = &((FxProc *)p)->b;
  if (b->home && b->home->heap) sp_gc_mark(b->home);
  /* the Proc of a Method keeps the Method */
  if (b->cfn == fx_method_value_step) sp_gc_mark(b->ctx);
}

/* A Proc value for a block. A block that a Proc carries answers that Proc.
   The Proc can outlive the scope of the block, so its home frame moves to
   the heap. */
static sp_RbVal fx_block_proc(const FxBlock *b) {
  if (b->proc) return sp_box_obj(b->proc, SP_BUILTIN_PROC);
  /* the env of a home on the C stack stays a root of that frame here */
  FxFrame *home = b->node && fx_frame_live(b) ? fx_frame_heap(b->home) : b->home;
  FxProc *p = (FxProc *)sp_gc_alloc(sizeof *p, NULL, fx_proc_scan);
  p->p.lambda_p = b->lambda ? TRUE : FALSE;
  p->b = *b;
  p->b.home = home;
  p->b.proc = p;
  return sp_box_obj(p, SP_BUILTIN_PROC);
}

/* The block that a call passes: a block literal (into *lit), `&expr` or
   `&`. *holder roots the Proc of `&expr`. Answers NULL for no block, and
   also when a jump is pending. */
static const FxBlock *fx_make_block(FxFrame *f, const pm_node_t *bn, FxBlock *lit, sp_RbVal *holder) {
  if (!bn) return NULL;
  if (PM_NODE_TYPE_P(bn, PM_BLOCK_NODE)) {
    *lit = (FxBlock){ .node = bn, .home = f, .serial = f->serial, .fs = f->fs, .depth = f->depth, .site = lit };
    return lit;
  }
  const pm_node_t *e = ((const pm_block_argument_node_t *)bn)->expression;
  if (!e) return f->block;   /* def m(&) ... n(&) */
  sp_RbVal v = fx_eval(f, e);
  if (L.ctl != FX_CTL_NONE) return NULL;
  *holder = v;
  if (v.tag == SP_TAG_NIL) return NULL;
  if (v.tag == SP_TAG_SYM) {
    *lit = (FxBlock){ .sym = (sp_sym)v.v.i, .lambda = 1 };
    return lit;
  }
  if (fx_is_proc(v)) return &((FxProc *)v.v.p)->b;
  if (fx_is_method_value(v)) return fx_method_value_block(v);
  fx_unsupported(bn, "block argument that is not a Proc or a Symbol");
}

/* ---- calls ---- */

/* A keyword Hash of only `**h`, which passes nothing when it is empty. */
static int fx_kwsplat_only(const pm_node_t *a) {
  if (!PM_NODE_TYPE_P(a, PM_KEYWORD_HASH_NODE)) return 0;
  const pm_node_list_t *e = &((const pm_keyword_hash_node_t *)a)->elements;
  for (size_t i = 0; i < e->size; i++)
    if (!PM_NODE_TYPE_P(e->nodes[i], PM_ASSOC_SPLAT_NODE)) return 0;
  return 1;
}

/* Whether the count of the arguments is known at run time only: a splat,
   `...`, or a keyword Hash of only `**h`. */
static int fx_args_dynamic(const pm_arguments_node_t *args) {
  if (!args) return 0;
  for (size_t i = 0; i < args->arguments.size; i++) {
    const pm_node_t *a = args->arguments.nodes[i];
    if (PM_NODE_TYPE_P(a, PM_SPLAT_NODE) || PM_NODE_TYPE_P(a, PM_FORWARDING_ARGUMENTS_NODE) || fx_kwsplat_only(a))
      return 1;
  }
  return 0;
}

static int fx_has_forwarding(const pm_arguments_node_t *args) {
  for (size_t i = 0; args && i < args->arguments.size; i++)
    if (PM_NODE_TYPE_P(args->arguments.nodes[i], PM_FORWARDING_ARGUMENTS_NODE)) return 1;
  return 0;
}

/* The argument count of a call site that needs a fixed count. */
static int fx_argc(const pm_arguments_node_t *args) {
  if (!args) return 0;
  if (fx_args_dynamic(args)) fx_unsupported((const pm_node_t *)args, "argument kind");
  return (int)args->arguments.size;
}

/* The frame of the method (or the program) that holds frame f. */
static FxFrame *fx_method_frame(FxFrame *f) {
  while (f->parent) f = f->parent;
  return f;
}

/* Pushes the elements of a splat: an Array spreads, nil passes nothing
   and any other value passes itself. */
static void fx_spread(sp_PolyArray *out, sp_RbVal v) {
  if (v.tag == SP_TAG_NIL) return;
  SP_GC_ROOT_RBVAL(v);
  /* a value that is not an Array spreads its to_a, as CRuby's splat does;
     a value with no to_a is one element */
  if (!fx_is_array_value(v)) {
    /* the builtin kinds that have to_a, which respond_to? may not see */
    int seq = v.tag == SP_TAG_OBJ && v.v.p &&
              (v.cls_id == SP_BUILTIN_RANGE || v.cls_id == SP_BUILTIN_FLOAT_RANGE || v.cls_id == SP_BUILTIN_STR_RANGE ||
               v.cls_id == SP_BUILTIN_ENUMERATOR || sp_poly_is_hash_kind(v.cls_id));
    if (!seq && !sp_poly_truthy(fx_respond_to(v, sp_box_sym(sp_sym_intern("to_a")), sp_box_bool(1)))) {
      sp_PolyArray_push(out, v);
      return;
    }
    sp_RbVal a = fx_send(v, "to_a", 0, NULL);
    if (!fx_is_array_value(a)) {
      const char *c = sp_poly_class_name(v);
      sp_raise_cls("TypeError", sp_sprintf("can't convert %s to Array (%s#to_a gives %s)", c, c,
                                           sp_poly_class_name(a)));
    }
    v = a;
  }
  for (sp_int i = 0; i < sp_poly_length(v); i++) sp_PolyArray_push(out, sp_poly_arr_get(v, i));
}

/* Evaluates arguments whose count is known at run time only into *out.
   Sets *kw to the keyword Hash when it is the last argument. Answers 0
   when a jump is pending. */
static int fx_args_spread(FxFrame *f, const pm_arguments_node_t *args, sp_PolyArray *out, const void **kw) {
  sp_RbVal v = sp_box_nil();
  SP_GC_ROOT_RBVAL(v);
  for (size_t i = 0; i < args->arguments.size; i++) {
    const pm_node_t *a = args->arguments.nodes[i];
    *kw = NULL;
    if (PM_NODE_TYPE_P(a, PM_SPLAT_NODE)) {
      const pm_node_t *e = ((const pm_splat_node_t *)a)->expression;
      v = e ? fx_eval(f, e) : fx_method_frame(f)->anon[0];
      if (L.ctl != FX_CTL_NONE) return 0;
      fx_spread(out, v);
    } else if (PM_NODE_TYPE_P(a, PM_FORWARDING_ARGUMENTS_NODE)) {
      FxFrame *mf = fx_method_frame(f);
      fx_spread(out, mf->anon[0]);
      v = mf->anon[1];
      if (v.tag != SP_TAG_NIL && sp_poly_length(v) > 0) {
        sp_PolyArray_push(out, v);
        *kw = v.v.p;
      }
    } else {
      v = fx_eval(f, a);
      if (L.ctl != FX_CTL_NONE) return 0;
      if (fx_kwsplat_only(a) && sp_poly_length(v) == 0) continue;
      sp_PolyArray_push(out, v);
      if (PM_NODE_TYPE_P(a, PM_KEYWORD_HASH_NODE)) *kw = v.v.p;
    }
  }
  return 1;
}

/* Declares argc and argv for a call site and evaluates the arguments: into
   a C array when the count is fixed, else into a rooted Array. kw is the
   keyword Hash of the call, or NULL. dyn is fx_args_dynamic(args), which a
   call node keeps in its record. */
#define FX_ARGS(f, args, argc, argv, kw, ok) FX_ARGS_DYN(f, args, fx_args_dynamic(args), argc, argv, kw, ok)
#define FX_ARGS_DYN(f, args, dyn, argc, argv, kw, ok)                                    \
  int argc = (dyn) ? -1 : (args) ? (int)(args)->arguments.size : 0;                        \
  sp_RbVal argc##_vla[argc > 0 ? argc : 1];                                                \
  sp_RbVal *argv = argc##_vla;                                                             \
  sp_PolyArray *argc##_spread = NULL;                                                      \
  SP_GC_ROOT(argc##_spread);                                                               \
  const void *kw = NULL;                                                                   \
  int ok;                                                                                  \
  if (argc >= 0) {                                                                         \
    ok = fx_args(f, args, argc, argv);                                                   \
    if (ok && argc > 0 && PM_NODE_TYPE_P((args)->arguments.nodes[argc - 1], PM_KEYWORD_HASH_NODE)) \
      kw = argv[argc - 1].v.p;                                                             \
  } else {                                                                                 \
    argc##_spread = sp_PolyArray_new();                                                    \
    ok = fx_args_spread(f, args, argc##_spread, &kw);                                    \
    argc = (int)argc##_spread->len;                                                        \
    argv = argc##_spread->data;                                                            \
  }

/* Roots argv[0..argc) and evaluates the arguments into it. Answers 0 when
   a jump is pending. The roots stay until the caller's SP_GC_SAVE ends. */
static int fx_args(FxFrame *f, const pm_arguments_node_t *args, int argc, sp_RbVal *argv) {
  for (int i = 0; i < argc; i++) {
    argv[i] = sp_box_nil();
    _sp_gc_root_push((void **)((uintptr_t)&argv[i] | (uintptr_t)1));
  }
  for (int i = 0; i < argc; i++) {
    argv[i] = fx_eval(f, args->arguments.nodes[i]);
    if (L.ctl != FX_CTL_NONE) return 0;
  }
  return 1;
}

/* A call that passed the block lit ends here when the block breaks. */
static sp_RbVal fx_catch_break(const FxBlock *lit, sp_RbVal v) {
  if (L.ctl == FX_CTL_BREAK_BLOCK && L.ctl_target == lit) {
    L.ctl = FX_CTL_NONE;
    return L.ctl_val;
  }
  return v;
}

static int fx_vis_denied(const FxMethod *m, int check, const sp_RbVal *caller);   /* builtin.c */

/* A call on an object of the program, with the method that the cache cc
   of the call node keeps for the class of the object. The object is recv,
   or self for a call with no receiver. For such an object, fx_call_self
   and fx_send_v also look up the method first, so the cache changes no
   order. Answers 0 for any other value, for no method and for a method
   that the visibility denies: the general path then takes the call. */
static int fx_call_cached(FxFrame *f, const pm_call_node_t *c, FxCallCache *cc, sp_sym sym, sp_RbVal recv,
                          int argc, sp_RbVal *argv, const FxBlock *blk, sp_RbVal *out) {
  sp_RbVal r = c->receiver ? recv : f->self;
  if (!fx_is_fx_object(r)) return 0;
  if (cc->cls_id != r.cls_id || cc->gen != fx_methods_gen) {
    cc->m = fx_find_method(fx_class_by_id(r.cls_id), sym);
    cc->cls_id = r.cls_id;
    cc->gen = fx_methods_gen;
  }
  const FxMethod *m = cc->m;
  if (!m) return 0;
  /* a receiver other than `self.` sees the public methods, and the
     protected ones of a class of self */
  if (c->receiver && fx_vis_denied(m, !PM_NODE_TYPE_P(c->receiver, PM_SELF_NODE), &f->self)) return 0;
  *out = fx_invoke_b(m, r, argc, argv, blk);
  return 1;
}

static sp_RbVal fx_call(FxFrame *f, const pm_call_node_t *c) {
  SP_GC_SAVE();
  const sp_sym sym = fx_sym(c->name);
  const char *name = sp_sym_to_s(sym);
  FxCallCache *cc = &fx_side((const pm_node_t *)c)->call;
  if (!cc->ready) {
    cc->dyn = (uint8_t)fx_args_dynamic(c->arguments);
    cc->fwd = (uint8_t)fx_has_forwarding(c->arguments);
    cc->ready = 1;
  }
  sp_RbVal recv = sp_box_nil();
  SP_GC_ROOT_RBVAL(recv);
  if (c->receiver) {
    recv = fx_eval(f, c->receiver);
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
    if (PM_NODE_FLAG_P((const pm_node_t *)c, PM_CALL_NODE_FLAGS_SAFE_NAVIGATION) && recv.tag == SP_TAG_NIL)
      return recv;
  }
  FX_ARGS_DYN(f, c->arguments, cc->dyn, argc, argv, kw, ok);
  if (!ok) return sp_box_nil();
  FxBlock lit;
  sp_RbVal holder = sp_box_nil();
  SP_GC_ROOT_RBVAL(holder);
  const FxBlock *blk = fx_make_block(f, c->block, &lit, &holder);
  if (L.ctl != FX_CTL_NONE) return sp_box_nil();
  if (!c->block && cc->fwd) blk = fx_method_frame(f)->block;
  /* the callee's binder takes the keyword Hash; a builtin leaves it, so
     it is cleared after the call, before its address can be reused */
  L.kwhash = kw;
  sp_RbVal v;
  /* require_relative and __dir__ answer from the file of the call, unless
     the program defines its own */
  if (c->receiver && !blk && !kw && fx_fast_call(recv, sym, argc, argv, &v)) {
  } else if (!c->receiver && !blk && fx_load_call((const pm_node_t *)c, f->self, name, argc, argv, &v)) {
  } else if (fx_call_cached(f, c, cc, sym, recv, argc, argv, blk, &v)) {
  } else {
    L.vcall = PM_NODE_FLAG_P((const pm_node_t *)c, PM_CALL_NODE_FLAGS_VARIABLE_CALL);
    /* a receiver other than `self.` sees the public methods, and the
       protected ones of a class of self */
    if (!c->receiver) v = fx_call_self(f, name, argc, argv, blk);
    else v = fx_send_v(recv, name, argc, argv, blk, !PM_NODE_TYPE_P(c->receiver, PM_SELF_NODE), &f->self);
  }
  L.kwhash = NULL;
  v = fx_catch_break(&lit, v);
  if (!c->receiver) return v;
  /* `obj.x = v` answers v, whatever x= returns */
  if (PM_NODE_FLAG_P((const pm_node_t *)c, PM_CALL_NODE_FLAGS_ATTRIBUTE_WRITE) && argc > 0) return argv[argc - 1];
  return v;
}

static sp_RbVal fx_loop(const FxBlock *blk);   /* the exceptions section */
static sp_RbVal fx_catch(int argc, sp_RbVal *argv, const FxBlock *blk);
static sp_RbVal fx_eval_string(FxFrame *f, int argc, sp_RbVal *argv);   /* load.c */

/* The hook of at_exit: the runtime keeps a Proc of its own in its table of
   hooks (sp_at_exit_push), whose fn yields to the block of the Proc of
   Fluxpinel (cap). A jump out of the hook ends there. */
static sp_int fx_at_exit_fn(void *cap, sp_int argc, sp_int *args) {
  (void)argc;
  (void)args;
  fx_yield(&((FxProc *)cap)->b, 0, NULL);
  L.ctl = FX_CTL_NONE;
  return 0;
}

/* The Kernel methods that take the block of the call. Answers 1 and sets
   *out when it handles the call. */
static int fx_kernel_block(FxFrame *f, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                             sp_RbVal *out) {
  if (argc == 0 && (!strcmp(name, "block_given?") || !strcmp(name, "iterator?"))) {
    *out = sp_box_bool(f->block != NULL);
    return 1;
  }
  if (!blk) return 0;
  if (argc == 0 && (!strcmp(name, "proc") || !strcmp(name, "lambda"))) {
    /* a break in the Proc of a literal has no call to end */
    if (blk->node && !blk->proc) {
      FxBlock l = *blk;
      l.lambda = name[0] == 'l';
      l.site = NULL;
      *out = fx_block_proc(&l);
    } else {
      *out = fx_block_proc(blk);
    }
    return 1;
  }
  if (argc == 0 && !strcmp(name, "loop")) {
    *out = fx_loop(blk);
    return 1;
  }
  if (argc <= 1 && !strcmp(name, "catch")) {
    *out = fx_catch(argc, argv, blk);
    return 1;
  }
  /* at_exit: the Proc of the block, which the runtime runs as a hook when
     the program ends (sp_at_exit_run) */
  if (argc == 0 && !strcmp(name, "at_exit")) {
    FxBlock l = *blk;
    l.site = NULL;
    sp_RbVal pv = fx_block_proc(&l);
    SP_GC_ROOT_RBVAL(pv);
    sp_Proc *h = sp_proc_new((void *)fx_at_exit_fn, pv.v.p, sp_bm_cap_scan);
    sp_at_exit_push(h);
    *out = pv;
    return 1;
  }
  return 0;
}

/* A call with no receiver: a method of self, then Kernel, then a builtin
   method of self. In a class body or a class method, self is the class. */
static sp_RbVal fx_call_self(FxFrame *f, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk) {
  /* fx_call marks a call with no arguments and no parentheses */
  int why = L.vcall ? FX_MISS_VCALL : FX_MISS_METHOD;
  L.vcall = 0;
  sp_sym s = fx_name_sym(name);
  sp_RbVal self = f->self, out;
  if (self.tag == SP_TAG_CLASS) {
    FxClass *k = fx_class_value(self);
    FxMethod *m = k ? fx_find_smethod(k, s) : NULL;
    if (m) return fx_invoke_b(m, self, argc, argv, blk);
    if (fx_class_core(f, self, name, argc, argv, blk, &out)) return out;
  }
  FxMethod *m = fx_find_method(fx_class_of(self), s);
  if (m) return fx_invoke_b(m, self, argc, argv, blk);
  /* define_method, private, public and autoload of main act on Object, as
     a top-level def does; define_method there is public */
  if (fx_is_fx_object(self) && self.v.p == L.main.v.p) {
    if (!strcmp(name, "define_method")) return fx_define_proc_method(L.object, 0, FX_VIS_PUBLIC, argc, argv, blk);
    if ((!strcmp(name, "private") || !strcmp(name, "public") || !strcmp(name, "autoload") ||
         !strcmp(name, "autoload?")) &&
        fx_class_core(f, fx_class_box(L.object), name, argc, argv, blk, &out))
      return out;
  }
  if (fx_kernel_block(f, name, argc, argv, blk, &out)) return out;
  /* Kernel#eval needs the frame of the call for its locals */
  if (argc >= 1 && !blk && !strcmp(name, "eval")) return fx_eval_string(f, argc, argv);
  if (blk) return fx_send_b(self, name, argc, argv, blk);
  if (fx_exc_kernel(name, argc, argv, &out)) return out;
  if (fx_fiber_kernel(name, argc, argv, &out)) return out;
  if (fx_kernel(name, argc, argv, &out)) return out;
  if (fx_object_core(self, name, argc, argv, &out)) return out;
  if (!fx_is_fx_object(self) && self.tag != SP_TAG_CLASS) return fx_send(self, name, argc, argv);
  if (fx_method_missing(self, name, argc, argv, NULL, why, &out)) return out;
  fx_trace_missing("Kernel#", name, argc);
  /* a bare name that could be a local variable is a NameError */
  fx_no_method(self, name, why);
}

/* The arguments that a bare super passes, into out: the current values of
   the parameters, in order, with *rest spread, then a keyword Hash of the
   keywords and **rest. Answers that Hash, or NULL for none. */
static const void *fx_zsuper_args(FxFrame *f, const pm_node_t *n, sp_PolyArray *out) {
  f = fx_method_frame(f);   /* a super in a block reads the method's parameters */
  const pm_parameters_node_t *ps = f->method->def ? f->method->def->parameters : NULL;
  if (!ps) return NULL;
  const pm_node_list_t *lists[3] = { &ps->requireds, &ps->optionals, NULL };
  for (int l = 0; lists[l]; l++)
    for (size_t i = 0; i < lists[l]->size; i++) {
      const pm_node_t *p = lists[l]->nodes[i];
      pm_constant_id_t name = PM_NODE_TYPE_P(p, PM_OPTIONAL_PARAMETER_NODE)
                                ? ((const pm_optional_parameter_node_t *)p)->name
                                : ((const pm_required_parameter_node_t *)p)->name;
      if (!PM_NODE_TYPE_P(p, PM_OPTIONAL_PARAMETER_NODE) && !PM_NODE_TYPE_P(p, PM_REQUIRED_PARAMETER_NODE))
        fx_unsupported(n, "bare super with a parameter pattern");
      sp_PolyArray_push(out, *fx_local(f, n, name, 0));
    }
  const pm_node_t *kr = ps->keyword_rest;
  int fwd = kr && PM_NODE_TYPE_P(kr, PM_FORWARDING_PARAMETER_NODE);
  if (fwd || (ps->rest && PM_NODE_TYPE_P(ps->rest, PM_REST_PARAMETER_NODE))) {
    pm_constant_id_t rn = ps->rest ? ((const pm_rest_parameter_node_t *)ps->rest)->name : 0;
    fx_spread(out, rn ? *fx_local(f, n, rn, 0) : f->anon[0]);
  }
  for (size_t i = 0; i < ps->posts.size; i++)
    sp_PolyArray_push(out, *fx_local(f, n, ((const pm_required_parameter_node_t *)ps->posts.nodes[i])->name, 0));
  /* the keywords, with their current values, then **rest */
  if (!ps->keywords.size && !kr) return NULL;
  sp_PolyPolyHash *h = sp_PolyPolyHash_new();
  SP_GC_ROOT(h);
  for (size_t i = 0; i < ps->keywords.size; i++) {
    const pm_node_t *p = ps->keywords.nodes[i];
    pm_constant_id_t name = PM_NODE_TYPE_P(p, PM_REQUIRED_KEYWORD_PARAMETER_NODE)
                              ? ((const pm_required_keyword_parameter_node_t *)p)->name
                              : ((const pm_optional_keyword_parameter_node_t *)p)->name;
    sp_PolyPolyHash_set(h, sp_box_sym(fx_sym(name)), *fx_local(f, n, name, 0));
  }
  sp_RbVal kwrest = sp_box_nil();
  if (fwd) kwrest = f->anon[1];
  else if (kr && PM_NODE_TYPE_P(kr, PM_KEYWORD_REST_PARAMETER_NODE)) {
    pm_constant_id_t rn = ((const pm_keyword_rest_parameter_node_t *)kr)->name;
    kwrest = rn ? *fx_local(f, n, rn, 0) : f->anon[1];
  }
  sp_PolyPolyHash *r = kwrest.tag == SP_TAG_NIL ? NULL : sp_poly_as_poly_poly_hash(kwrest);
  for (sp_int i = 0; r && i < r->len; i++) sp_PolyPolyHash_set(h, r->keys[r->order[i]], r->vals[r->order[i]]);
  if (h->len == 0) return NULL;
  sp_PolyArray_push(out, sp_box_obj(h, SP_BUILTIN_POLY_POLY_HASH));
  return h;
}

static sp_RbVal fx_super_call(FxFrame *f, const pm_node_t *n, const pm_arguments_node_t *args, int forward,
                                const FxBlock *blk);

/* super, and super with no arguments, which passes the parameters. Both
   pass the block of the method, unless the super has its own. */
static sp_RbVal fx_super(FxFrame *f, const pm_node_t *n, const pm_arguments_node_t *args, int forward,
                           const pm_node_t *bnode) {
  SP_GC_SAVE();
  if (!f->method) fx_unsupported(n, "super outside a method");
  FxBlock lit;
  sp_RbVal holder = sp_box_nil();
  SP_GC_ROOT_RBVAL(holder);
  const FxBlock *blk = bnode ? fx_make_block(f, bnode, &lit, &holder) : f->block;
  if (L.ctl != FX_CTL_NONE) return sp_box_nil();
  sp_RbVal v = fx_super_call(f, n, args, forward, blk);
  L.kwhash = NULL;
  return fx_catch_break(&lit, v);
}

static sp_RbVal fx_super_call(FxFrame *f, const pm_node_t *n, const pm_arguments_node_t *args, int forward,
                                const FxBlock *blk) {
  SP_GC_SAVE();
  FX_ARGS(f, forward ? NULL : args, argc, argv, kw, ok);
  if (!ok) return sp_box_nil();
  if (forward && f->method->kind == FX_M_PROC)
    sp_raise_cls("RuntimeError", "implicit argument passing of super from method defined by define_method() is "
                                 "not supported. Specify all arguments explicitly.");
  if (forward) {
    argc_spread = sp_PolyArray_new();
    kw = fx_zsuper_args(f, n, argc_spread);
    argc = (int)argc_spread->len;
    argv = argc_spread->data;
  }
  L.kwhash = kw;
  const FxMethod *m = f->method;
  FxMethod *s = NULL;
  int found = 0;
  /* a class method, or a method of a module that the class extends: the
     singleton path of the class */
  if (f->self.tag == SP_TAG_CLASS && fx_class_value(f->self))
    s = fx_find_ssuper(fx_class_value(f->self), m, &found);
  if (!found) s = fx_find_super(fx_class_of(f->self), m);
  if (s) return fx_invoke_b(s, f->self, argc, argv, blk);
  const char *name = sp_sym_to_s(m->name);
  sp_RbVal out;
  /* the builtin method_missing: the error of the method that was missing;
     the builtin respond_to_missing?: false */
  if (!strcmp(name, "method_missing") && argc >= 1 && argv[0].tag == SP_TAG_SYM)
    fx_no_method(f->self, sp_sym_to_s((sp_sym)argv[0].v.i), L.mm_why);
  if (!strcmp(name, "respond_to_missing?")) return sp_box_bool(0);
  /* the builtin initialize: an exception takes its message, a Struct or a
     Data its members */
  if (!strcmp(name, "initialize")) {
    if (fx_is_exc(f->self)) fx_exc_init(f->self, argc, argv);
    else if (fx_arysub_of(f->self)) fx_arysub_init(f->self, argc, argv, blk);
    else if (fx_is_fx_object(f->self) && fx_record_of(fx_class_of(f->self))) fx_record_init(f->self, argc, argv);
    return sp_box_nil();
  }
  if (f->self.tag == SP_TAG_CLASS && fx_class_core(NULL, f->self, name, argc, argv, blk, &out)) return out;
  /* an object of the program: the defaults of Object, which do not call
     the hooks (to_s through a hook would call the program's to_s again) */
  if (fx_is_fx_object(f->self)) {
    /* a method of Enumerable, over the each of the object */
    if (fx_enum_core(f->self, name, argc, argv, blk, &out)) return out;
    if (blk) fx_gap(sp_sprintf("super with a block to the builtin %s", name));
    if (fx_object_core(f->self, name, argc, argv, &out)) return out;
  } else if (fx_builtin_call(f->self, name, argc, argv, blk, &out)) {
    return out;
  } else if (blk) {
    /* the method that CRuby finds is not a builtin one that Fluxpinel
       knows (a method of a module that a singleton class includes) */
    fx_gap(sp_sprintf("super with a block to the builtin %s", name));
  }
  fx_trace_missing("super#", name, argc);
  sp_raise_cls("NoMethodError", sp_sprintf("super: no superclass method '%s'", name));
}

/* ---- control flow ---- */

static sp_RbVal fx_eval_stmts(FxFrame *f, const pm_statements_node_t *s) {
  sp_RbVal v = sp_box_nil();
  if (!s) return v;
  for (size_t i = 0; i < s->body.size; i++) {
    v = fx_eval(f, s->body.nodes[i]);
    if (L.ctl != FX_CTL_NONE) break;
  }
  return v;
}

/* while and until. post is set for `begin ... end while c`, whose body
   runs once before the first test. A redo runs the body again with no
   test. */
static sp_RbVal fx_while(FxFrame *f, const pm_node_t *pred, const pm_statements_node_t *body, int until,
                         int post) {
  for (int first = 1;; first = 0) {
    /* a back-edge lets another green thread run, as in AOT code */
    if (SP_UNLIKELY(SP_SAFEPOINT_POLL())) fx_safepoint();
    if (!(post && first)) {
      sp_RbVal c = fx_eval(f, pred);
      if (L.ctl != FX_CTL_NONE) return sp_box_nil();
      if (sp_poly_truthy(c) == until) return sp_box_nil();
    }
    do {
      if (L.ctl == FX_CTL_REDO) L.ctl = FX_CTL_NONE;
      fx_eval_stmts(f, body);
    } while (L.ctl == FX_CTL_REDO);
    if (L.ctl == FX_CTL_BREAK) { L.ctl = FX_CTL_NONE; return L.ctl_val; }
    if (L.ctl == FX_CTL_NEXT) { L.ctl = FX_CTL_NONE; continue; }
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
  }
}

/* for x in c: each of c with a block of C. The index and the locals of the
   body are those of the scope, as in CRuby. A break ends the each, and
   answers its value; the loop answers what each answers. */
static int fx_mw_prepare(FxFrame *f, const pm_node_t *t, sp_PolyArray *pre);
static void fx_mw_assign(FxFrame *f, const pm_node_t *t, sp_RbVal v, sp_PolyArray *pre, sp_int *pi);

typedef struct {
  FxFrame *f;
  const pm_for_node_t *n;
  const FxBlock *self;
} FxFor;

static sp_RbVal fx_for_step(void *ctx, int argc, sp_RbVal *argv) {
  SP_GC_SAVE();
  FxFor *w = (FxFor *)ctx;
  /* as the parameters of a block: one index takes the first value that
     each yields, and `for a, b` takes them all */
  sp_RbVal v = argc >= 1 ? argv[0] : sp_box_nil();
  SP_GC_ROOT_RBVAL(v);
  if (argc > 1 && PM_NODE_TYPE_P(w->n->index, PM_MULTI_TARGET_NODE)) {
    sp_PolyArray *p = sp_PolyArray_new();
    v = sp_box_poly_array(p);
    for (int i = 0; i < argc; i++) sp_PolyArray_push(p, argv[i]);
  }
  sp_PolyArray *pre = sp_PolyArray_new();
  SP_GC_ROOT(pre);
  if (!fx_mw_prepare(w->f, w->n->index, pre)) return sp_box_nil();
  sp_int pi = 0;
  fx_mw_assign(w->f, w->n->index, v, pre, &pi);
  if (L.ctl != FX_CTL_NONE) return sp_box_nil();
  do {
    if (L.ctl == FX_CTL_REDO) L.ctl = FX_CTL_NONE;
    fx_eval_stmts(w->f, w->n->statements);
  } while (L.ctl == FX_CTL_REDO);
  if (L.ctl == FX_CTL_NEXT) L.ctl = FX_CTL_NONE;
  if (L.ctl == FX_CTL_BREAK) {
    L.ctl = FX_CTL_BREAK_BLOCK;
    L.ctl_target = w->self;
  }
  return sp_box_nil();
}

static sp_RbVal fx_for(FxFrame *f, const pm_for_node_t *n) {
  sp_RbVal c = fx_eval(f, n->collection);
  if (L.ctl != FX_CTL_NONE) return sp_box_nil();
  SP_GC_ROOT_RBVAL(c);
  FxFor w = { .f = f, .n = n };
  FxBlock cb = { .cfn = fx_for_step, .ctx = &w };
  w.self = &cb;
  sp_RbVal v = fx_send_b(c, "each", 0, NULL, &cb);
  if (L.ctl == FX_CTL_BREAK_BLOCK && L.ctl_target == &cb) {
    L.ctl = FX_CTL_NONE;
    return L.ctl_val;
  }
  return v;
}

/* Whether the value v of a when clause takes the subject: v === subject,
   or v itself for a case with no subject. */
static int fx_when_hit(sp_RbVal v, const sp_RbVal *subject) {
  if (!subject) return sp_poly_truthy(v);
  sp_RbVal s = *subject, out;
  if (fx_fast_eqq(v, s, &out)) return sp_poly_truthy(out);
  return sp_poly_truthy(fx_send(v, "===", 1, &s));
}

/* case / when: the first when with a value that takes the subject runs,
   else the else clause. A splat (`when *list`) tests each element. */
static sp_RbVal fx_case(FxFrame *f, const pm_case_node_t *c) {
  SP_GC_SAVE();
  sp_RbVal subject = sp_box_nil(), v = sp_box_nil();
  SP_GC_ROOT_RBVAL(subject);
  SP_GC_ROOT_RBVAL(v);
  if (c->predicate) {
    subject = fx_eval(f, c->predicate);
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
  }
  const sp_RbVal *sp = c->predicate ? &subject : NULL;
  for (size_t i = 0; i < c->conditions.size; i++) {
    const pm_when_node_t *w = (const pm_when_node_t *)c->conditions.nodes[i];
    for (size_t j = 0; j < w->conditions.size; j++) {
      const pm_node_t *cn = w->conditions.nodes[j];
      int hit = 0;
      if (PM_NODE_TYPE_P(cn, PM_SPLAT_NODE)) {
        sp_PolyArray *list = sp_PolyArray_new();
        SP_GC_ROOT(list);
        v = fx_eval(f, ((const pm_splat_node_t *)cn)->expression);
        if (L.ctl != FX_CTL_NONE) return sp_box_nil();
        fx_spread(list, v);
        for (sp_int k = 0; k < list->len && !hit; k++) hit = fx_when_hit(list->data[k], sp);
      } else {
        v = fx_eval(f, cn);
        if (L.ctl != FX_CTL_NONE) return sp_box_nil();
        hit = fx_when_hit(v, sp);
      }
      if (hit) return fx_eval_stmts(f, w->statements);
    }
  }
  return c->else_clause ? fx_eval(f, (const pm_node_t *)c->else_clause) : sp_box_nil();
}

/* The state of one pattern match. With want set (a single pattern: `=>`,
   or a case/in with one `in` and no else), why keeps the check that failed
   last, for the message of NoMatchingPatternError, as CRuby gives it. */
typedef struct {
  int want;
  int key_missing;  /* why is a missing key: NoMatchingPatternKeyError */
  const char *why;
} FxPat;

/* a ++ b ++ c, in one allocation. The arguments stay rooted across it, so
   a long inspect survives. */
static const char *fx_cat3(const char *a, const char *b, const char *c) {
  SP_GC_ROOT_STR(a);
  SP_GC_ROOT_STR(b);
  SP_GC_ROOT_STR(c);
  size_t la = strlen(a), lb = strlen(b), lc = strlen(c);
  char *s = sp_str_alloc(la + lb + lc);
  memcpy(s, a, la);
  memcpy(s + la, b, lb);
  memcpy(s + la + lb, c, lc);
  s[la + lb + lc] = 0;
  return s;
}

static int fx_pat_fail(FxPat *p, const char *why, int key) {
  p->why = why;
  p->key_missing = key;
  return 0;
}

/* "<inspect of v><tail>", for a check on one value. */
static int fx_pat_fail_on(FxPat *p, sp_RbVal v, const char *tail) {
  if (!p->want) return 0;
  const char *s = sp_poly_inspect(v);
  return fx_pat_fail(p, fx_cat3(s, tail, ""), 0);
}

/* A value pattern (a literal, a constant, a Range, a pin, ...): pat === v. */
static int fx_pat_value(FxFrame *f, FxPat *p, const pm_node_t *n, sp_RbVal v) {
  SP_GC_ROOT_RBVAL(v);
  sp_RbVal pv = fx_eval(f, n);
  if (L.ctl != FX_CTL_NONE) return 0;
  SP_GC_ROOT_RBVAL(pv);
  sp_RbVal arg = v;
  if (sp_poly_truthy(fx_send(pv, "===", 1, &arg))) return 1;
  if (!p->want) return 0;
  const char *a = sp_poly_inspect(pv);
  SP_GC_ROOT(a);
  const char *b = sp_poly_inspect(v);
  SP_GC_ROOT(b);
  return fx_pat_fail(p, fx_cat3(fx_cat3(a, " === ", b), " does not return true", ""), 0);
}

static int fx_pattern(FxFrame *f, FxPat *p, const pm_node_t *n, sp_RbVal v);

/* The Array that v.deconstruct gives, or nil when v has no deconstruct.
   An Array is its own. */
static sp_RbVal fx_pat_deconstruct(FxPat *p, sp_RbVal v) {
  if (fx_is_array_value(v)) return v;
  SP_GC_ROOT_RBVAL(v);
  if (!sp_poly_truthy(fx_respond_to(v, sp_box_sym(sp_sym_intern("deconstruct")), sp_box_bool(0)))) {
    fx_pat_fail_on(p, v, " does not respond to #deconstruct");
    return sp_box_nil();
  }
  sp_RbVal d = fx_send(v, "deconstruct", 0, NULL);
  if (!fx_is_array_value(d)) sp_raise_cls("TypeError", "deconstruct must return Array");
  return d;
}

/* Matches the elements [from, from + list->size) of the Array d. */
static int fx_pat_elems(FxFrame *f, FxPat *p, const pm_node_list_t *list, sp_RbVal d, sp_int from) {
  for (size_t i = 0; i < list->size; i++)
    if (!fx_pattern(f, p, list->nodes[i], sp_poly_arr_get(d, from + (sp_int)i))) return 0;
  return 1;
}

/* Binds the splat `*name` to the elements [from, to) of the Array d. A
   splat with no name, or an implicit rest (`in a,`), binds nothing. */
static void fx_pat_rest(FxFrame *f, const pm_node_t *rest, sp_RbVal d, sp_int from, sp_int to) {
  if (!rest || !PM_NODE_TYPE_P(rest, PM_SPLAT_NODE)) return;
  const pm_node_t *t = ((const pm_splat_node_t *)rest)->expression;
  if (!t) return;
  SP_GC_ROOT_RBVAL(d);
  sp_PolyArray *a = sp_PolyArray_new();
  SP_GC_ROOT(a);
  for (sp_int i = from; i < to; i++) sp_PolyArray_push(a, sp_poly_arr_get(d, i));
  const pm_local_variable_target_node_t *x = (const pm_local_variable_target_node_t *)t;
  *fx_local(f, t, x->name, x->depth) = sp_box_poly_array(a);
}

/* [a, *rest, b] and Const(a, b): the constant takes v, then the elements
   of v.deconstruct take the patterns. */
static int fx_pat_array(FxFrame *f, FxPat *p, const pm_array_pattern_node_t *n, sp_RbVal v) {
  SP_GC_ROOT_RBVAL(v);
  if (n->constant && !fx_pat_value(f, p, n->constant, v)) return 0;
  sp_RbVal d = fx_pat_deconstruct(p, v);
  if (d.tag == SP_TAG_NIL) return 0;
  SP_GC_ROOT_RBVAL(d);
  sp_int len = sp_poly_length(d), pre = (sp_int)n->requireds.size, post = (sp_int)n->posts.size;
  if (n->rest ? len < pre + post : len != pre + post) {
    if (!p->want) return 0;
    const char *s = sp_poly_inspect(d);
    SP_GC_ROOT(s);
    return fx_pat_fail(p, fx_cat3(s, sp_sprintf(" length mismatch (given %lld, expected %lld%s)", (long long)len,
                                                (long long)(pre + post), n->rest ? "+" : ""), ""), 0);
  }
  if (!fx_pat_elems(f, p, &n->requireds, d, 0)) return 0;
  if (!fx_pat_elems(f, p, &n->posts, d, len - post)) return 0;
  fx_pat_rest(f, n->rest, d, pre, len - post);
  return 1;
}

/* [*, a, b, *]: the first run of elements that takes the patterns. */
static int fx_pat_find(FxFrame *f, FxPat *p, const pm_find_pattern_node_t *n, sp_RbVal v) {
  SP_GC_ROOT_RBVAL(v);
  if (n->constant && !fx_pat_value(f, p, n->constant, v)) return 0;
  sp_RbVal d = fx_pat_deconstruct(p, v);
  if (d.tag == SP_TAG_NIL) return 0;
  SP_GC_ROOT_RBVAL(d);
  sp_int len = sp_poly_length(d), k = (sp_int)n->requireds.size;
  if (len < k) {
    if (!p->want) return 0;
    const char *s = sp_poly_inspect(d);
    SP_GC_ROOT(s);
    return fx_pat_fail(p, fx_cat3(s, sp_sprintf(" length mismatch (given %lld, expected %lld+)", (long long)len,
                                                (long long)k), ""), 0);
  }
  /* a failed try is not the reason: the message names the find pattern */
  int want = p->want;
  p->want = 0;
  for (sp_int at = 0; at + k <= len; at++) {
    int hit = fx_pat_elems(f, p, &n->requireds, d, at);
    if (L.ctl != FX_CTL_NONE) { p->want = want; return 0; }
    if (hit) {
      p->want = want;
      fx_pat_rest(f, (const pm_node_t *)n->left, d, 0, at);
      fx_pat_rest(f, n->right, d, at + k, len);
      return 1;
    }
  }
  p->want = want;
  return fx_pat_fail_on(p, d, " does not match to find pattern");
}

/* {a:, b: pat, **rest} and Const(a:): the constant takes v, then
   v.deconstruct_keys gives a Hash. Each key must be there, and its value
   takes the pattern. `**rest` binds the other pairs, and `**nil` wants
   none. A Hash is its own deconstruct_keys. */
static int fx_pat_hash(FxFrame *f, FxPat *p, const pm_hash_pattern_node_t *n, sp_RbVal v) {
  SP_GC_ROOT_RBVAL(v);
  if (n->constant && !fx_pat_value(f, p, n->constant, v)) return 0;
  const pm_node_t *rest = n->rest;
  int named_rest = rest && PM_NODE_TYPE_P(rest, PM_ASSOC_SPLAT_NODE) && ((const pm_assoc_splat_node_t *)rest)->value;
  sp_RbVal d = v;
  SP_GC_ROOT_RBVAL(d);
  if (!fx_is_hash(v)) {
    if (!sp_poly_truthy(fx_respond_to(v, sp_box_sym(sp_sym_intern("deconstruct_keys")), sp_box_bool(0))))
      return fx_pat_fail_on(p, v, " does not respond to #deconstruct_keys");
    /* the keys of the pattern, or nil for `**rest` and for no keys, as in
       CRuby: a Struct answers all its members for nil */
    sp_RbVal keys = sp_box_nil();
    SP_GC_ROOT_RBVAL(keys);
    if (!named_rest && n->elements.size > 0) {
      sp_PolyArray *a = sp_PolyArray_new();
      keys = sp_box_poly_array(a);
      for (size_t i = 0; i < n->elements.size; i++) {
        sp_RbVal k = fx_eval(f, ((const pm_assoc_node_t *)n->elements.nodes[i])->key);
        if (L.ctl != FX_CTL_NONE) return 0;
        sp_PolyArray_push(a, k);
      }
    }
    d = fx_send(v, "deconstruct_keys", 1, &keys);
    if (!fx_is_hash(d)) sp_raise_cls("TypeError", "deconstruct_keys must return Hash");
  }
  if (n->elements.size == 0 && !rest) {
    if (sp_poly_truthy(fx_send(d, "empty?", 0, NULL))) return 1;
    return fx_pat_fail_on(p, d, " is not empty");
  }
  sp_RbVal left = sp_box_nil();
  SP_GC_ROOT_RBVAL(left);
  if (rest) left = fx_send(d, "dup", 0, NULL);
  for (size_t i = 0; i < n->elements.size; i++) {
    const pm_assoc_node_t *a = (const pm_assoc_node_t *)n->elements.nodes[i];
    sp_RbVal k = fx_eval(f, a->key);
    if (L.ctl != FX_CTL_NONE) return 0;
    SP_GC_ROOT_RBVAL(k);
    if (!sp_poly_truthy(fx_send(d, "key?", 1, &k))) {
      if (!p->want) return 0;
      const char *s = sp_poly_inspect(k);
      return fx_pat_fail(p, fx_cat3("key not found: ", s, ""), 1);
    }
    sp_RbVal e = fx_send(d, "[]", 1, &k);
    if (!fx_pattern(f, p, a->value, e)) return 0;
    if (rest) fx_send(left, "delete", 1, &k);
  }
  if (named_rest) return fx_pattern(f, p, ((const pm_assoc_splat_node_t *)rest)->value, left);
  if (rest && PM_NODE_TYPE_P(rest, PM_NO_KEYWORDS_PARAMETER_NODE) &&
      !sp_poly_truthy(fx_send(left, "empty?", 0, NULL))) {
    if (!p->want) return 0;
    const char *s = sp_poly_inspect(left);
    return fx_pat_fail(p, fx_cat3("rest of ", s, " is not empty"), 0);
  }
  return 1;
}

/* Whether v takes the pattern n. A name in the pattern binds as the match
   goes, as in CRuby. Answers 0 also when a jump is pending. */
static int fx_pattern(FxFrame *f, FxPat *p, const pm_node_t *n, sp_RbVal v) {
  SP_GC_ROOT_RBVAL(v);
  switch (PM_NODE_TYPE(n)) {
  case PM_LOCAL_VARIABLE_TARGET_NODE: {
    const pm_local_variable_target_node_t *x = (const pm_local_variable_target_node_t *)n;
    *fx_local(f, n, x->name, x->depth) = v;
    return 1;
  }
  case PM_IMPLICIT_NODE:
    return fx_pattern(f, p, ((const pm_implicit_node_t *)n)->value, v);
  case PM_CAPTURE_PATTERN_NODE: {
    const pm_capture_pattern_node_t *c = (const pm_capture_pattern_node_t *)n;
    if (!fx_pattern(f, p, c->value, v)) return 0;
    *fx_local(f, (const pm_node_t *)c->target, c->target->name, c->target->depth) = v;
    return 1;
  }
  case PM_ALTERNATION_PATTERN_NODE: {
    const pm_alternation_pattern_node_t *a = (const pm_alternation_pattern_node_t *)n;
    if (fx_pattern(f, p, a->left, v)) return 1;
    if (L.ctl != FX_CTL_NONE) return 0;
    return fx_pattern(f, p, a->right, v);
  }
  case PM_PINNED_VARIABLE_NODE:
    return fx_pat_value(f, p, ((const pm_pinned_variable_node_t *)n)->variable, v);
  case PM_PINNED_EXPRESSION_NODE:
    return fx_pat_value(f, p, ((const pm_pinned_expression_node_t *)n)->expression, v);
  case PM_ARRAY_PATTERN_NODE:
    return fx_pat_array(f, p, (const pm_array_pattern_node_t *)n, v);
  case PM_FIND_PATTERN_NODE:
    return fx_pat_find(f, p, (const pm_find_pattern_node_t *)n, v);
  case PM_HASH_PATTERN_NODE:
    return fx_pat_hash(f, p, (const pm_hash_pattern_node_t *)n, v);
  default:
    return fx_pat_value(f, p, n, v);
  }
}

/* Raises the NoMatchingPatternError of a subject that no pattern took:
   "<inspect>: <why>" for a single pattern, else the inspect alone. */
static __attribute__((noreturn)) void fx_pat_raise(FxPat *p, sp_RbVal subject) {
  SP_GC_ROOT_RBVAL(subject);
  const char *s = sp_poly_inspect(subject);
  SP_GC_ROOT(s);
  const char *msg = p->want && p->why ? fx_cat3(s, ": ", p->why) : s;
  sp_raise_cls(p->key_missing ? "NoMatchingPatternKeyError" : "NoMatchingPatternError", msg);
}

/* case / in: the first `in` whose pattern takes the subject, and whose
   guard (`if` / `unless`) holds, runs. With no else, no match raises. */
static sp_RbVal fx_case_match(FxFrame *f, const pm_case_match_node_t *c) {
  SP_GC_SAVE();
  sp_RbVal subject = fx_eval(f, c->predicate);
  if (L.ctl != FX_CTL_NONE) return sp_box_nil();
  SP_GC_ROOT_RBVAL(subject);
  FxPat p = { c->conditions.size == 1 && !c->else_clause, 0, NULL };
  SP_GC_ROOT(p.why);
  for (size_t i = 0; i < c->conditions.size; i++) {
    const pm_in_node_t *in = (const pm_in_node_t *)c->conditions.nodes[i];
    const pm_node_t *pat = in->pattern, *guard = NULL;
    int unless = PM_NODE_TYPE_P(pat, PM_UNLESS_NODE);
    if (PM_NODE_TYPE_P(pat, PM_IF_NODE) || unless) {
      const pm_statements_node_t *st = unless ? ((const pm_unless_node_t *)pat)->statements
                                              : ((const pm_if_node_t *)pat)->statements;
      guard = unless ? ((const pm_unless_node_t *)pat)->predicate : ((const pm_if_node_t *)pat)->predicate;
      pat = st->body.nodes[0];
    }
    int hit = fx_pattern(f, &p, pat, subject);
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
    if (hit && guard) {
      sp_RbVal g = fx_eval(f, guard);
      if (L.ctl != FX_CTL_NONE) return sp_box_nil();
      hit = sp_poly_truthy(g) != unless;
      if (!hit && p.want) fx_pat_fail(&p, "guard clause does not return true", 0);
    }
    if (hit) return fx_eval_stmts(f, in->statements);
  }
  if (c->else_clause) return fx_eval(f, (const pm_node_t *)c->else_clause);
  fx_pat_raise(&p, subject);
}

/* `v in pat` answers whether v takes pat; `v => pat` raises when not. */
static sp_RbVal fx_match(FxFrame *f, const pm_node_t *value, const pm_node_t *pat, int required) {
  SP_GC_SAVE();
  sp_RbVal v = fx_eval(f, value);
  if (L.ctl != FX_CTL_NONE) return sp_box_nil();
  SP_GC_ROOT_RBVAL(v);
  FxPat p = { required, 0, NULL };
  SP_GC_ROOT(p.why);
  int hit = fx_pattern(f, &p, pat, v);
  if (L.ctl != FX_CTL_NONE) return sp_box_nil();
  if (required) {
    if (!hit) fx_pat_raise(&p, v);
    return sp_box_nil();
  }
  return sp_box_bool(hit);
}

/* The value that return / break / next carry: nil, one value, or an Array.
   A splat (`next *x`) makes an Array of the values it spreads. */
static sp_RbVal fx_jump_value(FxFrame *f, const pm_arguments_node_t *args) {
  if (!args || args->arguments.size == 0) return sp_box_nil();
  if (args->arguments.size == 1 && !PM_NODE_TYPE_P(args->arguments.nodes[0], PM_SPLAT_NODE))
    return fx_eval(f, args->arguments.nodes[0]);
  sp_PolyArray *a = sp_PolyArray_new();
  SP_GC_ROOT(a);
  for (size_t i = 0; i < args->arguments.size; i++) {
    const pm_node_t *e = args->arguments.nodes[i];
    if (PM_NODE_TYPE_P(e, PM_SPLAT_NODE)) {
      const pm_node_t *x = ((const pm_splat_node_t *)e)->expression;
      sp_RbVal v = x ? fx_eval(f, x) : fx_method_frame(f)->anon[0];
      if (L.ctl != FX_CTL_NONE) return sp_box_nil();
      fx_spread(a, v);
      continue;
    }
    sp_RbVal v = fx_eval(f, e);
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
    sp_PolyArray_push(a, v);
  }
  return sp_box_poly_array(a);
}

static sp_RbVal fx_jump(FxFrame *f, FxCtl kind, const pm_arguments_node_t *args) {
  sp_RbVal v = fx_jump_value(f, args);
  if (L.ctl != FX_CTL_NONE) return sp_box_nil();
  L.ctl_val = v;
  L.ctl = kind;
  if (kind == FX_CTL_RETURN) {
    /* a return in a block leaves the method that holds it, or the lambda */
    FxFrame *t = f;
    while (t->parent && !t->lambda) t = t->parent;
    /* a heap frame returns from its frame on the C stack, which a Proc can
       outlive (CRuby: LocalJumpError) */
    if (t->heap) {
      if (!fx_env_live(t)) {
        L.ctl = FX_CTL_NONE;
        sp_raise_cls("LocalJumpError", "unexpected return");
      }
      t = ((FxEnv *)t)->stack;
    }
    L.ctl_target = t;
  }
  return sp_box_nil();
}

/* ---- literals ---- */

/* An Integer literal: an Integer, or a Bignum when it does not fit 64 bits
   (in every mode, as in CRuby). The Bignum comes from the decimal text, as
   spinel's codegen makes it (sp_bigint_new_str). */
static sp_RbVal fx_integer(const pm_integer_t *iv) {
  uint64_t u = 0;
  int big = iv->values && iv->length > 2;
  if (!iv->values) u = iv->value;
  else if (iv->length == 2) u = (uint64_t)iv->values[1] << 32 | iv->values[0];
  if (big || u > (uint64_t)INT64_MAX + (iv->negative ? 1 : 0)) {
    pm_buffer_t buf;
    pm_buffer_init(&buf);
    pm_integer_string(&buf, iv);
    const char *s = sp_sprintf("%.*s", (int)pm_buffer_length(&buf), pm_buffer_value(&buf));
    pm_buffer_free(&buf);
    SP_GC_ROOT_STR(s);
    return sp_box_bigint(sp_bigint_new_str(s, 10));
  }
  return sp_box_int(iv->negative ? (sp_int)(0 - u) : (sp_int)u);
}

/* A new mutable String with the given bytes, as a literal makes one. */
static sp_RbVal fx_new_string(const uint8_t *p, size_t len) {
  char *d = sp_str_alloc(len);
  if (len) memcpy(d, p, len);
  d[len] = 0;
  return sp_box_str_new(d);
}

/* As CRuby does, a part that is not a String converts with to_s when it
   is evaluated, and the parts join after the last one: a String part
   shows a change that a later part makes to it. */
static sp_RbVal fx_interpolate(FxFrame *f, const pm_node_list_t *parts) {
  sp_PolyArray *vals = sp_PolyArray_new();
  SP_GC_ROOT(vals);
  sp_RbVal acc = sp_box_nil(), v = sp_box_nil();
  SP_GC_ROOT_RBVAL(acc);
  SP_GC_ROOT_RBVAL(v);
  for (size_t i = 0; i < parts->size; i++) {
    const pm_node_t *p = parts->nodes[i];
    if (PM_NODE_TYPE_P(p, PM_STRING_NODE)) {
      const pm_string_t *s = &((const pm_string_node_t *)p)->unescaped;
      v = fx_new_string(pm_string_source(s), pm_string_length(s));
    } else if (PM_NODE_TYPE_P(p, PM_EMBEDDED_STATEMENTS_NODE)) {
      v = fx_eval_stmts(f, ((const pm_embedded_statements_node_t *)p)->statements);
      if (L.ctl != FX_CTL_NONE) return sp_box_nil();
      if (!fx_is_string(v)) v = sp_poly_to_s_box(v);
    } else {
      fx_unsupported(p, "interpolation part");
    }
    sp_PolyArray_push(vals, v);
  }
  acc = fx_new_string(NULL, 0);
  for (sp_int i = 0; i < vals->len; i++) acc = sp_poly_add(acc, vals->data[i]);
  return acc;
}

/* An Array literal. `*x` spreads x (fx_spread), and a bare `*` the
   anonymous rest of the method. */
static sp_RbVal fx_array(FxFrame *f, const pm_array_node_t *n) {
  sp_PolyArray *a = sp_PolyArray_new();
  SP_GC_ROOT(a);
  for (size_t i = 0; i < n->elements.size; i++) {
    const pm_node_t *e = n->elements.nodes[i];
    if (PM_NODE_TYPE_P(e, PM_SPLAT_NODE)) {
      const pm_node_t *x = ((const pm_splat_node_t *)e)->expression;
      sp_RbVal v = x ? fx_eval(f, x) : fx_method_frame(f)->anon[0];
      if (L.ctl != FX_CTL_NONE) return sp_box_nil();
      fx_spread(a, v);
      continue;
    }
    sp_RbVal v = fx_eval(f, e);
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
    sp_PolyArray_push(a, v);
  }
  return sp_box_poly_array(a);
}

/* A Hash literal, or the keyword arguments of a call (`m(a: 1)`), as a
   poly Hash. `**h` copies the entries of h. */
static sp_RbVal fx_hash_lit(FxFrame *f, const pm_node_list_t *elts) {
  sp_PolyPolyHash *h = sp_PolyPolyHash_new();
  SP_GC_ROOT(h);
  sp_RbVal k = sp_box_nil(), v = sp_box_nil();
  SP_GC_ROOT_RBVAL(k);
  SP_GC_ROOT_RBVAL(v);
  for (size_t i = 0; i < elts->size; i++) {
    const pm_node_t *e = elts->nodes[i];
    if (PM_NODE_TYPE_P(e, PM_ASSOC_SPLAT_NODE)) {
      const pm_node_t *x = ((const pm_assoc_splat_node_t *)e)->value;
      v = x ? fx_eval(f, x) : fx_method_frame(f)->anon[1];   /* `**` passes the method's **kwrest */
      if (L.ctl != FX_CTL_NONE) return sp_box_nil();
      if (v.tag == SP_TAG_NIL) continue;
      if (fx_is_fx_object(v)) v = fx_send(v, "to_hash", 0, NULL);
      sp_PolyPolyHash *o = sp_poly_as_poly_poly_hash(v);
      if (!o) sp_raise_cls("TypeError", sp_sprintf("no implicit conversion of %s into Hash", sp_poly_class_name(v)));
      for (sp_int j = 0; j < o->len; j++) sp_PolyPolyHash_set(h, o->keys[o->order[j]], o->vals[o->order[j]]);
      continue;
    }
    const pm_assoc_node_t *a = (const pm_assoc_node_t *)e;
    k = fx_eval(f, a->key);
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
    /* `{x:}` reads x */
    v = PM_NODE_TYPE_P(a->value, PM_IMPLICIT_NODE) ? fx_eval(f, ((const pm_implicit_node_t *)a->value)->value)
                                                   : fx_eval(f, a->value);
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
    sp_PolyPolyHash_set(h, k, v);
  }
  return sp_box_obj(h, SP_BUILTIN_POLY_POLY_HASH);
}

/* The text of a String end of a Range, or NULL for nil. */
static const char *fx_range_text(sp_RbVal v) {
  if (v.tag == SP_TAG_NIL) return NULL;
  if (v.tag == SP_TAG_STR) return v.v.s;
  return sp_poly_strbuf_copy(v);
}

/* A Range in the form of the runtime that spinel gives the same ends. A
   missing end makes a beginless or endless one.

   - Integer ends (or nil): sp_Range
   - an Integer begin and a Float end: sp_Range with the Float end
   - other Float ends (an Integer or nil on one side): sp_FloatRange
   - String or Symbol ends: sp_StrRange */
static sp_RbVal fx_range(FxFrame *f, const pm_range_node_t *n) {
  SP_GC_SAVE();
  sp_RbVal lo = n->left ? fx_eval(f, n->left) : sp_box_nil();
  if (L.ctl != FX_CTL_NONE) return sp_box_nil();
  SP_GC_ROOT_RBVAL(lo);
  sp_RbVal hi = n->right ? fx_eval(f, n->right) : sp_box_nil();
  if (L.ctl != FX_CTL_NONE) return sp_box_nil();
  SP_GC_ROOT_RBVAL(hi);
  int excl = PM_NODE_FLAG_P((const pm_node_t *)n, PM_RANGE_FLAGS_EXCLUDE_END);
  int lt = lo.tag, ht = hi.tag;
  int lnum = lt == SP_TAG_INT || lt == SP_TAG_FLT || lt == SP_TAG_NIL;
  int hnum = ht == SP_TAG_INT || ht == SP_TAG_FLT || ht == SP_TAG_NIL;
  if ((lt == SP_TAG_INT || lt == SP_TAG_NIL) && (ht == SP_TAG_INT || ht == SP_TAG_NIL)) {
    sp_oint first = { lt == SP_TAG_INT ? lo.v.i : 0, lt == SP_TAG_NIL };
    sp_oint last = { ht == SP_TAG_INT ? hi.v.i : 0, ht == SP_TAG_NIL };
    return sp_box_range(sp_range_new_o(first, last, excl));
  }
  if (lt == SP_TAG_INT && ht == SP_TAG_FLT) return sp_box_range(sp_range_new_fend(lo.v.i, hi.v.f, excl));
  if (lnum && hnum) {
    int om = (lt == SP_TAG_NIL ? SP_FRANGE_NO_BEGIN : lt == SP_TAG_INT ? SP_FRANGE_INT_BEGIN : 0) |
             (ht == SP_TAG_NIL ? SP_FRANGE_NO_END : ht == SP_TAG_INT ? SP_FRANGE_INT_END : 0);
    /* the runtime keeps an open side as an infinity */
    sp_float a = lt == SP_TAG_FLT ? lo.v.f : lt == SP_TAG_INT ? (sp_float)lo.v.i : -HUGE_VAL;
    sp_float b = ht == SP_TAG_FLT ? hi.v.f : ht == SP_TAG_INT ? (sp_float)hi.v.i : HUGE_VAL;
    return sp_box_frange(sp_frange_new_o(a, b, excl, om));
  }
  int lsym = lt == SP_TAG_SYM, hsym = ht == SP_TAG_SYM;
  if ((lsym || lt == SP_TAG_NIL) && (hsym || ht == SP_TAG_NIL)) {
    sp_StrRange r = sp_srange_new(lsym ? sp_sym_to_s((sp_sym)lo.v.i) : NULL,
                                  hsym ? sp_sym_to_s((sp_sym)hi.v.i) : NULL, excl);
    r.sym = 1;
    return sp_box_srange(r);
  }
  if ((fx_is_string(lo) || lt == SP_TAG_NIL) && (fx_is_string(hi) || ht == SP_TAG_NIL)) {
    const char *a = fx_range_text(lo);
    SP_GC_ROOT_STR(a);
    const char *b = fx_range_text(hi);
    SP_GC_ROOT_STR(b);
    return sp_box_srange(sp_srange_new(a, b, excl));
  }
  fx_unsupported((const pm_node_t *)n, "Range of values other than Integer, Float, String and Symbol");
}

/* ---- classes, modules and constants ---- */

/* The scope and the name that a constant path writes or defines. */
static void fx_const_target(FxFrame *f, const pm_node_t *path, FxClass **scope, sp_sym *name) {
  if (PM_NODE_TYPE_P(path, PM_CONSTANT_READ_NODE)) {
    *scope = f->cref;
    *name = fx_sym(((const pm_constant_read_node_t *)path)->name);
    return;
  }
  if (!PM_NODE_TYPE_P(path, PM_CONSTANT_PATH_NODE)) fx_unsupported(path, "constant target");
  const pm_constant_path_node_t *cp = (const pm_constant_path_node_t *)path;
  *name = fx_sym(cp->name);
  if (!cp->parent) { *scope = L.object; return; }
  sp_RbVal pv = fx_eval(f, cp->parent);
  *scope = fx_class_value(pv);
  if (!*scope) sp_raise_cls("TypeError", sp_sprintf("%s is not a class/module", sp_poly_inspect(pv)));
}

static sp_RbVal fx_class_def(FxFrame *f, const pm_node_t *path, const pm_node_t *super_node,
                               const pm_node_t *body, const pm_constant_id_list_t *locals, int is_module) {
  SP_GC_SAVE();
  FxClass *scope;
  sp_sym name;
  fx_const_target(f, path, &scope, &name);
  const char *cname = sp_sym_to_s(name);
  FxClass *k = NULL;
  /* an autoload constant loads its file first, so that the body reopens
     the class of the file */
  FxConst *own = fx_const_find(scope, name, 1);
  if (own) {
    k = fx_class_value(own->value);
    if (!k) sp_raise_cls("TypeError", sp_sprintf("%s is not a %s", cname, is_module ? "module" : "class"));
  } else if (scope == L.object) {
    k = fx_builtin(cname);   /* reopen a builtin class */
    if (!k && fx_unknown_builtin(cname)) fx_gap(sp_sprintf("reopening of the builtin %s", cname));
  }
  if (!k) {
    FxClass *super = NULL;
    if (!is_module) {
      super = L.object;
      if (super_node) {
        sp_RbVal sv = fx_eval(f, super_node);
        if (L.ctl != FX_CTL_NONE) return sp_box_nil();
        super = fx_class_value(sv);
        if (!super || super->is_module)
          sp_raise_cls("TypeError", sp_sprintf("superclass must be an instance of Class (given an instance of %s)",
                                               super ? "Module" : fx_class_of(sv)->name));
      }
    }
    const char *full = scope == L.object ? cname : sp_sprintf("%s::%s", scope->name, cname);
    k = fx_class_new(strdup(full), is_module, super, scope);
    fx_const_set(scope, name, fx_class_box(k));
    /* the hook of the superclass: inherited(subclass), before the body */
    FxMethod *hook = super ? fx_find_smethod(super, sp_sym_intern("inherited")) : NULL;
    if (hook) {
      sp_RbVal sub = fx_class_box(k);
      fx_invoke(hook, fx_class_box(super), 1, &sub);
    }
  }
  if (!body) return sp_box_nil();
  FX_FRAME(cf, locals, fx_class_box(k), k);
  return fx_eval(&cf, body);
}

static sp_RbVal fx_singleton_class(FxFrame *f, const pm_singleton_class_node_t *n) {
  SP_GC_SAVE();
  sp_RbVal target = fx_eval(f, n->expression);
  FxClass *k = fx_class_value(target);
  if (!k) fx_unsupported((const pm_node_t *)n, "singleton class of an object");
  if (!n->body) return sp_box_nil();
  FX_FRAME(sf, &n->locals, target, k);
  sf.singleton_defs = 1;
  return fx_eval(&sf, n->body);
}

static sp_RbVal fx_const_read(FxFrame *f, const pm_node_t *n) {
  sp_RbVal v;
  if (PM_NODE_TYPE_P(n, PM_CONSTANT_READ_NODE)) {
    sp_sym name = fx_sym(((const pm_constant_read_node_t *)n)->name);
    if (!fx_const_lookup(f->cref, name, &v)) fx_const_missing(sp_sym_to_s(name));
    return v;
  }
  const pm_constant_path_node_t *cp = (const pm_constant_path_node_t *)n;
  sp_sym name = fx_sym(cp->name);
  FxClass *scope = L.object;
  if (cp->parent) {
    sp_RbVal pv = fx_eval(f, cp->parent);
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
    scope = fx_class_value(pv);
    if (!scope) sp_raise_cls("TypeError", sp_sprintf("%s is not a class/module", sp_poly_inspect(pv)));
  }
  if (!fx_const_path(scope, name, &v)) fx_const_missing(fx_const_full(scope, name));
  return v;
}

/* A OP= v, A ||= v and A &&= v, also on a path X::A. A ||= v reads an
   undefined A as nil, as CRuby does (defined?); the others raise
   NameError for it. */
static sp_RbVal fx_op(sp_RbVal cur, pm_constant_id_t op, sp_RbVal v);

static sp_RbVal fx_const_logic_write(FxFrame *f, const pm_node_t *n) {
  SP_GC_SAVE();
  const pm_node_t *path = NULL, *value;
  pm_constant_id_t id = 0, op = 0;
  int kind;   /* 0: OP=, 1: ||=, 2: &&= */
  switch (PM_NODE_TYPE(n)) {
  case PM_CONSTANT_OPERATOR_WRITE_NODE: {
    const pm_constant_operator_write_node_t *w = (const pm_constant_operator_write_node_t *)n;
    id = w->name, value = w->value, op = w->binary_operator, kind = 0;
    break;
  }
  case PM_CONSTANT_OR_WRITE_NODE: {
    const pm_constant_or_write_node_t *w = (const pm_constant_or_write_node_t *)n;
    id = w->name, value = w->value, kind = 1;
    break;
  }
  case PM_CONSTANT_AND_WRITE_NODE: {
    const pm_constant_and_write_node_t *w = (const pm_constant_and_write_node_t *)n;
    id = w->name, value = w->value, kind = 2;
    break;
  }
  case PM_CONSTANT_PATH_OPERATOR_WRITE_NODE: {
    const pm_constant_path_operator_write_node_t *w = (const pm_constant_path_operator_write_node_t *)n;
    path = (const pm_node_t *)w->target, value = w->value, op = w->binary_operator, kind = 0;
    break;
  }
  case PM_CONSTANT_PATH_OR_WRITE_NODE: {
    const pm_constant_path_or_write_node_t *w = (const pm_constant_path_or_write_node_t *)n;
    path = (const pm_node_t *)w->target, value = w->value, kind = 1;
    break;
  }
  default: {
    const pm_constant_path_and_write_node_t *w = (const pm_constant_path_and_write_node_t *)n;
    path = (const pm_node_t *)w->target, value = w->value, kind = 2;
    break;
  }
  }
  FxClass *scope;
  sp_sym name;
  sp_RbVal cur = sp_box_nil();
  int hit;
  if (path) {
    fx_const_target(f, path, &scope, &name);
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
    hit = fx_const_path(scope, name, &cur);
  } else {
    scope = f->cref, name = fx_sym(id);
    hit = fx_const_lookup(scope, name, &cur);
  }
  if (!hit && kind != 1) fx_const_missing(path ? fx_const_full(scope, name) : sp_sym_to_s(name));
  SP_GC_ROOT_RBVAL(cur);
  if (kind == 1 && sp_poly_truthy(cur)) return cur;
  if (kind == 2 && !sp_poly_truthy(cur)) return cur;
  sp_RbVal v = fx_eval(f, value);
  if (L.ctl != FX_CTL_NONE) return sp_box_nil();
  if (kind == 0) v = fx_op(cur, op, v);
  fx_const_set(scope, name, v);
  return v;
}

/* x op= v on a local, an ivar or an attribute: read, send op, write. */
static sp_RbVal fx_op(sp_RbVal cur, pm_constant_id_t op, sp_RbVal v) {
  sp_RbVal out;
  if (fx_fast_call(cur, fx_sym(op), 1, &v, &out)) return out;
  SP_GC_ROOT_RBVAL(cur);
  SP_GC_ROOT_RBVAL(v);
  return fx_send(cur, sp_sym_to_s(fx_sym(op)), 1, &v);
}

static sp_RbVal fx_call_op_write(FxFrame *f, const pm_call_operator_write_node_t *n) {
  SP_GC_SAVE();
  if (!n->receiver) fx_unsupported((const pm_node_t *)n, "attribute op-assign with no receiver");
  sp_RbVal recv = fx_eval(f, n->receiver);
  SP_GC_ROOT_RBVAL(recv);
  if (L.ctl != FX_CTL_NONE) return sp_box_nil();
  /* a private reader and writer take `self.a`, not `(self).a` (prism's
     IGNORE_VISIBILITY) */
  int check = !PM_NODE_FLAG_P((const pm_node_t *)n, PM_CALL_NODE_FLAGS_IGNORE_VISIBILITY);
  sp_RbVal cur = fx_send_v(recv, sp_sym_to_s(fx_sym(n->read_name)), 0, NULL, NULL, check, &f->self);
  SP_GC_ROOT_RBVAL(cur);
  sp_RbVal v = fx_eval(f, n->value);
  if (L.ctl != FX_CTL_NONE) return sp_box_nil();
  sp_RbVal nv = fx_op(cur, n->binary_operator, v);
  SP_GC_ROOT_RBVAL(nv);
  fx_send_v(recv, sp_sym_to_s(fx_sym(n->write_name)), 1, &nv, NULL, check, &f->self);
  return nv;
}

static sp_RbVal fx_index_op_write(FxFrame *f, const pm_index_operator_write_node_t *n) {
  SP_GC_SAVE();
  if (n->block) fx_unsupported((const pm_node_t *)n, "block");
  sp_RbVal recv = fx_eval(f, n->receiver);
  SP_GC_ROOT_RBVAL(recv);
  if (L.ctl != FX_CTL_NONE) return sp_box_nil();
  int argc = fx_argc(n->arguments);
  sp_RbVal argv[argc + 1];
  if (!fx_args(f, n->arguments, argc, argv)) return sp_box_nil();
  static sp_sym aref = -1, aset = -1;
  if (aref < 0) {
    aref = sp_sym_intern("[]");
    aset = sp_sym_intern("[]=");
  }
  sp_RbVal cur;
  if (!fx_fast_call(recv, aref, argc, argv, &cur)) cur = fx_send(recv, "[]", argc, argv);
  SP_GC_ROOT_RBVAL(cur);
  sp_RbVal v = fx_eval(f, n->value);
  if (L.ctl != FX_CTL_NONE) return sp_box_nil();
  argv[argc] = fx_op(cur, n->binary_operator, v);
  _sp_gc_root_push((void **)((uintptr_t)&argv[argc] | (uintptr_t)1));
  sp_RbVal set;
  if (!fx_fast_call(recv, aset, argc + 1, argv, &set)) fx_send(recv, "[]=", argc + 1, argv);
  return argv[argc];
}

/* ---- multiple assignment ----

   As CRuby does, the receivers and the arguments of the targets come
   first, left to right, then the value, then each target takes its part.
   fx_mw_prepare keeps in pre what a target needs at its write: the
   receiver of `x.a`, the receiver and an Array of the arguments of
   `x[i]`, and the scope of `X::A` (nil for `::A`). */
static int fx_mw_prepare(FxFrame *f, const pm_node_t *t, sp_PolyArray *pre);

static int fx_mw_prepare_list(FxFrame *f, const pm_node_list_t *lefts, const pm_node_t *rest,
                                const pm_node_list_t *rights, sp_PolyArray *pre) {
  for (size_t i = 0; i < lefts->size; i++)
    if (!fx_mw_prepare(f, lefts->nodes[i], pre)) return 0;
  if (rest && !fx_mw_prepare(f, rest, pre)) return 0;
  for (size_t i = 0; i < rights->size; i++)
    if (!fx_mw_prepare(f, rights->nodes[i], pre)) return 0;
  return 1;
}

/* Evaluates n into pre. */
static int fx_mw_keep(FxFrame *f, const pm_node_t *n, sp_PolyArray *pre) {
  SP_GC_SAVE();
  sp_RbVal v = n ? fx_eval(f, n) : sp_box_nil();
  SP_GC_ROOT_RBVAL(v);
  sp_PolyArray_push(pre, v);
  return L.ctl == FX_CTL_NONE;
}

/* x[i]: the receiver, then an Array of the arguments. */
static int fx_mw_prepare_index(FxFrame *f, const pm_index_target_node_t *x, sp_PolyArray *pre) {
  SP_GC_SAVE();
  if (x->block) fx_unsupported((const pm_node_t *)x, "block");
  if (!fx_mw_keep(f, x->receiver, pre)) return 0;
  int argc = fx_argc(x->arguments);
  sp_RbVal argv[argc > 0 ? argc : 1];
  if (!fx_args(f, x->arguments, argc, argv)) return 0;
  sp_PolyArray *a = sp_PolyArray_new();
  SP_GC_ROOT(a);
  for (int i = 0; i < argc; i++) sp_PolyArray_push(a, argv[i]);
  sp_PolyArray_push(pre, sp_box_poly_array(a));
  return 1;
}

static int fx_mw_prepare(FxFrame *f, const pm_node_t *t, sp_PolyArray *pre) {
  switch (PM_NODE_TYPE(t)) {
  case PM_CALL_TARGET_NODE:
    return fx_mw_keep(f, ((const pm_call_target_node_t *)t)->receiver, pre);
  case PM_INDEX_TARGET_NODE:
    return fx_mw_prepare_index(f, (const pm_index_target_node_t *)t, pre);
  case PM_CONSTANT_PATH_TARGET_NODE:
    return fx_mw_keep(f, ((const pm_constant_path_target_node_t *)t)->parent, pre);
  case PM_MULTI_TARGET_NODE: {
    const pm_multi_target_node_t *mt = (const pm_multi_target_node_t *)t;
    return fx_mw_prepare_list(f, &mt->lefts, mt->rest, &mt->rights, pre);
  }
  case PM_SPLAT_NODE: {
    const pm_node_t *e = ((const pm_splat_node_t *)t)->expression;
    return e ? fx_mw_prepare(f, e, pre) : 1;
  }
  default:
    break;
  }
  return L.ctl == FX_CTL_NONE;
}

/* The elements that the targets take from v: an Array as it is, the
   to_ary of an object of the program, else [v]. */
static sp_RbVal fx_mw_array(sp_RbVal v) {
  if (fx_is_array_value(v)) return v;
  SP_GC_ROOT_RBVAL(v);
  if (fx_is_fx_object(v) && fx_find_method(fx_class_of(v), sp_sym_intern("to_ary"))) {
    sp_RbVal a = fx_send(v, "to_ary", 0, NULL);
    if (fx_is_array_value(a)) return a;
    if (a.tag != SP_TAG_NIL) {
      const char *cn = sp_poly_class_name(v);
      sp_raise_cls("TypeError", sp_sprintf("can't convert %s to Array (%s#to_ary gives %s)", cn, cn,
                                           sp_poly_class_name(a)));
    }
  }
  sp_PolyArray *a = sp_PolyArray_new();
  SP_GC_ROOT(a);
  sp_PolyArray_push(a, v);
  return sp_box_poly_array(a);
}

static void fx_mw_split(FxFrame *f, const pm_node_list_t *lefts, const pm_node_t *rest,
                          const pm_node_list_t *rights, sp_RbVal arr, sp_PolyArray *pre, sp_int *pi);

/* Writes v to the target t, with what fx_mw_prepare kept for it. */
static void fx_mw_assign(FxFrame *f, const pm_node_t *t, sp_RbVal v, sp_PolyArray *pre, sp_int *pi) {
  SP_GC_ROOT_RBVAL(v);
  switch (PM_NODE_TYPE(t)) {
  case PM_LOCAL_VARIABLE_TARGET_NODE: {
    const pm_local_variable_target_node_t *x = (const pm_local_variable_target_node_t *)t;
    *fx_local(f, t, x->name, x->depth) = v;
    return;
  }
  case PM_INSTANCE_VARIABLE_TARGET_NODE:
    fx_ivar_set_c(f->self, fx_sym(((const pm_instance_variable_target_node_t *)t)->name), v, &fx_side(t)->ivar);
    return;
  case PM_CLASS_VARIABLE_TARGET_NODE:
    fx_cvar_set(fx_cvar_base(f->cref), fx_sym(((const pm_class_variable_target_node_t *)t)->name), v);
    return;
  case PM_GLOBAL_VARIABLE_TARGET_NODE:
    fx_gvar_set(sp_sym_to_s(fx_sym(((const pm_global_variable_target_node_t *)t)->name)), v);
    return;
  case PM_CONSTANT_TARGET_NODE:
    fx_const_set(f->cref, fx_sym(((const pm_constant_target_node_t *)t)->name), v);
    return;
  case PM_CONSTANT_PATH_TARGET_NODE: {
    sp_RbVal sv = pre->data[(*pi)++];
    FxClass *scope = sv.tag == SP_TAG_NIL ? L.object : fx_class_value(sv);
    if (!scope) sp_raise_cls("TypeError", sp_sprintf("%s is not a class/module", sp_poly_inspect(sv)));
    fx_const_set(scope, fx_sym(((const pm_constant_path_target_node_t *)t)->name), v);
    return;
  }
  case PM_CALL_TARGET_NODE: {
    sp_RbVal recv = pre->data[(*pi)++];
    fx_send(recv, sp_sym_to_s(fx_sym(((const pm_call_target_node_t *)t)->name)), 1, &v);
    return;
  }
  case PM_INDEX_TARGET_NODE: {
    sp_RbVal recv = pre->data[(*pi)++], args = pre->data[(*pi)++];
    sp_int argc = sp_poly_length(args);
    sp_RbVal argv[argc + 1];
    for (sp_int i = 0; i < argc; i++) argv[i] = sp_poly_arr_get(args, i);
    argv[argc] = v;
    fx_send(recv, "[]=", (int)argc + 1, argv);
    return;
  }
  case PM_MULTI_TARGET_NODE: {
    const pm_multi_target_node_t *mt = (const pm_multi_target_node_t *)t;
    fx_mw_split(f, &mt->lefts, mt->rest, &mt->rights, fx_mw_array(v), pre, pi);
    return;
  }
  default:
    fx_unsupported(t, "target of a multiple assignment");
  }
}

/* The targets take the elements of arr: the lefts from the start, the
   rights from the end, and a splat the rest. */
static void fx_mw_split(FxFrame *f, const pm_node_list_t *lefts, const pm_node_t *rest,
                          const pm_node_list_t *rights, sp_RbVal arr, sp_PolyArray *pre, sp_int *pi) {
  SP_GC_ROOT_RBVAL(arr);
  sp_int len = sp_poly_length(arr), nl = (sp_int)lefts->size, nr = (sp_int)rights->size;
  for (sp_int i = 0; i < nl && L.ctl == FX_CTL_NONE; i++)
    fx_mw_assign(f, lefts->nodes[i], i < len ? sp_poly_arr_get(arr, i) : sp_box_nil(), pre, pi);
  sp_int rstart = len - nr > nl ? len - nr : nl;
  if (rest && PM_NODE_TYPE_P(rest, PM_SPLAT_NODE) && ((const pm_splat_node_t *)rest)->expression) {
    sp_PolyArray *a = sp_PolyArray_new();
    SP_GC_ROOT(a);
    for (sp_int i = nl; i < rstart; i++) sp_PolyArray_push(a, sp_poly_arr_get(arr, i));
    if (L.ctl == FX_CTL_NONE)
      fx_mw_assign(f, ((const pm_splat_node_t *)rest)->expression, sp_box_poly_array(a), pre, pi);
  }
  for (sp_int i = 0; i < nr && L.ctl == FX_CTL_NONE; i++)
    fx_mw_assign(f, rights->nodes[i], rstart + i < len ? sp_poly_arr_get(arr, rstart + i) : sp_box_nil(), pre, pi);
}

/* a, b = v: answers v. */
static sp_RbVal fx_multi_write(FxFrame *f, const pm_multi_write_node_t *w) {
  SP_GC_SAVE();
  sp_PolyArray *pre = sp_PolyArray_new();
  SP_GC_ROOT(pre);
  if (!fx_mw_prepare_list(f, &w->lefts, w->rest, &w->rights, pre)) return sp_box_nil();
  sp_RbVal v = fx_eval(f, w->value);
  if (L.ctl != FX_CTL_NONE) return sp_box_nil();
  SP_GC_ROOT_RBVAL(v);
  sp_int pi = 0;
  fx_mw_split(f, &w->lefts, w->rest, &w->rights, fx_mw_array(v), pre, &pi);
  return v;
}

/* x.a ||= v and x.a &&= v: read, test, and write only when needed. x&.a
   answers nil for a nil x, and does not evaluate v. */
static sp_RbVal fx_call_logic_write(FxFrame *f, const pm_node_t *n) {
  SP_GC_SAVE();
  int is_or = PM_NODE_TYPE_P(n, PM_CALL_OR_WRITE_NODE);
  const pm_node_t *recv_n, *value;
  pm_constant_id_t read, write;
  if (is_or) {
    const pm_call_or_write_node_t *w = (const pm_call_or_write_node_t *)n;
    recv_n = w->receiver, value = w->value, read = w->read_name, write = w->write_name;
  } else {
    const pm_call_and_write_node_t *w = (const pm_call_and_write_node_t *)n;
    recv_n = w->receiver, value = w->value, read = w->read_name, write = w->write_name;
  }
  if (!recv_n) fx_unsupported(n, "attribute logic-assign with no receiver");
  sp_RbVal recv = fx_eval(f, recv_n);
  SP_GC_ROOT_RBVAL(recv);
  if (L.ctl != FX_CTL_NONE) return sp_box_nil();
  if (PM_NODE_FLAG_P(n, PM_CALL_NODE_FLAGS_SAFE_NAVIGATION) && recv.tag == SP_TAG_NIL) return recv;
  int check = !PM_NODE_FLAG_P(n, PM_CALL_NODE_FLAGS_IGNORE_VISIBILITY);
  sp_RbVal cur = fx_send_v(recv, sp_sym_to_s(fx_sym(read)), 0, NULL, NULL, check, &f->self);
  if (sp_poly_truthy(cur) == is_or) return cur;
  sp_RbVal v = fx_eval(f, value);
  if (L.ctl != FX_CTL_NONE) return sp_box_nil();
  SP_GC_ROOT_RBVAL(v);
  fx_send_v(recv, sp_sym_to_s(fx_sym(write)), 1, &v, NULL, check, &f->self);
  return v;
}

/* x[i] ||= v and x[i] &&= v: read, test, and write only when needed. */
static sp_RbVal fx_index_logic_write(FxFrame *f, const pm_node_t *n) {
  SP_GC_SAVE();
  int is_or = PM_NODE_TYPE_P(n, PM_INDEX_OR_WRITE_NODE);
  const pm_node_t *recv_n, *value, *block;
  const pm_arguments_node_t *args;
  if (is_or) {
    const pm_index_or_write_node_t *w = (const pm_index_or_write_node_t *)n;
    recv_n = w->receiver, args = w->arguments, value = w->value, block = (const pm_node_t *)w->block;
  } else {
    const pm_index_and_write_node_t *w = (const pm_index_and_write_node_t *)n;
    recv_n = w->receiver, args = w->arguments, value = w->value, block = (const pm_node_t *)w->block;
  }
  if (block) fx_unsupported(n, "block");
  sp_RbVal recv = fx_eval(f, recv_n);
  SP_GC_ROOT_RBVAL(recv);
  if (L.ctl != FX_CTL_NONE) return sp_box_nil();
  int argc = fx_argc(args);
  sp_RbVal argv[argc + 1];
  if (!fx_args(f, args, argc, argv)) return sp_box_nil();
  sp_RbVal cur = fx_send(recv, "[]", argc, argv);
  if (sp_poly_truthy(cur) == is_or) return cur;
  argv[argc] = fx_eval(f, value);
  _sp_gc_root_push((void **)((uintptr_t)&argv[argc] | (uintptr_t)1));
  if (L.ctl != FX_CTL_NONE) return sp_box_nil();
  fx_send(recv, "[]=", argc + 1, argv);
  return argv[argc];
}

/* ---- exceptions ---- */

/* The exception that a handler caught (FX_TRY_PUSH and FX_TRY_LANDED are
   in fluxpinel.h). */
static FxCaught fx_caught(void) {
  return (FxCaught){ (const char *)sp_last_exc_cls, sp_exc_msg[sp_exc_top], sp_exc_obj[sp_exc_top] };
}

/* The object of a caught exception, made now when the runtime raised by
   name. It takes as its cause the exception that a rescue handled when
   the raise happened (sp_pending_cause), as the code of spinel does. */
static sp_Exception *fx_caught_obj(FxCaught c) {
  sp_Exception *e = c.obj ? (sp_Exception *)c.obj : sp_exc_new_for_catch(c.cls, c.msg);
  if (!e->cause) {
    sp_gc_wb(e);
    e->cause = (sp_Exception *)sp_pending_cause;
  }
  sp_pending_cause = NULL;
  return e;
}

/* Raises a caught exception again, the same object. */
static __attribute__((noreturn)) void fx_reraise(FxCaught c) {
  sp_pending_exc_obj = c.obj;
  sp_bt_keep = 1;
  sp_raise_cls(c.cls, c.msg);
}

/* Binds `=> target` of a rescue clause. */
static void fx_rescue_bind(FxFrame *f, const pm_node_t *t, sp_RbVal v) {
  if (PM_NODE_TYPE_P(t, PM_LOCAL_VARIABLE_TARGET_NODE)) {
    const pm_local_variable_target_node_t *l = (const pm_local_variable_target_node_t *)t;
    *fx_local(f, t, l->name, l->depth) = v;
  } else if (PM_NODE_TYPE_P(t, PM_INSTANCE_VARIABLE_TARGET_NODE)) {
    fx_ivar_set(f->self, fx_sym(((const pm_instance_variable_target_node_t *)t)->name), v);
  } else {
    fx_unsupported(t, "rescue target");
  }
}

/* Whether a rescue clause takes the exception ev of class cls. A clause
   with no class takes a StandardError. */
static int fx_rescue_takes(FxFrame *f, const pm_rescue_node_t *r, const char *cls, sp_RbVal ev) {
  /* Fiber#kill unwinds with this: ensure clauses run, no rescue takes it */
  if (!strcmp(cls, "FiberKillSignal")) return 0;
  if (r->exceptions.size == 0) return sp_exc_is_standard_error(cls);
  sp_RbVal t = sp_box_nil();
  SP_GC_ROOT_RBVAL(t);
  for (size_t i = 0; i < r->exceptions.size; i++) {
    const pm_node_t *x = r->exceptions.nodes[i];
    if (PM_NODE_TYPE_P(x, PM_SPLAT_NODE)) {
      t = fx_eval(f, ((const pm_splat_node_t *)x)->expression);
      for (sp_int j = 0; fx_is_array_value(t) && j < sp_poly_length(t); j++)
        if (fx_exc_matches(cls, ev, sp_poly_arr_get(t, j))) return 1;
      continue;
    }
    t = fx_eval(f, x);
    if (fx_exc_matches(cls, ev, t)) return 1;
  }
  return 0;
}

/* The rescue clauses for a caught exception: the first that takes it
   runs, else the exception goes on. Sets *retry for a retry. */
static sp_RbVal fx_rescue_run(FxFrame *f, const pm_rescue_node_t *r, FxCaught c, int *retry) {
  sp_Exception *e = fx_caught_obj(c);
  SP_GC_ROOT(e);
  sp_RbVal ev = fx_exc_box(e);
  SP_GC_ROOT_RBVAL(ev);
  for (; r; r = r->subsequent) {
    if (!fx_rescue_takes(f, r, c.cls, ev)) continue;
    sp_rescue_push(e);
    if (r->reference) fx_rescue_bind(f, r->reference, ev);
    sp_RbVal v = fx_eval_stmts(f, r->statements);
    sp_rescue_sp--;
    if (L.ctl == FX_CTL_RETRY) {
      L.ctl = FX_CTL_NONE;
      *retry = 1;
    }
    return v;
  }
  c.obj = e;
  fx_reraise(c);
}

/* begin/rescue/else: the body under a handler, then the else clause when
   nothing was raised. A rescue clause runs outside the handler, so a raise
   in it goes to the next handler out. */
static sp_RbVal fx_rescue(FxFrame *f, const pm_begin_node_t *b) {
  SP_GC_SAVE();
  sp_RbVal v = sp_box_nil();
  SP_GC_ROOT_RBVAL(v);
  const int depth = fx_fs()->depth;
retry:
  FX_TRY_PUSH();
  if (setjmp(sp_exc_stack[sp_exc_top - 1]) == 0) {
    v = fx_eval_stmts(f, b->statements);
    sp_exc_top--;
  } else {
    FX_TRY_LANDED(depth);
    if (sp_unwind_kind != SP_UNWIND_NONE) sp_unwind_resume();
    FxCaught c = fx_caught();
    int again = 0;
    v = fx_rescue_run(f, b->rescue_clause, c, &again);
    if (again) goto retry;
    return v;
  }
  if (L.ctl == FX_CTL_NONE && b->else_clause) v = fx_eval(f, (const pm_node_t *)b->else_clause);
  return v;
}

/* begin/ensure: the ensure clause runs after the body, after a jump out of
   it, and after a raise, which then goes on. A jump in the ensure clause
   takes the place of the jump or the raise that was passing. */
static sp_RbVal fx_begin(FxFrame *f, const pm_begin_node_t *b) {
  if (!b->ensure_clause) return b->rescue_clause ? fx_rescue(f, b) : fx_eval_stmts(f, b->statements);
  SP_GC_SAVE();
  sp_RbVal v = sp_box_nil();
  SP_GC_ROOT_RBVAL(v);
  const int depth = fx_fs()->depth;
  FxCaught c = { NULL, NULL, NULL };
  sp_Exception *exc = NULL;
  SP_GC_ROOT(exc);
  FX_TRY_PUSH();
  if (setjmp(sp_exc_stack[sp_exc_top - 1]) == 0) {
    v = b->rescue_clause ? fx_rescue(f, b) : fx_eval_stmts(f, b->statements);
    sp_exc_top--;
  } else {
    FX_TRY_LANDED(depth);
    if (sp_unwind_kind != SP_UNWIND_NONE) {
      fx_eval_stmts(f, b->ensure_clause->statements);
      sp_unwind_resume();
    }
    c = fx_caught();
    exc = fx_caught_obj(c);
    c.obj = exc;
  }
  /* the jump that passes through the ensure clause */
  FxCtl ctl = L.ctl;
  sp_RbVal ctl_val = L.ctl_val;
  SP_GC_ROOT_RBVAL(ctl_val);
  const void *ctl_target = L.ctl_target;
  L.ctl = FX_CTL_NONE;
  if (exc) sp_rescue_push(exc);
  fx_eval_stmts(f, b->ensure_clause->statements);
  if (exc) sp_rescue_sp--;
  if (L.ctl != FX_CTL_NONE) return sp_box_nil();
  L.ctl = ctl;
  L.ctl_val = ctl_val;
  L.ctl_target = ctl_target;
  if (exc) fx_reraise(c);
  return v;
}

/* Kernel#loop: a StopIteration ends the loop, as Enumerator#next raises
   it at the end, and the loop answers its result. Any other exception
   goes on. */
static sp_RbVal fx_loop(const FxBlock *blk) {
  SP_GC_SAVE();
  const int depth = fx_fs()->depth;
  FX_TRY_PUSH();
  if (setjmp(sp_exc_stack[sp_exc_top - 1]) == 0) {
    for (;;) {
      fx_yield(blk, 0, NULL);
      if (L.ctl != FX_CTL_NONE) break;
    }
    sp_exc_top--;
    return sp_box_nil();
  }
  FX_TRY_LANDED(depth);
  if (sp_unwind_kind != SP_UNWIND_NONE) sp_unwind_resume();
  FxCaught c = fx_caught();
  if (!sp_exc_cls_matches(c.cls, "StopIteration")) fx_reraise(c);
  return sp_exc_result(fx_caught_obj(c));
}

/* Kernel#catch: the block under a catch entry, as the code that spinel
   emits does (src/codegen_call_kernel.c). A throw of the tag lands here,
   and the catch answers the value of the throw. With no tag, a new Object
   is the tag. The landing restores the GC roots, the frame stack, and the
   exception that an ensure clause had in flight when it threw. */
static sp_RbVal fx_catch(int argc, sp_RbVal *argv, const FxBlock *blk) {
  SP_GC_SAVE();
  sp_RbVal tag = argc ? argv[0] : fx_new_object(L.object);
  SP_GC_ROOT_RBVAL(tag);
  sp_RbVal v = sp_box_nil();
  SP_GC_ROOT_RBVAL(v);
  const int depth = fx_fs()->depth;
  void *const cause = sp_inflight_cause;
  const int rescue_sp = sp_rescue_sp;
  sp_catch_check_depth();
  const int top = sp_catch_top;
  sp_catch_tag[top] = sp_catch_tag_of(tag, &sp_catch_tag_kind[top]);
  sp_catch_val[top] = sp_box_nil();
  sp_catch_exc_top[top] = sp_exc_top;
  sp_catch_rootmark[top] = sp_gc_nroots;
  sp_catch_top++;
  if (setjmp(sp_catch_stack[top]) == 0) {
    v = fx_yield(blk, 1, &tag);
    sp_catch_top = top;
    return v;
  }
  sp_catch_top = top;
  sp_gc_nroots = sp_catch_rootmark[top];
  sp_inflight_cause = cause;
  sp_rescue_sp = rescue_sp;
  fx_fs()->depth = depth;
  L.ctl = FX_CTL_NONE;
  L.kwhash = NULL;
  return sp_catch_val[top];
}

/* expr rescue fallback: a StandardError from expr gives fallback. */
static sp_RbVal fx_rescue_modifier(FxFrame *f, const pm_rescue_modifier_node_t *n) {
  SP_GC_SAVE();
  sp_RbVal v = sp_box_nil();
  SP_GC_ROOT_RBVAL(v);
  const int depth = fx_fs()->depth;
  FX_TRY_PUSH();
  if (setjmp(sp_exc_stack[sp_exc_top - 1]) == 0) {
    v = fx_eval(f, n->expression);
    sp_exc_top--;
    return v;
  }
  FX_TRY_LANDED(depth);
  if (sp_unwind_kind != SP_UNWIND_NONE) sp_unwind_resume();
  FxCaught c = fx_caught();
  if (!sp_exc_is_standard_error(c.cls) || !strcmp(c.cls, "FiberKillSignal")) fx_reraise(c);
  /* $! is the exception while the fallback runs */
  sp_Exception *e = fx_caught_obj(c);
  SP_GC_ROOT(e);
  sp_rescue_push(e);
  v = fx_eval(f, n->rescue_expression);
  sp_rescue_sp--;
  return v;
}

/* ---- defined? ---- */

/* Evaluates n for defined?: 0 when it raises a StandardError, which
   defined? answers as nil. */
static int fx_defined_eval(FxFrame *f, const pm_node_t *n, sp_RbVal *out) {
  SP_GC_SAVE();
  const int depth = fx_fs()->depth;
  FX_TRY_PUSH();
  if (setjmp(sp_exc_stack[sp_exc_top - 1]) == 0) {
    *out = fx_eval(f, n);
    sp_exc_top--;
    return L.ctl == FX_CTL_NONE;
  }
  FX_TRY_LANDED(depth);
  if (sp_unwind_kind != SP_UNWIND_NONE) sp_unwind_resume();
  FxCaught c = fx_caught();
  if (!sp_exc_is_standard_error(c.cls) || !strcmp(c.cls, "FiberKillSignal")) fx_reraise(c);
  return 0;
}

/* The globals that are there with no assignment. */
static int fx_gvar_builtin(const char *g) {
  static const char *const names[] = { "$!", "$~", "$_", "$stdout", "$stderr", "$stdin", "$/", "$?", "$$",
                                       "$0", "$PROGRAM_NAME", "$*", "$LOAD_PATH", "$LOADED_FEATURES",
                                       "$VERBOSE", "$DEBUG", "$,", "$;", "$\\", "$@", NULL };
  for (int i = 0; names[i]; i++) if (!strcmp(g, names[i])) return 1;
  return 0;
}

static const char *fx_defined(FxFrame *f, const pm_node_t *n);

/* Whether each of the nodes is defined. */
static int fx_defined_all(FxFrame *f, const pm_node_list_t *list) {
  for (size_t i = 0; i < list->size; i++) {
    const pm_node_t *e = list->nodes[i];
    if (PM_NODE_TYPE_P(e, PM_SPLAT_NODE)) e = ((const pm_splat_node_t *)e)->expression;
    if (e && !fx_defined(f, e)) return 0;
  }
  return 1;
}

/* Whether a call with no receiver reaches a method: one of the program, a
   builtin one, a method of Kernel, or a method of Module (and Class) when
   self is a module (or a class). */
/* An instance of a subclass of BasicObject has the methods of its class
   and those of BasicObject, but not those of Kernel. */
static int fx_defined_basic(sp_RbVal self, sp_RbVal name, int priv, int *hit) {
  if (fx_is_a(self, L.object)) return 0;
  static const char *const basic[] = { "!", "!=", "==", "equal?", "instance_eval", "instance_exec", "__send__",
                                       "__id__", NULL };
  const FxMethod *m = fx_find_method(fx_class_of(self), (sp_sym)name.v.i);
  *hit = m && (priv || m->vis == FX_VIS_PUBLIC);
  for (int i = 0; !*hit && basic[i]; i++) *hit = !strcmp(sp_sym_to_s((sp_sym)name.v.i), basic[i]);
  return 1;
}

static int fx_defined_fcall(sp_RbVal self, sp_RbVal name) {
  int hit;
  if (fx_defined_basic(self, name, 1, &hit)) return hit;
  const char *n = sp_sym_to_s((sp_sym)name.v.i);
  FxClass *k = fx_class_value(self);
  /* a module has no superclass, new or allocate */
  if (k && k->is_module && (!strcmp(n, "superclass") || !strcmp(n, "new") || !strcmp(n, "allocate"))) return 0;
  if (sp_poly_truthy(fx_respond_to(self, name, sp_box_bool(1)))) return 1;
  if (fx_object_method_name(n)) return 1;
  if (!k) return 0;
  static const char *const mod[] = {
    "name", "to_s", "inspect", "ancestors", "attr", "attr_accessor", "attr_reader", "attr_writer",
    "private", "public", "protected", "module_function", "private_constant", "public_constant",
    "private_class_method", "public_class_method", "define_method", "alias_method", "remove_method",
    "undef_method", "instance_method", "public_instance_method", "method_defined?", "instance_methods",
    "include", "include?", "prepend", "extend", "const_get", "const_set", "const_defined?", "constants",
    "class_eval", "module_eval", "class_exec", "module_exec", "class_variable_get", "class_variable_set",
    "class_variable_defined?", "class_variables", "remove_class_variable", NULL };
  static const char *const cls[] = { "new", "allocate", "superclass", "subclasses", NULL };
  for (int i = 0; mod[i]; i++) if (!strcmp(n, mod[i])) return strcmp(n, "module_function") || k->is_module;
  if (!k->is_module)
    for (int i = 0; cls[i]; i++) if (!strcmp(n, cls[i])) return 1;
  return 0;
}

/* The method of the running frame, through the frames of its blocks. */
static const FxMethod *fx_frame_method(const FxFrame *f) {
  for (; f; f = f->parent) if (f->method) return f->method;
  return NULL;
}

/* What defined?(n) answers, as CRuby's defined_expr: the kind of the
   expression, or NULL (nil) when a name in it is not defined. A receiver
   runs; a StandardError that it raises answers nil. */
static const char *fx_defined(FxFrame *f, const pm_node_t *n) {
  switch (PM_NODE_TYPE(n)) {
  case PM_NIL_NODE:
    return "nil";
  case PM_TRUE_NODE:
    return "true";
  case PM_FALSE_NODE:
    return "false";
  case PM_SELF_NODE:
    return "self";
  case PM_PARENTHESES_NODE: {
    const pm_node_t *b = ((const pm_parentheses_node_t *)n)->body;
    if (!b) return "nil";
    if (b && PM_NODE_TYPE_P(b, PM_STATEMENTS_NODE) && ((const pm_statements_node_t *)b)->body.size == 1)
      return fx_defined(f, ((const pm_statements_node_t *)b)->body.nodes[0]);
    return "expression";
  }
  case PM_ARRAY_NODE:
    return fx_defined_all(f, &((const pm_array_node_t *)n)->elements) ? "expression" : NULL;
  case PM_LOCAL_VARIABLE_READ_NODE:
  case PM_IT_LOCAL_VARIABLE_READ_NODE:
    return "local-variable";
  case PM_INSTANCE_VARIABLE_READ_NODE: {
    return fx_ivar_defined(f->self, fx_sym(((const pm_instance_variable_read_node_t *)n)->name)) ? "instance-variable"
                                                                                                 : NULL;
  }
  case PM_CLASS_VARIABLE_READ_NODE: {
    if (!f->cref || f->cref == L.object) return NULL;
    return fx_cvar_find(f->cref, fx_sym(((const pm_class_variable_read_node_t *)n)->name), NULL) ? "class variable"
                                                                                                 : NULL;
  }
  case PM_GLOBAL_VARIABLE_READ_NODE: {
    const char *g = fx_gvar_canon(sp_sym_to_s(fx_sym(((const pm_global_variable_read_node_t *)n)->name)));
    return fx_gvar_builtin(g) || fx_gvar_slot(sp_sym_intern(g)) ? "global-variable" : NULL;
  }
  case PM_BACK_REFERENCE_READ_NODE:
    return fx_re_last().tag == SP_TAG_NIL ? NULL : "global-variable";
  case PM_NUMBERED_REFERENCE_READ_NODE: {
    sp_RbVal m = fx_re_last();
    if (m.tag == SP_TAG_NIL) return NULL;
    SP_GC_ROOT_RBVAL(m);
    sp_RbVal i = sp_box_int((sp_int)((const pm_numbered_reference_read_node_t *)n)->number);
    return fx_send(m, "[]", 1, &i).tag == SP_TAG_NIL ? NULL : "global-variable";
  }
  case PM_CONSTANT_READ_NODE: {
    /* a lookup with no read, so that a missing constant is not a gap of
       Fluxpinel (fx_const_missing) */
    sp_RbVal v;
    return fx_const_lookup_peek(f->cref, fx_sym(((const pm_constant_read_node_t *)n)->name), &v) ? "constant" : NULL;
  }
  case PM_CONSTANT_PATH_NODE: {
    const pm_constant_path_node_t *p = (const pm_constant_path_node_t *)n;
    FxClass *scope = L.object;
    if (p->parent) {
      if (!fx_defined(f, p->parent)) return NULL;
      sp_RbVal pv;
      if (!fx_defined_eval(f, p->parent, &pv)) return NULL;
      scope = fx_class_value(pv);
      if (!scope) return NULL;
    }
    sp_RbVal v;
    sp_sym name = fx_sym(p->name);
    if (scope == L.object) return fx_const_scoped_peek(L.object, name, &v) ? "constant" : NULL;
    /* A::X looks in A and its ancestors, but not in Object, as CRuby's
       rb_const_defined_from; a builtin class also has its nested ones */
    const FxClass *anc[128];
    int na = fx_ancestors(scope, anc, 128);
    for (int i = 0; i < na; i++)
      if (anc[i] != L.object && fx_const_find(anc[i], name, 0)) return "constant";
    if ((scope->cls_id < 0 || fx_kernel_cls(scope->cls_id)) && fx_const_scoped_peek(scope, name, &v)) return "constant";
    return NULL;
  }
  case PM_CALL_NODE: {
    const pm_call_node_t *c = (const pm_call_node_t *)n;
    if (c->arguments && !fx_defined_all(f, &c->arguments->arguments)) return NULL;
    sp_RbVal name = sp_box_sym(fx_sym(c->name));
    if (!c->receiver) return fx_defined_fcall(f->self, name) ? "method" : NULL;
    if (!fx_defined(f, c->receiver)) return NULL;
    sp_RbVal r;
    if (!fx_defined_eval(f, c->receiver, &r)) return NULL;
    SP_GC_ROOT_RBVAL(r);
    /* a call with a receiver, self.x too, asks for a public method, as CRuby */
    int hit;
    if (fx_defined_basic(r, name, 0, &hit)) return hit ? "method" : NULL;
    return sp_poly_truthy(fx_respond_to(r, name, sp_box_bool(0))) ? "method" : NULL;
  }
  case PM_YIELD_NODE:
    return f->block ? "yield" : NULL;
  case PM_SUPER_NODE:
  case PM_FORWARDING_SUPER_NODE: {
    const FxMethod *m = fx_frame_method(f);
    if (!m) return NULL;
    if (fx_find_super(fx_class_of(f->self), m)) return "super";
    return fx_builtin_responds(f->self, sp_sym_to_s(m->name)) ? "super" : NULL;
  }
  case PM_LOCAL_VARIABLE_WRITE_NODE:
  case PM_LOCAL_VARIABLE_OPERATOR_WRITE_NODE:
  case PM_LOCAL_VARIABLE_OR_WRITE_NODE:
  case PM_LOCAL_VARIABLE_AND_WRITE_NODE:
  case PM_INSTANCE_VARIABLE_WRITE_NODE:
  case PM_INSTANCE_VARIABLE_OPERATOR_WRITE_NODE:
  case PM_INSTANCE_VARIABLE_OR_WRITE_NODE:
  case PM_INSTANCE_VARIABLE_AND_WRITE_NODE:
  case PM_CLASS_VARIABLE_WRITE_NODE:
  case PM_CLASS_VARIABLE_OPERATOR_WRITE_NODE:
  case PM_CLASS_VARIABLE_OR_WRITE_NODE:
  case PM_CLASS_VARIABLE_AND_WRITE_NODE:
  case PM_GLOBAL_VARIABLE_WRITE_NODE:
  case PM_GLOBAL_VARIABLE_OPERATOR_WRITE_NODE:
  case PM_GLOBAL_VARIABLE_OR_WRITE_NODE:
  case PM_GLOBAL_VARIABLE_AND_WRITE_NODE:
  case PM_CONSTANT_WRITE_NODE:
  case PM_CONSTANT_OPERATOR_WRITE_NODE:
  case PM_CONSTANT_OR_WRITE_NODE:
  case PM_CONSTANT_AND_WRITE_NODE:
  case PM_CONSTANT_PATH_WRITE_NODE:
  case PM_CONSTANT_PATH_OPERATOR_WRITE_NODE:
  case PM_CONSTANT_PATH_OR_WRITE_NODE:
  case PM_CONSTANT_PATH_AND_WRITE_NODE:
  case PM_CALL_OPERATOR_WRITE_NODE:
  case PM_CALL_OR_WRITE_NODE:
  case PM_CALL_AND_WRITE_NODE:
  case PM_INDEX_OPERATOR_WRITE_NODE:
  case PM_INDEX_OR_WRITE_NODE:
  case PM_INDEX_AND_WRITE_NODE:
  case PM_MULTI_WRITE_NODE:
    return "assignment";
  default:
    return "expression";
  }
}

/* ---- the evaluator ---- */

static sp_RbVal fx_eval(FxFrame *f, const pm_node_t *n) {
  switch (PM_NODE_TYPE(n)) {
  case PM_STATEMENTS_NODE:
    return fx_eval_stmts(f, (const pm_statements_node_t *)n);
  case PM_PARENTHESES_NODE: {
    const pm_node_t *b = ((const pm_parentheses_node_t *)n)->body;
    return b ? fx_eval(f, b) : sp_box_nil();
  }
  case PM_INTEGER_NODE:
    return fx_integer(&((const pm_integer_node_t *)n)->value);
  case PM_RATIONAL_NODE: {
    /* prism gives the parts reduced: 1.5r is 3/2 */
    const pm_rational_node_t *r = (const pm_rational_node_t *)n;
    sp_RbVal num = fx_integer(&r->numerator);
    SP_GC_ROOT_RBVAL(num);
    sp_RbVal den = fx_integer(&r->denominator);
    SP_GC_ROOT_RBVAL(den);
    /* a part beyond 64 bits makes a big Rational */
    if (num.tag == SP_TAG_BIGINT || den.tag == SP_TAG_BIGINT) {
      sp_Bigint *bn = sp_poly_as_bigint(num);
      SP_GC_ROOT(bn);
      return sp_box_brat(bn, sp_poly_as_bigint(den));
    }
    return sp_box_rational(sp_rational_new(num.v.i, den.v.i));
  }
  case PM_IMAGINARY_NODE: {
    /* 2i, 1.5i and 3ri: the number is the imaginary part, and a Float
       or a Rational makes it Float-classed (a Rational is its Float) */
    sp_RbVal v = fx_eval(f, ((const pm_imaginary_node_t *)n)->numeric);
    int fl = v.tag == SP_TAG_FLT || sp_poly_is_rational(v) ? SP_CPLX_IM_F : 0;
    return sp_box_complex((sp_Complex){ 0.0, sp_poly_to_f_with_rational(v), (unsigned char)fl });
  }
  case PM_FLOAT_NODE:
    return sp_box_float(((const pm_float_node_t *)n)->value);
  case PM_STRING_NODE: {
    const pm_string_t *s = &((const pm_string_node_t *)n)->unescaped;
    return fx_new_string(pm_string_source(s), pm_string_length(s));
  }
  case PM_INTERPOLATED_STRING_NODE:
    return fx_interpolate(f, &((const pm_interpolated_string_node_t *)n)->parts);
  case PM_INTERPOLATED_SYMBOL_NODE: {
    sp_RbVal s = fx_interpolate(f, &((const pm_interpolated_symbol_node_t *)n)->parts);
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
    return fx_send(s, "to_sym", 0, NULL);
  }
  case PM_SYMBOL_NODE: {
    const pm_string_t *s = &((const pm_symbol_node_t *)n)->unescaped;
    return sp_box_sym(fx_sym_intern_n((const char *)pm_string_source(s), pm_string_length(s)));
  }
  case PM_TRUE_NODE: return sp_box_bool(1);
  case PM_FALSE_NODE: return sp_box_bool(0);
  case PM_NIL_NODE: return sp_box_nil();
  case PM_ARRAY_NODE:
    return fx_array(f, (const pm_array_node_t *)n);
  case PM_LOCAL_VARIABLE_READ_NODE: {
    const pm_local_variable_read_node_t *r = (const pm_local_variable_read_node_t *)n;
    return *fx_local(f, n, r->name, r->depth);
  }
  case PM_LOCAL_VARIABLE_WRITE_NODE: {
    const pm_local_variable_write_node_t *w = (const pm_local_variable_write_node_t *)n;
    sp_RbVal v = fx_eval(f, w->value);
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
    *fx_local(f, n, w->name, w->depth) = v;
    return v;
  }
  case PM_LOCAL_VARIABLE_OPERATOR_WRITE_NODE: {
    const pm_local_variable_operator_write_node_t *w = (const pm_local_variable_operator_write_node_t *)n;
    /* x OP= v reads x before v runs, as CRuby does. The operand can make
       a Proc, which moves the slots: the write looks the slot up again. */
    sp_RbVal cur = *fx_local(f, n, w->name, w->depth);
    SP_GC_ROOT_RBVAL(cur);
    sp_RbVal v = fx_eval(f, w->value);
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
    sp_RbVal r = fx_op(cur, w->binary_operator, v);
    return *fx_local(f, n, w->name, w->depth) = r;
  }
  case PM_AND_NODE: {
    const pm_and_node_t *a = (const pm_and_node_t *)n;
    sp_RbVal l = fx_eval(f, a->left);
    if (L.ctl != FX_CTL_NONE || !sp_poly_truthy(l)) return l;
    return fx_eval(f, a->right);
  }
  case PM_OR_NODE: {
    const pm_or_node_t *o = (const pm_or_node_t *)n;
    sp_RbVal l = fx_eval(f, o->left);
    if (L.ctl != FX_CTL_NONE || sp_poly_truthy(l)) return l;
    return fx_eval(f, o->right);
  }
  case PM_IF_NODE: {
    const pm_if_node_t *i = (const pm_if_node_t *)n;
    sp_RbVal c = fx_eval(f, i->predicate);
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
    if (sp_poly_truthy(c)) return fx_eval_stmts(f, i->statements);
    return i->subsequent ? fx_eval(f, i->subsequent) : sp_box_nil();
  }
  case PM_UNLESS_NODE: {
    const pm_unless_node_t *u = (const pm_unless_node_t *)n;
    sp_RbVal c = fx_eval(f, u->predicate);
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
    if (!sp_poly_truthy(c)) return fx_eval_stmts(f, u->statements);
    return u->else_clause ? fx_eval(f, (const pm_node_t *)u->else_clause) : sp_box_nil();
  }
  case PM_ELSE_NODE:
    return fx_eval_stmts(f, ((const pm_else_node_t *)n)->statements);
  case PM_CASE_NODE:
    return fx_case(f, (const pm_case_node_t *)n);
  case PM_CASE_MATCH_NODE:
    return fx_case_match(f, (const pm_case_match_node_t *)n);
  case PM_DEFINED_NODE: {
    const char *k = fx_defined(f, ((const pm_defined_node_t *)n)->value);
    /* CRuby answers a frozen String */
    return k ? sp_box_str(k) : sp_box_nil();
  }
  case PM_MATCH_PREDICATE_NODE: {
    const pm_match_predicate_node_t *m = (const pm_match_predicate_node_t *)n;
    return fx_match(f, m->value, m->pattern, 0);
  }
  case PM_MATCH_REQUIRED_NODE: {
    const pm_match_required_node_t *m = (const pm_match_required_node_t *)n;
    return fx_match(f, m->value, m->pattern, 1);
  }
  case PM_WHILE_NODE: {
    const pm_while_node_t *w = (const pm_while_node_t *)n;
    return fx_while(f, w->predicate, w->statements, 0, PM_NODE_FLAG_P(n, PM_LOOP_FLAGS_BEGIN_MODIFIER));
  }
  case PM_UNTIL_NODE: {
    const pm_until_node_t *u = (const pm_until_node_t *)n;
    return fx_while(f, u->predicate, u->statements, 1, PM_NODE_FLAG_P(n, PM_LOOP_FLAGS_BEGIN_MODIFIER));
  }
  case PM_FOR_NODE:
    return fx_for(f, (const pm_for_node_t *)n);
  case PM_REDO_NODE:
    L.ctl = FX_CTL_REDO;
    L.ctl_val = sp_box_nil();
    return sp_box_nil();
  case PM_RETURN_NODE:
    return fx_jump(f, FX_CTL_RETURN, ((const pm_return_node_t *)n)->arguments);
  case PM_BREAK_NODE:
    return fx_jump(f, FX_CTL_BREAK, ((const pm_break_node_t *)n)->arguments);
  case PM_NEXT_NODE:
    return fx_jump(f, FX_CTL_NEXT, ((const pm_next_node_t *)n)->arguments);
  case PM_DEF_NODE: {
    const pm_def_node_t *d = (const pm_def_node_t *)n;
    fx_define(f, d);
    return sp_box_sym(fx_sym(d->name));
  }
  case PM_ALIAS_METHOD_NODE: {
    const pm_alias_method_node_t *a = (const pm_alias_method_node_t *)n;
    if (!PM_NODE_TYPE_P(a->new_name, PM_SYMBOL_NODE) || !PM_NODE_TYPE_P(a->old_name, PM_SYMBOL_NODE))
      fx_unsupported(n, "alias of an interpolated name");
    sp_RbVal to = fx_eval(f, a->new_name), from = fx_eval(f, a->old_name);
    fx_alias(f->definee ? f->definee : f->cref, f->singleton_defs == 1 || f->singleton_defs == 4, (sp_sym)to.v.i,
             (sp_sym)from.v.i);
    return sp_box_nil();
  }
  case PM_CALL_NODE:
    return fx_call(f, (const pm_call_node_t *)n);
  case PM_CALL_OPERATOR_WRITE_NODE:
    return fx_call_op_write(f, (const pm_call_operator_write_node_t *)n);
  case PM_INDEX_OPERATOR_WRITE_NODE:
    return fx_index_op_write(f, (const pm_index_operator_write_node_t *)n);
  case PM_MULTI_WRITE_NODE:
    return fx_multi_write(f, (const pm_multi_write_node_t *)n);
  case PM_SUPER_NODE: {
    const pm_super_node_t *s = (const pm_super_node_t *)n;
    return fx_super(f, n, s->arguments, 0, s->block);
  }
  case PM_FORWARDING_SUPER_NODE:
    return fx_super(f, n, NULL, 1, (const pm_node_t *)((const pm_forwarding_super_node_t *)n)->block);
  case PM_YIELD_NODE: {
    SP_GC_SAVE();
    const pm_arguments_node_t *args = ((const pm_yield_node_t *)n)->arguments;
    if (!f->block) sp_raise_cls("LocalJumpError", "no block given (yield)");
    FX_ARGS(f, args, argc, argv, kw, ok);
    if (!ok) return sp_box_nil();
    L.kwhash = kw;
    sp_RbVal v = fx_yield(f->block, argc, argv);
    L.kwhash = NULL;
    return v;
  }
  case PM_LAMBDA_NODE: {
    FxBlock b = { .node = n, .home = f, .serial = f->serial, .fs = f->fs, .depth = f->depth, .lambda = 1 };
    return fx_block_proc(&b);
  }
  case PM_IT_LOCAL_VARIABLE_READ_NODE:
    return *f->it;
  case PM_RANGE_NODE:
    return fx_range(f, (const pm_range_node_t *)n);
  case PM_HASH_NODE:
    return fx_hash_lit(f, &((const pm_hash_node_t *)n)->elements);
  case PM_KEYWORD_HASH_NODE:
    return fx_hash_lit(f, &((const pm_keyword_hash_node_t *)n)->elements);
  case PM_INDEX_OR_WRITE_NODE:
  case PM_INDEX_AND_WRITE_NODE:
    return fx_index_logic_write(f, n);
  case PM_CALL_OR_WRITE_NODE:
  case PM_CALL_AND_WRITE_NODE:
    return fx_call_logic_write(f, n);
  case PM_SELF_NODE:
    return f->self;
  case PM_CLASS_NODE: {
    const pm_class_node_t *c = (const pm_class_node_t *)n;
    return fx_class_def(f, c->constant_path, c->superclass, c->body, &c->locals, 0);
  }
  case PM_MODULE_NODE: {
    const pm_module_node_t *m = (const pm_module_node_t *)n;
    return fx_class_def(f, m->constant_path, NULL, m->body, &m->locals, 1);
  }
  case PM_SINGLETON_CLASS_NODE:
    return fx_singleton_class(f, (const pm_singleton_class_node_t *)n);
  case PM_CONSTANT_READ_NODE:
  case PM_CONSTANT_PATH_NODE:
    return fx_const_read(f, n);
  case PM_CONSTANT_WRITE_NODE: {
    const pm_constant_write_node_t *w = (const pm_constant_write_node_t *)n;
    sp_RbVal v = fx_eval(f, w->value);
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
    fx_const_set(f->cref, fx_sym(w->name), v);
    return v;
  }
  case PM_CONSTANT_PATH_WRITE_NODE: {
    const pm_constant_path_write_node_t *w = (const pm_constant_path_write_node_t *)n;
    FxClass *scope;
    sp_sym name;
    fx_const_target(f, (const pm_node_t *)w->target, &scope, &name);
    sp_RbVal v = fx_eval(f, w->value);
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
    fx_const_set(scope, name, v);
    return v;
  }
  case PM_CONSTANT_OPERATOR_WRITE_NODE:
  case PM_CONSTANT_OR_WRITE_NODE:
  case PM_CONSTANT_AND_WRITE_NODE:
  case PM_CONSTANT_PATH_OPERATOR_WRITE_NODE:
  case PM_CONSTANT_PATH_OR_WRITE_NODE:
  case PM_CONSTANT_PATH_AND_WRITE_NODE:
    return fx_const_logic_write(f, n);
  case PM_CLASS_VARIABLE_READ_NODE:
    return fx_cvar_get(fx_cvar_base(f->cref), fx_sym(((const pm_class_variable_read_node_t *)n)->name));
  case PM_CLASS_VARIABLE_WRITE_NODE: {
    const pm_class_variable_write_node_t *w = (const pm_class_variable_write_node_t *)n;
    FxClass *c = fx_cvar_base(f->cref);
    sp_RbVal v = fx_eval(f, w->value);
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
    fx_cvar_set(c, fx_sym(w->name), v);
    return v;
  }
  case PM_CLASS_VARIABLE_OPERATOR_WRITE_NODE: {
    const pm_class_variable_operator_write_node_t *w = (const pm_class_variable_operator_write_node_t *)n;
    FxClass *c = fx_cvar_base(f->cref);
    sp_sym name = fx_sym(w->name);
    sp_RbVal cur = fx_cvar_get(c, name);
    SP_GC_ROOT_RBVAL(cur);
    sp_RbVal v = fx_eval(f, w->value);
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
    sp_RbVal nv = fx_op(cur, w->binary_operator, v);
    fx_cvar_set(c, name, nv);
    return nv;
  }
  case PM_CLASS_VARIABLE_OR_WRITE_NODE:
  case PM_CLASS_VARIABLE_AND_WRITE_NODE: {
    /* @@x ||= v reads an undefined @@x as nil, as CRuby does (defined?) */
    int is_or = PM_NODE_TYPE_P(n, PM_CLASS_VARIABLE_OR_WRITE_NODE);
    pm_constant_id_t id = is_or ? ((const pm_class_variable_or_write_node_t *)n)->name
                                : ((const pm_class_variable_and_write_node_t *)n)->name;
    const pm_node_t *value = is_or ? ((const pm_class_variable_or_write_node_t *)n)->value
                                   : ((const pm_class_variable_and_write_node_t *)n)->value;
    FxClass *c = fx_cvar_base(f->cref);
    sp_sym name = fx_sym(id);
    FxConst *k = fx_cvar_find(c, name, NULL);
    if (!k && !is_or) fx_cvar_get(c, name);
    sp_RbVal cur = k ? k->value : sp_box_nil();
    if (sp_poly_truthy(cur) == is_or) return cur;
    sp_RbVal v = fx_eval(f, value);
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
    fx_cvar_set(c, name, v);
    return v;
  }
  case PM_INSTANCE_VARIABLE_READ_NODE:
    return fx_ivar_get_c(f->self, fx_sym(((const pm_instance_variable_read_node_t *)n)->name), &fx_side(n)->ivar);
  case PM_INSTANCE_VARIABLE_WRITE_NODE: {
    const pm_instance_variable_write_node_t *w = (const pm_instance_variable_write_node_t *)n;
    sp_RbVal v = fx_eval(f, w->value);
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
    fx_ivar_set_c(f->self, fx_sym(w->name), v, &fx_side(n)->ivar);
    return v;
  }
  case PM_INSTANCE_VARIABLE_OPERATOR_WRITE_NODE: {
    const pm_instance_variable_operator_write_node_t *w = (const pm_instance_variable_operator_write_node_t *)n;
    sp_sym name = fx_sym(w->name);
    FxIvarCache *ic = &fx_side(n)->ivar;
    /* @x OP= v reads @x before v runs, as CRuby does */
    sp_RbVal cur = fx_ivar_get_c(f->self, name, ic);
    SP_GC_ROOT_RBVAL(cur);
    sp_RbVal v = fx_eval(f, w->value);
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
    sp_RbVal nv = fx_op(cur, w->binary_operator, v);
    fx_ivar_set_c(f->self, name, nv, ic);
    return nv;
  }
  case PM_INSTANCE_VARIABLE_OR_WRITE_NODE: {
    const pm_instance_variable_or_write_node_t *w = (const pm_instance_variable_or_write_node_t *)n;
    sp_sym name = fx_sym(w->name);
    FxIvarCache *ic = &fx_side(n)->ivar;
    sp_RbVal cur = fx_ivar_get_c(f->self, name, ic);
    if (sp_poly_truthy(cur)) return cur;
    sp_RbVal v = fx_eval(f, w->value);
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
    fx_ivar_set_c(f->self, name, v, ic);
    return v;
  }
  case PM_INSTANCE_VARIABLE_AND_WRITE_NODE: {
    const pm_instance_variable_and_write_node_t *w = (const pm_instance_variable_and_write_node_t *)n;
    sp_sym name = fx_sym(w->name);
    FxIvarCache *ic = &fx_side(n)->ivar;
    sp_RbVal cur = fx_ivar_get_c(f->self, name, ic);
    if (!sp_poly_truthy(cur)) return cur;
    sp_RbVal v = fx_eval(f, w->value);
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
    fx_ivar_set_c(f->self, name, v, ic);
    return v;
  }
  case PM_LOCAL_VARIABLE_OR_WRITE_NODE: {
    const pm_local_variable_or_write_node_t *w = (const pm_local_variable_or_write_node_t *)n;
    sp_RbVal cur = *fx_local(f, n, w->name, w->depth);
    if (sp_poly_truthy(cur)) return cur;
    sp_RbVal v = fx_eval(f, w->value);
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
    *fx_local(f, n, w->name, w->depth) = v;   /* after: the slots can move */
    return v;
  }
  case PM_LOCAL_VARIABLE_AND_WRITE_NODE: {
    const pm_local_variable_and_write_node_t *w = (const pm_local_variable_and_write_node_t *)n;
    sp_RbVal cur = *fx_local(f, n, w->name, w->depth);
    if (!sp_poly_truthy(cur)) return cur;
    sp_RbVal v = fx_eval(f, w->value);
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
    *fx_local(f, n, w->name, w->depth) = v;   /* after: the slots can move */
    return v;
  }
  case PM_BEGIN_NODE:
    return fx_begin(f, (const pm_begin_node_t *)n);
  case PM_RESCUE_MODIFIER_NODE:
    return fx_rescue_modifier(f, (const pm_rescue_modifier_node_t *)n);
  case PM_RETRY_NODE:
    L.ctl = FX_CTL_RETRY;
    L.ctl_val = sp_box_nil();
    return sp_box_nil();
  case PM_GLOBAL_VARIABLE_READ_NODE: {
    /* $!: the exception that a rescue handles; $~: the last match */
    const char *g = sp_sym_to_s(fx_sym(((const pm_global_variable_read_node_t *)n)->name));
    if (!strcmp(g, "$~")) return fx_re_last();
    if (strcmp(g, "$!")) return fx_gvar_eval(f, n);
    return sp_rescue_sp > 0 ? fx_exc_box((sp_Exception *)sp_exc_handling[sp_rescue_sp - 1]) : sp_box_nil();
  }
  case PM_GLOBAL_VARIABLE_WRITE_NODE:
  case PM_GLOBAL_VARIABLE_OPERATOR_WRITE_NODE:
  case PM_GLOBAL_VARIABLE_OR_WRITE_NODE:
  case PM_GLOBAL_VARIABLE_AND_WRITE_NODE:
    return fx_gvar_eval(f, n);
  case PM_REGULAR_EXPRESSION_NODE:
  case PM_INTERPOLATED_REGULAR_EXPRESSION_NODE:
    return fx_re_literal(f, n);
  case PM_SOURCE_FILE_NODE: {
    const pm_string_t *s = &((const pm_source_file_node_t *)n)->filepath;
    return fx_new_string(pm_string_source(s), pm_string_length(s));
  }
  case PM_SOURCE_LINE_NODE:
    return sp_box_int(fx_line(n));
  case PM_MATCH_WRITE_NODE:
    return fx_re_match_write(f, (const pm_match_write_node_t *)n);
  case PM_NUMBERED_REFERENCE_READ_NODE:
    return fx_re_nth((int)((const pm_numbered_reference_read_node_t *)n)->number);
  case PM_BACK_REFERENCE_READ_NODE:
    return fx_re_backref(sp_sym_to_s(fx_sym(((const pm_back_reference_read_node_t *)n)->name)));
  default:
    fx_unsupported(n, "node");
  }
}
