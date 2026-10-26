/* re.c -- Regexp and MatchData in the walker.

   A Regexp is a compiled pattern of the runtime (mrb_regexp_pattern),
   boxed with SP_BUILTIN_REGEX. The runtime allocates a pattern with malloc
   and never frees it, as in the AOT code. A literal compiles when the
   walker first evaluates it, and answers the same pattern after that.

   The match registers ($~, $1, ...) are the registers of the runtime. A
   method whose body can match saves the registers of its caller on entry
   and puts them back on exit (sp_re_frame), as spinel codegen does.

   spinel emits the String methods that take a Regexp, and the forms with
   a block or a Hash, from codegen code. Fluxpinel writes them here on the
   runtime helpers. A MatchData answers most methods through the generated
   wrappers. */

#include "fluxpinel.h"

#define FX_IS(s) (!strcmp(name, (s)))
#define FX_STOP() do { if (L.ctl != FX_CTL_NONE) { *out = sp_box_nil(); return 1; } } while (0)

static int fx_is_regexp(sp_RbVal v) {
  return v.tag == SP_TAG_OBJ && v.cls_id == SP_BUILTIN_REGEX && v.v.p;
}

static int fx_is_md(sp_RbVal v) {
  return v.tag == SP_TAG_OBJ && v.cls_id == SP_BUILTIN_MATCHDATA && v.v.p;
}

static sp_RbVal fx_re_box(mrb_regexp_pattern *p) {
  return sp_box_obj(p, SP_BUILTIN_REGEX);
}

/* A runtime string as a new String value, or nil for NULL. */
static sp_RbVal fx_re_str(const char *s) {
  SP_GC_ROOT_STR(s);
  return s ? fx_new_string((const uint8_t *)s, sp_str_byte_len(s)) : sp_box_nil();
}

static sp_RbVal fx_re_md(sp_MatchData *m) {
  return m ? sp_box_obj(m, SP_BUILTIN_MATCHDATA) : sp_box_nil();
}

/* sp_re_matchdata sets the captures but not the positions that $~ reads,
   so a MatchData that a match answers sets the registers again. */
static sp_MatchData *fx_re_md_set(sp_MatchData *m) {
  SP_GC_ROOT(m);
  if (m) sp_re_set_last_match(m->pat, m->source, m->caps, m->ncap * 2);
  return m;
}

/* A copy of the bytes of a String. $~ keeps the subject, so a match reads
   a copy that a later change to the receiver (slice!, a block) cannot reach. */
static const char *fx_re_subject(sp_RbVal r, const char *name) {
  const char *s = sp_poly_recv_s(r, name);
  size_t n = sp_str_byte_len(s);
  char *d = sp_str_alloc(n);
  memcpy(d, s, n);
  d[n] = 0;
  return sp_str_bin_like(s, d);   /* a binary String matches by bytes */
}

/* ---- literals ---- */

/* The patterns of the literals, by node, in a table with open addressing. */
typedef struct {
  const pm_node_t *node;
  mrb_regexp_pattern *pat;
} FxReLit;

static FxReLit *fx_re_lits;
static size_t fx_re_cap, fx_re_n;

static FxReLit *fx_re_find(FxReLit *t, size_t cap, const pm_node_t *n) {
  size_t i = ((uintptr_t)n >> 3) & (cap - 1);
  while (t[i].node && t[i].node != n) i = (i + 1) & (cap - 1);
  return &t[i];
}

static void fx_re_cache(const pm_node_t *n, mrb_regexp_pattern *p) {
  if (2 * (fx_re_n + 1) > fx_re_cap) {
    size_t cap = fx_re_cap ? fx_re_cap * 2 : 64;
    FxReLit *t = calloc(cap, sizeof *t);
    for (size_t i = 0; i < fx_re_cap; i++)
      if (fx_re_lits[i].node) *fx_re_find(t, cap, fx_re_lits[i].node) = fx_re_lits[i];
    free(fx_re_lits);
    fx_re_lits = t;
    fx_re_cap = cap;
  }
  FxReLit *e = fx_re_find(fx_re_lits, fx_re_cap, n);
  e->node = n;
  e->pat = p;
  fx_re_n++;
}

/* The engine flags of a literal, as re_engine_flags of codegen maps them. */
static uint32_t fx_re_flags(const pm_node_t *n) {
  uint32_t f = 0;
  if (PM_NODE_FLAG_P(n, PM_REGULAR_EXPRESSION_FLAGS_IGNORE_CASE)) f |= 1;
  if (PM_NODE_FLAG_P(n, PM_REGULAR_EXPRESSION_FLAGS_EXTENDED)) f |= 8;
  if (PM_NODE_FLAG_P(n, PM_REGULAR_EXPRESSION_FLAGS_MULTI_LINE)) f |= 6;
  return f;
}

