/* hash.c -- Hash in the walker.

   A Hash that the walker makes is a spinel sp_PolyPolyHash (keys and
   values are sp_RbVal; the entries keep their insertion order). A Hash
   from the runtime can be a typed one (sp_StrIntHash, ...): the methods
   that read take a poly copy, and the methods that change it stop as a
   gap.

   The Enumerable methods of a Hash are in iter.c, which yields [key,
   value] pairs. This file has the methods that are special to Hash, and
   the hooks that hash a user object as a key. */

#include "lupe.h"

#define LUPE_IS(s) (!strcmp(name, (s)))

static sp_PolyPolyHash *lupe_hash_mut(sp_RbVal r, const char *name) {
  if (r.cls_id != SP_BUILTIN_POLY_POLY_HASH) lupe_gap(sp_sprintf("Hash#%s on a typed Hash", name));
  if (sp_gc_is_frozen(r.v.p)) sp_raise_frozen_hash_at(r.v.p, r.cls_id);
  return (sp_PolyPolyHash *)r.v.p;
}

static sp_RbVal lupe_hash_box(sp_PolyPolyHash *h) {
  return sp_box_obj(h, SP_BUILTIN_POLY_POLY_HASH);
}

/* The default proc of Hash.new { |h, k| }: it calls the LupeProc in self. */
static sp_RbVal lupe_hash_dproc(sp_PolyPolyHash *h, sp_RbVal k, void *self) {
  sp_RbVal argv[2] = { lupe_hash_box(h), k };
  SP_GC_ROOT_RBVAL(argv[0]);
  SP_GC_ROOT_RBVAL(argv[1]);
  return lupe_yield(&((LupeProc *)self)->b, 2, argv);
}

/* A copy of the Hash h with the same default. */
static sp_PolyPolyHash *lupe_hash_dup(sp_PolyPolyHash *h) {
  SP_GC_ROOT(h);
  sp_PolyPolyHash *o = sp_PolyPolyHash_new();
  SP_GC_ROOT(o);
  sp_gc_wb(o);   /* a direct store into a GC object, as every one below */
  o->default_v = h->default_v;
  o->dproc = h->dproc;
  o->dproc_self = h->dproc_self;
  o->ident = h->ident;
  for (sp_int i = 0; i < h->len; i++) sp_PolyPolyHash_set(o, h->keys[h->order[i]], h->vals[h->order[i]]);
  return o;
}

/* Hash.new, Hash.new(default) and Hash.new { |h, k| }. */
static int lupe_hash_class(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const LupeBlock *blk,
                           sp_RbVal *out) {
  LupeClass *k = lupe_class_value(r);
  if (!k || strcmp(k->name, "Hash") || !LUPE_IS("new")) return 0;
  /* Hash.new(capacity: n) sizes the table; the size is not visible */
  if (argc >= 1 && lupe_is_hash(argv[argc - 1]) && argv[argc - 1].v.p == L.kwhash) argc--;
  if (argc > 1) return 0;
  sp_PolyPolyHash *h = sp_PolyPolyHash_new();
  SP_GC_ROOT(h);
  sp_gc_wb(h);
  if (argc == 1) h->default_v = argv[0];
  if (blk) {
    if (argc) sp_raise_cls("ArgumentError", "wrong number of arguments (given 1, expected 0)");
    LupeBlock b = *blk;
    b.site = NULL;
    sp_RbVal p = lupe_block_proc(&b);
    /* h can be old after the allocation of p */
    sp_gc_wb(h);
    h->dproc = lupe_hash_dproc;
    h->dproc_self = p.v.p;
  }
  *out = lupe_hash_box(h);
  return 1;
}

