/* hash.c -- Hash in the walker.

   A Hash that the walker makes is a spinel sp_PolyPolyHash (keys and
   values are sp_RbVal; the entries keep their insertion order). A Hash
   from the runtime can be a typed one (sp_StrIntHash, ...): the methods
   that read take a poly copy, and the methods that change it stop as a
   gap.

   The Enumerable methods of a Hash are in iter.c, which yields [key,
   value] pairs. This file has the methods that are special to Hash, and
   the hooks that hash a user object as a key. */

#include "fluxpinel.h"

#define FX_IS(s) (!strcmp(name, (s)))

static sp_PolyPolyHash *fx_hash_mut(sp_RbVal r, const char *name) {
  if (r.cls_id != SP_BUILTIN_POLY_POLY_HASH) fx_gap(sp_sprintf("Hash#%s on a typed Hash", name));
  if (sp_gc_is_frozen(r.v.p)) sp_raise_frozen_hash_at(r.v.p, r.cls_id);
  return (sp_PolyPolyHash *)r.v.p;
}

static sp_RbVal fx_hash_box(sp_PolyPolyHash *h) {
  return sp_box_obj(h, SP_BUILTIN_POLY_POLY_HASH);
}

/* The default proc of Hash.new { |h, k| }: it calls the FxProc in self. */
static sp_RbVal fx_hash_dproc(sp_PolyPolyHash *h, sp_RbVal k, void *self) {
  sp_RbVal argv[2] = { fx_hash_box(h), k };
  SP_GC_ROOT_RBVAL(argv[0]);
  SP_GC_ROOT_RBVAL(argv[1]);
  return fx_yield(&((FxProc *)self)->b, 2, argv);
}