/* /re/ and /#{x}/. An interpolated literal compiles at each evaluation,
   but once with /o. */
static sp_RbVal fx_re_literal(FxFrame *f, const pm_node_t *n) {
  if (fx_re_cap) {
    mrb_regexp_pattern *p = fx_re_find(fx_re_lits, fx_re_cap, n)->pat;
    if (p) return fx_re_box(p);
  }
  mrb_regexp_pattern *p;
  if (PM_NODE_TYPE_P(n, PM_REGULAR_EXPRESSION_NODE)) {
    const pm_string_t *s = &((const pm_regular_expression_node_t *)n)->unescaped;
    p = re_compile((const char *)pm_string_source(s), (int64_t)pm_string_length(s), fx_re_flags(n));
    fx_re_cache(n, p);
    return fx_re_box(p);
  }
  sp_RbVal src = fx_interpolate(f, &((const pm_interpolated_regular_expression_node_t *)n)->parts);
  if (L.ctl != FX_CTL_NONE) return sp_box_nil();
  SP_GC_ROOT_RBVAL(src);
  const char *s = sp_poly_recv_s(src, "Regexp");
  p = re_compile(s, (int64_t)sp_str_byte_len(s), fx_re_flags(n));
  if (PM_NODE_FLAG_P(n, PM_REGULAR_EXPRESSION_FLAGS_ONCE)) fx_re_cache(n, p);
  return fx_re_box(p);
}

/* ---- the match registers ---- */

static sp_RbVal fx_re_last(void) {
  return fx_re_md(sp_re_last_matchdata());
}

/* $1, $2, ... */
static sp_RbVal fx_re_nth(int n) {
  if (n <= 0) return sp_box_nil();
  if (n < 10) return fx_re_str(sp_re_captures[n]);
  sp_MatchData *m = sp_re_last_matchdata();
  SP_GC_ROOT(m);
  return m ? fx_re_str(sp_MatchData_aref(m, n)) : sp_box_nil();
}

/* $&, $`, $' and $+. */
static sp_RbVal fx_re_backref(const char *g) {
  switch (g[1]) {
  case '&': return fx_re_str(sp_re_match_str);
  case '`': return fx_re_str(sp_re_pre_match());
  case '\'': return fx_re_str(sp_re_post_match());
  case '+': return fx_re_str(sp_re_last_paren_match());
  default: return sp_box_nil();
  }
}

/* /(?<name>..)/ =~ s: a match sets the local of each group, a miss sets
   them to nil. */
static sp_RbVal fx_re_match_write(FxFrame *f, const pm_match_write_node_t *w) {
  sp_RbVal v = fx_call(f, w->call);
  if (L.ctl != FX_CTL_NONE) return sp_box_nil();
  SP_GC_ROOT_RBVAL(v);
  for (size_t i = 0; i < w->targets.size; i++) {
    const pm_local_variable_target_node_t *t = (const pm_local_variable_target_node_t *)w->targets.nodes[i];
    sp_RbVal g = sp_box_nil();
    if (v.tag != SP_TAG_NIL) g = fx_re_str(sp_re_named_capture(sp_re_last_pat, sp_sym_to_s(fx_sym(t->name))));
    *fx_local(f, (const pm_node_t *)t, t->name, t->depth) = g;
  }
  return v;
}

/* Whether a method body can set the registers: a Regexp literal, or a call
   of a method of the match family. A nested def has its own frame. */
static bool fx_re_visit(const pm_node_t *n, void *data) {
  static const char *const names[] = { "=~", "!~", "match", "match?", "scan", "gsub", "gsub!", "sub", "sub!",
                                       "grep", "grep_v", NULL };
  int *hit = data;
  if (*hit || PM_NODE_TYPE_P(n, PM_DEF_NODE)) return false;
  switch (PM_NODE_TYPE(n)) {
  case PM_REGULAR_EXPRESSION_NODE:
  case PM_INTERPOLATED_REGULAR_EXPRESSION_NODE:
  case PM_MATCH_WRITE_NODE:
    *hit = 1;
    return false;
  case PM_CALL_NODE: {
    const char *s = sp_sym_to_s(fx_sym(((const pm_call_node_t *)n)->name));
    for (int i = 0; names[i]; i++)
      if (!strcmp(s, names[i])) { *hit = 1; return false; }
    return true;
  }
  default:
    return true;
  }
}

