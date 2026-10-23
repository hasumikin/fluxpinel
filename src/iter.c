/* iter.c -- the builtin methods that take a block, Proc and Range.

   spinel emits a builtin method with a block as an inline C loop, so no
   runtime function or generated wrapper calls a block. Fluxpinel writes these
   methods here, on the poly API, and calls the block with fx_yield.

   After each block call, a method returns at once when a jump is pending
   (L.ctl): a break ends the call that passed the block, and a return
   ends a method further out. */

#include "fluxpinel.h"

static sp_RbVal fx_pair(sp_RbVal a, sp_RbVal b);

/* ---- sequences: the elements of an Array, an Integer Range or a Hash ---- */

typedef struct {
  sp_RbVal src;           /* rooted by the caller: the receiver, the items of a
                             finite Enumerator, or a walker over a generator */
  int range;
  int walk;               /* src is a walker (sp_enum_walker_boxed) */
  sp_PolyPolyHash *hash;  /* a Hash: src itself, or a poly copy of a typed one */
  sp_int i, last;
} FxSeq;

/* The caller roots a FxSeq with this, after fx_seq_open. */
#define FX_SEQ_ROOT(s) SP_GC_ROOT((s).hash); SP_GC_ROOT_RBVAL((s).src)

static int fx_is_range(sp_RbVal v) {
  return v.tag == SP_TAG_OBJ && v.cls_id == SP_BUILTIN_RANGE && v.v.p;
}

static int fx_is_hash(sp_RbVal v) {
  return v.tag == SP_TAG_OBJ && sp_poly_is_hash_kind(v.cls_id) && v.v.p;
}

/* The caller roots s->src. A typed Hash gives a poly copy, which the
   caller must root through s->hash when it allocates. */
/* What each { } answers: the receiver, or for an Enumerator made from a
   value (`4.times`) that value. A generator would run again, so it answers
   itself. */
static sp_RbVal fx_each_answer(sp_RbVal r) {
  if (r.tag != SP_TAG_OBJ || r.cls_id != SP_BUILTIN_ENUMERATOR || !r.v.p) return r;
  const sp_Enumerator *e = (const sp_Enumerator *)r.v.p;
  return !e->gen && e->has_src ? e->source : r;
}

static int fx_seq_open(sp_RbVal r, FxSeq *s) {
  s->src = r;
  s->i = 0;
  s->range = 0;
  s->walk = 0;
  s->hash = NULL;
  if (fx_is_array(r)) return 1;
  /* an Enumerator: its items, or one item at a time from a generator, so
     that an endless one stops where the block breaks */
  if (r.tag == SP_TAG_OBJ && r.cls_id == SP_BUILTIN_ENUMERATOR && r.v.p) {
    /* a live one (each_char of a String handle) is read as a copy that
       holds its items as they are now */
    r = s->src = sp_box_obj(sp_enum_walk((sp_Enumerator *)r.v.p), SP_BUILTIN_ENUMERATOR);
    SP_GC_ROOT_RBVAL(s->src);
    sp_PolyArray *items = sp_enum_finite_items(r);
    if (items) { s->src = sp_box_poly_array(items); return 1; }
    s->src = sp_enum_walker_boxed(r);
    if (s->src.tag == SP_TAG_NIL) s->src = sp_box_poly_array(sp_Enumerator_to_a((sp_Enumerator *)r.v.p));
    else s->walk = 1;
    return 1;
  }
  if (fx_is_hash(r)) {
    s->hash = sp_poly_as_poly_poly_hash(r);
    return 1;
  }
  /* a String Range walks its Array of Strings (or of Symbols); a Float
     Range does not walk, as in CRuby */
  if (r.tag == SP_TAG_OBJ && r.cls_id == SP_BUILTIN_STR_RANGE && r.v.p) {
    s->src = fx_send(r, "to_a", 0, NULL);
    return 1;
  }
  if (r.tag == SP_TAG_OBJ && r.cls_id == SP_BUILTIN_FLOAT_RANGE && r.v.p) {
    const sp_FloatRange *g = (const sp_FloatRange *)r.v.p;
    sp_raise_cls("TypeError", g->omitted & SP_FRANGE_NO_BEGIN ? "can't iterate from NilClass"
                                                               : "can't iterate from Float");
  }
  if (!fx_is_range(r)) return 0;
  const sp_Range *g = (const sp_Range *)r.v.p;
  if (g->nobeg) sp_raise_cls("TypeError", "can't iterate from NilClass");
  s->range = 1;
  s->i = g->first;
  s->last = g->noend ? INT64_MAX : g->excl ? g->last - 1 : g->last;
  return 1;
}

/* An Array is read live, so that elements that the block appends are seen. */
static int fx_seq_next(FxSeq *s, sp_RbVal *v) {
  if (s->range) {
    if (s->i > s->last) return 0;
    *v = sp_box_int(s->i);
    if (s->i == INT64_MAX) s->last = INT64_MIN;   /* an endless Range stops at the last word */
    else s->i++;
    return 1;
  }
  if (s->walk) {
    if (s->i >= sp_enum_walk_len(s->src)) return 0;
    *v = sp_enum_walk_at(s->src, s->i++);
    return 1;
  }
  if (s->hash) {
    if (s->i >= s->hash->len) return 0;
    sp_int slot = s->hash->order[s->i++];
    *v = fx_pair(s->hash->keys[slot], s->hash->vals[slot]);
    return 1;
  }
  if (s->i >= sp_poly_length(s->src)) return 0;
  *v = sp_poly_arr_get(s->src, s->i++);
  return 1;
}