/* A copy of the Hash h with the same default. */
static sp_PolyPolyHash *fx_hash_dup(sp_PolyPolyHash *h) {
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
static int fx_hash_class(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                           sp_RbVal *out) {
  FxClass *k = fx_class_value(r);
  if (!k || strcmp(k->name, "Hash")) return 0;
  /* Hash[h], Hash[[[k, v], ...]] and Hash[k, v, ...]: the entries, with no
     default */
  if (FX_IS("[]") && !blk) {
    sp_PolyPolyHash *h = sp_PolyPolyHash_new();
    SP_GC_ROOT(h);
    if (argc == 1 && fx_is_hash(argv[0])) {
      const sp_PolyPolyHash *o = (const sp_PolyPolyHash *)argv[0].v.p;
      if (argv[0].cls_id != SP_BUILTIN_POLY_POLY_HASH) fx_gap("Hash[] of a typed Hash");
      for (sp_int i = 0; i < o->len; i++) sp_PolyPolyHash_set(h, o->keys[o->order[i]], o->vals[o->order[i]]);
    } else if (argc == 1 && fx_is_array(argv[0])) {
      sp_int n = sp_poly_length(argv[0]);
      for (sp_int i = 0; i < n; i++) {
        sp_RbVal pair = sp_poly_arr_get(argv[0], i);
        sp_int pn = fx_is_array(pair) ? sp_poly_length(pair) : -1;
        if (pn < 1 || pn > 2)
          sp_raise_cls("ArgumentError", sp_sprintf("invalid number of elements (%lld for 1..2)", (long long)(pn < 0 ? 0 : pn)));
        sp_PolyPolyHash_set(h, sp_poly_arr_get(pair, 0), pn == 2 ? sp_poly_arr_get(pair, 1) : sp_box_nil());
      }
    } else {
      if (argc % 2) sp_raise_cls("ArgumentError", "odd number of arguments for Hash");
      for (int i = 0; i < argc; i += 2) sp_PolyPolyHash_set(h, argv[i], argv[i + 1]);
    }
    *out = fx_hash_box(h);
    return 1;
  }
  if (!FX_IS("new")) return 0;
  /* Hash.new(capacity: n) sizes the table; the size is not visible */
  if (argc >= 1 && fx_is_hash(argv[argc - 1]) && argv[argc - 1].v.p == L.kwhash) argc--;
  if (argc > 1) return 0;
  sp_PolyPolyHash *h = sp_PolyPolyHash_new();
  SP_GC_ROOT(h);
  sp_gc_wb(h);
  if (argc == 1) h->default_v = argv[0];
  if (blk) {
    if (argc) sp_raise_cls("ArgumentError", "wrong number of arguments (given 1, expected 0)");
    FxBlock b = *blk;
    b.site = NULL;
    sp_RbVal p = fx_block_proc(&b);
    /* h can be old after the allocation of p */
    sp_gc_wb(h);
    h->dproc = fx_hash_dproc;
    h->dproc_self = p.v.p;
  }
  *out = fx_hash_box(h);
  return 1;
}

/* The methods of a Hash. Answers 1 and sets *out when it handles the call. */
static int fx_hash_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                          sp_RbVal *out) {
  if (r.tag == SP_TAG_CLASS) return fx_hash_class(r, name, argc, argv, blk, out);
  if (!fx_is_hash(r)) return 0;
  SP_GC_ROOT_RBVAL(r);
  sp_PolyPolyHash *h = sp_poly_as_poly_poly_hash(r);
  SP_GC_ROOT(h);
  sp_RbVal v = sp_box_nil(), y = sp_box_nil();
  SP_GC_ROOT_RBVAL(v);
  SP_GC_ROOT_RBVAL(y);
  if (argc == 1 && FX_IS("[]")) { *out = sp_PolyPolyHash_get(h, argv[0]); return 1; }
  if (argc == 2 && (FX_IS("[]=") || FX_IS("store"))) {
    sp_PolyPolyHash_set(fx_hash_mut(r, name), argv[0], argv[1]);
    *out = argv[1];
    return 1;
  }
  if (argc == 1 && (FX_IS("key?") || FX_IS("has_key?") || FX_IS("include?") || FX_IS("member?"))) {
    *out = sp_box_bool(sp_PolyPolyHash_has_key(h, argv[0]));
    return 1;
  }
  if (argc == 1 && (FX_IS("value?") || FX_IS("has_value?"))) {
    *out = sp_box_bool(sp_PolyPolyHash_has_value(h, argv[0]));
    return 1;
  }
  if ((argc == 1 || argc == 2) && FX_IS("fetch")) {
    if (sp_PolyPolyHash_has_key(h, argv[0])) { *out = sp_PolyPolyHash_get(h, argv[0]); return 1; }
    if (blk) { *out = fx_y1(blk, argv[0]); return 1; }
    if (argc == 2) { *out = argv[1]; return 1; }
    /* KeyError#receiver and #key: the runtime keeps them for the raise */
    sp_exc_stage_recv(r);
    sp_exc_stage_key(argv[0]);
    sp_raise_cls("KeyError", sp_sprintf("key not found: %s", sp_poly_inspect(argv[0])));
  }
  if (argc == 1 && FX_IS("delete")) {
    sp_PolyPolyHash *m = fx_hash_mut(r, name);
    if (sp_PolyPolyHash_has_key(m, argv[0])) {
      *out = sp_PolyPolyHash_get(m, argv[0]);
      sp_PolyPolyHash_delete(m, argv[0]);
    } else {
      *out = blk ? fx_y1(blk, argv[0]) : sp_box_nil();
    }
    return 1;
  }
  if (argc == 1 && FX_IS("key")) {
    for (sp_int i = 0; i < h->len; i++)
      if (sp_poly_truthy(sp_poly_eq_value(h->vals[h->order[i]], argv[0], 0))) {
        *out = h->keys[h->order[i]];
        return 1;
      }
    *out = sp_box_nil();
    return 1;
  }
  if (argc == 0 && !blk) {
    if (FX_IS("size") || FX_IS("length") || FX_IS("count")) { *out = sp_box_int(h->len); return 1; }
    if (FX_IS("empty?")) { *out = sp_box_bool(h->len == 0); return 1; }
    if (FX_IS("keys") || FX_IS("values")) {
      sp_PolyArray *a = sp_PolyArray_new();
      SP_GC_ROOT(a);
      for (sp_int i = 0; i < h->len; i++) sp_PolyArray_push(a, name[0] == 'k' ? h->keys[h->order[i]] : h->vals[h->order[i]]);
      *out = sp_box_poly_array(a);
      return 1;
    }
    if (FX_IS("to_a") || FX_IS("entries")) { *out = sp_box_poly_array(fx_seq_array(r)); return 1; }
    if (FX_IS("to_h") || FX_IS("to_hash")) { *out = r; return 1; }
    if (FX_IS("dup") || FX_IS("clone")) { *out = fx_hash_box(fx_hash_dup(h)); return 1; }
    if (FX_IS("default")) { *out = h->default_v; return 1; }
    if (FX_IS("invert")) {
      sp_PolyPolyHash *o = sp_PolyPolyHash_new();
      SP_GC_ROOT(o);
      for (sp_int i = 0; i < h->len; i++) sp_PolyPolyHash_set(o, h->vals[h->order[i]], h->keys[h->order[i]]);
      *out = fx_hash_box(o);
      return 1;
    }
    if (FX_IS("compact")) {
      /* a copy keeps the default and the default proc */
      sp_PolyPolyHash *o = fx_hash_dup(h);
      SP_GC_ROOT(o);
      for (sp_int i = 0; i < h->len; i++)
        if (h->vals[h->order[i]].tag == SP_TAG_NIL) sp_PolyPolyHash_delete(o, h->keys[h->order[i]]);
      *out = fx_hash_box(o);
      return 1;
    }
    if (FX_IS("first")) {
      *out = h->len ? fx_pair(h->keys[h->order[0]], h->vals[h->order[0]]) : sp_box_nil();
      return 1;
    }
    if (FX_IS("sort") || FX_IS("min") || FX_IS("max")) {
      sp_RbVal s = fx_sorted(fx_seq_array(r), NULL, NULL);
      sp_int n = sp_poly_length(s);
      *out = name[0] == 's' ? s : n == 0 ? sp_box_nil() : sp_poly_arr_get(s, name[1] == 'i' ? 0 : n - 1);
      return 1;
    }
    if (FX_IS("clear")) {
      sp_PolyPolyHash *m = fx_hash_mut(r, name);
      while (m->len) sp_PolyPolyHash_delete(m, m->keys[m->order[0]]);
      *out = r;
      return 1;
    }
  }
  if (argc == 1 && !blk && FX_IS("default=")) {
    sp_PolyPolyHash *m = fx_hash_mut(r, name);
    sp_gc_wb(m);
    m->default_v = argv[0];
    *out = argv[0];
    return 1;
  }
  if (argc == 1 && (FX_IS("==") || FX_IS("eql?"))) {
    *out = sp_poly_eq_value(r, argv[0], 0);
    return 1;
  }
  /* a <= b: every entry of a is in b with an == value */
  if (argc == 1 && (FX_IS("<=") || FX_IS("<") || FX_IS(">=") || FX_IS(">"))) {
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
  if (argc >= 1 && FX_IS("dig")) {
    v = sp_PolyPolyHash_get(h, argv[0]);
    *out = argc == 1 || v.tag == SP_TAG_NIL ? v : fx_send(v, "dig", argc - 1, argv + 1);
    return 1;
  }
  if (FX_IS("values_at")) {
    sp_PolyArray *a = sp_PolyArray_new();
    SP_GC_ROOT(a);
    for (int i = 0; i < argc; i++) sp_PolyArray_push(a, sp_PolyPolyHash_get(h, argv[i]));
    *out = sp_box_poly_array(a);
    return 1;
  }
  if (FX_IS("slice") || FX_IS("except")) {
    sp_PolyPolyHash *o = FX_IS("slice") ? sp_PolyPolyHash_new() : fx_hash_dup(h);
    SP_GC_ROOT(o);
    for (int i = 0; i < argc; i++) {
      if (name[0] == 'e') sp_PolyPolyHash_delete(o, argv[i]);
      else if (sp_PolyPolyHash_has_key(h, argv[i])) sp_PolyPolyHash_set(o, argv[i], sp_PolyPolyHash_get(h, argv[i]));
    }
    sp_gc_wb(o);
    o->dproc = NULL;
    o->dproc_self = NULL;
    o->default_v = sp_box_nil();
    *out = fx_hash_box(o);
    return 1;
  }
  /* merge and update: a block picks the value of a key in both */
  int bang = FX_IS("merge!") || FX_IS("update");
  if (bang || FX_IS("merge")) {
    sp_PolyPolyHash *o = bang ? fx_hash_mut(r, name) : fx_hash_dup(h);
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
          v = fx_yield(blk, 3, args);
          if (L.ctl != FX_CTL_NONE) { *out = sp_box_nil(); return 1; }
        }
        sp_PolyPolyHash_set(o, k, v);
      }
    }
    *out = bang ? r : fx_hash_box(o);
    return 1;
  }
  if (!blk) return 0;
  /* the methods with a block that answer a Hash */
  int sel = FX_IS("select") || FX_IS("filter"), rej = FX_IS("reject");
  int msel = FX_IS("select!") || FX_IS("filter!") || FX_IS("keep_if");
  int mrej = FX_IS("reject!") || FX_IS("delete_if");
  int tv = FX_IS("transform_values"), tk = FX_IS("transform_keys");
  int tvb = FX_IS("transform_values!"), tkb = FX_IS("transform_keys!");
  if (argc == 0 && (sel || rej || msel || mrej || tv || tk || tvb || tkb)) {
    sp_PolyPolyHash *o = sp_PolyPolyHash_new();
    SP_GC_ROOT(o);
    sp_int n = h->len;
    for (sp_int i = 0; i < h->len; i++) {
      sp_RbVal k = h->keys[h->order[i]];
      v = h->vals[h->order[i]];
      y = tv || tvb ? fx_y1(blk, v) : tk || tkb ? fx_y1(blk, k) : fx_y2(blk, k, v);
      if (L.ctl != FX_CTL_NONE) { *out = sp_box_nil(); return 1; }
      if (tv || tvb) sp_PolyPolyHash_set(o, k, y);
      else if (tk || tkb) sp_PolyPolyHash_set(o, y, v);
      else if (sp_poly_truthy(y) == (sel || msel)) sp_PolyPolyHash_set(o, k, v);
    }
    if (sel || rej || tv || tk) { *out = fx_hash_box(o); return 1; }
    sp_PolyPolyHash *m = fx_hash_mut(r, name);
    int changed = o->len != n || tvb || tkb;
    while (m->len) sp_PolyPolyHash_delete(m, m->keys[m->order[0]]);
    for (sp_int i = 0; i < o->len; i++) sp_PolyPolyHash_set(m, o->keys[o->order[i]], o->vals[o->order[i]]);
    int bangname = name[strlen(name) - 1] == '!' && !tvb && !tkb;
    *out = bangname && !changed ? sp_box_nil() : r;
    return 1;
  }
  if (argc == 0 && (FX_IS("each_key") || FX_IS("each_value"))) {
    for (sp_int i = 0; i < h->len; i++) {
      fx_y1(blk, name[5] == 'k' ? h->keys[h->order[i]] : h->vals[h->order[i]]);
      if (L.ctl != FX_CTL_NONE) { *out = sp_box_nil(); return 1; }
    }
    *out = r;
    return 1;
  }
  return 0;
}