/* Saves the registers into rf when the method m can match. Answers whether
   it did; the caller then calls sp_re_frame_pop. */
static int fx_re_frame_enter(const FxMethod *m, sp_re_frame *rf) {
  FxMethod *w = (FxMethod *)m;
  if (!w->matches) {
    int hit = 0;
    if (m->def->body) pm_visit_child_nodes(m->def->body, fx_re_visit, &hit);
    w->matches = hit ? 2 : 1;
  }
  if (w->matches != 2) return 0;
  sp_re_frame_push(rf);
  return 1;
}

/* ---- Regexp ---- */

static int fx_is_kwhash(sp_RbVal v) {
  return v.tag == SP_TAG_OBJ && v.v.p == L.kwhash && v.v.p;
}

/* Regexp.new, .compile, .escape, .quote, .union and .last_match. */
static int fx_re_class(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  FxClass *k = fx_class_value(r);
  if (!k || strcmp(k->name, "Regexp")) return 0;
  if ((FX_IS("new") || FX_IS("compile")) && argc >= 1) {
    if (fx_is_kwhash(argv[argc - 1])) argc--;   /* timeout: has no effect */
    if (argc < 1 || argc > 2) return 0;
    sp_RbVal p = argv[0];
    /* a Regexp keeps its source and options, and the second argument does not count */
    if (fx_is_regexp(p)) {
      *out = fx_re_box(re_compile(sp_re_source(p.v.p), (int64_t)sp_re_source_len(p.v.p), sp_re_raw_flags(p.v.p)));
      return 1;
    }
    const char *s = sp_poly_arg_str_chk(p);
    SP_GC_ROOT_STR(s);
    uint32_t fl = argc == 2 ? sp_re_opts_from_poly(argv[1]) : 0;
    *out = fx_re_box(re_compile(s, (int64_t)(s ? sp_str_byte_len(s) : 0), fl));
    return 1;
  }
  if ((FX_IS("escape") || FX_IS("quote")) && argc == 1) {
    sp_RbVal a = argv[0];
    if (a.tag == SP_TAG_SYM) a = fx_re_str(sp_sym_to_s((sp_sym)a.v.i));
    SP_GC_ROOT_RBVAL(a);
    *out = fx_re_str(sp_re_escape(sp_poly_arg_str_chk(a)));
    return 1;
  }
  if (FX_IS("union")) {
    /* union(a, b, ...) or union([a, b, ...]): a String joins escaped, a
       Regexp by its (?on-off:src) form. A Symbol alone joins by its name. */
    sp_PolyArray *a = sp_PolyArray_new();
    SP_GC_ROOT(a);
    if (argc == 1 && fx_is_array(argv[0])) {
      for (sp_int i = 0; i < sp_poly_length(argv[0]); i++) sp_PolyArray_push(a, sp_poly_arr_get(argv[0], i));
    } else {
      for (int i = 0; i < argc; i++) sp_PolyArray_push(a, argv[i]);
    }
    if (a->len == 1 && a->data[0].tag == SP_TAG_SYM)
      sp_PolyArray_set(a, 0, fx_re_str(sp_sym_to_s((sp_sym)a->data[0].v.i)));
    for (sp_int i = 0; i < a->len; i++) {
      sp_RbVal v = a->data[i];
      if (fx_is_string(v) || fx_is_regexp(v)) continue;
      sp_raise_cls("TypeError", sp_sprintf("no implicit conversion of %s into String",
                                           v.tag == SP_TAG_NIL ? "nil" : sp_poly_class_name(v)));
    }
    *out = fx_re_box(sp_re_union_array(a));
    return 1;
  }
  if (FX_IS("last_match") && argc <= 1) {
    sp_RbVal m = fx_re_last();
    SP_GC_ROOT_RBVAL(m);
    *out = argc == 0 || m.tag == SP_TAG_NIL ? m : sp_poly_md_group((sp_MatchData *)m.v.p, argv[0]);
    return 1;
  }
  return 0;
}

/* match, match? and =~ of a String or a Regexp. The Regexp side is the
   pattern, and the other side is the subject. */