/* The elements of a sequence as a new Array. */
static sp_PolyArray *fx_seq_array(sp_RbVal r) {
  sp_PolyArray *a = sp_PolyArray_new();
  SP_GC_ROOT(a);
  FxSeq s;
  sp_RbVal v;
  fx_seq_open(r, &s);
  FX_SEQ_ROOT(s);
  while (fx_seq_next(&s, &v)) {
    /* a generator step that yielded several values is an Array as a value */
    if (SP_UNLIKELY(sp_poly_is_pack(v))) v = sp_box_poly_array(sp_PolyArray_dup((sp_PolyArray *)v.v.p));
    sp_PolyArray_push(a, v);
  }
  return a;
}

/* ---- helpers ---- */

static sp_RbVal fx_y1(const FxBlock *b, sp_RbVal v) {
  SP_GC_ROOT_RBVAL(v);
  /* the item of a generator step that yielded several values: the block
     takes them as several arguments */
  if (SP_UNLIKELY(sp_poly_is_pack(v))) return fx_yield(b, (int)sp_poly_length(v), ((sp_PolyArray *)v.v.p)->data);
  return fx_yield(b, 1, &v);
}

static sp_RbVal fx_y2(const FxBlock *b, sp_RbVal v, sp_RbVal w) {
  sp_RbVal argv[2] = { v, w };
  SP_GC_ROOT_RBVAL(argv[0]);
  SP_GC_ROOT_RBVAL(argv[1]);
  return fx_yield(b, 2, argv);
}

static sp_RbVal fx_pair(sp_RbVal a, sp_RbVal b) {
  SP_GC_ROOT_RBVAL(a);
  SP_GC_ROOT_RBVAL(b);
  sp_PolyArray *p = sp_PolyArray_new();
  SP_GC_ROOT(p);
  sp_PolyArray_push(p, a);
  sp_PolyArray_push(p, b);
  return sp_box_poly_array(p);
}

/* a <=> b as a C int, through the methods of the program. */
static sp_int fx_cmp(sp_RbVal a, sp_RbVal b) {
  SP_GC_ROOT_RBVAL(a);
  SP_GC_ROOT_RBVAL(b);
  sp_RbVal c = fx_send(a, "<=>", 1, &b);
  if (c.tag != SP_TAG_INT) fx_cmp_failed(a, b);
  return c.v.i;
}

/* The result of a sort block: an Integer, or a value that compares with 0. */
static sp_int fx_cmp_result(sp_RbVal c, sp_RbVal a, sp_RbVal b) {
  if (c.tag == SP_TAG_INT) return c.v.i;
  if (c.tag == SP_TAG_NIL) fx_cmp_failed(a, b);
  return fx_cmp(c, sp_box_int(0));
}

/* A stable merge sort of the indexes idx[0..n) of vals. With blk, the
   block compares two values; else keys (or vals) compare with <=>. */
typedef struct {
  sp_PolyArray *vals, *keys;
  const FxBlock *blk;
} FxSortCtx;

static sp_int fx_sort_cmp(const FxSortCtx *c, sp_int i, sp_int j) {
  if (L.ctl != FX_CTL_NONE) return 0;
  sp_RbVal a = c->vals->data[i], b = c->vals->data[j];
  if (c->blk) return fx_cmp_result(fx_y2(c->blk, a, b), a, b);
  if (c->keys) return fx_cmp(c->keys->data[i], c->keys->data[j]);
  return fx_cmp(a, b);
}

static void fx_merge_sort(const FxSortCtx *c, sp_int *idx, sp_int *tmp, sp_int n) {
  if (n < 2) return;
  sp_int h = n / 2;
  fx_merge_sort(c, idx, tmp, h);
  fx_merge_sort(c, idx + h, tmp, n - h);
  sp_int i = 0, j = h, k = 0;
  while (i < h && j < n) tmp[k++] = fx_sort_cmp(c, idx[j], idx[i]) < 0 ? idx[j++] : idx[i++];
  while (i < h) tmp[k++] = idx[i++];
  while (j < n) tmp[k++] = idx[j++];
  memcpy(idx, tmp, sizeof *idx * (size_t)n);
}

static sp_RbVal fx_sorted(sp_PolyArray *vals, sp_PolyArray *keys, const FxBlock *blk) {
  SP_GC_ROOT(vals);
  SP_GC_ROOT(keys);
  sp_int n = vals->len;
  sp_int *idx = malloc(sizeof *idx * (size_t)(n ? n : 1)), *tmp = malloc(sizeof *tmp * (size_t)(n ? n : 1));
  for (sp_int i = 0; i < n; i++) idx[i] = i;
  FxSortCtx c = { vals, keys, blk };
  fx_merge_sort(&c, idx, tmp, n);
  sp_PolyArray *out = sp_PolyArray_new();
  SP_GC_ROOT(out);
  for (sp_int i = 0; i < n; i++) sp_PolyArray_push(out, vals->data[idx[i]]);
  free(idx);
  free(tmp);
  return sp_box_poly_array(out);
}

/* Replaces the elements of a mutable Array with those of src. */
static void fx_array_replace(sp_RbVal r, sp_PolyArray *src) {
  sp_PolyArray *a = (sp_PolyArray *)r.v.p;
  a->len = 0;
  for (sp_int i = 0; i < src->len; i++) sp_PolyArray_push(a, src->data[i]);
}

/* ---- the methods ---- */

#define FX_IS(s) (!strcmp(name, (s)))
#define FX_STOP() do { if (L.ctl != FX_CTL_NONE) { *out = sp_box_nil(); return 1; } } while (0)

