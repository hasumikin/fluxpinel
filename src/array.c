/* array.c -- the methods of Array that spinel emits from codegen code.

   spinel inlines most methods of Array at the call site, so the generated
   table (builtin.c) has no wrapper for them. On a receiver of unknown type,
   the AOT code calls the sp_poly_* helpers of the runtime, or takes the
   Array as a poly array, runs the poly form and writes the result back
   (sp_poly_arr_writeback). Fluxpinel does the same here.

   A mutator takes its receiver through sp_poly_array_recv, which raises
   FrozenError before any change. A poly array is its own work array; a
   typed one is a copy that the write-back puts back in its own form.

   The Enumerable methods that take a block are in iter.c. */

#include "fluxpinel.h"

#define FX_IS(s) (!strcmp(name, (s)))

/* The arguments as a new poly array. */
static sp_PolyArray *fx_ary_args(int argc, sp_RbVal *argv) {
  sp_PolyArray *a = sp_PolyArray_new();
  SP_GC_ROOT(a);
  for (int i = 0; i < argc; i++) sp_PolyArray_push(a, argv[i]);
  return a;
}

/* Array#transpose: CRuby's IndexError when the rows differ in length. */
static sp_RbVal fx_ary_transpose(sp_RbVal r) {
  sp_PolyArray *a = sp_poly_to_poly_array(r);
  SP_GC_ROOT(a);
  sp_PolyArray *out = sp_PolyArray_new();
  SP_GC_ROOT(out);
  sp_int m = -1;
  for (sp_int i = 0; i < a->len; i++) {
    sp_int n = sp_poly_set_operand(a->data[i])->len;
    if (m >= 0 && n != m)
      sp_raise_cls("IndexError", sp_sprintf("element size differs (%lld should be %lld)", (long long)n, (long long)m));
    m = n;
  }
  for (sp_int j = 0; j < m; j++) {
    sp_PolyArray *col = sp_PolyArray_new();
    sp_PolyArray_push(out, sp_box_poly_array(col));
    for (sp_int i = 0; i < a->len; i++) sp_PolyArray_push(col, sp_poly_arr_get(a->data[i], j));
  }
  return sp_box_poly_array(out);
}

/* Array#tally: a Hash from each element to the count of it. */
static sp_RbVal fx_ary_tally(sp_RbVal r) {
  sp_PolyArray *a = sp_poly_to_poly_array(r);
  SP_GC_ROOT(a);
  sp_PolyPolyHash *h = sp_PolyPolyHash_new();
  SP_GC_ROOT(h);
  for (sp_int i = 0; i < a->len; i++) {
    sp_RbVal e = a->data[i];
    sp_int n = sp_PolyPolyHash_has_key(h, e) ? sp_PolyPolyHash_get(h, e).v.i : 0;
    sp_PolyPolyHash_set(h, e, sp_box_int(n + 1));
  }
  return sp_box_obj(h, SP_BUILTIN_POLY_POLY_HASH);
}

/* union, intersection and difference of any number of Arrays, and | & -
   of one. Each operand converts as CRuby converts it, or raises TypeError. */
static sp_RbVal fx_ary_setop(sp_RbVal r, char op, int argc, sp_RbVal *argv) {
  sp_PolyArray *acc = sp_PolyArray_dup(sp_poly_to_poly_array(r));
  SP_GC_ROOT(acc);
  if (op == '|') acc = sp_PolyArray_uniq(acc);
  for (int i = 0; i < argc; i++) {
    sp_PolyArray *b = sp_poly_set_operand(argv[i]);
    acc = op == '|' ? sp_PolyArray_union(acc, b) : op == '&' ? sp_PolyArray_intersect(acc, b)
                                                           : sp_PolyArray_difference(acc, b);
  }
  return sp_box_poly_array(acc);
}

/* Array.new, Array.new(ary), Array.new(n), Array.new(n, v) and
   Array.new(n) { |i| }, as the AOT code makes them. A copy of ary ignores
   the block, as CRuby does. */