/* tally and to_h with no block, on an Array. */
static int fx_array_hash(sp_RbVal r, const char *name, int argc, sp_RbVal *out) {
  if (!fx_is_array(r) || argc != 0) return 0;
  if (!FX_IS("tally") && !FX_IS("to_h")) return 0;
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
    if (!fx_is_array(e) || sp_poly_length(e) != 2)
      sp_raise_cls("TypeError", sp_sprintf("wrong element type %s at %lld (expected array)", sp_poly_class_name(e),
                                           (long long)i));
    sp_PolyPolyHash_set(h, sp_poly_arr_get(e, 0), sp_poly_arr_get(e, 1));
  }
  *out = fx_hash_box(h);
  return 1;
}

/* ---- user objects as keys ---- */

/* A user object hashes by its hash method, else by identity. */
static sp_int fx_hook_hash(int cls_id, void *p) {
  sp_RbVal obj = sp_box_obj(p, cls_id);
  if (!fx_is_fx_object(obj)) return (sp_int)(uintptr_t)p;
  SP_GC_ROOT_RBVAL(obj);
  FxMethod *m = fx_find_method(fx_lookup_class(obj), sp_sym_intern("hash"));
  int rec = !m && fx_record_of(fx_class_of(obj));   /* a Struct hashes by its members */
  if (!m && !rec) return (sp_int)(uintptr_t)p;
  sp_RbVal h = rec ? fx_send(obj, "hash", 0, NULL) : fx_invoke(m, obj, 0, NULL);
  if (h.tag == SP_TAG_INT) return h.v.i;
  return sp_rbval_hash_key(h);
}

/* A user object is eql? by its eql? method, else by identity. */
static sp_bool fx_hook_eql(int cls_id, void *a, void *b) {
  sp_RbVal x = sp_box_obj(a, cls_id), y = sp_box_obj(b, cls_id);
  if (a == b) return TRUE;
  if (!fx_is_fx_object(x)) return FALSE;
  SP_GC_ROOT_RBVAL(x);
  SP_GC_ROOT_RBVAL(y);
  FxMethod *m = fx_find_method(fx_lookup_class(x), sp_sym_intern("eql?"));
  if (!m && fx_record_of(fx_class_of(x))) return sp_poly_truthy(fx_send(x, "eql?", 1, &y)) ? TRUE : FALSE;
  if (!m) return FALSE;
  return sp_poly_truthy(fx_invoke(m, x, 1, &y)) ? TRUE : FALSE;
}

static void fx_hash_init(void) {
  SP_INSTALL_HOOK(sp_obj_hash_hook, fx_hook_hash);
  SP_INSTALL_HOOK(sp_obj_eql_hook, fx_hook_eql);
}

#undef FX_IS