/* The methods of an Array or a Range that take a block. */
static int fx_seq_iter(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                         sp_RbVal *out) {
  FxSeq s;
  sp_RbVal v = sp_box_nil(), y = sp_box_nil();
  SP_GC_ROOT_RBVAL(v);
  SP_GC_ROOT_RBVAL(y);
  sp_PolyArray *acc = NULL;
  SP_GC_ROOT(acc);
  /* a beginless Range counts down from its end */
  if (argc == 0 && FX_IS("reverse_each") && fx_is_range(r) && ((const sp_Range *)r.v.p)->nobeg) {
    const sp_Range *g = (const sp_Range *)r.v.p;
    if (g->noend) sp_raise_cls("TypeError", "can't iterate from NilClass");
    for (sp_int i = g->excl ? g->last - 1 : g->last;; i--) {
      fx_y1(blk, sp_box_int(i));
      FX_STOP();
      if (i == INT64_MIN) break;
    }
    *out = r;
    return 1;
  }
  if (!fx_seq_open(r, &s)) return 0;
  FX_SEQ_ROOT(s);
  if (argc == 0 && (FX_IS("each") || FX_IS("each_entry") || (s.hash && FX_IS("each_pair")))) {
    while (fx_seq_next(&s, &v)) { fx_y1(blk, v); FX_STOP(); }
    *out = fx_each_answer(r);
    return 1;
  }
  /* each_with_index, and Enumerator#with_index(offset) */
  if ((argc == 0 && FX_IS("each_with_index")) ||
      (FX_IS("with_index") && (argc == 0 || (argc == 1 && argv[0].tag == SP_TAG_INT)))) {
    sp_int from = argc == 1 ? argv[0].v.i : 0;
    for (sp_int i = from; fx_seq_next(&s, &v); i++) { fx_y2(blk, v, sp_box_int(i)); FX_STOP(); }
    *out = fx_each_answer(r);
    return 1;
  }
  if (argc == 0 && FX_IS("reverse_each")) {
    acc = fx_seq_array(r);
    for (sp_int i = acc->len - 1; i >= 0; i--) {
      fx_y1(blk, acc->data[i]);
      FX_STOP();
    }
    *out = r;
    return 1;
  }
  if (argc == 1 && FX_IS("each_with_object")) {
    sp_RbVal memo = argv[0];
    while (fx_seq_next(&s, &v)) { fx_y2(blk, v, memo); FX_STOP(); }
    *out = memo;
    return 1;
  }
  int map = FX_IS("map") || FX_IS("collect"), flat = FX_IS("flat_map") || FX_IS("collect_concat");
  int sel = FX_IS("select") || FX_IS("filter"), rej = FX_IS("reject"), fmap = FX_IS("filter_map");
  if (argc == 0 && (map || flat || sel || rej || fmap)) {
    acc = sp_PolyArray_new();
    while (fx_seq_next(&s, &v)) {
      y = fx_y1(blk, v);
      FX_STOP();
      if (map) sp_PolyArray_push(acc, y);
      else if (fmap) { if (sp_poly_truthy(y)) sp_PolyArray_push(acc, y); }
      else if (flat) {
        if (!fx_is_array(y)) { sp_PolyArray_push(acc, y); continue; }
        for (sp_int i = 0; i < sp_poly_length(y); i++) sp_PolyArray_push(acc, sp_poly_arr_get(y, i));
      } else if (sp_poly_truthy(y) == sel) sp_PolyArray_push(acc, v);
    }
    *out = sp_box_poly_array(acc);
    return 1;
  }
  int find = FX_IS("find") || FX_IS("detect");
  if (argc == 0 && (find || FX_IS("find_index") || FX_IS("index"))) {
    for (sp_int i = 0; fx_seq_next(&s, &v); i++) {
      y = fx_y1(blk, v);
      FX_STOP();
      if (sp_poly_truthy(y)) { *out = find ? v : sp_box_int(i); return 1; }
    }
    *out = sp_box_nil();
    return 1;
  }
  if (argc == 0 && (FX_IS("count") || FX_IS("any?") || FX_IS("all?") || FX_IS("none?") || FX_IS("one?"))) {
    sp_int n = 0, total = 0;
    while (fx_seq_next(&s, &v)) {
      y = fx_y1(blk, v);
      FX_STOP();
      total++;
      if (sp_poly_truthy(y)) n++;
      if (FX_IS("any?") && n) break;
      if (FX_IS("all?") && n < total) break;
      if (FX_IS("none?") && n) break;
    }
    if (name[0] == 'c') *out = sp_box_int(n);
    else if (FX_IS("any?")) *out = sp_box_bool(n > 0);
    else if (FX_IS("all?")) *out = sp_box_bool(n == total);
    else if (FX_IS("none?")) *out = sp_box_bool(n == 0);
    else *out = sp_box_bool(n == 1);
    return 1;
  }
  if (argc <= 1 && (FX_IS("inject") || FX_IS("reduce"))) {
    int have = argc == 1;
    sp_RbVal a = have ? argv[0] : sp_box_nil();
    SP_GC_ROOT_RBVAL(a);
    while (fx_seq_next(&s, &v)) {
      if (!have) { a = v; have = 1; continue; }
      a = fx_y2(blk, a, v);
      FX_STOP();
    }
    *out = a;
    return 1;
  }
  if (argc <= 1 && FX_IS("sum")) {
    acc = sp_PolyArray_new();
    int numeric = argc == 0 || fx_is_integer(argv[0]) || argv[0].tag == SP_TAG_FLT;
    while (fx_seq_next(&s, &v)) {
      y = fx_y1(blk, v);
      FX_STOP();
      sp_PolyArray_push(acc, y);
      if (!fx_is_integer(y) && y.tag != SP_TAG_FLT) numeric = 0;
    }
    /* numbers sum as the runtime sums them (Kahan-Babuska for Float);
       other values fold with +, from the seed */
    if (numeric) {
      y = sp_poly_sum(sp_box_poly_array(acc));
      *out = argc == 1 ? fx_send(argv[0], "+", 1, &y) : y;
      return 1;
    }
    y = argc == 1 ? argv[0] : sp_box_int(0);
    for (sp_int i = 0; i < acc->len; i++) y = fx_send(y, "+", 1, &acc->data[i]);
    *out = y;
    return 1;
  }
  if (argc == 0 && (FX_IS("min_by") || FX_IS("max_by"))) {
    sp_RbVal best = sp_box_nil(), bkey = sp_box_nil();
    SP_GC_ROOT_RBVAL(best);
    SP_GC_ROOT_RBVAL(bkey);
    int have = 0, max = name[1] == 'a';
    while (fx_seq_next(&s, &v)) {
      y = fx_y1(blk, v);
      FX_STOP();
      if (!have || (max ? fx_cmp(y, bkey) > 0 : fx_cmp(y, bkey) < 0)) { best = v; bkey = y; have = 1; }
    }
    *out = best;
    return 1;
  }
  if (argc == 0 && (FX_IS("min") || FX_IS("max"))) {
    sp_RbVal best = sp_box_nil();
    SP_GC_ROOT_RBVAL(best);
    int have = 0, max = name[1] == 'a';
    while (fx_seq_next(&s, &v)) {
      if (have) {
        y = fx_y2(blk, v, best);
        FX_STOP();
        sp_int c = fx_cmp_result(y, v, best);
        if (max ? c <= 0 : c >= 0) continue;
      }
      best = v;
      have = 1;
    }
    *out = best;
    return 1;
  }
  if (argc == 0 && (FX_IS("sort_by") || FX_IS("sort"))) {
    acc = fx_seq_array(r);
    sp_PolyArray *keys = NULL;
    SP_GC_ROOT(keys);
    if (name[4] == '_') {
      keys = sp_PolyArray_new();
      for (sp_int i = 0; i < acc->len; i++) {
        y = fx_y1(blk, acc->data[i]);
        FX_STOP();
        sp_PolyArray_push(keys, y);
      }
    }
    *out = fx_sorted(acc, keys, keys ? NULL : blk);
    FX_STOP();
    return 1;
  }
  if (argc == 0 && (FX_IS("group_by") || FX_IS("to_h"))) {
    sp_PolyPolyHash *h = sp_PolyPolyHash_new();
    SP_GC_ROOT(h);
    int group = name[0] == 'g';
    while (fx_seq_next(&s, &v)) {
      y = fx_y1(blk, v);
      FX_STOP();
      if (group) {
        sp_RbVal list = sp_PolyPolyHash_get(h, y);
        if (list.tag == SP_TAG_NIL) {
          acc = sp_PolyArray_new();
          sp_PolyPolyHash_set(h, y, sp_box_poly_array(acc));
          list = sp_box_poly_array(acc);
        }
        sp_PolyArray_push((sp_PolyArray *)list.v.p, v);
      } else {
        if (!fx_is_array(y) || sp_poly_length(y) != 2)
          sp_raise_cls("TypeError", sp_sprintf("wrong element type %s (expected array)", sp_poly_class_name(y)));
        sp_PolyPolyHash_set(h, sp_poly_arr_get(y, 0), sp_poly_arr_get(y, 1));
      }
    }
    *out = sp_box_obj(h, SP_BUILTIN_POLY_POLY_HASH);
    return 1;
  }
  if (argc == 0 && FX_IS("partition")) {
    sp_PolyArray *yes = sp_PolyArray_new(), *no = NULL;
    SP_GC_ROOT(yes);
    SP_GC_ROOT(no);
    no = sp_PolyArray_new();
    while (fx_seq_next(&s, &v)) {
      y = fx_y1(blk, v);
      FX_STOP();
      sp_PolyArray_push(sp_poly_truthy(y) ? yes : no, v);
    }
    *out = fx_pair(sp_box_poly_array(yes), sp_box_poly_array(no));
    return 1;
  }
  if (argc == 0 && (FX_IS("take_while") || FX_IS("drop_while"))) {
    acc = sp_PolyArray_new();
    int take = name[0] == 't', dropping = 1;
    while (fx_seq_next(&s, &v)) {
      if (take || dropping) {
        y = fx_y1(blk, v);
        FX_STOP();
        if (take && !sp_poly_truthy(y)) break;
        if (!take && sp_poly_truthy(y)) continue;
        dropping = 0;
      }
      sp_PolyArray_push(acc, v);
    }
    *out = sp_box_poly_array(acc);
    return 1;
  }
  if (argc == 1 && (FX_IS("each_slice") || FX_IS("each_cons")) && argv[0].tag == SP_TAG_INT) {
    sp_int n = argv[0].v.i;
    if (n <= 0) sp_raise_cls("ArgumentError", sp_sprintf("invalid %s", name[5] == 's' ? "slice size" : "size"));
    /* one item at a time, so that an endless generator stops at a break */
    int cons = name[5] == 'c';
    sp_PolyArray *win = sp_PolyArray_new();
    SP_GC_ROOT(win);
    while (fx_seq_next(&s, &v)) {
      sp_PolyArray_push(win, v);
      if (win->len < n) continue;
      acc = win;
      win = sp_PolyArray_new();
      /* each_cons keeps the last n - 1 items for the next window */
      for (sp_int j = 1; cons && j < acc->len; j++) sp_PolyArray_push(win, acc->data[j]);
      fx_y1(blk, sp_box_poly_array(acc));
      FX_STOP();
    }
    if (!cons && win->len) {
      fx_y1(blk, sp_box_poly_array(win));
      FX_STOP();
    }
    *out = r;
    return 1;
  }
  /* the mutators of an Array that the program built (a poly Array) */
  int mmap = FX_IS("map!") || FX_IS("collect!");
  int msel = FX_IS("select!") || FX_IS("filter!") || FX_IS("keep_if");
  int mrej = FX_IS("reject!") || FX_IS("delete_if");
  if (argc == 0 && (mmap || msel || mrej)) {
    if (r.cls_id != SP_BUILTIN_POLY_ARRAY) fx_gap(sp_sprintf("%s on a typed Array", name));
    acc = sp_PolyArray_new();
    sp_int before = sp_poly_length(r);
    while (fx_seq_next(&s, &v)) {
      y = fx_y1(blk, v);
      FX_STOP();
      if (mmap) sp_PolyArray_push(acc, y);
      else if (sp_poly_truthy(y) == msel) sp_PolyArray_push(acc, v);
    }
    fx_array_replace(r, acc);
    int bang = name[strlen(name) - 1] == '!';
    *out = !mmap && bang && acc->len == before ? sp_box_nil() : r;
    return 1;
  }
  return 0;
}