static int fx_array_class(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                            sp_RbVal *out) {
  FxClass *k = fx_class_value(r);
  if (!k || k->cls_id >= 0 || strcmp(k->name, "Array") || !FX_IS("new")) return 0;
  if (argc > 2) sp_raise_cls("ArgumentError", sp_sprintf("wrong number of arguments (given %d, expected 0..2)", argc));
  if (argc == 1 && fx_is_array(argv[0])) {
    *out = sp_box_poly_array(sp_PolyArray_dup(sp_poly_to_poly_array(argv[0])));
    return 1;
  }
  sp_PolyArray *a = sp_PolyArray_new();
  SP_GC_ROOT(a);
  *out = sp_box_poly_array(a);
  if (argc == 0) return 1;
  sp_int n = sp_poly_arg_int_chk(argv[0]);
  if (n < 0) sp_raise_cls("ArgumentError", "negative array size");
  for (sp_int i = 0; i < n; i++) {
    sp_RbVal v = argc == 2 ? argv[1] : sp_box_nil();
    if (blk) {
      sp_RbVal iv = sp_box_int(i);
      v = fx_yield(blk, 1, &iv);
      if (L.ctl != FX_CTL_NONE) { *out = sp_box_nil(); return 1; }
    }
    sp_PolyArray_push(a, v);
  }
  return 1;
}

/* The methods of Array with no block, and Array#fetch with one. Answers
   1 and sets *out when it handles the call. */