static int fx_re_matcher(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                           sp_RbVal *out) {
  int test = FX_IS("match?");
  if (!(FX_IS("=~") && argc == 1) && !((FX_IS("match") || test) && (argc == 1 || argc == 2))) return 0;
  sp_RbVal a = argv[0];
  SP_GC_ROOT_RBVAL(a);
  if (name[0] == '=' && fx_is_string(r) && !fx_is_regexp(a)) {
    /* str =~ obj calls obj =~ str */
    if (fx_is_string(a)) sp_raise_cls("TypeError", "wrong argument type String (expected Regexp)");
    *out = a.tag == SP_TAG_NIL ? sp_box_nil() : fx_send(a, "=~", 1, &r);
    return 1;
  }
  /* a Regexp does not match nil, and the miss clears $~ */
  if (fx_is_regexp(r) && a.tag == SP_TAG_NIL) {
    if (!test) sp_re_clear_last_match();
    *out = test ? sp_box_bool(0) : sp_box_nil();
    return 1;
  }
  sp_poly_match_check(r, name, 1, &a);
  if (argc == 2 && argv[1].tag != SP_TAG_INT) fx_gap(sp_sprintf("%s with a position that is not an Integer", name));
  /* the subject that $~ keeps is a copy of a String that can change */
  SP_GC_ROOT_RBVAL(r);
  if (!test && r.tag == SP_TAG_OBJ && r.cls_id == SP_BUILTIN_STRBUF) r = sp_box_str(fx_re_subject(r, name));
  if (!test && a.tag == SP_TAG_OBJ && a.cls_id == SP_BUILTIN_STRBUF) a = sp_box_str(fx_re_subject(a, name));
  if (name[0] == '=') {
    sp_oint i = sp_poly_match_index(r, a);
    *out = sp_box_oint(i);
    return 1;
  }
  if (test) {
    *out = sp_box_bool(argc == 2 ? sp_poly_match_p_at(r, a, argv[1].v.i) : sp_poly_match_p(r, a));
    return 1;
  }
  *out = fx_re_md(fx_re_md_set(argc == 2 ? sp_poly_match_data_at(r, a, argv[1].v.i) : sp_poly_match_data(r, a)));
  if (blk && out->tag != SP_TAG_NIL) *out = fx_y1(blk, *out);
  return 1;
}

static int fx_re_regexp(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                          sp_RbVal *out) {
  if (fx_re_matcher(r, name, argc, argv, blk, out)) return 1;
  if (FX_IS("===") && argc == 1) {
    *out = sp_box_bool(sp_re_case_eq((mrb_regexp_pattern *)r.v.p, argv[0]));
    return 1;
  }
  /* two patterns with the same source and options are equal */
  if ((FX_IS("==") || FX_IS("eql?") || FX_IS("!=")) && argc == 1) {
    int eq = fx_is_regexp(argv[0]) && sp_re_eq(r.v.p, argv[0].v.p);
    *out = sp_box_bool(name[0] == '!' ? !eq : eq);
    return 1;
  }
  /* the hash covers the options, as eql? does */
  if (FX_IS("hash") && argc == 0) { *out = sp_box_int(sp_re_hash(r.v.p)); return 1; }
  return 0;
}

/* ---- String ---- */

/* The answer of sub, gsub and their bang forms: a bang form changes the
   receiver, or answers nil when nothing matched. */
static int fx_re_subst_done(sp_RbVal r, int bang, int matched, const char *res, sp_RbVal *out) {
  SP_GC_ROOT_STR(res);
  if (!bang) { *out = fx_re_str(res); return 1; }
  if (!matched) { *out = sp_box_nil(); return 1; }
  if (r.tag != SP_TAG_OBJ) fx_gap("a bang method on a String that cannot change");
  sp_String_set_bin((sp_String *)r.v.p, res);
  *out = r;
  return 1;
}

/* The text that a block or a Hash gives for a match. */
static const char *fx_re_text(sp_RbVal v) {
  if (fx_is_string(v)) return sp_poly_recv_s(v, "to_s");
  v = fx_send(v, "to_s", 0, NULL);
  return fx_is_string(v) ? sp_poly_recv_s(v, "to_s") : sp_poly_to_s(v);
}

/* sub, gsub, sub! and gsub! with a block or a Hash. Before each block call,
   $~ is the match. */