/* The methods of a Hash. Answers 1 and sets *out when it handles the call. */
static int lupe_hash_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const LupeBlock *blk,
                          sp_RbVal *out) {
  if (r.tag == SP_TAG_CLASS) return lupe_hash_class(r, name, argc, argv, blk, out);
  if (!lupe_is_hash(r)) return 0;
  SP_GC_ROOT_RBVAL(r);
  sp_PolyPolyHash *h = sp_poly_as_poly_poly_hash(r);
  SP_GC_ROOT(h);
  sp_RbVal v = sp_box_nil(), y = sp_box_nil();
  SP_GC_ROOT_RBVAL(v);
  SP_GC_ROOT_RBVAL(y);
  if (argc == 1 && LUPE_IS("[]")) { *out = sp_PolyPolyHash_get(h, argv[0]); return 1; }
  if (argc == 2 && (LUPE_IS("[]=") || LUPE_IS("store"))) {
    sp_PolyPolyHash_set(lupe_hash_mut(r, name), argv[0], argv[1]);
    *out = argv[1];
    return 1;
  }
  if (argc == 1 && (LUPE_IS("key?") || LUPE_IS("has_key?") || LUPE_IS("include?") || LUPE_IS("member?"))) {
    *out = sp_box_bool(sp_PolyPolyHash_has_key(h, argv[0]));
    return 1;
  }
  if (argc == 1 && (LUPE_IS("value?") || LUPE_IS("has_value?"))) {
    *out = sp_box_bool(sp_PolyPolyHash_has_value(h, argv[0]));
    return 1;
  }
  if ((argc == 1 || argc == 2) && LUPE_IS("fetch")) {
    if (sp_PolyPolyHash_has_key(h, argv[0])) { *out = sp_PolyPolyHash_get(h, argv[0]); return 1; }
    if (blk) { *out = lupe_y1(blk, argv[0]); return 1; }
    if (argc == 2) { *out = argv[1]; return 1; }
    sp_raise_cls("KeyError", sp_sprintf("key not found: %s", sp_poly_inspect(argv[0])));
  }
  if (argc == 1 && LUPE_IS("delete")) {
    sp_PolyPolyHash *m = lupe_hash_mut(r, name);
    if (sp_PolyPolyHash_has_key(m, argv[0])) {
      *out = sp_PolyPolyHash_get(m, argv[0]);
      sp_PolyPolyHash_delete(m, argv[0]);
    } else {
      *out = blk ? lupe_y1(blk, argv[0]) : sp_box_nil();
    }
    return 1;
  }
  if (argc == 1 && LUPE_IS("key")) {
    for (sp_int i = 0; i < h->len; i++)
      if (sp_poly_truthy(sp_poly_eq_value(h->vals[h->order[i]], argv[0], 0))) {
        *out = h->keys[h->order[i]];
        return 1;
      }
    *out = sp_box_nil();
    return 1;
  }
  if (argc == 0 && !blk) {
    if (LUPE_IS("size") || LUPE_IS("length") || LUPE_IS("count")) { *out = sp_box_int(h->len); return 1; }
    if (LUPE_IS("empty?")) { *out = sp_box_bool(h->len == 0); return 1; }
    if (LUPE_IS("keys") || LUPE_IS("values")) {
      sp_PolyArray *a = sp_PolyArray_new();
      SP_GC_ROOT(a);
      for (sp_int i = 0; i < h->len; i++) sp_PolyArray_push(a, name[0] == 'k' ? h->keys[h->order[i]] : h->vals[h->order[i]]);
      *out = sp_box_poly_array(a);
      return 1;
    }
    if (LUPE_IS("to_a") || LUPE_IS("entries")) { *out = sp_box_poly_array(lupe_seq_array(r)); return 1; }
    if (LUPE_IS("to_h") || LUPE_IS("to_hash")) { *out = r; return 1; }
    if (LUPE_IS("dup") || LUPE_IS("clone")) { *out = lupe_hash_box(lupe_hash_dup(h)); return 1; }
    if (LUPE_IS("default")) { *out = h->default_v; return 1; }
    if (LUPE_IS("invert")) {
      sp_PolyPolyHash *o = sp_PolyPolyHash_new();
      SP_GC_ROOT(o);
      for (sp_int i = 0; i < h->len; i++) sp_PolyPolyHash_set(o, h->vals[h->order[i]], h->keys[h->order[i]]);
      *out = lupe_hash_box(o);
      return 1;
    }
    if (LUPE_IS("compact")) {
      /* a copy keeps the default and the default proc */
      sp_PolyPolyHash *o = lupe_hash_dup(h);
      SP_GC_ROOT(o);
      for (sp_int i = 0; i < h->len; i++)
        if (h->vals[h->order[i]].tag == SP_TAG_NIL) sp_PolyPolyHash_delete(o, h->keys[h->order[i]]);
      *out = lupe_hash_box(o);
      return 1;
    }
    if (LUPE_IS("first")) {
      *out = h->len ? lupe_pair(h->keys[h->order[0]], h->vals[h->order[0]]) : sp_box_nil();
      return 1;
    }
    if (LUPE_IS("sort") || LUPE_IS("min") || LUPE_IS("max")) {
      sp_RbVal s = lupe_sorted(lupe_seq_array(r), NULL, NULL);
      sp_int n = sp_poly_length(s);
      *out = name[0] == 's' ? s : n == 0 ? sp_box_nil() : sp_poly_arr_get(s, name[1] == 'i' ? 0 : n - 1);
      return 1;
    }
    if (LUPE_IS("clear")) {
      sp_PolyPolyHash *m = lupe_hash_mut(r, name);
      while (m->len) sp_PolyPolyHash_delete(m, m->keys[m->order[0]]);
      *out = r;
      return 1;
    }
  }
  if (argc == 1 && !blk && LUPE_IS("default=")) {
    sp_PolyPolyHash *m = lupe_hash_mut(r, name);
    sp_gc_wb(m);
    m->default_v = argv[0];
    *out = argv[0];
    return 1;
  }
  if (argc == 1 && (LUPE_IS("==") || LUPE_IS("eql?"))) {
    *out = sp_poly_eq_value(r, argv[0], 0);
    return 1;
  }
  /* a <= b: every entry of a is in b with an == value */
  if (argc == 1 && (LUPE_IS("<=") || LUPE_IS("<") || LUPE_IS(">=") || LUPE_IS(">"))) {
    sp_PolyPolyHash *o = sp_poly_as_poly_poly_hash(argv[0]);
    if (!o) sp_raise_cls("TypeError", sp_sprintf("no implicit conversion of %s into Hash", sp_poly_class_name(argv[0])));
    SP_GC_ROOT(o);
    sp_PolyPolyHash *small = name[0] == '<' ? h : o, *big = name[0] == '<' ? o : h;
    int sub = small->len <= big->len && (name[1] == '=' || small->len < big->len);
    for (sp_int i = 0; sub && i < small->len; i++) {
      sp_RbVal k = small->keys[small->order[i]];
      sub = sp_PolyPolyHash_has_key(big, k) &&
            sp_poly_truthy(sp_poly_eq_value(sp_PolyPolyHash_get(big, k), small->vals[small->order[i]], 0));
    }
    *out = sp_box_bool(sub);
    return 1;
  }
  if (argc >= 1 && LUPE_IS("dig")) {
    v = sp_PolyPolyHash_get(h, argv[0]);
    *out = argc == 1 || v.tag == SP_TAG_NIL ? v : lupe_send(v, "dig", argc - 1, argv + 1);
    return 1;
  }
  if (LUPE_IS("values_at")) {
    sp_PolyArray *a = sp_PolyArray_new();
    SP_GC_ROOT(a);
    for (int i = 0; i < argc; i++) sp_PolyArray_push(a, sp_PolyPolyHash_get(h, argv[i]));
    *out = sp_box_poly_array(a);
    return 1;
  }
  if (LUPE_IS("slice") || LUPE_IS("except")) {
    sp_PolyPolyHash *o = LUPE_IS("slice") ? sp_PolyPolyHash_new() : lupe_hash_dup(h);
    SP_GC_ROOT(o);
    for (int i = 0; i < argc; i++) {
      if (name[0] == 'e') sp_PolyPolyHash_delete(o, argv[i]);
      else if (sp_PolyPolyHash_has_key(h, argv[i])) sp_PolyPolyHash_set(o, argv[i], sp_PolyPolyHash_get(h, argv[i]));
    }
    sp_gc_wb(o);
    o->dproc = NULL;
    o->dproc_self = NULL;
    o->default_v = sp_box_nil();
    *out = lupe_hash_box(o);
    return 1;
  }
  /* merge and update: a block picks the value of a key in both */
  int bang = LUPE_IS("merge!") || LUPE_IS("update");
  if (bang || LUPE_IS("merge")) {
    sp_PolyPolyHash *o = bang ? lupe_hash_mut(r, name) : lupe_hash_dup(h);
    SP_GC_ROOT(o);
    for (int a = 0; a < argc; a++) {
      sp_PolyPolyHash *x = sp_poly_as_poly_poly_hash(argv[a]);
      if (!x) sp_raise_cls("TypeError", sp_sprintf("no implicit conversion of %s into Hash", sp_poly_class_name(argv[a])));
      SP_GC_ROOT(x);
      for (sp_int i = 0; i < x->len; i++) {
        sp_RbVal k = x->keys[x->order[i]];
        v = x->vals[x->order[i]];
        if (blk && sp_PolyPolyHash_has_key(o, k)) {
          sp_RbVal args[3] = { k, sp_PolyPolyHash_get(o, k), v };
          SP_GC_ROOT_RBVAL(args[0]);
          SP_GC_ROOT_RBVAL(args[1]);
          SP_GC_ROOT_RBVAL(args[2]);
          v = lupe_yield(blk, 3, args);
          if (L.ctl != LUPE_CTL_NONE) { *out = sp_box_nil(); return 1; }
        }
        sp_PolyPolyHash_set(o, k, v);
      }
    }
    *out = bang ? r : lupe_hash_box(o);
    return 1;
  }
  if (!blk) return 0;
  /* the methods with a block that answer a Hash */
  int sel = LUPE_IS("select") || LUPE_IS("filter"), rej = LUPE_IS("reject");
  int msel = LUPE_IS("select!") || LUPE_IS("filter!") || LUPE_IS("keep_if");
  int mrej = LUPE_IS("reject!") || LUPE_IS("delete_if");
  int tv = LUPE_IS("transform_values"), tk = LUPE_IS("transform_keys");
  int tvb = LUPE_IS("transform_values!"), tkb = LUPE_IS("transform_keys!");
  if (argc == 0 && (sel || rej || msel || mrej || tv || tk || tvb || tkb)) {
    sp_PolyPolyHash *o = sp_PolyPolyHash_new();
    SP_GC_ROOT(o);
    sp_int n = h->len;
    for (sp_int i = 0; i < h->len; i++) {
      sp_RbVal k = h->keys[h->order[i]];
      v = h->vals[h->order[i]];
      y = tv || tvb ? lupe_y1(blk, v) : tk || tkb ? lupe_y1(blk, k) : lupe_y2(blk, k, v);
      if (L.ctl != LUPE_CTL_NONE) { *out = sp_box_nil(); return 1; }
      if (tv || tvb) sp_PolyPolyHash_set(o, k, y);
      else if (tk || tkb) sp_PolyPolyHash_set(o, y, v);
      else if (sp_poly_truthy(y) == (sel || msel)) sp_PolyPolyHash_set(o, k, v);
    }
    if (sel || rej || tv || tk) { *out = lupe_hash_box(o); return 1; }
    sp_PolyPolyHash *m = lupe_hash_mut(r, name);
    int changed = o->len != n || tvb || tkb;
    while (m->len) sp_PolyPolyHash_delete(m, m->keys[m->order[0]]);
    for (sp_int i = 0; i < o->len; i++) sp_PolyPolyHash_set(m, o->keys[o->order[i]], o->vals[o->order[i]]);
    int bangname = name[strlen(name) - 1] == '!' && !tvb && !tkb;
    *out = bangname && !changed ? sp_box_nil() : r;
    return 1;
  }
  if (argc == 0 && (LUPE_IS("each_key") || LUPE_IS("each_value"))) {
    for (sp_int i = 0; i < h->len; i++) {
      lupe_y1(blk, name[5] == 'k' ? h->keys[h->order[i]] : h->vals[h->order[i]]);
      if (L.ctl != LUPE_CTL_NONE) { *out = sp_box_nil(); return 1; }
    }
    *out = r;
    return 1;
  }
  return 0;
}