static int fx_array_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                           sp_RbVal *out) {
  if (r.tag == SP_TAG_CLASS) return fx_array_class(r, name, argc, argv, blk, out);
  if (!fx_is_array(r)) return 0;
  SP_GC_ROOT_RBVAL(r);
  if (blk) {
    /* fetch(i) { |i| }: the block answers for an index outside the Array */
    if (argc == 1 && FX_IS("fetch")) {
      sp_int n = sp_poly_length(r), i = sp_poly_arg_int_chk(argv[0]);
      if (i < 0) i += n;
      *out = i >= 0 && i < n ? sp_poly_arr_get(r, i) : fx_y1(blk, argv[0]);
      return 1;
    }
    return 0;
  }
  sp_PolyArray *w = NULL;
  SP_GC_ROOT(w);
  if (argc == 0) {
    if (FX_IS("empty?")) { *out = sp_box_bool(sp_poly_length(r) == 0); return 1; }
    if (FX_IS("count")) { *out = sp_box_int(sp_poly_length(r)); return 1; }
    if (FX_IS("to_ary")) { *out = r; return 1; }
    /* each and each_with_index with no block: an Enumerator */
    if (FX_IS("each")) { *out = sp_box_obj(sp_Enumerator_new_from(r), SP_BUILTIN_ENUMERATOR); return 1; }
    if (FX_IS("each_with_index")) { *out = sp_box_obj(sp_Enumerator_new_ewi(r, 0), SP_BUILTIN_ENUMERATOR); return 1; }
    if (FX_IS("entries")) { *out = sp_box_poly_array(sp_PolyArray_dup(sp_poly_to_poly_array(r))); return 1; }
    if (FX_IS("pop") || FX_IS("shift")) {
      sp_poly_array_recv(r, name, 1);
      *out = name[0] == 'p' ? sp_poly_pop(r) : sp_poly_shift(r);
      return 1;
    }
    if (FX_IS("sort")) { *out = sp_box_poly_array(sp_poly_sort(r)); return 1; }
    if (FX_IS("uniq")) { *out = sp_box_poly_array(sp_poly_uniq(r)); return 1; }
    if (FX_IS("flatten")) { *out = sp_box_poly_array(sp_poly_flatten(r)); return 1; }
    if (FX_IS("min")) { *out = sp_poly_min(r); return 1; }
    if (FX_IS("max")) { *out = sp_poly_max(r); return 1; }
    if (FX_IS("minmax")) {
      sp_PolyArray *p = sp_PolyArray_new();
      SP_GC_ROOT(p);
      sp_PolyArray_push(p, sp_poly_min(r));
      sp_PolyArray_push(p, sp_poly_max(r));
      *out = sp_box_poly_array(p);
      return 1;
    }
    if (FX_IS("transpose")) { *out = fx_ary_transpose(r); return 1; }
    if (FX_IS("tally")) { *out = fx_ary_tally(r); return 1; }
    if (FX_IS("sort!")) {
      w = sp_poly_array_recv(r, name, 1);
      sp_PolyArray_sort_bang(w);
      sp_poly_arr_writeback(r, w);
      *out = r;
      return 1;
    }
    /* the bang forms answer nil when nothing changes */
    if (FX_IS("compact!") || FX_IS("flatten!")) {
      w = sp_poly_array_recv(r, name, 1);
      sp_RbVal c = name[0] == 'c' ? sp_PolyArray_compact_bangq(w) : sp_PolyArray_flatten_bangq(w);
      sp_poly_arr_writeback(r, w);
      *out = c.tag == SP_TAG_NIL ? c : r;
      return 1;
    }
  }
  if (argc == 1) {
    sp_RbVal a = argv[0];
    /* an object of the program converts by its to_int */
    if ((FX_IS("[]") || FX_IS("slice")) && fx_is_fx_object(a)) {
      *out = sp_poly_arr_get(r, sp_poly_arg_int_chk(a));
      return 1;
    }
    if (FX_IS("[]") || FX_IS("slice")) { *out = sp_poly_index_poly(r, a); return 1; }
    if (FX_IS("at")) { *out = sp_poly_arr_get(r, sp_poly_arg_int_chk(a)); return 1; }
    if (FX_IS("member?")) { *out = sp_box_bool(sp_PolyArray_include(sp_poly_to_poly_array(r), a)); return 1; }
    if (FX_IS("index") || FX_IS("find_index") || FX_IS("rindex")) {
      *out = sp_box_oint(sp_poly_arr_index_val(r, a, name[0] == 'r'));
      return 1;
    }
    if (FX_IS("count")) { *out = sp_box_int(sp_poly_count_val(r, a)); return 1; }
    if (FX_IS("pop") || FX_IS("shift")) {
      sp_poly_array_recv(r, name, 1);
      *out = sp_poly_pop_any(r, a, name[0] == 's');
      return 1;
    }
    if (FX_IS("flatten")) { *out = sp_box_poly_array(sp_poly_flatten_d(r, sp_poly_arg_int_chk(a), 1)); return 1; }
    if (FX_IS("min")) { *out = sp_poly_arr_min_n(r, sp_poly_arg_int_chk(a)); return 1; }
    if (FX_IS("max")) { *out = sp_poly_arr_max_n(r, sp_poly_arg_int_chk(a)); return 1; }
    if (FX_IS("sample")) { *out = sp_poly_arr_sample_n(r, sp_poly_arg_int_chk(a)); return 1; }
    if (FX_IS("sum")) { *out = sp_poly_sum_seed(r, a); return 1; }
    if (FX_IS("replace")) { *out = sp_poly_replace_any(r, a); return 1; }
    if (FX_IS("intersect?")) {
      sp_PolyArray *b = sp_poly_set_operand(a);
      SP_GC_ROOT(b);
      *out = sp_box_bool(sp_PolyArray_intersect_p(sp_poly_to_poly_array(r), b));
      return 1;
    }
    if (FX_IS("|") || FX_IS("&")) { *out = fx_ary_setop(r, name[0], 1, argv); return 1; }
    if (FX_IS("assoc") || FX_IS("rassoc")) {
      sp_PolyArray *p = sp_poly_to_poly_array(r);
      p = name[0] == 'a' ? sp_PolyArray_assoc(p, a) : sp_PolyArray_rassoc(p, a);
      *out = p ? sp_box_poly_array(p) : sp_box_nil();
      return 1;
    }
    if (FX_IS("delete")) {
      w = sp_poly_array_recv(r, name, 1);
      *out = sp_PolyArray_delete(w, a);
      sp_poly_arr_writeback(r, w);
      return 1;
    }
    if (FX_IS("flatten!")) {
      w = sp_poly_array_recv(r, name, 1);
      sp_RbVal c = sp_PolyArray_flatten_bangq_depth(w, sp_poly_arg_int_chk(a));
      sp_poly_arr_writeback(r, w);
      *out = c.tag == SP_TAG_NIL ? c : r;
      return 1;
    }
  }
  if ((argc == 1 || argc == 2) && FX_IS("fetch")) {
    *out = sp_poly_fetch(r, argv[0], argc == 2, argc == 2 ? argv[1] : sp_box_nil());
    return 1;
  }
  /* a[range] = v and a[start, len] = v: v converts by its to_ary, and the
     call answers v as it was */
  if (argc == 2 && FX_IS("[]=") && fx_is_range(argv[0])) {
    sp_poly_array_recv(r, name, 1);
    sp_poly_splice_range(r, *(sp_Range *)argv[0].v.p, sp_poly_ary_operand(argv[1]));
    *out = argv[1];
    return 1;
  }
  if (argc == 3 && FX_IS("[]=")) {
    sp_poly_array_recv(r, name, 1);
    sp_poly_splice(r, sp_poly_arg_int_chk(argv[0]), sp_poly_arg_int_chk(argv[1]), sp_poly_ary_operand(argv[2]));
    *out = argv[2];
    return 1;
  }
  /* fill(v), fill(v, start) and fill(v, start, len) with Integer offsets */
  if (argc >= 1 && argc <= 3 && FX_IS("fill") && (argc < 2 || argv[1].tag == SP_TAG_INT) &&
      (argc < 3 || argv[2].tag == SP_TAG_INT)) {
    *out = sp_poly_fill(r, argv[0], argc, argc >= 2 ? argv[1].v.i : 0, argc == 3 ? argv[2].v.i : 0);
    return 1;
  }
  if (FX_IS("push") || FX_IS("append")) {
    w = sp_poly_array_recv(r, name, 1);
    for (int i = 0; i < argc; i++) sp_PolyArray_push(w, argv[i]);
    sp_poly_arr_writeback(r, w);
    *out = r;
    return 1;
  }
  if (FX_IS("unshift") || FX_IS("prepend")) {
    sp_poly_array_recv(r, name, 1);
    *out = argc ? sp_poly_insert_n(r, 0, fx_ary_args(argc, argv)) : r;
    return 1;
  }
  if (argc >= 1 && FX_IS("insert")) {
    sp_poly_array_recv(r, name, 1);
    *out = sp_poly_insert_n(r, sp_poly_arg_int_chk(argv[0]), fx_ary_args(argc - 1, argv + 1));
    return 1;
  }
  /* concat(*arrays): each operand is read before any append, as a.concat(a) */
  if (FX_IS("concat")) {
    w = sp_poly_array_recv(r, name, 1);
    sp_PolyArray *ops = sp_PolyArray_new();
    SP_GC_ROOT(ops);
    for (int i = 0; i < argc; i++) sp_PolyArray_push(ops, sp_box_poly_array(sp_PolyArray_dup(sp_poly_set_operand(argv[i]))));
    for (sp_int i = 0; i < ops->len; i++) sp_PolyArray_append_all(w, (sp_PolyArray *)ops->data[i].v.p);
    sp_poly_arr_writeback(r, w);
    *out = r;
    return 1;
  }
  if (FX_IS("union")) { *out = fx_ary_setop(r, '|', argc, argv); return 1; }
  if (FX_IS("intersection")) { *out = fx_ary_setop(r, '&', argc, argv); return 1; }
  if (FX_IS("difference")) { *out = fx_ary_setop(r, '-', argc, argv); return 1; }
  if (argc >= 1 && FX_IS("dig")) { *out = sp_poly_dig_n(r, argc, argv); return 1; }
  if (FX_IS("values_at")) { *out = sp_poly_values_at_n(r, argc, argv); return 1; }
  /* zip(*others): a Range or an Enumerator operand gives its first
     members, as many as the receiver has (it can be endless) */
  if (FX_IS("zip")) {
    sp_PolyArray *ops = fx_ary_args(argc, argv);
    SP_GC_ROOT(ops);
    sp_int n = sp_poly_length(r);
    for (sp_int i = 0; i < ops->len; i++) {
      sp_RbVal o = ops->data[i];
      if (!fx_is_range(o) && !(o.tag == SP_TAG_OBJ && o.cls_id == SP_BUILTIN_ENUMERATOR && o.v.p)) continue;
      w = sp_PolyArray_new();
      FxSeq s;
      sp_RbVal v;
      fx_seq_open(ops->data[i], &s);
      FX_SEQ_ROOT(s);
      while (w->len < n && fx_seq_next(&s, &v)) sp_PolyArray_push(w, v);
      sp_PolyArray_set(ops, i, sp_box_poly_array(w));
    }
    *out = sp_box_poly_array(sp_poly_zip_n(r, ops));
    return 1;
  }
  if (FX_IS("product")) { *out = sp_box_poly_array(sp_poly_product_n(r, fx_ary_args(argc, argv))); return 1; }
  return 0;
}