static int fx_re_subst_each(sp_RbVal r, const char *name, sp_RbVal pv, sp_RbVal hash, const FxBlock *blk,
                              sp_RbVal *out) {
  int once = name[0] == 's', bang = name[strlen(name) - 1] == '!';
  SP_GC_ROOT_RBVAL(r);
  SP_GC_ROOT_RBVAL(pv);
  SP_GC_ROOT_RBVAL(hash);
  mrb_regexp_pattern *pat;
  if (fx_is_regexp(pv)) {
    pat = (mrb_regexp_pattern *)pv.v.p;
  } else {
    const char *ps = sp_re_escape(sp_poly_arg_str_chk(pv));
    pat = re_compile(ps, (int64_t)sp_str_byte_len(ps), 0);
  }
  const char *s = fx_re_subject(r, name);
  SP_GC_ROOT_STR(s);
  sp_String *acc = sp_String_new("");
  SP_GC_ROOT(acc);
  sp_RbVal m = sp_box_nil(), v = sp_box_nil();
  SP_GC_ROOT_RBVAL(m);
  SP_GC_ROOT_RBVAL(v);
  int64_t slen = (int64_t)sp_str_byte_len(s), pos = 0;
  int caps[64], matched = 0;
  sp_re_clear_last_match();
  while (pos <= slen) {
    int n = spinel_re_exec(pat, s, slen, pos, caps, 64, sp_str_is_binary(s));
    if (n <= 0 || caps[0] < 0) break;
    matched = 1;
    int b = caps[0], e = caps[1];
    sp_String_append_n(acc, s + pos, (size_t)(b - pos));
    sp_re_set_last_match(pat, s, caps, n);
    m = fx_new_string((const uint8_t *)s + b, (size_t)(e - b));
    if (blk) v = fx_y1(blk, m);
    else v = fx_send(hash, "[]", 1, &m);
    FX_STOP();
    const char *t = fx_re_text(v);
    sp_String_append_n(acc, t, sp_str_byte_len(t));
    pos = e;
    if (b == e) {
      /* a match of no width keeps the next character and steps past it */
      if (e < slen) sp_String_append_n(acc, s + e, 1);
      pos = e + 1;
    }
    if (once) break;
  }
  if (pos < slen) sp_String_append_n(acc, s + pos, (size_t)(slen - pos));
  /* the block's own matches leave $~ at the last match of the call */
  return fx_re_subst_done(r, bang, matched, sp_String_cstr(acc), out);
}

static int fx_re_subst(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                         sp_RbVal *out) {
  if (!(FX_IS("sub") || FX_IS("gsub") || FX_IS("sub!") || FX_IS("gsub!"))) return 0;
  if (argc == 2 && fx_is_hash(argv[1])) return fx_re_subst_each(r, name, argv[0], argv[1], NULL, out);
  if (argc == 1 && blk) return fx_re_subst_each(r, name, argv[0], sp_box_nil(), blk, out);
  if (argc != 2) return 0;
  int once = name[0] == 's', bang = name[strlen(name) - 1] == '!';
  const char *s = fx_re_subject(r, name);
  SP_GC_ROOT_STR(s);
  const char *rep = sp_poly_arg_str_chk(argv[1]);
  sp_re_sub_matched = 0;
  const char *res = sp_poly_pat_gsub(argv[0], s, rep, once);
  return fx_re_subst_done(r, bang, sp_re_sub_matched, res, out);
}

/* scan with a block: each match, or the groups of each match, with $~ set.
   As rb_str_scan does, each turn searches the subject as it is then, from
   the end of the last match: the block can change its bytes, or its
   encoding. A block that changes its length raises. */
static int fx_re_scan_each(sp_RbVal r, sp_RbVal pv, const FxBlock *blk, sp_RbVal *out) {
  SP_GC_ROOT_RBVAL(r);
  SP_GC_ROOT_RBVAL(pv);
  mrb_regexp_pattern *pat = (mrb_regexp_pattern *)pv.v.p;
  const char *s = sp_poly_recv_s(r, "scan");
  SP_GC_ROOT_STR(s);
  sp_RbVal v = sp_box_nil();
  SP_GC_ROOT_RBVAL(v);
  const int64_t len = (int64_t)sp_str_byte_len(s);
  /* last: the start of the search of the last match */
  int64_t pos = 0, last = -1;
  int caps[64];
  while (pos <= len) {
    s = sp_poly_recv_s(r, "scan");
    int n = spinel_re_exec(pat, s, len, pos, caps, 64, sp_str_is_binary(s));
    if (n <= 0 || caps[0] < 0) break;
    sp_re_set_last_match(pat, s, caps, n);
    int pairs = (n > 64 ? 64 : n) / 2;
    if (pairs <= 1) {
      v = fx_new_string((const uint8_t *)s + caps[0], (size_t)(caps[1] - caps[0]));
    } else {
      sp_PolyArray *row = sp_PolyArray_new();
      v = sp_box_poly_array(row);
      for (int g = 1; g < pairs; g++)
        sp_PolyArray_push(row, caps[g * 2] < 0 ? sp_box_nil()
                                               : fx_new_string((const uint8_t *)s + caps[g * 2],
                                                                 (size_t)(caps[g * 2 + 1] - caps[g * 2])));
    }
    last = pos;
    pos = caps[1];
    if (caps[0] == caps[1]) pos++;
    fx_y1(blk, v);
    FX_STOP();
    if ((int64_t)sp_str_byte_len(sp_poly_recv_s(r, "scan")) != len) sp_raise_cls("RuntimeError", "string modified");
  }
  /* $~ is the last match, which the search runs again for, since the block
     can match on its own; with no match, nil */
  if (last < 0) {
    sp_re_clear_last_match();
  } else {
    s = sp_poly_recv_s(r, "scan");
    int n = spinel_re_exec(pat, s, len, last, caps, 64, sp_str_is_binary(s));
    if (n > 0 && caps[0] >= 0) sp_re_set_last_match(pat, s, caps, n);
    else sp_re_clear_last_match();
  }
  *out = r;
  return 1;
}

