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
  /* an endless String Range: the Enumerator of the runtime that follows
     succ, one element at a time */
  if (r.tag == SP_TAG_OBJ && r.cls_id == SP_BUILTIN_STR_RANGE && r.v.p && ((const sp_StrRange *)r.v.p)->first &&
      !((const sp_StrRange *)r.v.p)->last)
    r = s->src = sp_box_obj(sp_srange_endless_enum(r, 1), SP_BUILTIN_ENUMERATOR);
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
  /* minmax with a block: min and max with the same block */
  if (argc == 0 && FX_IS("minmax")) {
    sp_RbVal lo = sp_box_nil(), hi = sp_box_nil();
    SP_GC_ROOT_RBVAL(lo);
    SP_GC_ROOT_RBVAL(hi);
    int have = 0;
    while (fx_seq_next(&s, &v)) {
      if (!have) { lo = hi = v; have = 1; continue; }
      y = fx_y2(blk, v, lo);
      FX_STOP();
      if (fx_cmp_result(y, v, lo) < 0) lo = v;
      y = fx_y2(blk, v, hi);
      FX_STOP();
      if (fx_cmp_result(y, v, hi) > 0) hi = v;
    }
    *out = fx_pair(lo, hi);
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

static int fx_big_upto(sp_RbVal r, const char *name, sp_RbVal lim, const FxBlock *blk, sp_RbVal *out);

/* Numeric#step with no block, of an Integer or a Float: an Enumerator over
   the values that the step with a block (Numeric#step of the prelude)
   gives, with the same errors. CRuby answers an
   Enumerator::ArithmeticSequence, which spinel does not have. An Integer
   walk goes to sp_int_step_enum, as a blockless upto does; a Float walk
   takes the count of CRuby's ruby_float_step. */
static int fx_num_step(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (!FX_IS("step") || (r.tag != SP_TAG_INT && r.tag != SP_TAG_FLT)) return 0;
  sp_RbVal by = sp_box_nil(), to = sp_box_nil();
  int has_by = 0, has_to = 0;
  if (argc && fx_is_hash(argv[argc - 1]) && argv[argc - 1].v.p == L.kwhash) {
    sp_PolyPolyHash *h = sp_poly_as_poly_poly_hash(argv[argc - 1]);
    sp_RbVal kb = sp_box_sym(sp_sym_intern("by")), kt = sp_box_sym(sp_sym_intern("to"));
    if ((has_by = sp_PolyPolyHash_has_key(h, kb))) by = sp_PolyPolyHash_get(h, kb);
    if ((has_to = sp_PolyPolyHash_has_key(h, kt))) to = sp_PolyPolyHash_get(h, kt);
    if (h->len > has_by + has_to) fx_gap("Numeric#step with a keyword other than by: and to:");
    argc--;
  }
  if (argc > 2) sp_raise_cls("ArgumentError", sp_sprintf("wrong number of arguments (given %d, expected 0..2)", argc));
  if (argc >= 1 && has_to) sp_raise_cls("ArgumentError", "to is given twice");
  if (argc == 2 && has_by) sp_raise_cls("ArgumentError", "step is given twice");
  sp_RbVal lim = argc ? argv[0] : to;
  sp_RbVal st = argc == 2 ? argv[1] : by.tag == SP_TAG_NIL ? sp_box_int(1) : by;
  SP_GC_ROOT_RBVAL(lim);
  SP_GC_ROOT_RBVAL(st);
  /* CRuby raises for a step or a limit that is not a number only when
     the walk starts, which an Enumerator over the values cannot do */
  int st_num = fx_is_integer(st) || st.tag == SP_TAG_FLT || sp_poly_is_rat_kind(st);
  int lim_num = fx_is_integer(lim) || lim.tag == SP_TAG_FLT || sp_poly_is_rat_kind(lim);
  if (!st_num || (lim.tag != SP_TAG_NIL && !lim_num)) fx_gap("Numeric#step with no block and a value that is not a number");
  if (sp_poly_eq(st, sp_box_int(0))) sp_raise_cls("ArgumentError", "step can't be 0");
  if (r.tag == SP_TAG_FLT || lim.tag == SP_TAG_FLT || st.tag == SP_TAG_FLT) {
    if (sp_poly_is_rat_kind(lim) || sp_poly_is_rat_kind(st)) fx_gap("Numeric#step of a Float and a Rational");
    double b = sp_poly_to_f(r), u = sp_poly_to_f(st);
    if (lim.tag == SP_TAG_NIL || (isinf(sp_poly_to_f(lim)) && (sp_poly_to_f(lim) > 0) == (u > 0))) {
      *out = sp_range_endless_step_v(sp_box_float(b), sp_box_float(u));
      return 1;
    }
    double e = sp_poly_to_f(lim), n = (e - b) / u;
    double err = (fabs(b) + fabs(e) + fabs(e - b)) / fabs(u) * 2.220446049250313e-16;
    if (err > 0.5) err = 0.5;
    sp_PolyArray *acc = sp_PolyArray_new();
    SP_GC_ROOT(acc);
    /* an infinite step gives the start alone, when it heads to the limit */
    if (isinf(u)) {
      if (u > 0 ? b <= e : b >= e) sp_PolyArray_push(acc, sp_box_float(b));
    } else if (n >= 0) {
      sp_int k = (sp_int)floor(n + err);
      for (sp_int i = 0; i <= k; i++) {
        double d = i * u + b;
        if (u >= 0 ? e < d : d < e) d = e;
        sp_PolyArray_push(acc, sp_box_float(d));
      }
    }
    *out = sp_box_obj(sp_Enumerator_new_from_items(acc), SP_BUILTIN_ENUMERATOR);
    return 1;
  }
  if (st.tag != SP_TAG_INT || (lim.tag != SP_TAG_NIL && !fx_is_integer(lim)))
    fx_gap("Numeric#step with a Bignum or a Rational step or limit");
  if (lim.tag == SP_TAG_NIL) {
    *out = sp_range_endless_step_v(r, st);
    return 1;
  }
  sp_int l = sp_int_step_lim(lim, st.v.i > 0);
  sp_Range rng = sp_range_new_step(r.v.i, l, 0, st.v.i);
  *out = sp_box_obj(sp_int_step_enum(rng, r.v.i, "step", lim), SP_BUILTIN_ENUMERATOR);
  return 1;
}

/* Integer#times, #upto, #downto and #step with Integer bounds. */
static int fx_int_iter(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                         sp_RbVal *out) {
  if (argc == 1 && (FX_IS("upto") || FX_IS("downto")) && fx_is_integer(r) && fx_is_integer(argv[0]) &&
      (r.tag == SP_TAG_BIGINT || argv[0].tag == SP_TAG_BIGINT) && (blk || r.tag == SP_TAG_BIGINT))
    return fx_big_upto(r, name, argv[0], blk, out);
  if (r.tag != SP_TAG_INT) return 0;
  /* a blockless upto or downto: the Enumerator of the runtime, which walks
     a long span lazily and reads a Float or a Bignum limit */
  if (!blk && argc == 1 && (FX_IS("upto") || FX_IS("downto"))) {
    int up = name[0] == 'u';
    sp_Range rng = sp_range_new_step(r.v.i, sp_int_step_lim(argv[0], up), 0, up ? 1 : -1);
    *out = sp_box_obj(sp_int_step_enum(rng, r.v.i, name, argv[0]), SP_BUILTIN_ENUMERATOR);
    return 1;
  }
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

/* String#each_char: one String per UTF-8 character. String#each_grapheme_cluster:
   one per match of \X (re.c). */
static int fx_str_iter(sp_RbVal r, const char *name, int argc, const FxBlock *blk, sp_RbVal *out) {
  if (!fx_is_string(r) || argc != 0 || !blk || (!FX_IS("each_char") && !FX_IS("each_grapheme_cluster"))) return 0;
  SP_GC_ROOT_RBVAL(r);
  if (FX_IS("each_grapheme_cluster")) {
    sp_RbVal a = fx_re_graphemes(sp_poly_recv_s(r, name));
    SP_GC_ROOT_RBVAL(a);
    const sp_PolyArray *pa = (const sp_PolyArray *)a.v.p;
    for (sp_int i = 0; i < pa->len; i++) {
      fx_y1(blk, pa->data[i]);
      FX_STOP();
    }
    *out = r;
    return 1;
  }
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

static sp_RbVal fx_srange_side(sp_StrRange g, int end);
static sp_RbVal fx_srange_to_a(sp_StrRange g);

/* How CRuby names a value in a TypeError of a coercion: the inspect of
   nil, true, false and a Symbol, else the class name. */
static const char *fx_coerce_name(sp_RbVal v) {
  if (v.tag == SP_TAG_NIL || v.tag == SP_TAG_BOOL || v.tag == SP_TAG_SYM) return sp_poly_inspect(v);
  return sp_poly_class_name(v);
}

/* One value of a step: to the block, or to the Array of a blockless step. */
#define FX_STEP_PUT(x)                          \
  do {                                          \
    sp_RbVal x_ = (x);                          \
    if (acc) {                                  \
      sp_PolyArray_push(acc, x_);               \
    } else {                                    \
      fx_y1(blk, x_);                           \
      FX_STOP();                                \
    }                                           \
  } while (0)

/* The Enumerator of a blockless step over the values in acc, labeled
   as the call: #<Enumerator: "a".."e":step(2)>. */
static sp_RbVal fx_step_enum(sp_RbVal r, const char *op, sp_RbVal stride, sp_PolyArray *acc) {
  SP_GC_ROOT_RBVAL(r);
  SP_GC_ROOT_RBVAL(stride);
  sp_Enumerator *e = sp_Enumerator_new_from_items(acc);
  return sp_box_obj(sp_enum_step_labeled(e, r, op, &stride, 1), SP_BUILTIN_ENUMERATOR);
}

/* Integer#upto and #downto from a Bignum: the values one at a time, with
   the helpers of a boxed number. With no block, an Enumerator over them,
   labeled as CRuby labels it. */
static int fx_big_upto(sp_RbVal r, const char *name, sp_RbVal lim, const FxBlock *blk, sp_RbVal *out) {
  int up = name[0] == 'u';
  SP_GC_ROOT_RBVAL(r);
  SP_GC_ROOT_RBVAL(lim);
  sp_RbVal span = up ? sp_poly_sub(lim, r) : sp_poly_sub(r, lim);
  if (!blk && span.tag == SP_TAG_BIGINT) fx_gap(sp_sprintf("Integer#%s over more values than a word counts", name));
  sp_PolyArray *acc = blk ? NULL : sp_PolyArray_new();
  SP_GC_ROOT(acc);
  sp_RbVal i = r;
  SP_GC_ROOT_RBVAL(i);
  for (; sp_poly_cmp_ck(i, lim) * (up ? 1 : -1) <= 0; i = sp_poly_add(i, sp_box_int(up ? 1 : -1))) FX_STEP_PUT(i);
  *out = acc ? fx_step_enum(r, name, lim, acc) : r;
  return 1;
}

/* Range#step and #% of an Integer, a Float or a String Range, with a block
   or as an Enumerator. An endless Range walks until the block breaks; with
   no block, the Enumerator of the runtime walks it. */
static int fx_range_step(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                         sp_RbVal *out) {
  if (!(FX_IS("step") || FX_IS("%")) || r.tag != SP_TAG_OBJ || !r.v.p) return 0;
  int cls = r.cls_id;
  if (cls != SP_BUILTIN_RANGE && cls != SP_BUILTIN_FLOAT_RANGE && cls != SP_BUILTIN_STR_RANGE) return 0;
  if (argc > 1) sp_raise_cls("ArgumentError", sp_sprintf("wrong number of arguments (given %d, expected 0..1)", argc));
  SP_GC_ROOT_RBVAL(r);
  sp_RbVal n = argc ? argv[0] : sp_box_int(1);
  SP_GC_ROOT_RBVAL(n);
  sp_PolyArray *acc = blk ? NULL : sp_PolyArray_new();
  SP_GC_ROOT(acc);
  if (cls == SP_BUILTIN_STR_RANGE) {
    if (n.tag != SP_TAG_INT && n.tag != SP_TAG_FLT)
      sp_raise_cls("TypeError", sp_sprintf("no implicit conversion of %s into String",
                                           n.tag == SP_TAG_NIL ? "nil" : n.tag == SP_TAG_BOOL ? (n.v.b ? "true" : "false")
                                                                                              : sp_poly_class_name(n)));
    if (!blk) {
      *out = sp_box_obj(sp_srange_step_enum(r, n, name), SP_BUILTIN_ENUMERATOR);
      return 1;
    }
    if (n.tag == SP_TAG_FLT) sp_raise_cls("TypeError", "no implicit conversion of Float into String");
    sp_StrRange g = SP_SRANGE_OF(r.v.p);
    SP_GC_ROOT_SRANGE(g);
    if (!g.first) sp_raise_cls("TypeError", "can't iterate from NilClass");
    sp_RbVal v = fx_srange_side(g, 0);
    SP_GC_ROOT_RBVAL(v);
    if (n.v.i <= 0) {
      /* a step of zero or less gives the begin alone, as CRuby 4.0 does */
      if (!g.last || sp_poly_length(fx_srange_to_a(g)) > 0) FX_STEP_PUT(v);
    } else if (g.last) {
      sp_RbVal all = fx_srange_to_a(g);
      SP_GC_ROOT_RBVAL(all);
      for (sp_int i = 0; i < sp_poly_length(all); i += n.v.i) FX_STEP_PUT(sp_poly_arr_get(all, i));
    } else {
      /* an endless String Range: succ until the block breaks */
      for (;;) {
        FX_STEP_PUT(v);
        for (sp_int k = 0; k < n.v.i; k++) v = fx_send(v, "succ", 0, NULL);
      }
    }
    *out = r;
    return 1;
  }
  const sp_Range *ig = cls == SP_BUILTIN_RANGE ? (const sp_Range *)r.v.p : NULL;
  int flt = cls == SP_BUILTIN_FLOAT_RANGE || n.tag == SP_TAG_FLT || (ig && ig->fe);
  if (n.tag != SP_TAG_INT && n.tag != SP_TAG_FLT)
    sp_raise_cls("TypeError", sp_sprintf("%s can't be coerced into %s", fx_coerce_name(n), flt ? "Float" : "Integer"));
  if (n.tag == SP_TAG_INT ? n.v.i == 0 : n.v.f == 0.0) sp_raise_cls("ArgumentError", "step can't be 0");
  if (!flt) {
    if (ig->nobeg) sp_raise_cls("ArgumentError", "#step iteration for beginless ranges is meaningless");
    if (!blk && ig->noend) { *out = sp_range_endless_step_v(sp_box_int(ig->first), n); return 1; }
    sp_int s = n.v.i;
    for (sp_int i = ig->first;; i += s) {
      if (!ig->noend && (s > 0 ? (ig->excl ? i >= ig->last : i > ig->last)
                                : (ig->excl ? i <= ig->last : i < ig->last)))
        break;
      FX_STEP_PUT(sp_box_int(i));
      if ((s > 0 && i > INT64_MAX - s) || (s < 0 && i < INT64_MIN - s)) break;
    }
  } else {
    /* the Float walk of CRuby (ruby_float_step) over the sides as Floats */
    sp_FloatRange fr;
    if (ig) {
      if (ig->nobeg) sp_raise_cls("ArgumentError", "#step iteration for beginless ranges is meaningless");
      fr = sp_frange_new_o((sp_float)ig->first, ig->noend ? HUGE_VAL : ig->fe ? ig->fend : (sp_float)ig->last,
                           ig->excl, ig->noend ? SP_FRANGE_NO_END : 0);
    } else {
      fr = *(const sp_FloatRange *)r.v.p;
    }
    sp_float unit = n.tag == SP_TAG_FLT ? n.v.f : (sp_float)n.v.i;
    if (!blk && (fr.omitted & SP_FRANGE_NO_END) && !(fr.omitted & SP_FRANGE_NO_BEGIN)) {
      *out = sp_range_endless_step_v(sp_box_float(fr.first), n);
      return 1;
    }
    sp_float cnt = sp_frange_step_count(fr, unit);
    for (sp_int i = 0; (sp_float)i < cnt; i++) FX_STEP_PUT(sp_box_float(sp_frange_step_at(fr, unit, i)));
  }
  *out = acc ? fx_step_enum(r, name, n, acc) : r;
  return 1;
}

#undef FX_STEP_PUT

/* min, max and minmax with an argument or a block, last(n) and count with
   an argument or a block: the same call on the elements of the Range, as
   Enumerable gives it. to_a raises for a Range that cannot walk. */
static int fx_range_via_array(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                              sp_RbVal *out) {
  if (r.tag != SP_TAG_OBJ || !r.v.p ||
      (r.cls_id != SP_BUILTIN_RANGE && r.cls_id != SP_BUILTIN_FLOAT_RANGE && r.cls_id != SP_BUILTIN_STR_RANGE))
    return 0;
  int hit = ((FX_IS("min") || FX_IS("max") || FX_IS("minmax") || FX_IS("count")) && (argc > 0 || blk)) ||
            (FX_IS("last") && argc == 1);
  if (!hit) return 0;
  /* a Float Range goes to Enumerable, which cannot walk it; max checks the
     open end first, as CRuby's range_max */
  if (r.cls_id == SP_BUILTIN_FLOAT_RANGE) {
    const sp_FloatRange *g = (const sp_FloatRange *)r.v.p;
    if (FX_IS("max") && (g->omitted & SP_FRANGE_NO_END))
      sp_raise_cls("RangeError", "cannot get the maximum of endless range");
    sp_raise_cls("TypeError", g->omitted & SP_FRANGE_NO_BEGIN ? "can't iterate from NilClass"
                                                               : "can't iterate from Float");
  }
  if (argc == 1 && argv[0].tag == SP_TAG_INT && argv[0].v.i < 0 && !FX_IS("count"))
    sp_raise_cls("ArgumentError", FX_IS("last") ? "negative array size" : "negative array size (or size too big)");
  /* max(n) of a beginless Integer Range counts down from the end */
  if (r.cls_id == SP_BUILTIN_RANGE && ((const sp_Range *)r.v.p)->nobeg && FX_IS("max") && argc == 1 && !blk &&
      argv[0].tag == SP_TAG_INT) {
    const sp_Range *g = (const sp_Range *)r.v.p;
    if (g->noend) sp_raise_cls("RangeError", "cannot get the maximum of endless range");
    sp_PolyArray *a = sp_PolyArray_new();
    SP_GC_ROOT(a);
    sp_int top = g->fe ? (sp_int)floor(g->fend) - (g->excl && floor(g->fend) == g->fend) : g->excl ? g->last - 1 : g->last;
    for (sp_int i = 0; i < argv[0].v.i; i++) sp_PolyArray_push(a, sp_box_int(top - i));
    *out = sp_box_poly_array(a);
    return 1;
  }
  if (r.cls_id == SP_BUILTIN_RANGE && ((const sp_Range *)r.v.p)->nobeg)
    sp_raise_cls("TypeError", "can't iterate from NilClass");
  sp_RbVal a = fx_send(r, "to_a", 0, NULL);
  SP_GC_ROOT_RBVAL(a);
  /* minmax with a block: min and max with the same block */
  if (FX_IS("minmax") && blk) {
    sp_RbVal hi = fx_send_b(a, "max", argc, argv, blk);
    SP_GC_ROOT_RBVAL(hi);
    sp_RbVal lo = fx_send_b(a, "min", argc, argv, blk);
    SP_GC_ROOT_RBVAL(lo);
    *out = fx_pair(lo, hi);
    return 1;
  }
  *out = fx_send_b(a, name, argc, argv, blk);
  return 1;
}

/* any?(pattern), all?, none? and one? of a sequence: pattern === each
   value, in place of a block. */
static int fx_seq_pattern(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (argc != 1 || !(FX_IS("any?") || FX_IS("all?") || FX_IS("none?") || FX_IS("one?"))) return 0;
  FxSeq s;
  if (!fx_seq_open(r, &s)) return 0;
  FX_SEQ_ROOT(s);
  sp_RbVal pat = argv[0], v = sp_box_nil();
  SP_GC_ROOT_RBVAL(pat);
  SP_GC_ROOT_RBVAL(v);
  sp_int n = 0, total = 0;
  while (fx_seq_next(&s, &v)) {
    total++;
    if (sp_poly_truthy(fx_send(pat, "===", 1, &v))) n++;
    if ((FX_IS("any?") || FX_IS("none?")) && n) break;
    if (FX_IS("all?") && n < total) break;
    if (FX_IS("one?") && n > 1) break;
  }
  *out = sp_box_bool(FX_IS("any?") ? n > 0 : FX_IS("all?") ? n == total : FX_IS("none?") ? n == 0 : n == 1);
  return 1;
}

/* ---- Enumerable on a Fluxpinel object ---- */

/* What a block of C does with each value that each yields. */
enum { FX_EN_COLLECT, FX_EN_FIND, FX_EN_INCLUDE, FX_EN_INDEX, FX_EN_TAKE_WHILE };

typedef struct {
  int mode;
  sp_int limit;            /* FX_EN_COLLECT: stop after this many; -1 for all */
  sp_PolyArray *acc;       /* the values that the walk keeps */
  const FxBlock *user;     /* the block of the call, for find and take_while */
  sp_RbVal arg;            /* the value that include? and find_index look for */
  sp_RbVal found;
  sp_int index;
  int hit;
  const FxBlock *self;     /* the block of C, which a stop names */
} FxEnumWalk;

/* Ends the each that got the block of C, as a break of it does. */
static sp_RbVal fx_cblock_stop(const FxBlock *b) {
  L.ctl = FX_CTL_BREAK_BLOCK;
  L.ctl_target = b;
  L.ctl_val = sp_box_nil();
  return sp_box_nil();
}

/* One value of each: several yielded values are one Array, as each_entry
   gives them. */
static sp_RbVal fx_enum_walk_step(void *ctx, int argc, sp_RbVal *argv) {
  FxEnumWalk *w = (FxEnumWalk *)ctx;
  sp_RbVal v = argc == 0 ? sp_box_nil() : argv[0];
  SP_GC_ROOT_RBVAL(v);
  if (argc > 1) {
    sp_PolyArray *p = sp_PolyArray_new();
    v = sp_box_poly_array(p);
    for (int i = 0; i < argc; i++) sp_PolyArray_push(p, argv[i]);
  }
  switch (w->mode) {
  case FX_EN_COLLECT:
    sp_PolyArray_push(w->acc, v);
    if (w->limit >= 0 && w->acc->len >= w->limit) return fx_cblock_stop(w->self);
    return sp_box_nil();
  case FX_EN_FIND:
  case FX_EN_TAKE_WHILE: {
    sp_RbVal t = fx_y1(w->user, v);
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
    if (w->mode == FX_EN_TAKE_WHILE) {
      if (!sp_poly_truthy(t)) return fx_cblock_stop(w->self);
      sp_PolyArray_push(w->acc, v);
      return sp_box_nil();
    }
    if (!sp_poly_truthy(t)) return sp_box_nil();
    w->found = v;
    w->hit = 1;
    return fx_cblock_stop(w->self);
  }
  default: {
    int eq = w->user ? sp_poly_truthy(fx_y1(w->user, v)) : sp_poly_truthy(fx_send(v, "==", 1, &w->arg));
    if (L.ctl != FX_CTL_NONE) return sp_box_nil();
    if (!eq) { w->index++; return sp_box_nil(); }
    w->hit = 1;
    return fx_cblock_stop(w->self);
  }
  }
}

/* Calls each of r with a block of C for w. Answers 0 when a jump of the
   program (a break of a block of the call) is pending. */
static int fx_enum_walk(sp_RbVal r, FxEnumWalk *w) {
  FxBlock cb = { .cfn = fx_enum_walk_step, .ctx = w };
  w->self = &cb;
  fx_send_b(r, "each", 0, NULL, &cb);
  if (L.ctl == FX_CTL_BREAK_BLOCK && L.ctl_target == &cb) L.ctl = FX_CTL_NONE;
  return L.ctl == FX_CTL_NONE;
}

/* The methods of Enumerable on an object whose class includes Enumerable
   and defines each, but not the method. A method that can stop early
   (first, take, find, include?, find_index, take_while) stops the each;
   any other one makes the same call on the Array of all the values. */
static int fx_enum_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                        sp_RbVal *out) {
  if (!fx_is_fx_object(r) || FX_IS("each") || !fx_builtin_module_name(r, name)) return 0;
  /* a method of Kernel (class, inspect, ...) is not one of Enumerable;
     select is both, and Enumerable comes first in the ancestors */
  if (fx_object_method_name(name) && !FX_IS("select")) return 0;
  if (!fx_is_a(r, fx_builtin("Enumerable")) || !fx_find_method(fx_class_of(r), sp_sym_intern("each"))) return 0;
  SP_GC_ROOT_RBVAL(r);
  FxEnumWalk w = { .mode = FX_EN_COLLECT, .limit = -1, .arg = sp_box_nil(), .found = sp_box_nil() };
  w.acc = sp_PolyArray_new();
  SP_GC_ROOT(w.acc);
  SP_GC_ROOT_RBVAL(w.found);
  if ((FX_IS("first") || FX_IS("take")) && argc <= 1 && !blk) {
    if (argc == 1) {
      sp_int n = sp_poly_arg_int_chk(argv[0]);
      if (n < 0) sp_raise_cls("ArgumentError", "attempt to take negative size");
      w.limit = n;
    } else {
      if (FX_IS("take")) return 0;
      w.limit = 1;
    }
    if (w.limit > 0 && !fx_enum_walk(r, &w)) { *out = sp_box_nil(); return 1; }
    *out = argc == 1 ? sp_box_poly_array(w.acc) : w.acc->len ? w.acc->data[0] : sp_box_nil();
    return 1;
  }
  int find = (FX_IS("find") || FX_IS("detect")) && argc == 0 && blk;
  int incl = (FX_IS("include?") || FX_IS("member?")) && argc == 1;
  int index = FX_IS("find_index") && ((argc == 1 && !blk) || (argc == 0 && blk));
  int twhile = FX_IS("take_while") && argc == 0 && blk;
  if (find || incl || index || twhile) {
    w.mode = find ? FX_EN_FIND : twhile ? FX_EN_TAKE_WHILE : incl ? FX_EN_INCLUDE : FX_EN_INDEX;
    w.user = argc == 0 ? blk : NULL;
    if (argc == 1) w.arg = argv[0];
    SP_GC_ROOT_RBVAL(w.arg);
    if (!fx_enum_walk(r, &w)) { *out = sp_box_nil(); return 1; }
    *out = find ? w.found : twhile ? sp_box_poly_array(w.acc) : incl ? sp_box_bool(w.hit)
                                   : w.hit ? sp_box_int(w.index) : sp_box_nil();
    return 1;
  }
  if (!fx_enum_walk(r, &w)) { *out = sp_box_nil(); return 1; }
  sp_RbVal a = sp_box_poly_array(w.acc);
  SP_GC_ROOT_RBVAL(a);
  if (FX_IS("to_a") || FX_IS("entries")) { *out = a; return 1; }
  /* each_with_index { } and its kin answer the receiver, as CRuby does;
     each_with_object answers the memo */
  sp_RbVal v = fx_send_b(a, FX_IS("each_entry") ? "each" : name, argc, argv, blk);
  *out = blk && ((!strncmp(name, "each_", 5) && !FX_IS("each_with_object")) || FX_IS("reverse_each")) ? r : v;
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
  /* then with no block: an Enumerator over the receiver, #<Enumerator: 1:then> */
  if (!blk && argc == 0 && (FX_IS("then") || FX_IS("yield_self"))) {
    SP_GC_ROOT_RBVAL(r);
    sp_PolyArray *one = sp_PolyArray_new();
    SP_GC_ROOT(one);
    sp_PolyArray_push(one, r);
    /* CRuby names yield_self by its alias then */
    const char *m = sp_sprintf("then");
    SP_GC_ROOT_STR(m);
    sp_Enumerator *e = sp_Enumerator_new_from_items(one);
    sp_gc_wb(e);
    e->source = r;
    e->has_src = TRUE;
    e->meth = m;
    *out = sp_box_obj(e, SP_BUILTIN_ENUMERATOR);
    return 1;
  }
  if (!blk)
    return fx_inject_sym(r, name, argc, argv, out) || fx_num_step(r, name, argc, argv, out) ||
           fx_int_iter(r, name, argc, argv, NULL, out) ||
           fx_range_step(r, name, argc, argv, NULL, out) || fx_range_via_array(r, name, argc, argv, NULL, out) ||
           fx_seq_pattern(r, name, argc, argv, out);
  if (fx_range_step(r, name, argc, argv, blk, out) || fx_range_via_array(r, name, argc, argv, blk, out)) return 1;
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

static int fx_params_arity(const pm_parameters_node_t *ps, int lambda);

static int fx_proc_arity(const FxBlock *b) {
  if (b->cfn == fx_method_value_step) return fx_method_value_arity((const FxMethodValue *)b->ctx);
  if (!b->node) return -2;   /* &:sym, as CRuby answers for a lambda from a Symbol */
  const pm_constant_id_list_t *locals;
  const pm_node_t *params, *body;
  fx_block_parts(b->node, &locals, &params, &body);
  if (!params) return 0;
  if (PM_NODE_TYPE_P(params, PM_IT_PARAMETERS_NODE)) return 1;
  if (PM_NODE_TYPE_P(params, PM_NUMBERED_PARAMETERS_NODE)) return ((const pm_numbered_parameters_node_t *)params)->maximum;
  return fx_params_arity(((const pm_block_parameters_node_t *)params)->parameters, b->lambda);
}

/* The arity of a parameter list, of a lambda or a method when lambda is 1.
   CRuby: min counts the required parameters, plus 1 for any required
   keyword. A lambda with an optional part, or a proc with *rest, answers
   -(min + 1). */
static int fx_params_arity(const pm_parameters_node_t *ps, int lambda) {
  if (!ps) return 0;
  int req = (int)(ps->requireds.size + ps->posts.size);
  int rest = ps->rest && PM_NODE_TYPE_P(ps->rest, PM_REST_PARAMETER_NODE);
  int reqkw = 0, optkw = ps->keyword_rest != NULL;
  for (size_t i = 0; i < ps->keywords.size; i++) {
    if (PM_NODE_TYPE_P(ps->keywords.nodes[i], PM_REQUIRED_KEYWORD_PARAMETER_NODE)) reqkw = 1;
    else optkw = 1;
  }
  int min = req + reqkw;
  int opt = lambda ? rest || ps->optionals.size || (!reqkw && optkw) : rest;
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

/* ---- Method ---- */

static int fx_is_method_value(sp_RbVal v) {
  return v.tag == SP_TAG_OBJ && v.cls_id == SP_BUILTIN_METHOD && v.v.p;
}

static void fx_method_value_scan(void *p) {
  FxMethodValue *mv = (FxMethodValue *)p;
  sp_mark_rbval(mv->recv);
  if (mv->proc) sp_gc_mark(mv->proc);
}

/* obj.method(:name), and public_method when public_only is 1: answers 0
   when r has no such method. A method of the program comes first, as a
   call finds it; a builtin method of the name is the other case. */
static int fx_method_value_new(sp_RbVal r, sp_RbVal name, int public_only, sp_RbVal *out) {
  const char *mn = fx_name_arg(name);
  sp_sym s = sp_sym_intern(mn);
  const FxMethod *m = r.tag == SP_TAG_CLASS ? fx_find_smethod(fx_class_value(r), s) : NULL;
  if (!m && (fx_is_fx_object(r) || r.tag == SP_TAG_CLASS || fx_reopened_builtins || fx_is_exc(r))) {
    m = fx_find_method(fx_class_of(r), s);
    /* a top-level def does not hide a builtin method of the value */
    if (m && m->owner == L.object && !fx_is_fx_object(r) && fx_builtin_responds(r, mn)) m = NULL;
    if (m && fx_builtin_shadows(r, m, mn)) m = NULL;
  }
  if (m && public_only && m->vis != FX_VIS_PUBLIC) return 0;
  if (!m && !sp_poly_truthy(fx_respond_to(r, name, sp_box_bool(!public_only)))) return 0;
  *out = fx_method_value_make(r, m, s, NULL, 0);
  return 1;
}

/* A Method of recv, or an UnboundMethod of the class or module k. */
static sp_RbVal fx_method_value_make(sp_RbVal recv, const FxMethod *m, sp_sym name, const FxClass *k, int unbound) {
  SP_GC_ROOT_RBVAL(recv);
  FxMethodValue *mv = (FxMethodValue *)sp_gc_alloc(sizeof *mv, NULL, fx_method_value_scan);
  mv->bm.self = recv.v.p;
  mv->bm.name = sp_sym_to_s(name);
  mv->bm.unbound = unbound;
  mv->recv = recv;
  mv->m = m;
  mv->name = name;
  mv->klass = k;
  return sp_box_obj(mv, SP_BUILTIN_METHOD);
}

/* The class or module that owns the method: of the program, else the one
   that gave an UnboundMethod, else the class of the receiver. */
static const FxClass *fx_method_value_owner(const FxMethodValue *mv) {
  if (mv->m) return mv->m->owner;
  return mv->klass ? mv->klass : fx_class_of(mv->recv);
}

/* Whether v can be the receiver of a method of k (UnboundMethod#bind):
   any value for Kernel, Object and BasicObject, else an instance of k. */
static int fx_method_value_bindable(sp_RbVal v, const FxClass *k) {
  if (k == L.object || !strcmp(k->name, "Kernel") || !strcmp(k->name, "BasicObject")) return 1;
  return fx_is_a(v, k);
}

static sp_RbVal fx_method_value_call(const FxMethodValue *mv, int argc, sp_RbVal *argv, const FxBlock *blk) {
  if (mv->m) return fx_invoke_b(mv->m, mv->recv, argc, argv, blk);
  return fx_send_b(mv->recv, sp_sym_to_s(mv->name), argc, argv, blk);
}

/* The C block of the Proc of a Method: a yield calls the method. */
static sp_RbVal fx_method_value_step(void *ctx, int argc, sp_RbVal *argv) {
  return fx_method_value_call((const FxMethodValue *)ctx, argc, argv, NULL);
}

/* The block for `&m`, from the Proc of the Method: a lambda. */
static const FxBlock *fx_method_value_block(sp_RbVal v) {
  FxMethodValue *mv = (FxMethodValue *)v.v.p;
  if (!mv->proc) {
    SP_GC_ROOT_RBVAL(v);
    FxBlock b = { .cfn = fx_method_value_step, .ctx = mv, .lambda = 1 };
    FxProc *p = (FxProc *)fx_block_proc(&b).v.p;
    sp_gc_wb(mv);
    mv->proc = p;
  }
  return &mv->proc->b;
}

static int fx_method_value_arity(const FxMethodValue *mv) {
  const FxMethod *m = mv->m;
  if (!m) return -1;
  switch (m->kind) {
  case FX_M_DEF: return fx_params_arity(m->def->parameters, 1);
  case FX_M_READER: return 0;
  case FX_M_WRITER: return 1;
  case FX_M_PROC: return fx_proc_arity(&((FxProc *)m->proc)->b);
  default: return -1;
  }
}

/* The parameters of a def, as Method#inspect writes them. */
static void fx_params_text(sp_String *o, const pm_parameters_node_t *ps) {
  if (!ps) return;
  int n = 0;
#define FX_SEP() (n++ ? sp_String_append(o, ", ") : (void)0)
  for (size_t i = 0; i < ps->requireds.size; i++) {
    const pm_node_t *p = ps->requireds.nodes[i];
    FX_SEP();
    sp_String_append(o, PM_NODE_TYPE_P(p, PM_REQUIRED_PARAMETER_NODE)
                          ? sp_sym_to_s(fx_sym(((const pm_required_parameter_node_t *)p)->name)) : "_");
  }
  for (size_t i = 0; i < ps->optionals.size; i++) {
    FX_SEP();
    sp_String_append(o, sp_sym_to_s(fx_sym(((const pm_optional_parameter_node_t *)ps->optionals.nodes[i])->name)));
    sp_String_append(o, "=...");
  }
  if (ps->rest && PM_NODE_TYPE_P(ps->rest, PM_REST_PARAMETER_NODE)) {
    pm_constant_id_t rn = ((const pm_rest_parameter_node_t *)ps->rest)->name;
    FX_SEP();
    sp_String_append(o, "*");
    if (rn) sp_String_append(o, sp_sym_to_s(fx_sym(rn)));
  }
  for (size_t i = 0; i < ps->posts.size; i++) {
    const pm_node_t *p = ps->posts.nodes[i];
    FX_SEP();
    sp_String_append(o, PM_NODE_TYPE_P(p, PM_REQUIRED_PARAMETER_NODE)
                          ? sp_sym_to_s(fx_sym(((const pm_required_parameter_node_t *)p)->name)) : "_");
  }
  for (size_t i = 0; i < ps->keywords.size; i++) {
    const pm_node_t *p = ps->keywords.nodes[i];
    int req = PM_NODE_TYPE_P(p, PM_REQUIRED_KEYWORD_PARAMETER_NODE);
    pm_constant_id_t kn = req ? ((const pm_required_keyword_parameter_node_t *)p)->name
                              : ((const pm_optional_keyword_parameter_node_t *)p)->name;
    FX_SEP();
    sp_String_append(o, sp_sym_to_s(fx_sym(kn)));
    sp_String_append(o, req ? ":" : ": ...");
  }
  const pm_node_t *kr = ps->keyword_rest;
  if (kr && PM_NODE_TYPE_P(kr, PM_FORWARDING_PARAMETER_NODE)) {
    FX_SEP();
    sp_String_append(o, "...");
  } else if (kr && PM_NODE_TYPE_P(kr, PM_KEYWORD_REST_PARAMETER_NODE)) {
    pm_constant_id_t rn = ((const pm_keyword_rest_parameter_node_t *)kr)->name;
    FX_SEP();
    sp_String_append(o, "**");
    if (rn) sp_String_append(o, sp_sym_to_s(fx_sym(rn)));
  } else if (kr && PM_NODE_TYPE_P(kr, PM_NO_KEYWORDS_PARAMETER_NODE)) {
    FX_SEP();
    sp_String_append(o, "**nil");
  }
  if (ps->block) {
    pm_constant_id_t bn = ps->block->name;
    FX_SEP();
    sp_String_append(o, "&");
    if (bn) sp_String_append(o, sp_sym_to_s(fx_sym(bn)));
  }
#undef FX_SEP
}

/* One entry of Method#parameters: [kind, name], or [kind] for a name of
   0 with no stand-in (a destructuring parameter). */
static void fx_param_push(sp_PolyArray *a, const char *kind, pm_constant_id_t name, const char *anon) {
  sp_PolyArray *e = sp_PolyArray_new();
  SP_GC_ROOT(e);
  sp_PolyArray_push(e, sp_box_sym(sp_sym_intern(kind)));
  if (name) sp_PolyArray_push(e, sp_box_sym(fx_sym(name)));
  else if (anon) sp_PolyArray_push(e, sp_box_sym(sp_sym_intern(anon)));
  sp_PolyArray_push(a, sp_box_poly_array(e));
}

/* Method#parameters of a parameter list, as CRuby lists them: an
   anonymous *, ** or & has the name :*, :** or :&. */
static sp_RbVal fx_params_list(const pm_parameters_node_t *ps) {
  sp_PolyArray *a = sp_PolyArray_new();
  SP_GC_ROOT(a);
  if (!ps) return sp_box_poly_array(a);
  for (size_t i = 0; i < ps->requireds.size; i++) {
    const pm_node_t *p = ps->requireds.nodes[i];
    fx_param_push(a, "req", PM_NODE_TYPE_P(p, PM_REQUIRED_PARAMETER_NODE)
                              ? ((const pm_required_parameter_node_t *)p)->name : 0, NULL);
  }
  for (size_t i = 0; i < ps->optionals.size; i++)
    fx_param_push(a, "opt", ((const pm_optional_parameter_node_t *)ps->optionals.nodes[i])->name, NULL);
  if (ps->rest && PM_NODE_TYPE_P(ps->rest, PM_REST_PARAMETER_NODE))
    fx_param_push(a, "rest", ((const pm_rest_parameter_node_t *)ps->rest)->name, "*");
  for (size_t i = 0; i < ps->posts.size; i++) {
    const pm_node_t *p = ps->posts.nodes[i];
    fx_param_push(a, "req", PM_NODE_TYPE_P(p, PM_REQUIRED_PARAMETER_NODE)
                              ? ((const pm_required_parameter_node_t *)p)->name : 0, NULL);
  }
  for (size_t i = 0; i < ps->keywords.size; i++) {
    const pm_node_t *p = ps->keywords.nodes[i];
    if (PM_NODE_TYPE_P(p, PM_REQUIRED_KEYWORD_PARAMETER_NODE))
      fx_param_push(a, "keyreq", ((const pm_required_keyword_parameter_node_t *)p)->name, NULL);
    else
      fx_param_push(a, "key", ((const pm_optional_keyword_parameter_node_t *)p)->name, NULL);
  }
  const pm_node_t *kr = ps->keyword_rest;
  if (kr && PM_NODE_TYPE_P(kr, PM_FORWARDING_PARAMETER_NODE)) {
    fx_param_push(a, "rest", 0, "*");
    fx_param_push(a, "keyrest", 0, "**");
    fx_param_push(a, "block", 0, "&");
  } else if (kr && PM_NODE_TYPE_P(kr, PM_KEYWORD_REST_PARAMETER_NODE)) {
    fx_param_push(a, "keyrest", ((const pm_keyword_rest_parameter_node_t *)kr)->name, "**");
  } else if (kr && PM_NODE_TYPE_P(kr, PM_NO_KEYWORDS_PARAMETER_NODE)) {
    fx_param_push(a, "nokey", 0, NULL);
  }
  if (ps->block) fx_param_push(a, "block", ps->block->name, "&");
  return sp_box_poly_array(a);
}

/* Method#parameters: of a def or of the block of define_method; a
   reader has none and a writer one; a builtin method takes a rest, as
   CRuby answers for a method of C with an arity of -1. */
static sp_RbVal fx_method_value_params(const FxMethodValue *mv) {
  const FxMethod *m = mv->m;
  sp_PolyArray *a;
  if (m && m->kind == FX_M_DEF) return fx_params_list(m->def->parameters);
  if (m && m->kind == FX_M_PROC) {
    const FxBlock *b = &((FxProc *)m->proc)->b;
    const pm_constant_id_list_t *locals;
    const pm_node_t *params, *body;
    if (!b->node) return fx_params_list(NULL);
    fx_block_parts(b->node, &locals, &params, &body);
    if (params && PM_NODE_TYPE_P(params, PM_BLOCK_PARAMETERS_NODE))
      return fx_params_list(((const pm_block_parameters_node_t *)params)->parameters);
    a = sp_PolyArray_new();
    SP_GC_ROOT(a);
    if (params && PM_NODE_TYPE_P(params, PM_IT_PARAMETERS_NODE)) fx_param_push(a, "req", 0, NULL);
    if (params && PM_NODE_TYPE_P(params, PM_NUMBERED_PARAMETERS_NODE))
      for (int i = 1; i <= ((const pm_numbered_parameters_node_t *)params)->maximum; i++)
        fx_param_push(a, "req", 0, sp_sprintf("_%d", i));
    return sp_box_poly_array(a);
  }
  a = sp_PolyArray_new();
  SP_GC_ROOT(a);
  if (m && m->kind == FX_M_READER) return sp_box_poly_array(a);
  fx_param_push(a, m && m->kind == FX_M_WRITER ? "req" : "rest", 0, NULL);
  return sp_box_poly_array(a);
}

/* #<Method: C#name(params) file:line>; C.name for a method of a class,
   and C(Owner)#name when another class defines it. A builtin method
   shows (*) for its parameters. An UnboundMethod shows the class or
   module that gave it. */
static const char *fx_method_value_inspect(const FxMethodValue *mv) {
  const FxMethod *m = mv->m;
  sp_String *o = sp_String_new(mv->bm.unbound ? "#<UnboundMethod: " : "#<Method: ");
  SP_GC_ROOT(o);
  int single = !mv->bm.unbound && mv->recv.tag == SP_TAG_CLASS && m && m->owner != fx_class_of(mv->recv);
  const FxClass *k = mv->bm.unbound ? mv->klass : fx_class_of(mv->recv);
  if (single) {
    sp_String_append(o, sp_poly_inspect(mv->recv));
    sp_String_append(o, ".");
  } else {
    sp_String_append(o, k->name);
    if (m && m->owner != k) {
      sp_String_append(o, "(");
      sp_String_append(o, m->owner->name);
      sp_String_append(o, ")");
    }
    sp_String_append(o, "#");
  }
  sp_String_append(o, sp_sym_to_s(mv->name));
  sp_String_append(o, "(");
  if (!m) sp_String_append(o, "*");
  else if (m->kind == FX_M_DEF) fx_params_text(o, m->def->parameters);
  sp_String_append(o, ")");
  if (m && m->kind == FX_M_DEF) {
    const pm_node_t *d = (const pm_node_t *)m->def;
    sp_String_append(o, sp_sprintf(" %s:%d", fx_source_of(d)->path, fx_line(d)));
  }
  sp_String_append(o, ">");
  return sp_String_cstr(o);
}

static int fx_method_value_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                                  sp_RbVal *out) {
  const FxMethodValue *mv = (const FxMethodValue *)r.v.p;
  if (mv->bm.unbound) {
    /* bind(obj) and bind_call(obj, *args): the Method of obj */
    if (argc >= 1 && (FX_IS("bind") || FX_IS("bind_call"))) {
      const FxClass *k = fx_method_value_owner(mv);
      if (!fx_method_value_bindable(argv[0], k))
        sp_raise_cls("TypeError", sp_sprintf("bind argument must be an instance of %s", k->name));
      SP_GC_ROOT_RBVAL(r);
      sp_RbVal bound = fx_method_value_make(argv[0], mv->m, mv->name, mv->klass, 0);
      if (FX_IS("bind") && argc == 1) { *out = bound; return 1; }
      if (FX_IS("bind")) return 0;
      SP_GC_ROOT_RBVAL(bound);
      *out = fx_method_value_call((const FxMethodValue *)bound.v.p, argc - 1, argv + 1, blk);
      return 1;
    }
    if (FX_IS("call") || FX_IS("()") || FX_IS("[]") || FX_IS("===") || FX_IS("to_proc") || FX_IS("receiver") ||
        FX_IS("unbind"))
      return 0;
  } else if (FX_IS("call") || FX_IS("()") || FX_IS("[]") || FX_IS("===")) {
    SP_GC_ROOT_RBVAL(r);
    *out = fx_method_value_call(mv, argc, argv, blk);
    return 1;
  }
  /* the same method of the same receiver */
  if (argc == 1 && !blk && (FX_IS("==") || FX_IS("eql?"))) {
    const FxMethodValue *o = fx_is_method_value(argv[0]) ? (const FxMethodValue *)argv[0].v.p : NULL;
    *out = sp_box_bool(o && o->m == mv->m && o->name == mv->name && o->recv.tag == mv->recv.tag &&
                       o->recv.cls_id == mv->recv.cls_id && o->recv.v.i == mv->recv.v.i &&
                       o->bm.unbound == mv->bm.unbound && fx_method_value_owner(o) == fx_method_value_owner(mv));
    return 1;
  }
  if (blk || argc) return 0;
  if (FX_IS("to_proc")) { *out = sp_box_obj(fx_method_value_block(r)->proc, SP_BUILTIN_PROC); return 1; }
  if (FX_IS("unbind")) {
    *out = fx_method_value_make(sp_box_nil(), mv->m, mv->name, fx_method_value_owner(mv), 1);
    return 1;
  }
  if (FX_IS("arity")) { *out = sp_box_int(fx_method_value_arity(mv)); return 1; }
  if (FX_IS("parameters")) { *out = fx_method_value_params(mv); return 1; }
  if (FX_IS("name")) { *out = sp_box_sym(mv->name); return 1; }
  if (FX_IS("receiver")) { *out = mv->recv; return 1; }
  if (FX_IS("owner")) { *out = fx_class_box(fx_method_value_owner(mv)); return 1; }
  if (FX_IS("source_location")) {
    if (!mv->m || mv->m->kind != FX_M_DEF) { *out = sp_box_nil(); return 1; }
    const pm_node_t *d = (const pm_node_t *)mv->m->def;
    sp_PolyArray *a = sp_PolyArray_new();
    SP_GC_ROOT(a);
    sp_PolyArray_push(a, sp_box_str_new(sp_str_dup(fx_source_of(d)->path)));
    sp_PolyArray_push(a, sp_box_int(fx_line(d)));
    *out = sp_box_poly_array(a);
    return 1;
  }
  if (FX_IS("inspect") || FX_IS("to_s")) { *out = sp_box_str_new(sp_str_dup(fx_method_value_inspect(mv))); return 1; }
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
    /* min and max read the sides, in the order of the checks of CRuby's
       range_min and range_max; minmax calls max first, as CRuby does */
    if (FX_IS("min") || FX_IS("max") || FX_IS("minmax")) {
      int nobeg = g->omitted & SP_FRANGE_NO_BEGIN, noend = g->omitted & SP_FRANGE_NO_END;
      sp_RbVal lo = sp_box_nil(), hi = sp_box_nil();
      if (!FX_IS("min")) {
        if (noend) sp_raise_cls("RangeError", "cannot get the maximum of endless range");
        int c = nobeg ? -1 : g->first > g->last ? 1 : g->first == g->last ? 0 : -1;
        if (c <= 0) {
          if (g->excl && !(g->omitted & SP_FRANGE_INT_END))
            sp_raise_cls("TypeError", "cannot exclude non Integer end value");
          if (!(g->excl && c == 0)) {
            if (g->excl) sp_raise_cls("TypeError", "cannot exclude end value with non Integer begin value");
            hi = fx_frange_side(g, 1);
          }
        }
      }
      SP_GC_ROOT_RBVAL(hi);
      if (!FX_IS("max")) {
        if (nobeg) sp_raise_cls("RangeError", "cannot get the minimum of beginless range");
        if (noend || !(g->first > g->last || (g->excl && g->first == g->last))) lo = fx_frange_side(g, 0);
      }
      SP_GC_ROOT_RBVAL(lo);
      *out = FX_IS("minmax") ? fx_pair(lo, hi) : FX_IS("min") ? lo : hi;
      return 1;
    }
    if (FX_IS("to_a") || FX_IS("entries")) {
      if (g->omitted & SP_FRANGE_NO_BEGIN) sp_raise_cls("TypeError", "can't iterate from NilClass");
      if (g->omitted & SP_FRANGE_NO_END) sp_raise_cls("RangeError", "cannot convert endless range to an array");
      sp_raise_cls("TypeError", "can't iterate from Float");
    }
    /* count of an open Range is Infinity, as CRuby's range_count */
    if (FX_IS("count") && (g->omitted & (SP_FRANGE_NO_BEGIN | SP_FRANGE_NO_END))) {
      *out = sp_box_float(HUGE_VAL);
      return 1;
    }
    if (FX_IS("size") || FX_IS("count")) {
      if (g->omitted & SP_FRANGE_NO_BEGIN) sp_raise_cls("TypeError", "can't iterate from NilClass");
      sp_raise_cls("TypeError", "can't iterate from Float");
    }
    return 0;
  }
  if (argc == 1 && (FX_IS("include?") || FX_IS("member?") || FX_IS("cover?") || FX_IS("==="))) {
    sp_RbVal a = argv[0];
    if (a.tag == SP_TAG_INT) *out = sp_box_bool(sp_frange_cover_i(*g, a.v.i));
    else if (a.tag == SP_TAG_FLT) *out = sp_box_bool(sp_frange_cover(*g, a.v.f));
    else *out = sp_box_bool(sp_frange_cover_poly(*g, a));
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
    sp_PolyArray_push(p, g.sym ? sp_box_sym(sp_sym_intern(SP_SEBN(a->data[i]))) : sp_box_str_new(sp_str_dup(SP_SEBN(a->data[i]))));
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
    if (FX_IS("minmax")) {
      sp_RbVal hi = fx_send(r, "max", 0, NULL);
      SP_GC_ROOT_RBVAL(hi);
      sp_RbVal lo = fx_send(r, "min", 0, NULL);
      SP_GC_ROOT_RBVAL(lo);
      *out = fx_pair(lo, hi);
      return 1;
    }
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

static int fx_is_any_range(sp_RbVal v) {
  return v.tag == SP_TAG_OBJ && v.v.p &&
         (v.cls_id == SP_BUILTIN_RANGE || v.cls_id == SP_BUILTIN_FLOAT_RANGE || v.cls_id == SP_BUILTIN_STR_RANGE);
}

/* a <=> b as -1, 0 or 1, or 2 when they do not compare */
static int fx_range_cmp(sp_RbVal a, sp_RbVal b) {
  sp_RbVal c = fx_send(a, "<=>", 1, &b);
  if (c.tag != SP_TAG_INT) return 2;
  return c.v.i < 0 ? -1 : c.v.i > 0;
}

/* Whether no value lies between b and e: a nil side is open. */
static int fx_range_empty(sp_RbVal b, sp_RbVal e, int excl) {
  if (b.tag == SP_TAG_NIL || e.tag == SP_TAG_NIL) return 0;
  int c = fx_range_cmp(b, e);
  return c == 2 || c > 0 || (c == 0 && excl);
}

/* Range#overlap?: neither Range is empty, and each begins before the
   other ends. */
static sp_RbVal fx_range_overlap(sp_RbVal r, sp_RbVal o) {
  if (!fx_is_any_range(o))
    sp_raise_cls("TypeError", sp_sprintf("wrong argument type %s (expected Range)", sp_poly_class_name(o)));
  SP_GC_ROOT_RBVAL(r);
  SP_GC_ROOT_RBVAL(o);
  sp_RbVal ab = fx_send(r, "begin", 0, NULL), ae = sp_box_nil(), bb = sp_box_nil(), be = sp_box_nil();
  SP_GC_ROOT_RBVAL(ab);
  SP_GC_ROOT_RBVAL(ae);
  SP_GC_ROOT_RBVAL(bb);
  SP_GC_ROOT_RBVAL(be);
  ae = fx_send(r, "end", 0, NULL);
  bb = fx_send(o, "begin", 0, NULL);
  be = fx_send(o, "end", 0, NULL);
  int ax = sp_poly_truthy(fx_send(r, "exclude_end?", 0, NULL));
  int bx = sp_poly_truthy(fx_send(o, "exclude_end?", 0, NULL));
  if (fx_range_empty(ab, ae, ax) || fx_range_empty(bb, be, bx)) return sp_box_bool(0);
  /* two beginless (or two endless) Ranges overlap when their ends compare */
  if (ab.tag == SP_TAG_NIL && bb.tag == SP_TAG_NIL && ae.tag != SP_TAG_NIL && be.tag != SP_TAG_NIL &&
      fx_range_cmp(ae, be) == 2)
    return sp_box_bool(0);
  if (ae.tag == SP_TAG_NIL && be.tag == SP_TAG_NIL && ab.tag != SP_TAG_NIL && bb.tag != SP_TAG_NIL &&
      fx_range_cmp(ab, bb) == 2)
    return sp_box_bool(0);
  return sp_box_bool(!fx_range_empty(ab, be, bx) && !fx_range_empty(bb, ae, ax));
}

/* Whether the value v lies in the sides b and e (nil is open). */
static int fx_range_covers(sp_RbVal b, sp_RbVal e, int excl, sp_RbVal v) {
  if (b.tag != SP_TAG_NIL) {
    int c = fx_range_cmp(b, v);
    if (c == 2 || c > 0) return 0;
  }
  if (e.tag != SP_TAG_NIL) {
    int c = fx_range_cmp(v, e);
    if (c == 2 || (excl ? c >= 0 : c > 0)) return 0;
  }
  return 1;
}

/* cover?(range) on any Range, as CRuby's r_cover_range_p: the other Range
   is not empty and lies inside this one. */
static sp_RbVal fx_range_cover_range(sp_RbVal r, sp_RbVal o) {
  SP_GC_ROOT_RBVAL(r);
  SP_GC_ROOT_RBVAL(o);
  sp_RbVal b = fx_send(r, "begin", 0, NULL), e = sp_box_nil(), ob = sp_box_nil(), oe = sp_box_nil();
  SP_GC_ROOT_RBVAL(b);
  SP_GC_ROOT_RBVAL(e);
  SP_GC_ROOT_RBVAL(ob);
  SP_GC_ROOT_RBVAL(oe);
  e = fx_send(r, "end", 0, NULL);
  ob = fx_send(o, "begin", 0, NULL);
  oe = fx_send(o, "end", 0, NULL);
  int x = sp_poly_truthy(fx_send(r, "exclude_end?", 0, NULL));
  int ox = sp_poly_truthy(fx_send(o, "exclude_end?", 0, NULL));
  if ((e.tag != SP_TAG_NIL && oe.tag == SP_TAG_NIL) || (b.tag != SP_TAG_NIL && ob.tag == SP_TAG_NIL))
    return sp_box_bool(0);
  if (ob.tag != SP_TAG_NIL && oe.tag != SP_TAG_NIL && fx_range_empty(ob, oe, ox)) return sp_box_bool(0);
  if (ob.tag != SP_TAG_NIL && !fx_range_covers(b, e, x, ob)) return sp_box_bool(0);
  if (e.tag == SP_TAG_NIL) return sp_box_bool(1);
  int c = fx_range_cmp(e, oe);
  if (c == 2) return sp_box_bool(0);
  if (x == ox) return sp_box_bool(c >= 0);
  if (x) return sp_box_bool(c > 0);
  if (c >= 0) return sp_box_bool(1);
  /* (1..3).cover?(1...4): the last Integer of the other Range */
  if (o.cls_id == SP_BUILTIN_RANGE && !((const sp_Range *)o.v.p)->fe && oe.tag == SP_TAG_INT) {
    sp_RbVal m = sp_box_int(oe.v.i - 1);
    return sp_box_bool(fx_range_cmp(e, m) >= 0 && fx_range_cmp(e, m) != 2);
  }
  return sp_box_bool(0);
}

static int fx_range_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (argc == 1 && FX_IS("overlap?") && fx_is_any_range(r)) {
    *out = fx_range_overlap(r, argv[0]);
    return 1;
  }
  /* cover?(range) with a side that is not an Integer; an Integer Range
     given an Integer Range takes the path below */
  if (argc == 1 && FX_IS("cover?") && fx_is_any_range(r) && fx_is_any_range(argv[0]) &&
      !(fx_is_range(r) && fx_is_range(argv[0]) && !((const sp_Range *)r.v.p)->fe &&
        !((const sp_Range *)argv[0].v.p)->fe)) {
    *out = fx_range_cover_range(r, argv[0]);
    return 1;
  }
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
    /* min and max of an open Range or of a Float end, as CRuby's range_min
       and range_max; minmax calls max first, as CRuby does */
    if (FX_IS("min") || FX_IS("max") || FX_IS("minmax")) {
      sp_RbVal lo = sp_box_nil(), hi = sp_box_nil();
      if (!FX_IS("min")) {
        if (g->noend) sp_raise_cls("RangeError", "cannot get the maximum of endless range");
        if (g->fe) {
          if (g->nobeg || (double)g->first <= g->fend) {
            if (g->excl) sp_raise_cls("TypeError", "cannot exclude non Integer end value");
            hi = sp_box_float(g->fend);
          }
        } else if (g->nobeg || size > 0) {
          hi = sp_box_int(last);
        }
      }
      SP_GC_ROOT_RBVAL(hi);
      if (!FX_IS("max")) {
        if (g->nobeg) sp_raise_cls("RangeError", "cannot get the minimum of beginless range");
        int empty = g->noend ? 0 : g->fe ? ((double)g->first > g->fend || (g->excl && (double)g->first == g->fend))
                                         : size <= 0;
        if (!empty) lo = sp_box_int(g->first);
      }
      SP_GC_ROOT_RBVAL(lo);
      *out = FX_IS("minmax") ? fx_pair(lo, hi) : FX_IS("min") ? lo : hi;
      return 1;
    }
    if (size < 0) return 0;
    if (FX_IS("to_a") || FX_IS("entries")) { *out = sp_box_poly_array(fx_seq_array(r)); return 1; }
    if (FX_IS("size") || FX_IS("count")) { *out = sp_box_int(size); return 1; }
    if (FX_IS("last")) { *out = g->fe ? sp_box_float(g->fend) : sp_box_int(g->last); return 1; }
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
    /* a Rational compares exactly with the ends, and a Bignum lies past
       every finite end */
    if (a.tag == SP_TAG_BIGINT || sp_poly_is_rat_kind(a)) {
      *out = sp_box_bool(sp_range_cover_poly((sp_Range *)g, a));
      return 1;
    }
    if (a.tag != SP_TAG_INT && a.tag != SP_TAG_FLT) { *out = sp_box_bool(0); return 1; }
    double x = a.tag == SP_TAG_INT ? (double)a.v.i : a.v.f;
    double e = g->fe ? g->fend : (double)g->last;
    int lo = g->nobeg || x >= (double)g->first;
    int hi = g->noend || (g->excl ? x < e : x <= e);
    *out = sp_box_bool(lo && hi);
    return 1;
  }
  if (argc == 1 && FX_IS("first") && argv[0].tag == SP_TAG_INT && argv[0].v.i < 0)
    sp_raise_cls("ArgumentError", "negative array size (or size too big)");
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