/* Integer#times, #upto, #downto and #step with Integer bounds. */
static int fx_int_iter(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                         sp_RbVal *out) {
  if (r.tag != SP_TAG_INT) return 0;
  sp_int from = r.v.i, to, step = 1;
  if (argc == 0 && FX_IS("times")) {
    from = 0;
    to = r.v.i - 1;
  } else if (argc == 1 && (FX_IS("upto") || FX_IS("downto")) && argv[0].tag == SP_TAG_INT) {
    to = argv[0].v.i;
    if (name[0] == 'd') step = -1;
  } else if (argc == 2 && FX_IS("step") && argv[0].tag == SP_TAG_INT && argv[1].tag == SP_TAG_INT) {
    to = argv[0].v.i;
    step = argv[1].v.i;
    if (step == 0) sp_raise_cls("ArgumentError", "step can't be 0");
  } else {
    return 0;
  }
  /* with no block: an Enumerator over the values */
  sp_PolyArray *a = blk ? NULL : sp_PolyArray_new();
  SP_GC_ROOT(a);
  for (sp_int i = from; step > 0 ? i <= to : i >= to; i += step) {
    if (a) {
      sp_PolyArray_push(a, sp_box_int(i));
    } else {
      fx_y1(blk, sp_box_int(i));
      FX_STOP();
    }
    if ((step > 0 && i > INT64_MAX - step) || (step < 0 && i < INT64_MIN - step)) break;
  }
  if (!a) { *out = r; return 1; }
  /* each on the Enumerator answers the Integer, as `4.times.each { }` does */
  sp_Enumerator *e = sp_Enumerator_new_from_items(a);
  sp_gc_wb(e);
  e->source = r;
  e->has_src = TRUE;
  *out = sp_box_obj(e, SP_BUILTIN_ENUMERATOR);
  return 1;
}