/* str[re], str[re, group], str.slice(...). */
static sp_RbVal fx_re_aref(const char *s, int argc, sp_RbVal *argv) {
  sp_MatchData *m = fx_re_md_set(sp_re_matchdata((mrb_regexp_pattern *)argv[0].v.p, s));
  SP_GC_ROOT(m);
  if (!m) return sp_box_nil();
  return sp_poly_md_group(m, argc == 2 ? argv[1] : sp_box_int(0));
}

/* The methods of a String with a Regexp, a block or a Hash. */
static int fx_re_string(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                          sp_RbVal *out) {
  if (fx_re_matcher(r, name, argc, argv, blk, out)) return 1;
  if (fx_re_subst(r, name, argc, argv, blk, out)) return 1;
  if (argc < 1 || !fx_is_regexp(argv[0])) {
    /* start_with?(a, re, ...) */
    if (!FX_IS("start_with?")) return 0;
    int any = 0;
    for (int i = 0; i < argc; i++) any |= fx_is_regexp(argv[i]);
    if (!any) return 0;
  }
  mrb_regexp_pattern *pat = argc >= 1 && fx_is_regexp(argv[0]) ? (mrb_regexp_pattern *)argv[0].v.p : NULL;
  if (FX_IS("scan") && argc == 1 && blk) return fx_re_scan_each(r, argv[0], blk, out);
  if (blk) return 0;
  const char *s = fx_re_subject(r, name);
  SP_GC_ROOT_STR(s);
  if (FX_IS("start_with?")) {
    sp_bool hit = FALSE;
    for (int i = 0; i < argc && !hit; i++) hit = sp_str_start_with_boxed(s, argv[i]);
    *out = sp_box_bool(hit);
    return 1;
  }
  if (FX_IS("scan") && argc == 1) {
    *out = sp_box_poly_array(sp_re_scan_poly(pat, s));
    return 1;
  }
  if (FX_IS("split") && (argc == 1 || (argc == 2 && argv[1].tag == SP_TAG_INT))) {
    sp_StrArray *a = sp_re_split_limit(pat, s, argc == 2 ? argv[1].v.i : 0);
    *out = sp_box_nullable_obj((void *)a, SP_BUILTIN_STR_ARRAY);
    return 1;
  }
  if ((FX_IS("[]") || FX_IS("slice")) && (argc == 1 || argc == 2)) {
    *out = fx_re_aref(s, argc, argv);
    return 1;
  }
  /* s[re] = v and s[re, n] = v: the span of the match, or of its group n */
  if (FX_IS("[]=") && (argc == 2 || argc == 3)) {
    fx_str_mut(r, name);
    sp_MatchData *m = fx_re_md_set(sp_re_matchdata(pat, s));
    SP_GC_ROOT(m);
    if (!m) sp_raise_cls("IndexError", "regexp not matched");
    sp_int g = argc == 3 ? sp_poly_arg_int_chk(argv[1]) : 0;
    sp_oint b = sp_MatchData_begin(m, g), e = sp_MatchData_end(m, g);
    if (b.nil) sp_raise_cls("IndexError", sp_sprintf("regexp group %lld not matched", (long long)g));
    sp_poly_splice(r, b.v, e.v - b.v, argv[argc - 1]);
    *out = argv[argc - 1];
    return 1;
  }
  /* index(re, pos) sets $~, as CRuby's does */
  if (FX_IS("index") && (argc == 1 || (argc == 2 && argv[1].tag == SP_TAG_INT))) {
    sp_MatchData *m = fx_re_md_set(sp_re_matchdata_at(pat, s, argc == 2 ? argv[1].v.i : 0));
    *out = m ? sp_box_oint(sp_MatchData_begin(m, 0)) : sp_box_nil();
    return 1;
  }
  if (FX_IS("rindex") && argc == 1) {
    *out = sp_box_oint(sp_re_rindex_opt(pat, s));
    return 1;
  }
  if (FX_IS("rindex") && argc == 2 && argv[1].tag == SP_TAG_INT) {
    *out = sp_box_oint(sp_re_rindex_from_opt(pat, s, argv[1].v.i));
    return 1;
  }
  if ((FX_IS("partition") || FX_IS("rpartition")) && argc == 1) {
    sp_PolyArray *a = sp_PolyArray_new();
    SP_GC_ROOT(a);
    if (name[0] == 'r') {
      sp_StrArray *p = sp_re_rpartition(pat, s);
      SP_GC_ROOT(p);
      for (sp_int i = 0; i < p->len; i++) sp_PolyArray_push(a, fx_re_str(SP_SEBN(p->data[i])));
    } else {
      sp_MatchData *m = fx_re_md_set(sp_re_matchdata(pat, s));
      SP_GC_ROOT(m);
      if (!m) {
        sp_PolyArray_push(a, fx_re_str(s));
        sp_PolyArray_push(a, fx_re_str(""));
        sp_PolyArray_push(a, fx_re_str(""));
      } else {
        sp_PolyArray_push(a, fx_re_str(sp_MatchData_pre_match(m)));
        sp_PolyArray_push(a, fx_re_str(sp_MatchData_aref(m, 0)));
        sp_PolyArray_push(a, fx_re_str(sp_MatchData_post_match(m)));
      }
    }
    *out = sp_box_poly_array(a);
    return 1;
  }
  if (FX_IS("slice!") && argc == 1) {
    if (r.tag != SP_TAG_OBJ) fx_gap("a bang method on a String that cannot change");
    const char *rest = NULL;
    SP_GC_ROOT_STR(rest);
    const char *got = sp_str_slice_re(pat, s, &rest);
    SP_GC_ROOT_STR(got);
    if (got) sp_String_set_bin((sp_String *)r.v.p, rest);
    *out = fx_re_str(got);
    return 1;
  }
  if (FX_IS("[]=") && argc == 2) {
    if (r.tag != SP_TAG_OBJ) fx_gap("a bang method on a String that cannot change");
    const char *res = sp_str_splice_re(pat, s, sp_poly_arg_str_chk(argv[1]));
    SP_GC_ROOT_STR(res);
    sp_String_set_bin((sp_String *)r.v.p, res);
    *out = argv[1];
    return 1;
  }
  return 0;
}