/* ---- the methods of a user object that the helpers above call ---- */

/* The method name of the class of a user object, else NULL. v.v.p can be
   NULL: the runtime asks whether the class has the method. */
static FxMethod *fx_hook_method(sp_RbVal v, const char *name) {
  if (v.tag != SP_TAG_OBJ || v.cls_id < FX_CLS_BASE) return NULL;
  FxClass *k = fx_class_by_id(v.cls_id);
  return k ? fx_find_method(k, sp_sym_intern(name)) : NULL;
}

/* a <=> b, for sort, min, max and their kin with no block. With no <=> in
   the program, the default of Object: 0 for the same object, else nil. */
static sp_int fx_hook_cmp(sp_RbVal a, sp_RbVal b, sp_bool *comparable) {
  if (a.tag == SP_TAG_OBJ && fx_kernel_cls(a.cls_id) && fx_kernel_hooks.cmp)
    return fx_kernel_hooks.cmp(a, b, comparable);
  FxMethod *m = fx_hook_method(a, "<=>");
  *comparable = !m && a.tag == b.tag && a.v.p == b.v.p;
  /* Object#<=> is 0 also for an == of the program that answers true */
  FxMethod *eq = m || *comparable || !a.v.p ? NULL : fx_hook_method(a, "==");
  if (eq) {
    SP_GC_ROOT_RBVAL(a);
    SP_GC_ROOT_RBVAL(b);
    *comparable = sp_poly_truthy(fx_invoke(eq, a, 1, &b));
  }
  if (!m || !a.v.p) return 0;
  SP_GC_ROOT_RBVAL(a);
  SP_GC_ROOT_RBVAL(b);
  sp_RbVal c = fx_invoke(m, a, 1, &b);
  *comparable = c.tag == SP_TAG_INT;
  return *comparable ? (c.v.i > 0) - (c.v.i < 0) : 0;
}

