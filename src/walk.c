/* walk.c -- the AST evaluator of Lupe.

   The walker evaluates the prism AST directly. Every value is an sp_RbVal.
   A C local that holds a value across a call that can allocate is rooted
   with SP_GC_ROOT_RBVAL, as the code that spinel generates does. */

#include "lupe.h"

static int lupe_line(const pm_node_t *n) {
  return pm_newline_list_line_column(&L.parser.newline_list, n->location.start, L.parser.start_line).line;
}

static void lupe_unsupported(const pm_node_t *n, const char *what) {
  fprintf(stderr, "lupe: %s:%d: unsupported %s (%s)\n", L.path, lupe_line(n), what,
          pm_node_type_to_str(PM_NODE_TYPE(n)));
  exit(2);
}

/* A semantics that Lupe does not model yet, met at run time with no node
   at hand. It stops as lupe_unsupported does, so the oracle counts the
   test as a gap of Lupe, not as a wrong answer. */
static void lupe_gap(const char *what) {
  fprintf(stderr, "lupe: %s: unsupported %s (runtime)\n", L.path, what);
  /* a gap can come on a green thread: end at once, without the exit
     handlers of the runtime, which expect the main thread */
  fflush(stdout);
  fflush(stderr);
  _exit(2);
}

static sp_sym lupe_sym(pm_constant_id_t id) {
  const pm_constant_t *c = pm_constant_pool_id_to_constant(&L.parser.constant_pool, id);
  return sp_sym_intern_n((const char *)c->start, c->length);
}

/* The slot of a local variable. depth counts the block scopes between the
   reference and the scope that owns the variable. */
static sp_RbVal *lupe_local(LupeFrame *f, const pm_node_t *n, pm_constant_id_t name, uint32_t depth) {
  for (uint32_t d = 0; d < depth; d++)
    if (!(f = f->parent)) lupe_unsupported(n, "local variable of an outer scope");
  for (size_t i = 0; i < f->names->size; i++)
    if (f->names->ids[i] == name) return &f->locals[i];
  lupe_unsupported(n, "local variable with no slot");
}

/* Declares the slots of a frame on the C stack and roots each one, and
   sets the self and the lexical class of the scope. Three slots follow
   the locals: `it`, the anonymous *rest and the anonymous **kwrest. The
   roots stay until the enclosing SP_GC_SAVE scope ends. */