/* ---- MatchData ---- */

/* The methods that the generated wrappers do not answer. */
static int fx_re_md_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  sp_MatchData *m = (sp_MatchData *)r.v.p;
  SP_GC_ROOT(m);
  if (FX_IS("[]") && argc == 1 && fx_is_range(argv[0])) {
    const sp_Range *g = (const sp_Range *)argv[0].v.p;
    *out = sp_box_poly_array(sp_MatchData_aref_range(m, g->nobeg ? INTPTR_MIN : g->first, g->noend ? INTPTR_MAX : g->last,
                                                     (int)g->excl));
    return 1;
  }
  if (FX_IS("[]") && argc == 1) { *out = sp_poly_md_group(m, argv[0]); return 1; }
  if (FX_IS("[]") && argc == 2 && argv[0].tag == SP_TAG_INT && argv[1].tag == SP_TAG_INT) {
    sp_PolyArray *a = sp_MatchData_aref_len(m, argv[0].v.i, argv[1].v.i);
    *out = a ? sp_box_poly_array(a) : sp_box_nil();
    return 1;
  }
  /* the content decides ==, eql? and hash */
  if ((FX_IS("==") || FX_IS("eql?") || FX_IS("!=")) && argc == 1) {
    int eq = fx_is_md(argv[0]) && sp_MatchData_eq(m, (sp_MatchData *)argv[0].v.p);
    *out = sp_box_bool(name[0] == '!' ? !eq : eq);
    return 1;
  }
  if (FX_IS("hash") && argc == 0) { *out = sp_box_int(sp_MatchData_hash(m)); return 1; }
  if (FX_IS("values_at")) {
    sp_PolyArray *a = sp_PolyArray_new();
    SP_GC_ROOT(a);
    for (int i = 0; i < argc; i++) {
      if (!fx_is_range(argv[i])) { sp_PolyArray_push(a, sp_poly_md_group(m, argv[i])); continue; }
      /* a Range of groups; a group past the last is nil */
      sp_int beg, end;
      fx_range_span(argv[i], m->ncap, &beg, &end);
      for (sp_int j = beg; j < end; j++) sp_PolyArray_push(a, fx_re_str(sp_MatchData_aref(m, j)));
    }
    *out = sp_box_poly_array(a);
    return 1;
  }
  /* named_captures(symbolize_names: true) */
  if (FX_IS("named_captures") && argc == 1 && fx_is_kwhash(argv[0])) {
    sp_RbVal key = sp_box_sym(sp_sym_intern("symbolize_names"));
    sp_RbVal sym = fx_send(argv[0], "[]", 1, &key);
    if (sp_poly_truthy(sym)) *out = sp_box_obj(sp_md_named_captures_sym(m), SP_BUILTIN_SYM_POLY_HASH);
    else *out = sp_box_obj(sp_md_named_captures(m), SP_BUILTIN_STR_POLY_HASH);
    return 1;
  }
  return 0;
}