/* String#each_char: one String per UTF-8 character. */
static int fx_str_iter(sp_RbVal r, const char *name, int argc, const FxBlock *blk, sp_RbVal *out) {
  if (!fx_is_string(r) || argc != 0 || !FX_IS("each_char")) return 0;
  SP_GC_ROOT_RBVAL(r);
  /* read the bytes again after each block call, which can collect */
  for (size_t i = 0; i < strlen(sp_poly_recv_s(r, name));) {
    const char *s = sp_poly_recv_s(r, name);
    size_t n = strlen(s), k = 1;
    while (i + k < n && ((unsigned char)s[i + k] & 0xC0) == 0x80) k++;
    char *c = sp_str_alloc(k);
    memcpy(c, s + i, k);
    c[k] = 0;
    fx_y1(blk, sp_box_str_new(c));
    FX_STOP();
    i += k;
  }
  *out = r;
  return 1;
}

/* inject(:sym) and inject(init, :sym): a method name in place of a block. */
static int fx_inject_sym(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (!(FX_IS("inject") || FX_IS("reduce")) || argc < 1 || argc > 2) return 0;
  if (argv[argc - 1].tag != SP_TAG_SYM) return 0;
  FxSeq s;
  if (!fx_seq_open(r, &s)) return 0;
  FX_SEQ_ROOT(s);
  const char *op = sp_sym_to_s((sp_sym)argv[argc - 1].v.i);
  sp_RbVal a = argc == 2 ? argv[0] : sp_box_nil(), v = sp_box_nil();
  SP_GC_ROOT_RBVAL(a);
  SP_GC_ROOT_RBVAL(v);
  int have = argc == 2;
  while (fx_seq_next(&s, &v)) {
    if (!have) { a = v; have = 1; continue; }
    a = fx_send(a, op, 1, &v);
  }
  *out = a;
  return 1;
}