#define LUPE_FRAME(f, namelist, self_, cref_)                                     \
  size_t f##_n = (namelist)->size;                                                \
  sp_RbVal f##_slots[f##_n + 3];                                                  \
  for (size_t f##_i = 0; f##_i < f##_n + 3; f##_i++) {                            \
    f##_slots[f##_i] = sp_box_nil();                                               \
    _sp_gc_root_push((void **)((uintptr_t)&f##_slots[f##_i] | (uintptr_t)1));      \
  }                                                                               \
  LupeFrame f __attribute__((cleanup(lupe_frame_exit))) = {                       \
    .locals = f##_slots, .names = (namelist), .self = (self_), .cref = (cref_),   \
    .it = &f##_slots[f##_n], .anon = &f##_slots[f##_n + 1], .serial = ++L.serial, \
    .depth = lupe_frame_enter(L.serial), .fs = lupe_fs() }

/* The frame stacks tell whether the home frame of a block is live. They
   are on the heap, so a check never reads the stack memory of a frame
   that has ended. */

static LupeFrameStack *lupe_fs_new(const void *fiber) {
  LupeFrameStack *s = calloc(1, sizeof *s);
  s->fiber = fiber;
  return s;
}

/* The table slot of a fiber, in a table with open addressing. */
static int lupe_fs_slot(const void *fiber) {
  int mask = L.fstacks_cap - 1, i = (int)(((uintptr_t)fiber >> 4) & (uintptr_t)mask);
  while (L.fstacks[i].fiber && L.fstacks[i].fiber != fiber) i = (i + 1) & mask;
  return i;
}

static void lupe_fs_put(const void *fiber, LupeFrameStack *s) {
  if (2 * (L.nfstacks + 1) > L.fstacks_cap) {
    int ocap = L.fstacks_cap;
    __typeof__(L.fstacks) old = L.fstacks;
    L.fstacks_cap = ocap ? ocap * 2 : 64;
    L.fstacks = calloc((size_t)L.fstacks_cap, sizeof *L.fstacks);
    for (int i = 0; i < ocap; i++)
      if (old[i].fiber) L.fstacks[lupe_fs_slot(old[i].fiber)] = old[i];
    free(old);
  }
  int i = lupe_fs_slot(fiber);
  if (!L.fstacks[i].fiber) L.nfstacks++;
  L.fstacks[i].fiber = fiber;
  L.fstacks[i].fs = s;
}

/* The frame stack of the running fiber. A fiber that Lupe started got one
   in lupe_fiber_body; any other is a root fiber (the main program, or a
   root that the scheduler adopted), whose frames are the main ones. */
static LupeFrameStack *lupe_fs(void) {
  const void *cur = sp_fiber_current;
  if (L.fs->fiber == cur) return L.fs;
  int i = L.fstacks_cap ? lupe_fs_slot(cur) : 0;
  if (L.fstacks_cap && L.fstacks[i].fiber) {
    L.fs = L.fstacks[i].fs;
  } else {
    lupe_fs_put(cur, L.main_fs);
    L.fs = L.main_fs;
  }
  return L.fs;
}

static void lupe_fs_init(void) {
  L.main_fs = L.fs = lupe_fs_new(sp_fiber_current);
}

static int lupe_frame_enter(uint64_t serial) {
  LupeFrameStack *s = lupe_fs();
  if (s->depth == s->cap) {
    s->cap = s->cap ? s->cap * 2 : 256;
    s->serial = realloc(s->serial, sizeof *s->serial * (size_t)s->cap);
  }
  s->serial[s->depth] = serial;
  return s->depth++;
}

/* A frame that ends pops its entry. A raise skips this cleanup; the
   rescue that lands cuts the stack back to its own depth. */
static void lupe_frame_exit(LupeFrame *f) {
  f->fs->depth = f->depth;
}

static int lupe_frame_live(const LupeBlock *b) {
  return b->depth < b->fs->depth && b->fs->serial[b->depth] == b->serial;
}

/* ---- methods ---- */

static void lupe_define(LupeFrame *f, const pm_def_node_t *def) {
  sp_sym name = lupe_sym(def->name);
  if (def->receiver) {
    if (!PM_NODE_TYPE_P(def->receiver, PM_SELF_NODE) || f->self.tag != SP_TAG_CLASS)
      lupe_unsupported((const pm_node_t *)def, "singleton method on an object");
    LupeClass *k = lupe_class_value(f->self);
    lupe_define_method(k, lupe_method_new(LUPE_M_DEF, name, def, 0, k), 1);
    return;
  }
  LupeClass *k = f->cref;
  LupeMethod *m = lupe_method_new(LUPE_M_DEF, name, def, 0, k);
  /* 1: inside class << self; 2: after module_function */
  if (f->singleton_defs != 1) lupe_define_method(k, m, 0);
  if (f->singleton_defs) lupe_define_method(k, m, 1);
}

static __attribute__((noreturn)) void lupe_arity_error(int argc, int nreq, int nopt, int rest) {
  if (rest) sp_raise_cls("ArgumentError", sp_sprintf("wrong number of arguments (given %d, expected %d+)", argc, nreq));
  if (nopt)
    sp_raise_cls("ArgumentError",
                 sp_sprintf("wrong number of arguments (given %d, expected %d..%d)", argc, nreq, nreq + nopt));
  sp_raise_cls("ArgumentError", sp_sprintf("wrong number of arguments (given %d, expected %d)", argc, nreq));
}

static int lupe_is_array_value(sp_RbVal v) {
  return v.tag == SP_TAG_OBJ && sp_poly_is_array_kind(v.cls_id);
}

/* Binds one value to a required parameter or to a pattern `(a, *b, c)`.
   A pattern takes the elements of an Array; any other value is one element. */
static void lupe_bind_target(LupeFrame *f, const pm_node_t *p, sp_RbVal v) {
  if (PM_NODE_TYPE_P(p, PM_REQUIRED_PARAMETER_NODE)) {
    /* a repeated `_x` takes its argument, but the name reads the first one */
    if (PM_NODE_FLAG_P(p, PM_PARAMETER_FLAGS_REPEATED_PARAMETER)) return;
    *lupe_local(f, p, ((const pm_required_parameter_node_t *)p)->name, 0) = v;
    return;
  }
  if (!PM_NODE_TYPE_P(p, PM_MULTI_TARGET_NODE)) lupe_unsupported(p, "parameter pattern");
  SP_GC_ROOT_RBVAL(v);
  const pm_multi_target_node_t *mt = (const pm_multi_target_node_t *)p;
  sp_int len = lupe_is_array_value(v) ? sp_poly_length(v) : 1;
  sp_int nl = (sp_int)mt->lefts.size, nr = (sp_int)mt->rights.size;
#define LUPE_ELT(i) (lupe_is_array_value(v) ? sp_poly_arr_get(v, (i)) : (i) == 0 ? v : sp_box_nil())
  for (sp_int i = 0; i < nl; i++) lupe_bind_target(f, mt->lefts.nodes[i], i < len ? LUPE_ELT(i) : sp_box_nil());
  sp_int rstart = len - nr > nl ? len - nr : nl;
  if (mt->rest && PM_NODE_TYPE_P(mt->rest, PM_SPLAT_NODE)) {
    const pm_node_t *e = ((const pm_splat_node_t *)mt->rest)->expression;
    if (e) {
      sp_PolyArray *a = sp_PolyArray_new();
      SP_GC_ROOT(a);
      for (sp_int i = nl; i < rstart; i++) sp_PolyArray_push(a, LUPE_ELT(i));
      lupe_bind_target(f, e, sp_box_poly_array(a));
    }
  }
  for (sp_int i = 0; i < nr; i++)
    lupe_bind_target(f, mt->rights.nodes[i], rstart + i < len ? LUPE_ELT(rstart + i) : sp_box_nil());
#undef LUPE_ELT
}

static sp_RbVal lupe_block_proc(const LupeBlock *b);

/* ": a, :b" for an ArgumentError message. */
static const char *lupe_key_list(sp_PolyArray *keys) {
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
static void lupe_bind_keywords(LupeFrame *f, const pm_parameters_node_t *ps, sp_RbVal kw) {
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
    sp_RbVal key = sp_box_sym(lupe_sym(name));
    sp_RbVal *slot = lupe_local(f, p, name, 0);
    if (h && sp_PolyPolyHash_has_key(h, key)) {
      *slot = sp_PolyPolyHash_get(h, key);
      sp_PolyArray_push(used, key);
    } else if (req) {
      sp_PolyArray_push(missing, key);
    } else {
      *slot = lupe_eval(f, ((const pm_optional_keyword_parameter_node_t *)p)->value);
    }
  }
  if (missing->len)
    sp_raise_cls("ArgumentError", sp_sprintf("missing keyword%s: %s", missing->len > 1 ? "s" : "",
                                             lupe_key_list(missing)));
  sp_PolyPolyHash *rest = sp_PolyPolyHash_new();
  SP_GC_ROOT(rest);
  for (sp_int i = 0; h && i < h->len; i++) {
    sp_RbVal k = h->keys[h->order[i]];
    if (!sp_PolyArray_include(used, k)) sp_PolyPolyHash_set(rest, k, h->vals[h->order[i]]);
  }
  const pm_node_t *kr = ps->keyword_rest;
  if (kr && PM_NODE_TYPE_P(kr, PM_KEYWORD_REST_PARAMETER_NODE)) {
    pm_constant_id_t rn = ((const pm_keyword_rest_parameter_node_t *)kr)->name;
    *(rn ? lupe_local(f, kr, rn, 0) : &f->anon[1]) = sp_box_obj(rest, SP_BUILTIN_POLY_POLY_HASH);
    return;
  }
  if (kr && PM_NODE_TYPE_P(kr, PM_FORWARDING_PARAMETER_NODE)) {
    f->anon[1] = sp_box_obj(rest, SP_BUILTIN_POLY_POLY_HASH);
    return;
  }
  if (rest->len) {
    sp_PolyArray *unknown = sp_PolyArray_new();
    SP_GC_ROOT(unknown);
    for (sp_int i = 0; i < rest->len; i++) sp_PolyArray_push(unknown, rest->keys[rest->order[i]]);
    sp_raise_cls("ArgumentError", sp_sprintf("unknown keyword%s: %s", unknown->len > 1 ? "s" : "",
                                             lupe_key_list(unknown)));
  }
}

/* Binds the arguments to the parameters: the required ones, the optional
   ones (a default is evaluated in the callee frame), *rest, the required
   ones after it and &block. With loose set (a block that is not a lambda),
   a missing argument is nil and an extra one is dropped. */
static void lupe_bind_list(LupeFrame *f, const pm_parameters_node_t *ps, int argc, sp_RbVal *argv, int loose) {
  const void *kwp = L.kwhash;
  L.kwhash = NULL;
  if (!ps) {
    if (argc != 0 && !loose) lupe_arity_error(argc, 0, 0, 0);
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
    lupe_arity_error(argc, npre + npost, nopt, rest);
  /* a block drops the extra arguments from the end */
  if (loose && !rest && argc > npre + nopt + npost) argc = npre + nopt + npost;
  int ai = 0;
  for (int i = 0; i < npre; i++, ai++) lupe_bind_target(f, ps->requireds.nodes[i], ai < argc ? argv[ai] : sp_box_nil());
  int nfill = argc - npre - npost;
  for (int i = 0; i < nopt; i++) {
    const pm_optional_parameter_node_t *p = (const pm_optional_parameter_node_t *)ps->optionals.nodes[i];
    sp_RbVal *slot = lupe_local(f, (const pm_node_t *)p, p->name, 0);
    if (i < nfill) *slot = argv[ai++];
    else *slot = lupe_eval(f, p->value);
  }
  if (fwd || (ps->rest && PM_NODE_TYPE_P(ps->rest, PM_REST_PARAMETER_NODE))) {
    sp_PolyArray *a = sp_PolyArray_new();
    SP_GC_ROOT(a);
    while (ai < argc - npost) sp_PolyArray_push(a, argv[ai++]);
    pm_constant_id_t rn = ps->rest ? ((const pm_rest_parameter_node_t *)ps->rest)->name : 0;
    *(rn ? lupe_local(f, ps->rest, rn, 0) : &f->anon[0]) = sp_box_poly_array(a);
  } else if (ps->rest && !PM_NODE_TYPE_P(ps->rest, PM_IMPLICIT_REST_NODE)) {
    lupe_unsupported(ps->rest, "rest parameter");
  }
  if (ai < argc - npost) ai = argc - npost;
  for (int i = 0; i < npost; i++, ai++) lupe_bind_target(f, ps->posts.nodes[i], ai < argc ? argv[ai] : sp_box_nil());
  if (haskw) lupe_bind_keywords(f, ps, kw);
  if (ps->block) {
    pm_constant_id_t bn = ((const pm_block_parameter_node_t *)ps->block)->name;
    if (bn) *lupe_local(f, (const pm_node_t *)ps->block, bn, 0) = f->block ? lupe_block_proc(f->block) : sp_box_nil();
  }
}

static void lupe_bind_params(LupeFrame *f, const pm_parameters_node_t *ps, int argc, sp_RbVal *argv) {
  lupe_bind_list(f, ps, argc, argv, 0);
}

/* The slot of the numbered parameter _i (1..9) of a block frame. */
static sp_RbVal *lupe_numbered(LupeFrame *f, const pm_node_t *n, int i) {
  char name[3] = { '_', (char)('0' + i), 0 };
  pm_constant_id_t id = pm_constant_pool_find(&L.parser.constant_pool, (const uint8_t *)name, 2);
  if (!id) lupe_unsupported(n, "numbered parameter");
  return lupe_local(f, n, id, 0);
}

/* Binds the arguments of a block call. A block spreads one Array argument
   over two or more parameters. A lambda does so only when a method yields
   to it (`h.map(&l)`), not when Proc#call calls it (strict). */
static void lupe_bind_block(LupeFrame *f, const pm_node_t *params, int argc, sp_RbVal *argv, int lambda,
                            int strict) {
  SP_GC_SAVE();
  if (!params) {
    if (lambda && argc != 0) lupe_arity_error(argc, 0, 0, 0);
    return;
  }
  if (PM_NODE_TYPE_P(params, PM_IT_PARAMETERS_NODE)) {
    if (lambda && argc != 1) lupe_arity_error(argc, 1, 0, 0);
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
      if (lambda && argc != 0) lupe_arity_error(argc, 0, 0, 0);
      return;
    }
    int n = (int)(ps->requireds.size + ps->optionals.size + ps->posts.size);
    many = n >= 2 || (n >= 1 && ps->rest) ? 2 : n;
  }
  int spread = !(lambda && strict) && argc == 1 && many >= 2 && lupe_is_array_value(argv[0]);
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
    lupe_bind_list(f, ps, argc, argv, !lambda);
    return;
  }
  if (lambda && argc != many) lupe_arity_error(argc, many, 0, 0);
  for (int i = 0; i < many; i++) *lupe_numbered(f, params, i + 1) = i < argc ? argv[i] : sp_box_nil();
}

static sp_RbVal lupe_invoke(const LupeMethod *m, sp_RbVal self, int argc, sp_RbVal *argv) {
  return lupe_invoke_b(m, self, argc, argv, NULL);
}

static sp_RbVal lupe_invoke_b(const LupeMethod *m, sp_RbVal self, int argc, sp_RbVal *argv, const LupeBlock *blk) {
  if (m->kind == LUPE_M_READER) {
    if (argc != 0) lupe_arity_error(argc, 0, 0, 0);
    return lupe_ivar_get(self, m->ivar);
  }
  if (m->kind == LUPE_M_WRITER) {
    if (argc != 1) lupe_arity_error(argc, 1, 0, 0);
    lupe_ivar_set(self, m->ivar, argv[0]);
    return argv[0];
  }
  SP_GC_SAVE();
  sp_RbVal me = self;
  SP_GC_ROOT_RBVAL(me);
  const pm_def_node_t *def = m->def;
  /* $~ is local to the method: the caller's registers come back on exit */
  sp_re_frame rf;
  int rx = lupe_re_frame_enter(m, &rf);
  LUPE_FRAME(f, &def->locals, me, m->owner);
  f.method = m;
  f.block = blk;
  lupe_bind_params(&f, def->parameters, argc, argv);
  sp_RbVal v = def->body ? lupe_eval(&f, def->body) : sp_box_nil();
  if (L.ctl == LUPE_CTL_RETURN && L.ctl_target == &f) { v = L.ctl_val; L.ctl = LUPE_CTL_NONE; }
  if (rx) sp_re_frame_pop(&rf);
  return v;
}

/* ---- blocks ---- */

static void lupe_block_parts(const pm_node_t *n, const pm_constant_id_list_t **locals, const pm_node_t **params,
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
   holds the block, or the lambda. strict is set for Proc#call. self and
   cref replace those of the home frame when they are set (the body of
   Class.new); the caller roots *self. */
static sp_RbVal lupe_yield_in(const LupeBlock *b, int argc, sp_RbVal *argv, int strict, const sp_RbVal *self,
                              LupeClass *cref) {
  if (!b->node) {
    if (self) lupe_gap("a Symbol block as a class body");
    if (argc < 1) sp_raise_cls("ArgumentError", "no receiver given");
    return lupe_send(argv[0], sp_sym_to_s(b->sym), argc - 1, argv + 1);
  }
  LupeFrame *h = b->home;
  if (!lupe_frame_live(b)) lupe_gap("a call of a block whose scope has returned");
  SP_GC_SAVE();
  const pm_constant_id_list_t *locals;
  const pm_node_t *params, *body;
  lupe_block_parts(b->node, &locals, &params, &body);
  if (SP_UNLIKELY(SP_SAFEPOINT_POLL())) sp_safepoint();
  LUPE_FRAME(bf, locals, self ? *self : h->self, cref ? cref : h->cref);
  bf.method = h->method;
  bf.parent = h;
  bf.block = h->block;
  bf.lambda = b->lambda;
  lupe_bind_block(&bf, params, argc, argv, b->lambda, strict);
  sp_RbVal v = body ? lupe_eval(&bf, body) : sp_box_nil();
  switch (L.ctl) {
  case LUPE_CTL_NEXT:
    L.ctl = LUPE_CTL_NONE;
    return L.ctl_val;
  case LUPE_CTL_BREAK:
    if (b->lambda) { L.ctl = LUPE_CTL_NONE; return L.ctl_val; }
    if (!b->site) {
      L.ctl = LUPE_CTL_NONE;
      sp_raise_cls("LocalJumpError", "break from proc-closure");
    }
    L.ctl = LUPE_CTL_BREAK_BLOCK;
    L.ctl_target = b->site;
    return sp_box_nil();
  case LUPE_CTL_RETURN:
    if (L.ctl_target == &bf) { L.ctl = LUPE_CTL_NONE; return L.ctl_val; }
    return v;
  default:
    return v;
  }
}

static sp_RbVal lupe_yield_x(const LupeBlock *b, int argc, sp_RbVal *argv, int strict) {
  return lupe_yield_in(b, argc, argv, strict, NULL, NULL);
}

static sp_RbVal lupe_yield(const LupeBlock *b, int argc, sp_RbVal *argv) {
  return lupe_yield_x(b, argc, argv, 0);
}

/* Calls a block with self and the definee replaced: a def in it defines a
   method of cref. */
static sp_RbVal lupe_yield_under(const LupeBlock *b, sp_RbVal self, LupeClass *cref, int argc, sp_RbVal *argv) {
  SP_GC_ROOT_RBVAL(self);
  return lupe_yield_in(b, argc, argv, 0, &self, cref);
}

static int lupe_is_proc(sp_RbVal v) {
  return v.tag == SP_TAG_OBJ && v.cls_id == SP_BUILTIN_PROC && v.v.p;
}

/* A Proc value for a block. A block that a Proc carries answers that Proc. */
static sp_RbVal lupe_block_proc(const LupeBlock *b) {
  if (b->proc) return sp_box_obj(b->proc, SP_BUILTIN_PROC);
  LupeProc *p = (LupeProc *)sp_gc_alloc(sizeof *p, NULL, NULL);
  p->p.lambda_p = b->lambda ? TRUE : FALSE;
  p->b = *b;
  p->b.proc = p;
  return sp_box_obj(p, SP_BUILTIN_PROC);
}

/* The block that a call passes: a block literal (into *lit), `&expr` or
   `&`. *holder roots the Proc of `&expr`. Answers NULL for no block, and
   also when a jump is pending. */
static const LupeBlock *lupe_make_block(LupeFrame *f, const pm_node_t *bn, LupeBlock *lit, sp_RbVal *holder) {
  if (!bn) return NULL;
  if (PM_NODE_TYPE_P(bn, PM_BLOCK_NODE)) {
    *lit = (LupeBlock){ .node = bn, .home = f, .serial = f->serial, .fs = f->fs, .depth = f->depth, .site = lit };
    return lit;
  }
  const pm_node_t *e = ((const pm_block_argument_node_t *)bn)->expression;
  if (!e) return f->block;   /* def m(&) ... n(&) */
  sp_RbVal v = lupe_eval(f, e);
  if (L.ctl != LUPE_CTL_NONE) return NULL;
  *holder = v;
  if (v.tag == SP_TAG_NIL) return NULL;
  if (v.tag == SP_TAG_SYM) {
    *lit = (LupeBlock){ .sym = (sp_sym)v.v.i, .lambda = 1 };
    return lit;
  }
  if (lupe_is_proc(v)) return &((LupeProc *)v.v.p)->b;
  lupe_unsupported(bn, "block argument that is not a Proc or a Symbol");
}

/* ---- calls ---- */

/* A keyword Hash of only `**h`, which passes nothing when it is empty. */
static int lupe_kwsplat_only(const pm_node_t *a) {
  if (!PM_NODE_TYPE_P(a, PM_KEYWORD_HASH_NODE)) return 0;
  const pm_node_list_t *e = &((const pm_keyword_hash_node_t *)a)->elements;
  for (size_t i = 0; i < e->size; i++)
    if (!PM_NODE_TYPE_P(e->nodes[i], PM_ASSOC_SPLAT_NODE)) return 0;
  return 1;
}

/* Whether the count of the arguments is known at run time only: a splat,
   `...`, or a keyword Hash of only `**h`. */
static int lupe_args_dynamic(const pm_arguments_node_t *args) {
  if (!args) return 0;
  for (size_t i = 0; i < args->arguments.size; i++) {
    const pm_node_t *a = args->arguments.nodes[i];
    if (PM_NODE_TYPE_P(a, PM_SPLAT_NODE) || PM_NODE_TYPE_P(a, PM_FORWARDING_ARGUMENTS_NODE) || lupe_kwsplat_only(a))
      return 1;
  }
  return 0;
}

static int lupe_has_forwarding(const pm_arguments_node_t *args) {
  for (size_t i = 0; args && i < args->arguments.size; i++)
    if (PM_NODE_TYPE_P(args->arguments.nodes[i], PM_FORWARDING_ARGUMENTS_NODE)) return 1;
  return 0;
}

/* The argument count of a call site that needs a fixed count. */
static int lupe_argc(const pm_arguments_node_t *args) {
  if (!args) return 0;
  if (lupe_args_dynamic(args)) lupe_unsupported((const pm_node_t *)args, "argument kind");
  return (int)args->arguments.size;
}

/* The frame of the method (or the program) that holds frame f. */
static LupeFrame *lupe_method_frame(LupeFrame *f) {
  while (f->parent) f = f->parent;
  return f;
}

/* Pushes the elements of a splat: an Array spreads, nil passes nothing
   and any other value passes itself. */
static void lupe_spread(sp_PolyArray *out, sp_RbVal v) {
  if (v.tag == SP_TAG_NIL) return;
  if (!(v.tag == SP_TAG_OBJ && (sp_poly_is_array_kind(v.cls_id) || v.cls_id == SP_BUILTIN_RANGE))) {
    sp_PolyArray_push(out, v);
    return;
  }
  SP_GC_ROOT_RBVAL(v);
  if (v.cls_id == SP_BUILTIN_RANGE) v = lupe_send(v, "to_a", 0, NULL);
  for (sp_int i = 0; i < sp_poly_length(v); i++) sp_PolyArray_push(out, sp_poly_arr_get(v, i));
}

/* Evaluates arguments whose count is known at run time only into *out.
   Sets *kw to the keyword Hash when it is the last argument. Answers 0
   when a jump is pending. */
static int lupe_args_spread(LupeFrame *f, const pm_arguments_node_t *args, sp_PolyArray *out, const void **kw) {
  sp_RbVal v = sp_box_nil();
  SP_GC_ROOT_RBVAL(v);
  for (size_t i = 0; i < args->arguments.size; i++) {
    const pm_node_t *a = args->arguments.nodes[i];
    *kw = NULL;
    if (PM_NODE_TYPE_P(a, PM_SPLAT_NODE)) {
      const pm_node_t *e = ((const pm_splat_node_t *)a)->expression;
      v = e ? lupe_eval(f, e) : lupe_method_frame(f)->anon[0];
      if (L.ctl != LUPE_CTL_NONE) return 0;
      lupe_spread(out, v);
    } else if (PM_NODE_TYPE_P(a, PM_FORWARDING_ARGUMENTS_NODE)) {
      LupeFrame *mf = lupe_method_frame(f);
      lupe_spread(out, mf->anon[0]);
      v = mf->anon[1];
      if (v.tag != SP_TAG_NIL && sp_poly_length(v) > 0) {
        sp_PolyArray_push(out, v);
        *kw = v.v.p;
      }
    } else {
      v = lupe_eval(f, a);
      if (L.ctl != LUPE_CTL_NONE) return 0;
      if (lupe_kwsplat_only(a) && sp_poly_length(v) == 0) continue;
      sp_PolyArray_push(out, v);
      if (PM_NODE_TYPE_P(a, PM_KEYWORD_HASH_NODE)) *kw = v.v.p;
    }
  }
  return 1;
}

/* Declares argc and argv for a call site and evaluates the arguments: into
   a C array when the count is fixed, else into a rooted Array. kw is the
   keyword Hash of the call, or NULL. */
#define LUPE_ARGS(f, args, argc, argv, kw, ok)                                            \
  int argc = lupe_args_dynamic(args) ? -1 : (args) ? (int)(args)->arguments.size : 0;       \
  sp_RbVal argc##_vla[argc > 0 ? argc : 1];                                                \
  sp_RbVal *argv = argc##_vla;                                                             \
  sp_PolyArray *argc##_spread = NULL;                                                      \
  SP_GC_ROOT(argc##_spread);                                                               \
  const void *kw = NULL;                                                                   \
  int ok;                                                                                  \
  if (argc >= 0) {                                                                         \
    ok = lupe_args(f, args, argc, argv);                                                   \
    if (ok && argc > 0 && PM_NODE_TYPE_P((args)->arguments.nodes[argc - 1], PM_KEYWORD_HASH_NODE)) \
      kw = argv[argc - 1].v.p;                                                             \
  } else {                                                                                 \
    argc##_spread = sp_PolyArray_new();                                                    \
    ok = lupe_args_spread(f, args, argc##_spread, &kw);                                    \
    argc = (int)argc##_spread->len;                                                        \
    argv = argc##_spread->data;                                                            \
  }

/* Roots argv[0..argc) and evaluates the arguments into it. Answers 0 when
   a jump is pending. The roots stay until the caller's SP_GC_SAVE ends. */
static int lupe_args(LupeFrame *f, const pm_arguments_node_t *args, int argc, sp_RbVal *argv) {
  for (int i = 0; i < argc; i++) {
    argv[i] = sp_box_nil();
    _sp_gc_root_push((void **)((uintptr_t)&argv[i] | (uintptr_t)1));
  }
  for (int i = 0; i < argc; i++) {
    argv[i] = lupe_eval(f, args->arguments.nodes[i]);
    if (L.ctl != LUPE_CTL_NONE) return 0;
  }
  return 1;
}

/* A call that passed the block lit ends here when the block breaks. */
static sp_RbVal lupe_catch_break(const LupeBlock *lit, sp_RbVal v) {
  if (L.ctl == LUPE_CTL_BREAK_BLOCK && L.ctl_target == lit) {
    L.ctl = LUPE_CTL_NONE;
    return L.ctl_val;
  }
  return v;
}

static sp_RbVal lupe_call(LupeFrame *f, const pm_call_node_t *c) {
  SP_GC_SAVE();
  const char *name = sp_sym_to_s(lupe_sym(c->name));
  sp_RbVal recv = sp_box_nil();
  SP_GC_ROOT_RBVAL(recv);
  if (c->receiver) {
    recv = lupe_eval(f, c->receiver);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    if (PM_NODE_FLAG_P((const pm_node_t *)c, PM_CALL_NODE_FLAGS_SAFE_NAVIGATION) && recv.tag == SP_TAG_NIL)
      return recv;
  }
  LUPE_ARGS(f, c->arguments, argc, argv, kw, ok);
  if (!ok) return sp_box_nil();
  LupeBlock lit;
  sp_RbVal holder = sp_box_nil();
  SP_GC_ROOT_RBVAL(holder);
  const LupeBlock *blk = lupe_make_block(f, c->block, &lit, &holder);
  if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
  if (!c->block && lupe_has_forwarding(c->arguments)) blk = lupe_method_frame(f)->block;
  /* the callee's binder takes the keyword Hash; a builtin leaves it, so
     it is cleared after the call, before its address can be reused */
  L.kwhash = kw;
  sp_RbVal v = c->receiver ? lupe_send_b(recv, name, argc, argv, blk) : lupe_call_self(f, name, argc, argv, blk);
  L.kwhash = NULL;
  v = lupe_catch_break(&lit, v);
  if (!c->receiver) return v;
  /* `obj.x = v` answers v, whatever x= returns */
  if (PM_NODE_FLAG_P((const pm_node_t *)c, PM_CALL_NODE_FLAGS_ATTRIBUTE_WRITE) && argc > 0) return argv[argc - 1];
  return v;
}

static sp_RbVal lupe_loop(const LupeBlock *blk);   /* the exceptions section */

/* The Kernel methods that take the block of the call. Answers 1 and sets
   *out when it handles the call. */
static int lupe_kernel_block(LupeFrame *f, const char *name, int argc, const LupeBlock *blk, sp_RbVal *out) {
  if (argc == 0 && (!strcmp(name, "block_given?") || !strcmp(name, "iterator?"))) {
    *out = sp_box_bool(f->block != NULL);
    return 1;
  }
  if (!blk) return 0;
  if (argc == 0 && (!strcmp(name, "proc") || !strcmp(name, "lambda"))) {
    /* a break in the Proc of a literal has no call to end */
    if (blk->node && !blk->proc) {
      LupeBlock l = *blk;
      l.lambda = name[0] == 'l';
      l.site = NULL;
      *out = lupe_block_proc(&l);
    } else {
      *out = lupe_block_proc(blk);
    }
    return 1;
  }
  if (argc == 0 && !strcmp(name, "loop")) {
    *out = lupe_loop(blk);
    return 1;
  }
  return 0;
}

/* A call with no receiver: a method of self, then Kernel, then a builtin
   method of self. In a class body or a class method, self is the class. */
static sp_RbVal lupe_call_self(LupeFrame *f, const char *name, int argc, sp_RbVal *argv, const LupeBlock *blk) {
  sp_sym s = sp_sym_intern(name);
  sp_RbVal self = f->self, out;
  if (self.tag == SP_TAG_CLASS) {
    LupeClass *k = lupe_class_value(self);
    LupeMethod *m = k ? lupe_find_smethod(k, s) : NULL;
    if (m) return lupe_invoke_b(m, self, argc, argv, blk);
    if (lupe_class_core(f, self, name, argc, argv, blk, &out)) return out;
  }
  LupeMethod *m = lupe_find_method(lupe_class_of(self), s);
  if (m) return lupe_invoke_b(m, self, argc, argv, blk);
  if (lupe_kernel_block(f, name, argc, blk, &out)) return out;
  if (blk) return lupe_send_b(self, name, argc, argv, blk);
  if (lupe_exc_kernel(name, argc, argv, &out)) return out;
  if (lupe_fiber_kernel(name, argc, argv, &out)) return out;
  if (lupe_kernel(name, argc, argv, &out)) return out;
  if (lupe_object_core(self, name, argc, argv, &out)) return out;
  if (!lupe_is_lupe_object(self) && self.tag != SP_TAG_CLASS) return lupe_send(self, name, argc, argv);
  lupe_trace_missing("Kernel", name, argc);
  if (lupe_is_lupe_object(self) && self.v.p == L.main.v.p)
    sp_raise_cls("NoMethodError", sp_sprintf("undefined method '%s' for main", name));
  sp_raise_cls("NoMethodError", sp_sprintf("undefined method '%s' for an instance of %s", name,
                                           sp_poly_class_name(self)));
}

/* The arguments that a bare super passes, into out: the current values of
   the parameters, in order, with *rest spread, then a keyword Hash of the
   keywords and **rest. Answers that Hash, or NULL for none. */
static const void *lupe_zsuper_args(LupeFrame *f, const pm_node_t *n, sp_PolyArray *out) {
  f = lupe_method_frame(f);   /* a super in a block reads the method's parameters */
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
        lupe_unsupported(n, "bare super with a parameter pattern");
      sp_PolyArray_push(out, *lupe_local(f, n, name, 0));
    }
  const pm_node_t *kr = ps->keyword_rest;
  int fwd = kr && PM_NODE_TYPE_P(kr, PM_FORWARDING_PARAMETER_NODE);
  if (fwd || (ps->rest && PM_NODE_TYPE_P(ps->rest, PM_REST_PARAMETER_NODE))) {
    pm_constant_id_t rn = ps->rest ? ((const pm_rest_parameter_node_t *)ps->rest)->name : 0;
    lupe_spread(out, rn ? *lupe_local(f, n, rn, 0) : f->anon[0]);
  }
  for (size_t i = 0; i < ps->posts.size; i++)
    sp_PolyArray_push(out, *lupe_local(f, n, ((const pm_required_parameter_node_t *)ps->posts.nodes[i])->name, 0));
  /* the keywords, with their current values, then **rest */
  if (!ps->keywords.size && !kr) return NULL;
  sp_PolyPolyHash *h = sp_PolyPolyHash_new();
  SP_GC_ROOT(h);
  for (size_t i = 0; i < ps->keywords.size; i++) {
    const pm_node_t *p = ps->keywords.nodes[i];
    pm_constant_id_t name = PM_NODE_TYPE_P(p, PM_REQUIRED_KEYWORD_PARAMETER_NODE)
                              ? ((const pm_required_keyword_parameter_node_t *)p)->name
                              : ((const pm_optional_keyword_parameter_node_t *)p)->name;
    sp_PolyPolyHash_set(h, sp_box_sym(lupe_sym(name)), *lupe_local(f, n, name, 0));
  }
  sp_RbVal kwrest = sp_box_nil();
  if (fwd) kwrest = f->anon[1];
  else if (kr && PM_NODE_TYPE_P(kr, PM_KEYWORD_REST_PARAMETER_NODE)) {
    pm_constant_id_t rn = ((const pm_keyword_rest_parameter_node_t *)kr)->name;
    kwrest = rn ? *lupe_local(f, n, rn, 0) : f->anon[1];
  }
  sp_PolyPolyHash *r = kwrest.tag == SP_TAG_NIL ? NULL : sp_poly_as_poly_poly_hash(kwrest);
  for (sp_int i = 0; r && i < r->len; i++) sp_PolyPolyHash_set(h, r->keys[r->order[i]], r->vals[r->order[i]]);
  if (h->len == 0) return NULL;
  sp_PolyArray_push(out, sp_box_obj(h, SP_BUILTIN_POLY_POLY_HASH));
  return h;
}

static sp_RbVal lupe_super_call(LupeFrame *f, const pm_node_t *n, const pm_arguments_node_t *args, int forward,
                                const LupeBlock *blk);

/* super, and super with no arguments, which passes the parameters. Both
   pass the block of the method, unless the super has its own. */
static sp_RbVal lupe_super(LupeFrame *f, const pm_node_t *n, const pm_arguments_node_t *args, int forward,
                           const pm_node_t *bnode) {
  SP_GC_SAVE();
  if (!f->method) lupe_unsupported(n, "super outside a method");
  LupeBlock lit;
  sp_RbVal holder = sp_box_nil();
  SP_GC_ROOT_RBVAL(holder);
  const LupeBlock *blk = bnode ? lupe_make_block(f, bnode, &lit, &holder) : f->block;
  if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
  sp_RbVal v = lupe_super_call(f, n, args, forward, blk);
  L.kwhash = NULL;
  return lupe_catch_break(&lit, v);
}

static sp_RbVal lupe_super_call(LupeFrame *f, const pm_node_t *n, const pm_arguments_node_t *args, int forward,
                                const LupeBlock *blk) {
  SP_GC_SAVE();
  LUPE_ARGS(f, forward ? NULL : args, argc, argv, kw, ok);
  if (!ok) return sp_box_nil();
  if (forward) {
    argc_spread = sp_PolyArray_new();
    kw = lupe_zsuper_args(f, n, argc_spread);
    argc = (int)argc_spread->len;
    argv = argc_spread->data;
  }
  L.kwhash = kw;
  const LupeMethod *m = f->method;
  LupeMethod *s;
  if (f->self.tag == SP_TAG_CLASS && lupe_table_get(&m->owner->smethods, m->name) == m)
    s = m->owner->super ? lupe_find_smethod(m->owner->super, m->name) : NULL;
  else
    s = lupe_find_super(lupe_class_of(f->self), m);
  if (s) return lupe_invoke_b(s, f->self, argc, argv, blk);
  const char *name = sp_sym_to_s(m->name);
  sp_RbVal out;
  /* the builtin initialize: an exception takes its message, a Struct or a
     Data its members */
  if (!strcmp(name, "initialize")) {
    if (lupe_is_exc(f->self)) lupe_exc_init(f->self, argc, argv);
    else if (lupe_is_lupe_object(f->self) && lupe_record_of(lupe_class_of(f->self))) lupe_record_init(f->self, argc, argv);
    return sp_box_nil();
  }
  if (f->self.tag == SP_TAG_CLASS && lupe_class_core(NULL, f->self, name, argc, argv, blk, &out)) return out;
  /* an object of the program: the defaults of Object, which do not call
     the hooks (to_s through a hook would call the program's to_s again) */
  if (lupe_is_lupe_object(f->self)) {
    if (blk) lupe_gap(sp_sprintf("super with a block to the builtin %s", name));
    if (lupe_object_core(f->self, name, argc, argv, &out)) return out;
  } else if (lupe_builtin_call(f->self, name, argc, argv, blk, &out)) {
    return out;
  }
  lupe_trace_missing("super", name, argc);
  sp_raise_cls("NoMethodError", sp_sprintf("super: no superclass method '%s'", name));
}

/* ---- control flow ---- */

static sp_RbVal lupe_eval_stmts(LupeFrame *f, const pm_statements_node_t *s) {
  sp_RbVal v = sp_box_nil();
  if (!s) return v;
  for (size_t i = 0; i < s->body.size; i++) {
    v = lupe_eval(f, s->body.nodes[i]);
    if (L.ctl != LUPE_CTL_NONE) break;
  }
  return v;
}

static sp_RbVal lupe_while(LupeFrame *f, const pm_node_t *pred, const pm_statements_node_t *body, int until) {
  for (;;) {
    /* a back-edge lets another green thread run, as in AOT code */
    if (SP_UNLIKELY(SP_SAFEPOINT_POLL())) sp_safepoint();
    sp_RbVal c = lupe_eval(f, pred);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    if (sp_poly_truthy(c) == until) return sp_box_nil();
    lupe_eval_stmts(f, body);
    if (L.ctl == LUPE_CTL_BREAK) { L.ctl = LUPE_CTL_NONE; return L.ctl_val; }
    if (L.ctl == LUPE_CTL_NEXT) { L.ctl = LUPE_CTL_NONE; continue; }
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
  }
}

/* The value that return / break / next carry: nil, one value, or an Array. */
static sp_RbVal lupe_jump_value(LupeFrame *f, const pm_arguments_node_t *args) {
  if (!args || args->arguments.size == 0) return sp_box_nil();
  if (args->arguments.size == 1) return lupe_eval(f, args->arguments.nodes[0]);
  sp_PolyArray *a = sp_PolyArray_new();
  SP_GC_ROOT(a);
  for (size_t i = 0; i < args->arguments.size; i++) sp_PolyArray_push(a, lupe_eval(f, args->arguments.nodes[i]));
  return sp_box_poly_array(a);
}

static sp_RbVal lupe_jump(LupeFrame *f, LupeCtl kind, const pm_arguments_node_t *args) {
  sp_RbVal v = lupe_jump_value(f, args);
  if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
  L.ctl_val = v;
  L.ctl = kind;
  if (kind == LUPE_CTL_RETURN) {
    /* a return in a block leaves the method that holds it, or the lambda */
    LupeFrame *t = f;
    while (t->parent && !t->lambda) t = t->parent;
    L.ctl_target = t;
  }
  return sp_box_nil();
}

/* ---- literals ---- */

static sp_RbVal lupe_integer(const pm_node_t *n, const pm_integer_t *iv) {
  uint64_t u;
  if (!iv->values) u = iv->value;
  else if (iv->length == 2) u = (uint64_t)iv->values[1] << 32 | iv->values[0];
  else lupe_unsupported(n, "integer literal beyond 64 bits");
  if (u > (uint64_t)INT64_MAX + (iv->negative ? 1 : 0)) lupe_unsupported(n, "integer literal beyond 64 bits");
  return sp_box_int(iv->negative ? (sp_int)(0 - u) : (sp_int)u);
}

/* A new mutable String with the given bytes, as a literal makes one. */
static sp_RbVal lupe_new_string(const uint8_t *p, size_t len) {
  char *d = sp_str_alloc(len);
  if (len) memcpy(d, p, len);
  d[len] = 0;
  return sp_box_str_new(d);
}

static sp_RbVal lupe_interpolate(LupeFrame *f, const pm_node_list_t *parts) {
  sp_RbVal acc = lupe_new_string(NULL, 0), v = sp_box_nil();
  SP_GC_ROOT_RBVAL(acc);
  SP_GC_ROOT_RBVAL(v);
  for (size_t i = 0; i < parts->size; i++) {
    const pm_node_t *p = parts->nodes[i];
    if (PM_NODE_TYPE_P(p, PM_STRING_NODE)) {
      const pm_string_t *s = &((const pm_string_node_t *)p)->unescaped;
      v = lupe_new_string(pm_string_source(s), pm_string_length(s));
    } else if (PM_NODE_TYPE_P(p, PM_EMBEDDED_STATEMENTS_NODE)) {
      v = lupe_eval_stmts(f, ((const pm_embedded_statements_node_t *)p)->statements);
      if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
      v = sp_poly_to_s_box(v);
    } else {
      lupe_unsupported(p, "interpolation part");
    }
    acc = sp_poly_add(acc, v);
  }
  return acc;
}

static sp_RbVal lupe_array(LupeFrame *f, const pm_array_node_t *n) {
  sp_PolyArray *a = sp_PolyArray_new();
  SP_GC_ROOT(a);
  for (size_t i = 0; i < n->elements.size; i++) {
    const pm_node_t *e = n->elements.nodes[i];
    if (PM_NODE_TYPE_P(e, PM_SPLAT_NODE)) lupe_unsupported(e, "splat in an Array literal");
    sp_RbVal v = lupe_eval(f, e);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    sp_PolyArray_push(a, v);
  }
  return sp_box_poly_array(a);
}

/* A Hash literal, or the keyword arguments of a call (`m(a: 1)`), as a
   poly Hash. `**h` copies the entries of h. */
static sp_RbVal lupe_hash_lit(LupeFrame *f, const pm_node_list_t *elts) {
  sp_PolyPolyHash *h = sp_PolyPolyHash_new();
  SP_GC_ROOT(h);
  sp_RbVal k = sp_box_nil(), v = sp_box_nil();
  SP_GC_ROOT_RBVAL(k);
  SP_GC_ROOT_RBVAL(v);
  for (size_t i = 0; i < elts->size; i++) {
    const pm_node_t *e = elts->nodes[i];
    if (PM_NODE_TYPE_P(e, PM_ASSOC_SPLAT_NODE)) {
      const pm_node_t *x = ((const pm_assoc_splat_node_t *)e)->value;
      v = x ? lupe_eval(f, x) : lupe_method_frame(f)->anon[1];   /* `**` passes the method's **kwrest */
      if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
      if (v.tag == SP_TAG_NIL) continue;
      if (lupe_is_lupe_object(v)) v = lupe_send(v, "to_hash", 0, NULL);
      sp_PolyPolyHash *o = sp_poly_as_poly_poly_hash(v);
      if (!o) sp_raise_cls("TypeError", sp_sprintf("no implicit conversion of %s into Hash", sp_poly_class_name(v)));
      for (sp_int j = 0; j < o->len; j++) sp_PolyPolyHash_set(h, o->keys[o->order[j]], o->vals[o->order[j]]);
      continue;
    }
    const pm_assoc_node_t *a = (const pm_assoc_node_t *)e;
    k = lupe_eval(f, a->key);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    /* `{x:}` reads x */
    v = PM_NODE_TYPE_P(a->value, PM_IMPLICIT_NODE) ? lupe_eval(f, ((const pm_implicit_node_t *)a->value)->value)
                                                   : lupe_eval(f, a->value);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    sp_PolyPolyHash_set(h, k, v);
  }
  return sp_box_obj(h, SP_BUILTIN_POLY_POLY_HASH);
}

/* A Range of Integers. A missing end makes a beginless or endless one. */
static sp_RbVal lupe_range(LupeFrame *f, const pm_range_node_t *n) {
  sp_RbVal lo = n->left ? lupe_eval(f, n->left) : sp_box_nil();
  if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
  sp_RbVal hi = n->right ? lupe_eval(f, n->right) : sp_box_nil();
  if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
  if ((lo.tag != SP_TAG_INT && lo.tag != SP_TAG_NIL) || (hi.tag != SP_TAG_INT && hi.tag != SP_TAG_NIL) ||
      (lo.tag == SP_TAG_NIL && hi.tag == SP_TAG_NIL))
    lupe_unsupported((const pm_node_t *)n, "Range of values other than Integer");
  sp_oint first = { lo.tag == SP_TAG_INT ? lo.v.i : 0, lo.tag == SP_TAG_NIL };
  sp_oint last = { hi.tag == SP_TAG_INT ? hi.v.i : 0, hi.tag == SP_TAG_NIL };
  return sp_box_range(sp_range_new_o(first, last, PM_NODE_FLAG_P((const pm_node_t *)n, PM_RANGE_FLAGS_EXCLUDE_END)));
}

/* ---- classes, modules and constants ---- */

/* The scope and the name that a constant path writes or defines. */
static void lupe_const_target(LupeFrame *f, const pm_node_t *path, LupeClass **scope, sp_sym *name) {
  if (PM_NODE_TYPE_P(path, PM_CONSTANT_READ_NODE)) {
    *scope = f->cref;
    *name = lupe_sym(((const pm_constant_read_node_t *)path)->name);
    return;
  }
  if (!PM_NODE_TYPE_P(path, PM_CONSTANT_PATH_NODE)) lupe_unsupported(path, "constant target");
  const pm_constant_path_node_t *cp = (const pm_constant_path_node_t *)path;
  *name = lupe_sym(cp->name);
  if (!cp->parent) { *scope = L.object; return; }
  sp_RbVal pv = lupe_eval(f, cp->parent);
  *scope = lupe_class_value(pv);
  if (!*scope) sp_raise_cls("TypeError", sp_sprintf("%s is not a class/module", sp_poly_inspect(pv)));
}

static sp_RbVal lupe_class_def(LupeFrame *f, const pm_node_t *path, const pm_node_t *super_node,
                               const pm_node_t *body, const pm_constant_id_list_t *locals, int is_module) {
  SP_GC_SAVE();
  LupeClass *scope;
  sp_sym name;
  lupe_const_target(f, path, &scope, &name);
  const char *cname = sp_sym_to_s(name);
  LupeClass *k = NULL;
  LupeConst *own = lupe_const_own(scope, name);
  if (own) {
    k = lupe_class_value(own->value);
    if (!k) sp_raise_cls("TypeError", sp_sprintf("%s is not a %s", cname, is_module ? "module" : "class"));
  } else if (scope == L.object) {
    k = lupe_builtin(cname);   /* reopen a builtin class */
    if (!k && lupe_unknown_builtin(cname)) lupe_gap(sp_sprintf("reopening of the builtin %s", cname));
  }
  if (!k) {
    LupeClass *super = NULL;
    if (!is_module) {
      super = L.object;
      if (super_node) {
        sp_RbVal sv = lupe_eval(f, super_node);
        if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
        super = lupe_class_value(sv);
        if (!super || super->is_module)
          sp_raise_cls("TypeError", sp_sprintf("superclass must be an instance of Class (given an instance of %s)",
                                               super ? "Module" : lupe_class_of(sv)->name));
      }
    }
    const char *full = scope == L.object ? cname : sp_sprintf("%s::%s", scope->name, cname);
    k = lupe_class_new(strdup(full), is_module, super, scope);
    lupe_const_set(scope, name, lupe_class_box(k));
    /* the hook of the superclass: inherited(subclass), before the body */
    LupeMethod *hook = super ? lupe_find_smethod(super, sp_sym_intern("inherited")) : NULL;
    if (hook) {
      sp_RbVal sub = lupe_class_box(k);
      lupe_invoke(hook, lupe_class_box(super), 1, &sub);
    }
  }
  if (!body) return sp_box_nil();
  LUPE_FRAME(cf, locals, lupe_class_box(k), k);
  return lupe_eval(&cf, body);
}

static sp_RbVal lupe_singleton_class(LupeFrame *f, const pm_singleton_class_node_t *n) {
  SP_GC_SAVE();
  sp_RbVal target = lupe_eval(f, n->expression);
  LupeClass *k = lupe_class_value(target);
  if (!k) lupe_unsupported((const pm_node_t *)n, "singleton class of an object");
  if (!n->body) return sp_box_nil();
  LUPE_FRAME(sf, &n->locals, target, k);
  sf.singleton_defs = 1;
  return lupe_eval(&sf, n->body);
}

static sp_RbVal lupe_const_read(LupeFrame *f, const pm_node_t *n) {
  sp_RbVal v;
  if (PM_NODE_TYPE_P(n, PM_CONSTANT_READ_NODE)) {
    sp_sym name = lupe_sym(((const pm_constant_read_node_t *)n)->name);
    if (!lupe_const_lookup(f->cref, name, &v)) lupe_const_missing(sp_sym_to_s(name));
    return v;
  }
  const pm_constant_path_node_t *cp = (const pm_constant_path_node_t *)n;
  sp_sym name = lupe_sym(cp->name);
  LupeClass *scope = L.object;
  if (cp->parent) {
    sp_RbVal pv = lupe_eval(f, cp->parent);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    scope = lupe_class_value(pv);
    if (!scope) sp_raise_cls("TypeError", sp_sprintf("%s is not a class/module", sp_poly_inspect(pv)));
  }
  if (!lupe_const_scoped(scope, name, &v)) lupe_const_missing(sp_sym_to_s(name));
  return v;
}

/* x op= v on a local, an ivar or an attribute: read, send op, write. */
static sp_RbVal lupe_op(sp_RbVal cur, pm_constant_id_t op, sp_RbVal v) {
  SP_GC_ROOT_RBVAL(cur);
  SP_GC_ROOT_RBVAL(v);
  return lupe_send(cur, sp_sym_to_s(lupe_sym(op)), 1, &v);
}

static sp_RbVal lupe_call_op_write(LupeFrame *f, const pm_call_operator_write_node_t *n) {
  SP_GC_SAVE();
  if (!n->receiver) lupe_unsupported((const pm_node_t *)n, "attribute op-assign with no receiver");
  sp_RbVal recv = lupe_eval(f, n->receiver);
  SP_GC_ROOT_RBVAL(recv);
  if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
  sp_RbVal cur = lupe_send(recv, sp_sym_to_s(lupe_sym(n->read_name)), 0, NULL);
  SP_GC_ROOT_RBVAL(cur);
  sp_RbVal v = lupe_eval(f, n->value);
  if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
  sp_RbVal nv = lupe_op(cur, n->binary_operator, v);
  SP_GC_ROOT_RBVAL(nv);
  lupe_send(recv, sp_sym_to_s(lupe_sym(n->write_name)), 1, &nv);
  return nv;
}

static sp_RbVal lupe_index_op_write(LupeFrame *f, const pm_index_operator_write_node_t *n) {
  SP_GC_SAVE();
  if (n->block) lupe_unsupported((const pm_node_t *)n, "block");
  sp_RbVal recv = lupe_eval(f, n->receiver);
  SP_GC_ROOT_RBVAL(recv);
  if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
  int argc = lupe_argc(n->arguments);
  sp_RbVal argv[argc + 1];
  if (!lupe_args(f, n->arguments, argc, argv)) return sp_box_nil();
  sp_RbVal cur = lupe_send(recv, "[]", argc, argv);
  SP_GC_ROOT_RBVAL(cur);
  sp_RbVal v = lupe_eval(f, n->value);
  if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
  argv[argc] = lupe_op(cur, n->binary_operator, v);
  _sp_gc_root_push((void **)((uintptr_t)&argv[argc] | (uintptr_t)1));
  lupe_send(recv, "[]=", argc + 1, argv);
  return argv[argc];
}

/* x[i] ||= v and x[i] &&= v: read, test, and write only when needed. */
static sp_RbVal lupe_index_logic_write(LupeFrame *f, const pm_node_t *n) {
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
  if (block) lupe_unsupported(n, "block");
  sp_RbVal recv = lupe_eval(f, recv_n);
  SP_GC_ROOT_RBVAL(recv);
  if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
  int argc = lupe_argc(args);
  sp_RbVal argv[argc + 1];
  if (!lupe_args(f, args, argc, argv)) return sp_box_nil();
  sp_RbVal cur = lupe_send(recv, "[]", argc, argv);
  if (sp_poly_truthy(cur) == is_or) return cur;
  argv[argc] = lupe_eval(f, value);
  _sp_gc_root_push((void **)((uintptr_t)&argv[argc] | (uintptr_t)1));
  if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
  lupe_send(recv, "[]=", argc + 1, argv);
  return argv[argc];
}

/* ---- exceptions ---- */

/* Pushes a rescue handler and pops it, as the code that spinel emits
   does (src/codegen: begin/rescue). LUPE_TRY_LANDED restores the GC roots
   and the rescue stack at a landing, and cuts the frame stack of Lupe,
   since a raise skips the cleanup of the frames that it unwinds. */
#define LUPE_TRY_PUSH()                                                                \
  sp_exc_check_depth();                                                                \
  sp_exc_rootmark[sp_exc_top] = sp_gc_nroots;                                          \
  sp_rescue_mark[sp_exc_top] = sp_rescue_sp;                                           \
  sp_exc_msg[sp_exc_top] = 0;                                                          \
  sp_exc_obj[sp_exc_top] = 0;                                                          \
  sp_exc_top++
#define LUPE_TRY_LANDED(d)                                                             \
  sp_exc_top--;                                                                        \
  sp_gc_nroots = sp_exc_rootmark[sp_exc_top];                                          \
  sp_rescue_sp = sp_rescue_mark[sp_exc_top];                                           \
  lupe_fs()->depth = (d);                                                              \
  L.kwhash = NULL

/* The exception that a handler caught: its class name, message and object
   (NULL when the runtime raised by name only). */
typedef struct {
  const char *cls, *msg;
  void *obj;
} LupeCaught;

static LupeCaught lupe_caught(void) {
  return (LupeCaught){ (const char *)sp_last_exc_cls, sp_exc_msg[sp_exc_top], sp_exc_obj[sp_exc_top] };
}

/* The object of a caught exception, made now when the runtime raised by
   name. It takes as its cause the exception that a rescue handled when
   the raise happened (sp_pending_cause), as the code of spinel does. */
static sp_Exception *lupe_caught_obj(LupeCaught c) {
  sp_Exception *e = c.obj ? (sp_Exception *)c.obj : sp_exc_new_for_catch(c.cls, c.msg);
  if (!e->cause) {
    sp_gc_wb(e);
    e->cause = (sp_Exception *)sp_pending_cause;
  }
  sp_pending_cause = NULL;
  return e;
}

/* Raises a caught exception again, the same object. */
static __attribute__((noreturn)) void lupe_reraise(LupeCaught c) {
  sp_pending_exc_obj = c.obj;
  sp_bt_keep = 1;
  sp_raise_cls(c.cls, c.msg);
}

/* Binds `=> target` of a rescue clause. */
static void lupe_rescue_bind(LupeFrame *f, const pm_node_t *t, sp_RbVal v) {
  if (PM_NODE_TYPE_P(t, PM_LOCAL_VARIABLE_TARGET_NODE)) {
    const pm_local_variable_target_node_t *l = (const pm_local_variable_target_node_t *)t;
    *lupe_local(f, t, l->name, l->depth) = v;
  } else if (PM_NODE_TYPE_P(t, PM_INSTANCE_VARIABLE_TARGET_NODE)) {
    lupe_ivar_set(f->self, lupe_sym(((const pm_instance_variable_target_node_t *)t)->name), v);
  } else {
    lupe_unsupported(t, "rescue target");
  }
}

/* Whether a rescue clause takes the exception ev of class cls. A clause
   with no class takes a StandardError. */
static int lupe_rescue_takes(LupeFrame *f, const pm_rescue_node_t *r, const char *cls, sp_RbVal ev) {
  /* Fiber#kill unwinds with this: ensure clauses run, no rescue takes it */
  if (!strcmp(cls, "FiberKillSignal")) return 0;
  if (r->exceptions.size == 0) return sp_exc_is_standard_error(cls);
  sp_RbVal t = sp_box_nil();
  SP_GC_ROOT_RBVAL(t);
  for (size_t i = 0; i < r->exceptions.size; i++) {
    const pm_node_t *x = r->exceptions.nodes[i];
    if (PM_NODE_TYPE_P(x, PM_SPLAT_NODE)) {
      t = lupe_eval(f, ((const pm_splat_node_t *)x)->expression);
      for (sp_int j = 0; lupe_is_array_value(t) && j < sp_poly_length(t); j++)
        if (lupe_exc_matches(cls, ev, sp_poly_arr_get(t, j))) return 1;
      continue;
    }
    t = lupe_eval(f, x);
    if (lupe_exc_matches(cls, ev, t)) return 1;
  }
  return 0;
}

/* The rescue clauses for a caught exception: the first that takes it
   runs, else the exception goes on. Sets *retry for a retry. */
static sp_RbVal lupe_rescue_run(LupeFrame *f, const pm_rescue_node_t *r, LupeCaught c, int *retry) {
  sp_Exception *e = lupe_caught_obj(c);
  SP_GC_ROOT(e);
  sp_RbVal ev = lupe_exc_box(e);
  SP_GC_ROOT_RBVAL(ev);
  for (; r; r = r->subsequent) {
    if (!lupe_rescue_takes(f, r, c.cls, ev)) continue;
    sp_rescue_push(e);
    if (r->reference) lupe_rescue_bind(f, r->reference, ev);
    sp_RbVal v = lupe_eval_stmts(f, r->statements);
    sp_rescue_sp--;
    if (L.ctl == LUPE_CTL_RETRY) {
      L.ctl = LUPE_CTL_NONE;
      *retry = 1;
    }
    return v;
  }
  c.obj = e;
  lupe_reraise(c);
}

/* begin/rescue/else: the body under a handler, then the else clause when
   nothing was raised. A rescue clause runs outside the handler, so a raise
   in it goes to the next handler out. */
static sp_RbVal lupe_rescue(LupeFrame *f, const pm_begin_node_t *b) {
  SP_GC_SAVE();
  sp_RbVal v = sp_box_nil();
  SP_GC_ROOT_RBVAL(v);
  const int depth = lupe_fs()->depth;
retry:
  LUPE_TRY_PUSH();
  if (setjmp(sp_exc_stack[sp_exc_top - 1]) == 0) {
    v = lupe_eval_stmts(f, b->statements);
    sp_exc_top--;
  } else {
    LUPE_TRY_LANDED(depth);
    if (sp_unwind_kind != SP_UNWIND_NONE) sp_unwind_resume();
    LupeCaught c = lupe_caught();
    int again = 0;
    v = lupe_rescue_run(f, b->rescue_clause, c, &again);
    if (again) goto retry;
    return v;
  }
  if (L.ctl == LUPE_CTL_NONE && b->else_clause) v = lupe_eval(f, (const pm_node_t *)b->else_clause);
  return v;
}

/* begin/ensure: the ensure clause runs after the body, after a jump out of
   it, and after a raise, which then goes on. A jump in the ensure clause
   takes the place of the jump or the raise that was passing. */
static sp_RbVal lupe_begin(LupeFrame *f, const pm_begin_node_t *b) {
  if (!b->ensure_clause) return b->rescue_clause ? lupe_rescue(f, b) : lupe_eval_stmts(f, b->statements);
  SP_GC_SAVE();
  sp_RbVal v = sp_box_nil();
  SP_GC_ROOT_RBVAL(v);
  const int depth = lupe_fs()->depth;
  LupeCaught c = { NULL, NULL, NULL };
  sp_Exception *exc = NULL;
  SP_GC_ROOT(exc);
  LUPE_TRY_PUSH();
  if (setjmp(sp_exc_stack[sp_exc_top - 1]) == 0) {
    v = b->rescue_clause ? lupe_rescue(f, b) : lupe_eval_stmts(f, b->statements);
    sp_exc_top--;
  } else {
    LUPE_TRY_LANDED(depth);
    if (sp_unwind_kind != SP_UNWIND_NONE) {
      lupe_eval_stmts(f, b->ensure_clause->statements);
      sp_unwind_resume();
    }
    c = lupe_caught();
    exc = lupe_caught_obj(c);
    c.obj = exc;
  }
  /* the jump that passes through the ensure clause */
  LupeCtl ctl = L.ctl;
  sp_RbVal ctl_val = L.ctl_val;
  SP_GC_ROOT_RBVAL(ctl_val);
  const void *ctl_target = L.ctl_target;
  L.ctl = LUPE_CTL_NONE;
  if (exc) sp_rescue_push(exc);
  lupe_eval_stmts(f, b->ensure_clause->statements);
  if (exc) sp_rescue_sp--;
  if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
  L.ctl = ctl;
  L.ctl_val = ctl_val;
  L.ctl_target = ctl_target;
  if (exc) lupe_reraise(c);
  return v;
}

/* Kernel#loop: a StopIteration ends the loop, as Enumerator#next raises
   it at the end, and the loop answers its result. Any other exception
   goes on. */
static sp_RbVal lupe_loop(const LupeBlock *blk) {
  SP_GC_SAVE();
  const int depth = lupe_fs()->depth;
  LUPE_TRY_PUSH();
  if (setjmp(sp_exc_stack[sp_exc_top - 1]) == 0) {
    for (;;) {
      lupe_yield(blk, 0, NULL);
      if (L.ctl != LUPE_CTL_NONE) break;
    }
    sp_exc_top--;
    return sp_box_nil();
  }
  LUPE_TRY_LANDED(depth);
  LupeCaught c = lupe_caught();
  if (!sp_exc_cls_matches(c.cls, "StopIteration")) lupe_reraise(c);
  return sp_exc_result(lupe_caught_obj(c));
}

/* expr rescue fallback: a StandardError from expr gives fallback. */
static sp_RbVal lupe_rescue_modifier(LupeFrame *f, const pm_rescue_modifier_node_t *n) {
  SP_GC_SAVE();
  sp_RbVal v = sp_box_nil();
  SP_GC_ROOT_RBVAL(v);
  const int depth = lupe_fs()->depth;
  LUPE_TRY_PUSH();
  if (setjmp(sp_exc_stack[sp_exc_top - 1]) == 0) {
    v = lupe_eval(f, n->expression);
    sp_exc_top--;
    return v;
  }
  LUPE_TRY_LANDED(depth);
  if (sp_unwind_kind != SP_UNWIND_NONE) sp_unwind_resume();
  LupeCaught c = lupe_caught();
  if (!sp_exc_is_standard_error(c.cls) || !strcmp(c.cls, "FiberKillSignal")) lupe_reraise(c);
  /* $! is the exception while the fallback runs */
  sp_Exception *e = lupe_caught_obj(c);
  SP_GC_ROOT(e);
  sp_rescue_push(e);
  v = lupe_eval(f, n->rescue_expression);
  sp_rescue_sp--;
  return v;
}

/* ---- the evaluator ---- */

static sp_RbVal lupe_eval(LupeFrame *f, const pm_node_t *n) {
  switch (PM_NODE_TYPE(n)) {
  case PM_STATEMENTS_NODE:
    return lupe_eval_stmts(f, (const pm_statements_node_t *)n);
  case PM_PARENTHESES_NODE: {
    const pm_node_t *b = ((const pm_parentheses_node_t *)n)->body;
    return b ? lupe_eval(f, b) : sp_box_nil();
  }
  case PM_INTEGER_NODE:
    return lupe_integer(n, &((const pm_integer_node_t *)n)->value);
  case PM_FLOAT_NODE:
    return sp_box_float(((const pm_float_node_t *)n)->value);
  case PM_STRING_NODE: {
    const pm_string_t *s = &((const pm_string_node_t *)n)->unescaped;
    return lupe_new_string(pm_string_source(s), pm_string_length(s));
  }
  case PM_INTERPOLATED_STRING_NODE:
    return lupe_interpolate(f, &((const pm_interpolated_string_node_t *)n)->parts);
  case PM_SYMBOL_NODE: {
    const pm_string_t *s = &((const pm_symbol_node_t *)n)->unescaped;
    return sp_box_sym(sp_sym_intern_n((const char *)pm_string_source(s), pm_string_length(s)));
  }
  case PM_TRUE_NODE: return sp_box_bool(1);
  case PM_FALSE_NODE: return sp_box_bool(0);
  case PM_NIL_NODE: return sp_box_nil();
  case PM_ARRAY_NODE:
    return lupe_array(f, (const pm_array_node_t *)n);
  case PM_LOCAL_VARIABLE_READ_NODE: {
    const pm_local_variable_read_node_t *r = (const pm_local_variable_read_node_t *)n;
    return *lupe_local(f, n, r->name, r->depth);
  }
  case PM_LOCAL_VARIABLE_WRITE_NODE: {
    const pm_local_variable_write_node_t *w = (const pm_local_variable_write_node_t *)n;
    sp_RbVal v = lupe_eval(f, w->value);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    *lupe_local(f, n, w->name, w->depth) = v;
    return v;
  }
  case PM_LOCAL_VARIABLE_OPERATOR_WRITE_NODE: {
    const pm_local_variable_operator_write_node_t *w = (const pm_local_variable_operator_write_node_t *)n;
    sp_RbVal *slot = lupe_local(f, n, w->name, w->depth);
    sp_RbVal v = lupe_eval(f, w->value);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    SP_GC_ROOT_RBVAL(v);
    *slot = lupe_send(*slot, sp_sym_to_s(lupe_sym(w->binary_operator)), 1, &v);
    return *slot;
  }
  case PM_AND_NODE: {
    const pm_and_node_t *a = (const pm_and_node_t *)n;
    sp_RbVal l = lupe_eval(f, a->left);
    if (L.ctl != LUPE_CTL_NONE || !sp_poly_truthy(l)) return l;
    return lupe_eval(f, a->right);
  }
  case PM_OR_NODE: {
    const pm_or_node_t *o = (const pm_or_node_t *)n;
    sp_RbVal l = lupe_eval(f, o->left);
    if (L.ctl != LUPE_CTL_NONE || sp_poly_truthy(l)) return l;
    return lupe_eval(f, o->right);
  }
  case PM_IF_NODE: {
    const pm_if_node_t *i = (const pm_if_node_t *)n;
    sp_RbVal c = lupe_eval(f, i->predicate);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    if (sp_poly_truthy(c)) return lupe_eval_stmts(f, i->statements);
    return i->subsequent ? lupe_eval(f, i->subsequent) : sp_box_nil();
  }
  case PM_UNLESS_NODE: {
    const pm_unless_node_t *u = (const pm_unless_node_t *)n;
    sp_RbVal c = lupe_eval(f, u->predicate);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    if (!sp_poly_truthy(c)) return lupe_eval_stmts(f, u->statements);
    return u->else_clause ? lupe_eval(f, (const pm_node_t *)u->else_clause) : sp_box_nil();
  }
  case PM_ELSE_NODE:
    return lupe_eval_stmts(f, ((const pm_else_node_t *)n)->statements);
  case PM_WHILE_NODE: {
    const pm_while_node_t *w = (const pm_while_node_t *)n;
    if (PM_NODE_FLAG_P(n, PM_LOOP_FLAGS_BEGIN_MODIFIER)) lupe_unsupported(n, "begin...end while");
    return lupe_while(f, w->predicate, w->statements, 0);
  }
  case PM_UNTIL_NODE: {
    const pm_until_node_t *u = (const pm_until_node_t *)n;
    if (PM_NODE_FLAG_P(n, PM_LOOP_FLAGS_BEGIN_MODIFIER)) lupe_unsupported(n, "begin...end until");
    return lupe_while(f, u->predicate, u->statements, 1);
  }
  case PM_RETURN_NODE:
    return lupe_jump(f, LUPE_CTL_RETURN, ((const pm_return_node_t *)n)->arguments);
  case PM_BREAK_NODE:
    return lupe_jump(f, LUPE_CTL_BREAK, ((const pm_break_node_t *)n)->arguments);
  case PM_NEXT_NODE:
    return lupe_jump(f, LUPE_CTL_NEXT, ((const pm_next_node_t *)n)->arguments);
  case PM_DEF_NODE: {
    const pm_def_node_t *d = (const pm_def_node_t *)n;
    lupe_define(f, d);
    return sp_box_sym(lupe_sym(d->name));
  }
  case PM_CALL_NODE:
    return lupe_call(f, (const pm_call_node_t *)n);
  case PM_CALL_OPERATOR_WRITE_NODE:
    return lupe_call_op_write(f, (const pm_call_operator_write_node_t *)n);
  case PM_INDEX_OPERATOR_WRITE_NODE:
    return lupe_index_op_write(f, (const pm_index_operator_write_node_t *)n);
  case PM_SUPER_NODE: {
    const pm_super_node_t *s = (const pm_super_node_t *)n;
    return lupe_super(f, n, s->arguments, 0, s->block);
  }
  case PM_FORWARDING_SUPER_NODE:
    return lupe_super(f, n, NULL, 1, (const pm_node_t *)((const pm_forwarding_super_node_t *)n)->block);
  case PM_YIELD_NODE: {
    SP_GC_SAVE();
    const pm_arguments_node_t *args = ((const pm_yield_node_t *)n)->arguments;
    if (!f->block) sp_raise_cls("LocalJumpError", "no block given (yield)");
    LUPE_ARGS(f, args, argc, argv, kw, ok);
    if (!ok) return sp_box_nil();
    L.kwhash = kw;
    sp_RbVal v = lupe_yield(f->block, argc, argv);
    L.kwhash = NULL;
    return v;
  }
  case PM_LAMBDA_NODE: {
    LupeBlock b = { .node = n, .home = f, .serial = f->serial, .fs = f->fs, .depth = f->depth, .lambda = 1 };
    return lupe_block_proc(&b);
  }
  case PM_IT_LOCAL_VARIABLE_READ_NODE:
    return *f->it;
  case PM_RANGE_NODE:
    return lupe_range(f, (const pm_range_node_t *)n);
  case PM_HASH_NODE:
    return lupe_hash_lit(f, &((const pm_hash_node_t *)n)->elements);
  case PM_KEYWORD_HASH_NODE:
    return lupe_hash_lit(f, &((const pm_keyword_hash_node_t *)n)->elements);
  case PM_INDEX_OR_WRITE_NODE:
  case PM_INDEX_AND_WRITE_NODE:
    return lupe_index_logic_write(f, n);
  case PM_SELF_NODE:
    return f->self;
  case PM_CLASS_NODE: {
    const pm_class_node_t *c = (const pm_class_node_t *)n;
    return lupe_class_def(f, c->constant_path, c->superclass, c->body, &c->locals, 0);
  }
  case PM_MODULE_NODE: {
    const pm_module_node_t *m = (const pm_module_node_t *)n;
    return lupe_class_def(f, m->constant_path, NULL, m->body, &m->locals, 1);
  }
  case PM_SINGLETON_CLASS_NODE:
    return lupe_singleton_class(f, (const pm_singleton_class_node_t *)n);
  case PM_CONSTANT_READ_NODE:
  case PM_CONSTANT_PATH_NODE:
    return lupe_const_read(f, n);
  case PM_CONSTANT_WRITE_NODE: {
    const pm_constant_write_node_t *w = (const pm_constant_write_node_t *)n;
    sp_RbVal v = lupe_eval(f, w->value);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    lupe_const_set(f->cref, lupe_sym(w->name), v);
    return v;
  }
  case PM_CONSTANT_PATH_WRITE_NODE: {
    const pm_constant_path_write_node_t *w = (const pm_constant_path_write_node_t *)n;
    LupeClass *scope;
    sp_sym name;
    lupe_const_target(f, (const pm_node_t *)w->target, &scope, &name);
    sp_RbVal v = lupe_eval(f, w->value);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    lupe_const_set(scope, name, v);
    return v;
  }
  case PM_INSTANCE_VARIABLE_READ_NODE:
    return lupe_ivar_get(f->self, lupe_sym(((const pm_instance_variable_read_node_t *)n)->name));
  case PM_INSTANCE_VARIABLE_WRITE_NODE: {
    const pm_instance_variable_write_node_t *w = (const pm_instance_variable_write_node_t *)n;
    sp_RbVal v = lupe_eval(f, w->value);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    lupe_ivar_set(f->self, lupe_sym(w->name), v);
    return v;
  }
  case PM_INSTANCE_VARIABLE_OPERATOR_WRITE_NODE: {
    const pm_instance_variable_operator_write_node_t *w = (const pm_instance_variable_operator_write_node_t *)n;
    sp_sym name = lupe_sym(w->name);
    sp_RbVal v = lupe_eval(f, w->value);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    sp_RbVal nv = lupe_op(lupe_ivar_get(f->self, name), w->binary_operator, v);
    lupe_ivar_set(f->self, name, nv);
    return nv;
  }
  case PM_INSTANCE_VARIABLE_OR_WRITE_NODE: {
    const pm_instance_variable_or_write_node_t *w = (const pm_instance_variable_or_write_node_t *)n;
    sp_sym name = lupe_sym(w->name);
    sp_RbVal cur = lupe_ivar_get(f->self, name);
    if (sp_poly_truthy(cur)) return cur;
    sp_RbVal v = lupe_eval(f, w->value);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    lupe_ivar_set(f->self, name, v);
    return v;
  }
  case PM_LOCAL_VARIABLE_OR_WRITE_NODE: {
    const pm_local_variable_or_write_node_t *w = (const pm_local_variable_or_write_node_t *)n;
    sp_RbVal *slot = lupe_local(f, n, w->name, w->depth);
    if (sp_poly_truthy(*slot)) return *slot;
    sp_RbVal v = lupe_eval(f, w->value);
    if (L.ctl != LUPE_CTL_NONE) return sp_box_nil();
    *slot = v;
    return v;
  }
  case PM_BEGIN_NODE:
    return lupe_begin(f, (const pm_begin_node_t *)n);
  case PM_RESCUE_MODIFIER_NODE:
    return lupe_rescue_modifier(f, (const pm_rescue_modifier_node_t *)n);
  case PM_RETRY_NODE:
    L.ctl = LUPE_CTL_RETRY;
    L.ctl_val = sp_box_nil();
    return sp_box_nil();
  case PM_GLOBAL_VARIABLE_READ_NODE: {
    /* $!: the exception that a rescue handles; $~: the last match */
    const char *g = sp_sym_to_s(lupe_sym(((const pm_global_variable_read_node_t *)n)->name));
    if (!strcmp(g, "$~")) return lupe_re_last();
    if (strcmp(g, "$!")) return lupe_gvar_eval(f, n);
    return sp_rescue_sp > 0 ? lupe_exc_box((sp_Exception *)sp_exc_handling[sp_rescue_sp - 1]) : sp_box_nil();
  }
  case PM_GLOBAL_VARIABLE_WRITE_NODE:
  case PM_GLOBAL_VARIABLE_OPERATOR_WRITE_NODE:
  case PM_GLOBAL_VARIABLE_OR_WRITE_NODE:
  case PM_GLOBAL_VARIABLE_AND_WRITE_NODE:
    return lupe_gvar_eval(f, n);
  case PM_REGULAR_EXPRESSION_NODE:
  case PM_INTERPOLATED_REGULAR_EXPRESSION_NODE:
    return lupe_re_literal(f, n);
  case PM_MATCH_WRITE_NODE:
    return lupe_re_match_write(f, (const pm_match_write_node_t *)n);
  case PM_NUMBERED_REFERENCE_READ_NODE:
    return lupe_re_nth((int)((const pm_numbered_reference_read_node_t *)n)->number);
  case PM_BACK_REFERENCE_READ_NODE:
    return lupe_re_backref(sp_sym_to_s(lupe_sym(((const pm_back_reference_read_node_t *)n)->name)));
  default:
    lupe_unsupported(n, "node");
  }
}