/* ---- Enumerable ---- */

/* grep and grep_v: the elements that pattern === takes, or does not take.
   A block maps them. */
static int fx_re_grep(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                        sp_RbVal *out) {
  if (!(FX_IS("grep") || FX_IS("grep_v")) || argc != 1) return 0;
  FxSeq s;
  SP_GC_ROOT_RBVAL(r);
  if (!fx_seq_open(r, &s)) return 0;
  FX_SEQ_ROOT(s);
  int keep = name[4] == 0;
  sp_PolyArray *a = sp_PolyArray_new();
  SP_GC_ROOT(a);
  sp_RbVal v = sp_box_nil(), w = sp_box_nil();
  SP_GC_ROOT_RBVAL(v);
  SP_GC_ROOT_RBVAL(w);
  while (fx_seq_next(&s, &v)) {
    if (sp_poly_truthy(fx_send(argv[0], "===", 1, &v)) != keep) continue;
    w = blk ? fx_y1(blk, v) : v;
    FX_STOP();
    sp_PolyArray_push(a, w);
  }
  *out = sp_box_poly_array(a);
  return 1;
}

/* ---- dispatch ---- */

/* The calls that this file answers. Answers 1 and sets *out when it
   handles the call. */
static int fx_re_call(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                        sp_RbVal *out) {
  if (r.tag == SP_TAG_CLASS) return !blk && fx_re_class(r, name, argc, argv, out);
  /* obj !~ x is the negation of obj =~ x, for any receiver */
  if (FX_IS("!~") && argc == 1 && !blk) {
    *out = sp_box_bool(!sp_poly_truthy(fx_send(r, "=~", 1, argv)));
    return 1;
  }
  if (fx_is_regexp(r)) return fx_re_regexp(r, name, argc, argv, blk, out);
  if (fx_is_string(r)) return fx_re_string(r, name, argc, argv, blk, out);
  /* a Symbol matches as its name */
  if (r.tag == SP_TAG_SYM && (FX_IS("=~") || FX_IS("match") || FX_IS("match?") || FX_IS("[]") ||
                              FX_IS("slice") || FX_IS("start_with?"))) {
    int any = 0;
    for (int i = 0; i < argc; i++) any |= fx_is_regexp(argv[i]);
    if (!any) return 0;
    sp_RbVal s = fx_re_str(sp_sym_to_s((sp_sym)r.v.i));
    SP_GC_ROOT_RBVAL(s);
    return fx_re_string(s, name, argc, argv, blk, out);
  }
  if (fx_is_md(r)) return !blk && fx_re_md_core(r, name, argc, argv, out);
  return fx_re_grep(r, name, argc, argv, blk, out);
}

static void fx_re_init(void) {
  /* a pattern that does not compile raises RegexpError */
  sp_re_set_error_handler(sp_re_default_error_handler);
  /* gsub, sub and scan leave $~ at their last match */
  sp_re_track_last = 1;
  FxClass *k = fx_builtin("Regexp");
  fx_const_set(k, sp_sym_intern("IGNORECASE"), sp_box_int(1));
  fx_const_set(k, sp_sym_intern("EXTENDED"), sp_box_int(2));
  fx_const_set(k, sp_sym_intern("MULTILINE"), sp_box_int(4));
}

#undef FX_IS
#undef FX_STOP