/* tally and to_h with no block, on an Array. */
static int lupe_array_hash(sp_RbVal r, const char *name, int argc, sp_RbVal *out) {
  if (!lupe_is_array(r) || argc != 0) return 0;
  if (!LUPE_IS("tally") && !LUPE_IS("to_h")) return 0;
  SP_GC_ROOT_RBVAL(r);
  sp_PolyPolyHash *h = sp_PolyPolyHash_new();
  SP_GC_ROOT(h);
  for (sp_int i = 0; i < sp_poly_length(r); i++) {
    sp_RbVal e = sp_poly_arr_get(r, i);
    if (name[0] == 't' && name[1] == 'a') {
      sp_RbVal c = sp_PolyPolyHash_get(h, e);
      sp_PolyPolyHash_set(h, e, sp_box_int(c.tag == SP_TAG_INT ? c.v.i + 1 : 1));
      continue;
    }
    if (!lupe_is_array(e) || sp_poly_length(e) != 2)
      sp_raise_cls("TypeError", sp_sprintf("wrong element type %s at %lld (expected array)", sp_poly_class_name(e),
                                           (long long)i));
    sp_PolyPolyHash_set(h, sp_poly_arr_get(e, 0), sp_poly_arr_get(e, 1));
  }
  *out = lupe_hash_box(h);
  return 1;
}