/* The builtin methods that take a block, and inject with a Symbol. */
static int fx_iter(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk, sp_RbVal *out) {
  if (!blk) return fx_inject_sym(r, name, argc, argv, out) || fx_int_iter(r, name, argc, argv, NULL, out);
  if (argc == 0 && FX_IS("tap")) {
    fx_y1(blk, r);
    FX_STOP();
    *out = r;
    return 1;
  }
  if (argc == 0 && (FX_IS("then") || FX_IS("yield_self"))) {
    *out = fx_y1(blk, r);
    return 1;
  }
  if (r.tag == SP_TAG_CLASS) {
    FxClass *k = fx_class_value(r);
    if (k && !strcmp(k->name, "Proc") && FX_IS("new") && argc == 0) {
      *out = fx_block_proc(blk);
      return 1;
    }
    if (k && !strcmp(k->name, "Array") && FX_IS("new") && argc == 1 && argv[0].tag == SP_TAG_INT) {
      if (argv[0].v.i < 0) sp_raise_cls("ArgumentError", "negative array size");
      sp_PolyArray *a = sp_PolyArray_new();
      SP_GC_ROOT(a);
      for (sp_int i = 0; i < argv[0].v.i; i++) {
        sp_RbVal y = fx_y1(blk, sp_box_int(i));
        FX_STOP();
        sp_PolyArray_push(a, y);
      }
      *out = sp_box_poly_array(a);
      return 1;
    }
    return 0;
  }
  return fx_seq_iter(r, name, argc, argv, blk, out) || fx_int_iter(r, name, argc, argv, blk, out) ||
         fx_str_iter(r, name, argc, blk, out);
}

/* ---- Proc ---- */

static int fx_proc_arity(const FxBlock *b) {
  if (!b->node) return -2;   /* &:sym, as CRuby answers for a lambda from a Symbol */
  const pm_constant_id_list_t *locals;
  const pm_node_t *params, *body;
  fx_block_parts(b->node, &locals, &params, &body);
  if (!params) return 0;
  if (PM_NODE_TYPE_P(params, PM_IT_PARAMETERS_NODE)) return 1;
  if (PM_NODE_TYPE_P(params, PM_NUMBERED_PARAMETERS_NODE)) return ((const pm_numbered_parameters_node_t *)params)->maximum;
  const pm_parameters_node_t *ps = ((const pm_block_parameters_node_t *)params)->parameters;
  if (!ps) return 0;
  /* CRuby: min counts the required parameters, plus 1 for any required
     keyword. A lambda with an optional part, or a proc with *rest,
     answers -(min + 1). */
  int req = (int)(ps->requireds.size + ps->posts.size);
  int rest = ps->rest && PM_NODE_TYPE_P(ps->rest, PM_REST_PARAMETER_NODE);
  int reqkw = 0, optkw = ps->keyword_rest != NULL;
  for (size_t i = 0; i < ps->keywords.size; i++) {
    if (PM_NODE_TYPE_P(ps->keywords.nodes[i], PM_REQUIRED_KEYWORD_PARAMETER_NODE)) reqkw = 1;
    else optkw = 1;
  }
  int min = req + reqkw;
  int opt = b->lambda ? rest || ps->optionals.size || (!reqkw && optkw) : rest;
  return opt ? -(min + 1) : min;
}

static int fx_proc_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  FxProc *p = (FxProc *)r.v.p;
  if (FX_IS("call") || FX_IS("()") || FX_IS("yield") || FX_IS("[]") || FX_IS("===")) {
    SP_GC_ROOT_RBVAL(r);
    *out = fx_yield_x(&p->b, argc, argv, 1);
    return 1;
  }
  if (argc == 0 && FX_IS("to_proc")) { *out = r; return 1; }
  if (argc == 0 && FX_IS("lambda?")) { *out = sp_box_bool(p->b.lambda); return 1; }
  if (argc == 0 && FX_IS("arity")) { *out = sp_box_int(fx_proc_arity(&p->b)); return 1; }
  return 0;
}

/* ---- Range ---- */

/* The indexes [*beg, *end) that values_at reads for an Integer Range over
   n values, as CRuby's rb_range_beg_len with err = 1 reads them: a begin
   before index 0 raises, and an index past the last reads nil. */
static void fx_range_span(sp_RbVal r, sp_int n, sp_int *beg, sp_int *end) {
  const sp_Range *g = (const sp_Range *)r.v.p;
  sp_int b = g->nobeg ? 0 : g->first, e = g->noend ? -1 : g->last;
  if (b < 0 && (b += n) < 0) sp_raise_cls("RangeError", sp_sprintf("%s out of range", sp_poly_inspect(r)));
  if (e < 0) e += n;
  if (g->noend || !g->excl) e++;
  *beg = b;
  *end = e;
}

/* The methods of an Integer Range with no block. */
/* The begin or end of a Float Range: nil for an open side, an Integer for
   an Integer side, else the Float. */
static sp_RbVal fx_frange_side(const sp_FloatRange *g, int end) {
  if (g->omitted & (end ? SP_FRANGE_NO_END : SP_FRANGE_NO_BEGIN)) return sp_box_nil();
  sp_float v = end ? g->last : g->first;
  if (g->omitted & (end ? SP_FRANGE_INT_END : SP_FRANGE_INT_BEGIN)) return sp_box_int((sp_int)v);
  return sp_box_float(v);
}

/* The methods of a Float Range that the generated wrappers do not cover:
   the sides, and the tests that compare a value with them. A Float Range
   does not walk, as in CRuby. */