/* to_ary and to_a: the Array, or nil for a class with no such method. To
   the question with no object, to_ary answers true for a class with it. */
static sp_RbVal fx_hook_to_ary(sp_RbVal v) {
  if (v.tag == SP_TAG_OBJ && fx_kernel_cls(v.cls_id) && fx_kernel_hooks.to_ary)
    return fx_kernel_hooks.to_ary(v);
  FxMethod *m = fx_hook_method(v, "to_ary");
  if (!m) return sp_box_nil();
  if (!v.v.p) return sp_box_bool(1);
  SP_GC_ROOT_RBVAL(v);
  return fx_invoke(m, v, 0, NULL);
}

static sp_RbVal fx_hook_to_a(sp_RbVal v) {
  if (v.tag == SP_TAG_OBJ && fx_kernel_cls(v.cls_id) && fx_kernel_hooks.to_a)
    return fx_kernel_hooks.to_a(v);
  FxMethod *m = fx_hook_method(v, "to_a");
  if (!m || !v.v.p) return sp_box_nil();
  SP_GC_ROOT_RBVAL(v);
  return fx_invoke(m, v, 0, NULL);
}

/* to_int and to_str, for an implicit conversion of an argument */
static sp_int fx_hook_to_int(int cls_id, void *p, int *ok) {
  if (fx_kernel_cls(cls_id) && fx_kernel_hooks.to_int) return fx_kernel_hooks.to_int(cls_id, p, ok);
  sp_RbVal v = sp_box_obj(p, cls_id);
  FxMethod *m = fx_hook_method(v, "to_int");
  *ok = 0;
  if (!m) return 0;
  SP_GC_ROOT_RBVAL(v);
  sp_RbVal i = fx_invoke(m, v, 0, NULL);
  *ok = i.tag == SP_TAG_INT;
  return *ok ? i.v.i : 0;
}

static const char *fx_hook_to_str(int cls_id, void *p) {
  if (fx_kernel_cls(cls_id) && fx_kernel_hooks.to_str) return fx_kernel_hooks.to_str(cls_id, p);
  sp_RbVal v = sp_box_obj(p, cls_id);
  FxMethod *m = fx_hook_method(v, "to_str");
  if (!m) return NULL;
  SP_GC_ROOT_RBVAL(v);
  sp_RbVal s = fx_invoke(m, v, 0, NULL);
  return fx_is_string(s) ? sp_poly_recv_s(s, "to_str") : NULL;
}

/* to_path, for a path argument (sp_poly_arg_path): NULL for a class with
   no to_path, which then takes to_str. */
static const char *fx_hook_to_path(int cls_id, void *p) {
  if (fx_kernel_cls(cls_id) && fx_kernel_hooks.to_path) return fx_kernel_hooks.to_path(cls_id, p);
  sp_RbVal v = sp_box_obj(p, cls_id);
  FxMethod *m = fx_hook_method(v, "to_path");
  if (!m) return NULL;
  SP_GC_ROOT_RBVAL(v);
  sp_RbVal s = fx_invoke(m, v, 0, NULL);
  if (!fx_is_string(s)) {
    const char *cn = sp_poly_class_name(v);
    sp_raise_cls("TypeError", sp_sprintf("can't convert %s to String (%s#to_path gives %s)", cn, cn,
                                         sp_poly_class_name(s)));
  }
  return sp_poly_recv_s(s, "to_path");
}

static void fx_array_init(void) {
  sp_obj_cmp_hook = fx_hook_cmp;
  sp_obj_to_ary_fn = fx_hook_to_ary;
  sp_obj_to_a_fn = fx_hook_to_a;
  sp_obj_to_int_fn = fx_hook_to_int;
  sp_obj_to_str_fn = fx_hook_to_str;
  sp_obj_to_path_fn = fx_hook_to_path;
}

#undef FX_IS