/* ---- user objects as keys ---- */

/* A user object hashes by its hash method, else by identity. */
static sp_int lupe_hook_hash(int cls_id, void *p) {
  sp_RbVal obj = sp_box_obj(p, cls_id);
  if (!lupe_is_lupe_object(obj)) return (sp_int)(uintptr_t)p;
  SP_GC_ROOT_RBVAL(obj);
  LupeMethod *m = lupe_find_method(lupe_class_of(obj), sp_sym_intern("hash"));
  if (!m) return (sp_int)(uintptr_t)p;
  sp_RbVal h = lupe_invoke(m, obj, 0, NULL);
  if (h.tag == SP_TAG_INT) return h.v.i;
  return sp_rbval_hash_key(h);
}

/* A user object is eql? by its eql? method, else by identity. */
static sp_bool lupe_hook_eql(int cls_id, void *a, void *b) {
  sp_RbVal x = sp_box_obj(a, cls_id), y = sp_box_obj(b, cls_id);
  if (a == b) return TRUE;
  if (!lupe_is_lupe_object(x)) return FALSE;
  SP_GC_ROOT_RBVAL(x);
  SP_GC_ROOT_RBVAL(y);
  LupeMethod *m = lupe_find_method(lupe_class_of(x), sp_sym_intern("eql?"));
  if (!m) return FALSE;
  return sp_poly_truthy(lupe_invoke(m, x, 1, &y)) ? TRUE : FALSE;
}

static void lupe_hash_init(void) {
  SP_INSTALL_HOOK(sp_obj_hash_hook, lupe_hook_hash);
  SP_INSTALL_HOOK(sp_obj_eql_hook, lupe_hook_eql);
}

#undef LUPE_IS