static int fx_frange_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  const sp_FloatRange *g = (const sp_FloatRange *)r.v.p;
  if (argc == 0) {
    if (FX_IS("begin")) { *out = fx_frange_side(g, 0); return 1; }
    if (FX_IS("end")) { *out = fx_frange_side(g, 1); return 1; }
    if (FX_IS("first") || FX_IS("last")) {
      int end = FX_IS("last");
      if (g->omitted & (end ? SP_FRANGE_NO_END : SP_FRANGE_NO_BEGIN))
        sp_raise_cls("RangeError", end ? "cannot get the last element of endless range"
                                       : "cannot get the first element of beginless range");
      *out = fx_frange_side(g, end);
      return 1;
    }
    if (FX_IS("exclude_end?")) { *out = sp_box_bool(g->excl); return 1; }
    if (FX_IS("size") || FX_IS("count") || FX_IS("to_a") || FX_IS("entries")) {
      if (g->omitted & SP_FRANGE_NO_BEGIN) sp_raise_cls("TypeError", "can't iterate from NilClass");
      sp_raise_cls("TypeError", "can't iterate from Float");
    }
    return 0;
  }
  if (argc == 1 && (FX_IS("include?") || FX_IS("member?") || FX_IS("cover?") || FX_IS("==="))) {
    sp_RbVal a = argv[0];
    if (a.tag == SP_TAG_INT) *out = sp_box_bool(sp_frange_cover_i(*g, a.v.i));
    else if (a.tag == SP_TAG_FLT) *out = sp_box_bool(sp_frange_cover(*g, a.v.f));
    else *out = sp_box_bool(0);
    return 1;
  }
  if (argc == 1 && (FX_IS("==") || FX_IS("eql?"))) {
    sp_RbVal o = argv[0];
    int same = o.tag == SP_TAG_OBJ && o.cls_id == SP_BUILTIN_FLOAT_RANGE && o.v.p &&
               sp_frange_eq(*g, *(const sp_FloatRange *)o.v.p);
    /* == compares the sides as numbers with an Integer Range: (1..5) == (1.0..5.0) */
    if (FX_IS("==") && fx_is_range(o)) {
      SP_GC_ROOT_RBVAL(r);
      SP_GC_ROOT_RBVAL(o);
      sp_RbVal a = fx_send(r, "begin", 0, NULL), b = fx_send(o, "begin", 0, NULL);
      SP_GC_ROOT_RBVAL(a);
      SP_GC_ROOT_RBVAL(b);
      same = sp_poly_eq(a, b) && sp_poly_eq(fx_send(r, "end", 0, NULL), fx_send(o, "end", 0, NULL)) &&
             g->excl == ((const sp_Range *)o.v.p)->excl;
    }
    *out = sp_box_bool(same);
    return 1;
  }
  return 0;
}

/* An end of a String Range as a value: nil, a String, or a Symbol. */
static sp_RbVal fx_srange_side(sp_StrRange g, int end) {
  const char *s = end ? g.last : g.first;
  if (!s) return sp_box_nil();
  return g.sym ? sp_box_sym(sp_sym_intern(s)) : sp_box_str_new(sp_str_dup(s));
}

/* The text of a value that a String Range compares, or NULL when the value
   is of another kind: a String for a String Range, a Symbol for a Symbol
   Range. */
static const char *fx_srange_arg(sp_StrRange g, sp_RbVal a) {
  if (g.sym) return a.tag == SP_TAG_SYM ? sp_sym_to_s((sp_sym)a.v.i) : NULL;
  if (a.tag == SP_TAG_STR) return a.v.s;
  return sp_poly_strbuf_copy(a);
}

/* The elements of a String Range, as Strings, or as Symbols for a Symbol
   Range. */
static sp_RbVal fx_srange_to_a(sp_StrRange g) {
  if (!g.first) sp_raise_cls("TypeError", "can't iterate from NilClass");
  if (!g.last) sp_raise_cls("RangeError", "cannot convert endless range to an array");
  SP_GC_ROOT_SRANGE(g);
  sp_StrArray *a = sp_srange_to_a(g);
  SP_GC_ROOT(a);
  sp_PolyArray *p = sp_PolyArray_new();
  SP_GC_ROOT(p);
  for (sp_int i = 0; i < a->len; i++)
    sp_PolyArray_push(p, g.sym ? sp_box_sym(sp_sym_intern(a->data[i])) : sp_box_str_new(sp_str_dup(a->data[i])));
  return sp_box_poly_array(p);
}

/* The methods of a String Range that the generated wrappers do not cover.
   include? walks the elements, as CRuby does, unless both ends and the
   value are one character; cover? and === compare with the ends. */
static int fx_srange_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  sp_StrRange g = SP_SRANGE_OF(r.v.p);
  SP_GC_ROOT_SRANGE(g);
  if (argc == 0) {
    if (FX_IS("begin")) { *out = fx_srange_side(g, 0); return 1; }
    if (FX_IS("end")) { *out = fx_srange_side(g, 1); return 1; }
    if (FX_IS("first") || FX_IS("last")) {
      int end = FX_IS("last");
      if (!(end ? g.last : g.first))
        sp_raise_cls("RangeError", end ? "cannot get the last element of endless range"
                                       : "cannot get the first element of beginless range");
      *out = fx_srange_side(g, end);
      return 1;
    }
    if (FX_IS("exclude_end?")) { *out = sp_box_bool(g.excl); return 1; }
    if (FX_IS("to_a") || FX_IS("entries")) { *out = fx_srange_to_a(g); return 1; }
    if (FX_IS("size")) { *out = sp_box_nil(); return 1; }
    if (FX_IS("count")) { *out = sp_box_int(sp_poly_length(fx_srange_to_a(g))); return 1; }
    return 0;
  }
  if (argc == 1 && (FX_IS("include?") || FX_IS("member?") || FX_IS("cover?") || FX_IS("==="))) {
    if ((FX_IS("include?") || FX_IS("member?")) && (!g.first || !g.last))
      sp_raise_cls("TypeError", "cannot determine inclusion in beginless/endless ranges");
    const char *x = fx_srange_arg(g, argv[0]);
    if (!x) { *out = sp_box_bool(0); return 1; }
    int walk = (FX_IS("include?") || FX_IS("member?")) && g.first && g.last &&
               !(strlen(g.first) == 1 && strlen(g.last) == 1 && strlen(x) == 1);
    if (!walk) { *out = sp_box_bool(sp_srange_cover(g, x)); return 1; }
    sp_RbVal all = fx_srange_to_a(g);
    SP_GC_ROOT_RBVAL(all);
    int hit = 0;
    for (sp_int i = 0; i < sp_poly_length(all) && !hit; i++) hit = sp_poly_eq(sp_poly_arr_get(all, i), argv[0]);
    *out = sp_box_bool(hit);
    return 1;
  }
  if (argc == 1 && (FX_IS("==") || FX_IS("eql?"))) {
    sp_RbVal o = argv[0];
    int same = o.tag == SP_TAG_OBJ && o.cls_id == SP_BUILTIN_STR_RANGE && o.v.p &&
               ((const sp_StrRange *)o.v.p)->sym == g.sym && sp_srange_eq(g, SP_SRANGE_OF(o.v.p));
    *out = sp_box_bool(same);
    return 1;
  }
  return 0;
}

static int fx_range_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (r.tag == SP_TAG_OBJ && r.cls_id == SP_BUILTIN_FLOAT_RANGE && r.v.p)
    return fx_frange_core(r, name, argc, argv, out);
  if (r.tag == SP_TAG_OBJ && r.cls_id == SP_BUILTIN_STR_RANGE && r.v.p)
    return fx_srange_core(r, name, argc, argv, out);
  if (!fx_is_range(r)) return 0;
  const sp_Range *g = (const sp_Range *)r.v.p;
  sp_int last = g->excl ? g->last - 1 : g->last;
  sp_int size = g->nobeg || g->noend ? -1 : last >= g->first ? last - g->first + 1 : 0;
  if (argc == 0) {
    if (FX_IS("begin") || (FX_IS("first") && !g->nobeg)) {
      *out = g->nobeg ? sp_box_nil() : sp_box_int(g->first);
      return 1;
    }
    if (FX_IS("end")) {
      *out = g->noend ? sp_box_nil() : g->fe ? sp_box_float(g->fend) : sp_box_int(g->last);
      return 1;
    }
    if (FX_IS("exclude_end?")) { *out = sp_box_bool(g->excl); return 1; }
    if (size < 0) return 0;
    if (FX_IS("to_a") || FX_IS("entries")) { *out = sp_box_poly_array(fx_seq_array(r)); return 1; }
    if (FX_IS("size") || FX_IS("count")) { *out = sp_box_int(size); return 1; }
    if (FX_IS("last")) { *out = sp_box_int(g->last); return 1; }
    if (FX_IS("min")) { *out = size ? sp_box_int(g->first) : sp_box_nil(); return 1; }
    if (FX_IS("max")) { *out = size ? sp_box_int(last) : sp_box_nil(); return 1; }
    if (FX_IS("sum")) { *out = sp_poly_sum(sp_box_poly_array(fx_seq_array(r))); return 1; }
    return 0;
  }
  /* cover?(range): the other Range lies inside this one */
  if (argc == 1 && FX_IS("cover?") && fx_is_range(argv[0])) {
    const sp_Range *o = (const sp_Range *)argv[0].v.p;
    sp_int olast = o->excl ? o->last - 1 : o->last;
    int lo = g->nobeg || (!o->nobeg && o->first >= g->first);
    int hi = g->noend || (!o->noend && olast <= last);
    *out = sp_box_bool(lo && hi && (o->nobeg || o->noend || o->first <= olast));
    return 1;
  }
  /* (1..5) == (1.0..5.0): the Float Range compares the sides as numbers */
  if (argc == 1 && FX_IS("==") && argv[0].tag == SP_TAG_OBJ && argv[0].cls_id == SP_BUILTIN_FLOAT_RANGE &&
      argv[0].v.p)
    return fx_frange_core(argv[0], name, 1, &r, out);
  if (argc == 1 && (FX_IS("include?") || FX_IS("member?") || FX_IS("cover?") || FX_IS("==="))) {
    sp_RbVal a = argv[0];
    /* nil..nil covers any value */
    if (g->nobeg && g->noend && (FX_IS("cover?") || FX_IS("==="))) { *out = sp_box_bool(1); return 1; }
    if (a.tag != SP_TAG_INT && a.tag != SP_TAG_FLT) { *out = sp_box_bool(0); return 1; }
    double x = a.tag == SP_TAG_INT ? (double)a.v.i : a.v.f;
    double e = g->fe ? g->fend : (double)g->last;
    int lo = g->nobeg || x >= (double)g->first;
    int hi = g->noend || (g->excl ? x < e : x <= e);
    *out = sp_box_bool(lo && hi);
    return 1;
  }
  if (argc == 1 && (FX_IS("first") || FX_IS("take")) && argv[0].tag == SP_TAG_INT && !g->nobeg) {
    sp_PolyArray *a = sp_PolyArray_new();
    SP_GC_ROOT(a);
    FxSeq s;
    sp_RbVal v;
    fx_seq_open(r, &s);
    FX_SEQ_ROOT(s);
    for (sp_int i = 0; i < argv[0].v.i && fx_seq_next(&s, &v); i++) sp_PolyArray_push(a, v);
    *out = sp_box_poly_array(a);
    return 1;
  }
  return 0;
}

#undef FX_IS
#undef FX_STOP
