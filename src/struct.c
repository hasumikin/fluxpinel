/* struct.c -- Struct, Data and anonymous classes.

   spinel compiles Struct.new and Data.define to a class of the program.
   Fluxpinel makes a FxClass for each one at run time: a record class. The
   class carries its members, and an instance keeps each member in a slot
   whose name has no @, so instance_variables does not list it. An
   accessor is an attr method on that slot, so a def of the program can
   replace it and call super.

   Class.new and Module.new make an anonymous class or module. Like a
   record class, it takes the name of the first constant that holds it
   (fx_const_set). */

#include "fluxpinel.h"

#define FX_IS(s) (!strcmp(name, (s)))
#define FX_STOP() do { if (L.ctl != FX_CTL_NONE) { *out = sp_box_nil(); return 1; } } while (0)

/* The record class that k is or inherits from, or NULL. */
static const FxClass *fx_record_of(const FxClass *k) {
  for (; k; k = k->super)
    if (k->record) return k;
  return NULL;
}

static FxClass *fx_class_anon(int is_module, FxClass *super, FxClass *lexical) {
  FxClass *k = fx_class_new(NULL, is_module, super, lexical);
  k->name = strdup(sp_sprintf("#<%s:0x%016llx>", is_module ? "Module" : "Class", (unsigned long long)(uintptr_t)k));
  k->anon = 1;
  return k;
}

static int fx_rec_index(const FxClass *rk, sp_sym s) {
  for (int i = 0; i < rk->nmembers; i++)
    if (rk->members[i] == s) return i;
  return -1;
}

static sp_RbVal fx_rec_get(sp_RbVal o, const FxClass *rk, int i) {
  return fx_ivar_get(o, rk->members[i]);
}

static void fx_rec_set(sp_RbVal o, const FxClass *rk, int i, sp_RbVal v) {
  fx_ivar_set(o, rk->members[i], v);
}

/* ":a, :b", or "a, b" without the colons, for the messages of CRuby. */
static const char *fx_rec_names(sp_PolyArray *names, int colon) {
  sp_String *b = sp_String_new("");
  SP_GC_ROOT(b);
  for (sp_int i = 0; i < names->len; i++) {
    if (i) sp_String_append(b, ", ");
    if (colon) sp_String_append(b, ":");
    sp_String_append(b, sp_sym_to_s((sp_sym)names->data[i].v.i));
  }
  return sp_str_dup(sp_String_cstr(b));
}

/* ---- the record classes ---- */

/* Struct.new(:a, :b, keyword_init: true) { body } and Data.define(:a, :b). */
static FxClass *fx_record_define(FxFrame *f, int kind, int argc, sp_RbVal *argv, const FxBlock *blk) {
  int kw_init = 0;
  if (argc > 0 && fx_is_kwhash(argv[argc - 1])) {
    sp_RbVal key = sp_box_sym(sp_sym_intern("keyword_init"));
    sp_RbVal ki = fx_send(argv[--argc], "[]", 1, &key);
    if (kind == FX_REC_STRUCT && ki.tag != SP_TAG_NIL) kw_init = sp_poly_truthy(ki) ? 1 : -1;
  }
  if (kind == FX_REC_STRUCT && argc > 0 && fx_is_string(argv[0]))
    fx_gap("Struct.new with a class name");
  sp_sym *members = malloc(sizeof *members * (size_t)(argc > 0 ? argc : 1));
  for (int i = 0; i < argc; i++) {
    sp_RbVal a = argv[i];
    if (a.tag == SP_TAG_SYM) members[i] = (sp_sym)a.v.i;
    else if (fx_is_string(a)) members[i] = sp_sym_intern(sp_poly_recv_s(a, "new"));
    else if (kind == FX_REC_STRUCT && i == 0)
      sp_raise_cls("TypeError", sp_sprintf("no implicit conversion of %s into String", sp_poly_class_name(a)));
    else sp_raise_cls("TypeError", sp_sprintf("%s is not a symbol nor a string", sp_poly_inspect(a)));
    for (int j = 0; j < i; j++)
      if (members[j] == members[i])
        sp_raise_cls("ArgumentError", sp_sprintf("duplicate member: %s", sp_sym_to_s(members[i])));
  }
  FxClass *k = fx_class_anon(0, fx_builtin(kind == FX_REC_DATA ? "Data" : "Struct"), f ? f->cref : L.object);
  k->record = kind;
  k->keyword_init = kw_init;
  k->members = members;
  k->nmembers = argc;
  for (int i = 0; i < argc; i++) {
    fx_define_method(k, fx_method_new(FX_M_READER, members[i], NULL, members[i], k), 0);
    if (kind == FX_REC_STRUCT)
      fx_define_method(k, fx_method_new(FX_M_WRITER, sp_sym_intern(sp_sprintf("%s=", sp_sym_to_s(members[i]))),
                                            NULL, members[i], k), 0);
  }
  if (blk) fx_yield_under(blk, fx_class_box(k), k, 0, NULL);
  return k;
}

/* Data.new takes its members as keywords: the positional form becomes a
   keyword Hash before initialize. */
static sp_RbVal fx_data_keywords(const FxClass *rk, int argc, sp_RbVal *argv) {
  if (argc > rk->nmembers)
    sp_raise_cls("ArgumentError", sp_sprintf("wrong number of arguments (given %d, expected 0..%d)", argc, rk->nmembers));
  sp_PolyPolyHash *h = sp_PolyPolyHash_new();
  SP_GC_ROOT(h);
  for (int i = 0; i < argc; i++) sp_PolyPolyHash_set(h, sp_box_sym(rk->members[i]), argv[i]);
  return sp_box_obj(h, SP_BUILTIN_POLY_POLY_HASH);
}

/* k.new(*args) and k[*args] for a record class k. */
static sp_RbVal fx_record_new(FxClass *k, int argc, sp_RbVal *argv, const FxBlock *blk) {
  const FxClass *rk = fx_record_of(k);
  sp_RbVal obj = fx_new_object(k), kw = sp_box_nil();
  SP_GC_ROOT_RBVAL(obj);
  SP_GC_ROOT_RBVAL(kw);
  for (int i = 0; i < rk->nmembers; i++) fx_rec_set(obj, rk, i, sp_box_nil());
  if (rk->record == FX_REC_DATA && !(argc == 1 && fx_is_kwhash(argv[0]))) {
    if (argc > 0 && fx_is_kwhash(argv[argc - 1])) fx_gap("Data.new with positional and keyword arguments");
    kw = fx_data_keywords(rk, argc, argv);
    argc = 1;
    argv = &kw;
    L.kwhash = kw.v.p;
  }
  FxMethod *init = fx_find_method(k, sp_sym_intern("initialize"));
  if (init) fx_invoke_b(init, obj, argc, argv, blk);
  else fx_record_init(obj, argc, argv);
  return obj;
}

/* The member that a Symbol, a String or an index names, or -1. It raises
   for no key. */
static int fx_record_find(const FxClass *rk, sp_RbVal key) {
  if (key.tag == SP_TAG_SYM) return fx_rec_index(rk, (sp_sym)key.v.i);
  if (fx_is_string(key)) return fx_rec_index(rk, sp_sym_intern(sp_poly_recv_s(key, "[]")));
  if (key.tag != SP_TAG_INT) return -1;
  sp_int i = key.v.i < 0 ? key.v.i + rk->nmembers : key.v.i;
  return i >= 0 && i < rk->nmembers ? (int)i : -1;
}

/* The member that a keyword names: a Symbol or a String, or for a Struct
   an index, converted as CRuby's rb_struct_pos converts it (a Float
   truncates, an object answers to_int). -1 for none; *shown is the key as
   an unknown keyword names it. */
static int fx_record_kw_index(const FxClass *rk, sp_RbVal key, sp_RbVal *shown) {
  *shown = key;
  if (key.tag == SP_TAG_SYM || fx_is_string(key)) return fx_record_find(rk, key);
  if (rk->record == FX_REC_DATA)
    sp_raise_cls("TypeError", sp_sprintf("%s is not a symbol nor a string", sp_poly_inspect(key)));
  if (key.tag == SP_TAG_FLT) key = sp_box_int((sp_int)key.v.f);
  else if (key.tag == SP_TAG_NIL) sp_raise_cls("TypeError", "no implicit conversion from nil to integer");
  else if (key.tag == SP_TAG_BOOL)
    sp_raise_cls("TypeError", sp_sprintf("no implicit conversion of %s into Integer", key.v.b ? "true" : "false"));
  else if (fx_is_fx_object(key) && fx_find_method(fx_class_of(key), sp_sym_intern("to_int")))
    key = fx_send(key, "to_int", 0, NULL);
  if (key.tag != SP_TAG_INT)
    sp_raise_cls("TypeError", sp_sprintf("no implicit conversion of %s into Integer", sp_poly_class_name(key)));
  *shown = key;
  return fx_record_find(rk, key);
}

/* Sets the members that a keyword Hash names. Answers the keys that name
   no member, which the caller reports after the missing ones. */
static sp_PolyArray *fx_record_keywords(sp_RbVal self, const FxClass *rk, sp_RbVal hv, int *seen) {
  sp_PolyPolyHash *h = sp_poly_as_poly_poly_hash(hv);
  SP_GC_ROOT(h);
  sp_PolyArray *unknown = sp_PolyArray_new();
  SP_GC_ROOT(unknown);
  for (sp_int j = 0; j < h->len; j++) {
    sp_int slot = h->order[j];
    sp_RbVal shown;
    int i = fx_record_kw_index(rk, h->keys[slot], &shown);
    if (i < 0) { sp_PolyArray_push(unknown, shown); continue; }
    fx_rec_set(self, rk, i, h->vals[slot]);
    if (seen) seen[i] = 1;
  }
  return unknown;
}

/* unknown keywords: z, w (a Struct) or unknown keyword: :z (a Data) */
static void fx_record_unknown(const FxClass *rk, sp_PolyArray *unknown) {
  SP_GC_ROOT(unknown);
  if (!unknown->len) return;
  sp_String *b = sp_String_new("");
  SP_GC_ROOT(b);
  for (sp_int i = 0; i < unknown->len; i++) {
    sp_RbVal k = unknown->data[i];
    if (i) sp_String_append(b, ", ");
    sp_String_append(b, rk->record == FX_REC_DATA ? sp_poly_inspect(k)
                        : k.tag == SP_TAG_SYM ? sp_sym_to_s((sp_sym)k.v.i) : sp_poly_to_s(k));
  }
  if (rk->record == FX_REC_STRUCT) sp_raise_cls("ArgumentError", sp_sprintf("unknown keywords: %s", b->data));
  sp_raise_cls("ArgumentError", sp_sprintf("unknown keyword%s: %s", unknown->len > 1 ? "s" : "", b->data));
}

/* The builtin initialize of a Struct or a Data, also for super. */
static void fx_record_init(sp_RbVal self, int argc, sp_RbVal *argv) {
  SP_GC_ROOT_RBVAL(self);
  const FxClass *rk = fx_record_of(fx_class_of(self));
  int kw = argc > 0 && fx_is_kwhash(argv[argc - 1]);
  if (rk->record == FX_REC_DATA) {
    if (argc > (kw ? 1 : 0))
      sp_raise_cls("ArgumentError", sp_sprintf("wrong number of arguments (given %d, expected 0)", argc - kw));
    int seen[rk->nmembers ? rk->nmembers : 1];
    memset(seen, 0, sizeof seen);
    sp_PolyArray *unknown = kw ? fx_record_keywords(self, rk, argv[0], seen) : sp_PolyArray_new();
    SP_GC_ROOT(unknown);
    /* a missing keyword is reported before an unknown one */
    sp_PolyArray *missing = sp_PolyArray_new();
    SP_GC_ROOT(missing);
    for (int i = 0; i < rk->nmembers; i++)
      if (!seen[i]) sp_PolyArray_push(missing, sp_box_sym(rk->members[i]));
    if (missing->len)
      sp_raise_cls("ArgumentError", sp_sprintf("missing keyword%s: %s", missing->len > 1 ? "s" : "",
                                               fx_rec_names(missing, 1)));
    fx_record_unknown(rk, unknown);
    sp_gc_freeze(self.v.p);
    return;
  }
  /* a Struct takes keywords with keyword_init: true, or keywords alone
     when keyword_init is not given */
  if (rk->keyword_init == 1 || (rk->keyword_init == 0 && kw && argc == 1)) {
    if (argc > 1 || (argc == 1 && !kw))
      sp_raise_cls("ArgumentError", sp_sprintf("wrong number of arguments (given %d, expected 0)", argc));
    if (argc == 1) fx_record_unknown(rk, fx_record_keywords(self, rk, argv[0], NULL));
    return;
  }
  if (argc > rk->nmembers) sp_raise_cls("ArgumentError", "struct size differs");
  for (int i = 0; i < argc; i++) fx_rec_set(self, rk, i, argv[i]);
}

/* Class.new, Module.new, Struct.new, Data.define, and new, [] and members
   of a record class. */
static int fx_struct_class(FxFrame *f, FxClass *k, const char *name, int argc, sp_RbVal *argv,
                             const FxBlock *blk, sp_RbVal *out) {
  if (k->anon && argc == 0 && FX_IS("name")) { *out = sp_box_nil(); return 1; }
  if (k->cls_id < 0) {
    int cls = !strcmp(k->name, "Class"), mod = !strcmp(k->name, "Module");
    if ((cls || mod) && FX_IS("new")) {
      FxClass *super = NULL;
      if (cls) {
        super = argc >= 1 ? fx_class_value(argv[0]) : L.object;
        if (!super || super->is_module)
          sp_raise_cls("TypeError", sp_sprintf("superclass must be an instance of Class (given an instance of %s)",
                                               super ? "Module" : fx_class_of(argv[0])->name));
        if (super->cls_id < 0 && super != L.object && strcmp(super->name, "BasicObject"))
          fx_gap(sp_sprintf("Class.new with the builtin superclass %s", super->name));
      }
      FxClass *c = fx_class_anon(mod, super, f ? f->cref : L.object);
      sp_RbVal cv = fx_class_box(c);
      SP_GC_ROOT_RBVAL(cv);
      FxMethod *hook = super ? fx_find_smethod(super, sp_sym_intern("inherited")) : NULL;
      if (hook) fx_invoke(hook, fx_class_box(super), 1, &cv);
      if (blk) fx_yield_under(blk, cv, c, 1, &cv);
      *out = cv;
      return 1;
    }
    if ((!strcmp(k->name, "Struct") && FX_IS("new")) || (!strcmp(k->name, "Data") && FX_IS("define"))) {
      FxClass *c = fx_record_define(f, k->name[0] == 'D' ? FX_REC_DATA : FX_REC_STRUCT, argc, argv, blk);
      *out = fx_class_box(c);
      return 1;
    }
    return 0;
  }
  const FxClass *rk = fx_record_of(k);
  if (!rk) return 0;
  if (FX_IS("new") || FX_IS("[]")) { *out = fx_record_new(k, argc, argv, blk); return 1; }
  if (FX_IS("members") && argc == 0) {
    sp_PolyArray *a = sp_PolyArray_new();
    SP_GC_ROOT(a);
    for (int i = 0; i < rk->nmembers; i++) sp_PolyArray_push(a, sp_box_sym(rk->members[i]));
    *out = sp_box_poly_array(a);
    return 1;
  }
  if (FX_IS("keyword_init?") && argc == 0 && rk->record == FX_REC_STRUCT) {
    *out = rk->keyword_init == 0 ? sp_box_nil() : sp_box_bool(rk->keyword_init == 1);
    return 1;
  }
  if (FX_IS("inspect") && argc == 0 && rk == k && k->keyword_init == 1) {
    *out = fx_re_str(sp_sprintf("%s(keyword_init: true)", k->name));
    return 1;
  }
  return 0;
}

/* ---- the instances ---- */

/* #<struct Point x=1, y=2> and #<data Point x=1>, with no name for an
   anonymous class. */
static const char *fx_record_inspect(sp_RbVal self) {
  SP_GC_ROOT_RBVAL(self);
  const FxClass *k = fx_class_of(self), *rk = fx_record_of(k);
  const char *kind = rk->record == FX_REC_DATA ? "data" : "struct";
  if (sp_poly_recur_seen(SP_POLY_RECUR_INSPECT, self.v.p, NULL))
    return sp_sprintf("#<%s %s:...>", kind, k->anon ? "" : k->name);
  int mark = sp_poly_recur_push(SP_POLY_RECUR_INSPECT, self.v.p, NULL);
  sp_String *b = sp_String_new(sp_sprintf("#<%s ", kind));
  SP_GC_ROOT(b);
  if (!k->anon) sp_String_append(b, k->name);
  for (int i = 0; i < rk->nmembers; i++) {
    sp_String_append(b, i ? ", " : k->anon ? "" : " ");
    /* a member that is not a plain identifier shows as a Symbol (:verbose?) */
    const char *m = sp_sym_to_s(rk->members[i]);
    int plain = isalpha((unsigned char)m[0]) || m[0] == '_' || (unsigned char)m[0] >= 0x80;
    for (const char *q = m; *q && plain; q++) plain = isalnum((unsigned char)*q) || *q == '_' || (unsigned char)*q >= 0x80;
    sp_String_append(b, plain ? m : sp_poly_inspect(sp_box_sym(rk->members[i])));
    sp_String_append(b, "=");
    sp_String_append(b, sp_poly_inspect(fx_rec_get(self, rk, i)));
  }
  sp_String_append(b, ">");
  sp_poly_recur_pop(mark);
  return sp_str_dup(sp_String_cstr(b));
}

/* The member that s[key] names: an index, a Symbol or a String. */
static int fx_record_key(const FxClass *rk, sp_RbVal key) {
  if (key.tag == SP_TAG_INT) {
    sp_int i = key.v.i, n = rk->nmembers;
    if (i < -n)
      sp_raise_cls("IndexError", sp_sprintf("offset %lld too small for struct(size:%d)", (long long)i, rk->nmembers));
    if (i >= n)
      sp_raise_cls("IndexError", sp_sprintf("offset %lld too large for struct(size:%d)", (long long)i, rk->nmembers));
    return (int)(i < 0 ? i + n : i);
  }
  const char *s = key.tag == SP_TAG_SYM ? sp_sym_to_s((sp_sym)key.v.i) : fx_is_string(key) ? sp_poly_recv_s(key, "[]")
                                                                                          : NULL;
  if (!s) sp_raise_cls("TypeError", sp_sprintf("no implicit conversion of %s into Integer", sp_poly_class_name(key)));
  int i = fx_rec_index(rk, sp_sym_intern(s));
  if (i < 0) sp_raise_cls("NameError", sp_sprintf("no member '%s' in struct", s));
  return i;
}

static sp_PolyArray *fx_record_values(sp_RbVal self, const FxClass *rk) {
  sp_PolyArray *a = sp_PolyArray_new();
  SP_GC_ROOT(a);
  for (int i = 0; i < rk->nmembers; i++) sp_PolyArray_push(a, fx_rec_get(self, rk, i));
  return a;
}

/* The methods of Enumerable that a Struct answers through its values. */
static int fx_record_enum(const char *name) {
  static const char *const names[] = { "select", "filter", "reject", "map", "collect", "flat_map", "filter_map",
                                       "find", "detect", "count", "any?", "all?", "none?", "one?", "sum", "min",
                                       "max", "sort", "sort_by", "min_by", "max_by", "each_with_index",
                                       "each_with_object", "inject", "reduce", "include?", "member?", "zip",
                                       "first", "take", "drop", "group_by", "partition", "tally", "find_index",
                                       "entries", "each_slice", "each_cons", "take_while", "drop_while", "uniq",
                                       "minmax", "to_set", "chunk_while", "slice_when", NULL };
  for (int i = 0; names[i]; i++)
    if (!strcmp(names[i], name)) return 1;
  return 0;
}

static int fx_record_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                            sp_RbVal *out) {
  if (!fx_is_fx_object(r)) return 0;
  const FxClass *rk = fx_record_of(fx_class_of(r));
  if (!rk) return 0;
  SP_GC_ROOT_RBVAL(r);
  int data = rk->record == FX_REC_DATA;
  sp_RbVal v = sp_box_nil();
  SP_GC_ROOT_RBVAL(v);
  if (argc == 0 && FX_IS("members")) {
    sp_PolyArray *a = sp_PolyArray_new();
    SP_GC_ROOT(a);
    for (int i = 0; i < rk->nmembers; i++) sp_PolyArray_push(a, sp_box_sym(rk->members[i]));
    *out = sp_box_poly_array(a);
    return 1;
  }
  if (argc == 0 && (FX_IS("inspect") || FX_IS("to_s"))) {
    *out = fx_re_str(fx_record_inspect(r));
    return 1;
  }
  if (argc == 0 && (FX_IS("deconstruct") || (!data && (FX_IS("to_a") || FX_IS("values"))))) {
    *out = sp_box_poly_array(fx_record_values(r, rk));
    return 1;
  }
  if (argc == 0 && FX_IS("to_h")) {
    sp_PolyPolyHash *h = sp_PolyPolyHash_new();
    SP_GC_ROOT(h);
    for (int i = 0; i < rk->nmembers; i++) {
      v = fx_rec_get(r, rk, i);
      if (!blk) { sp_PolyPolyHash_set(h, sp_box_sym(rk->members[i]), v); continue; }
      sp_RbVal pair = fx_y2(blk, sp_box_sym(rk->members[i]), v);
      FX_STOP();
      if (!fx_is_array(pair) || sp_poly_length(pair) != 2)
        sp_raise_cls("TypeError", sp_sprintf("wrong element type %s (expected array)", sp_poly_class_name(pair)));
      sp_PolyPolyHash_set(h, sp_poly_arr_get(pair, 0), sp_poly_arr_get(pair, 1));
    }
    *out = sp_box_obj(h, SP_BUILTIN_POLY_POLY_HASH);
    return 1;
  }
  if (argc == 1 && FX_IS("deconstruct_keys")) {
    sp_PolyPolyHash *h = sp_PolyPolyHash_new();
    SP_GC_ROOT(h);
    if (argv[0].tag == SP_TAG_NIL) {
      for (int i = 0; i < rk->nmembers; i++) sp_PolyPolyHash_set(h, sp_box_sym(rk->members[i]), fx_rec_get(r, rk, i));
    } else if (fx_is_array(argv[0])) {
      /* more keys than members answer {} */
      for (sp_int j = 0; sp_poly_length(argv[0]) <= rk->nmembers && j < sp_poly_length(argv[0]); j++) {
        sp_RbVal key = sp_poly_arr_get(argv[0], j);
        sp_RbVal shown;
        int i = data ? fx_record_kw_index(rk, key, &shown) : fx_record_find(rk, key);
        if (i < 0) break;
        sp_PolyPolyHash_set(h, key, fx_rec_get(r, rk, i));
      }
    } else {
      sp_raise_cls("TypeError", sp_sprintf("wrong argument type %s (expected Array or nil)", sp_poly_class_name(argv[0])));
    }
    *out = sp_box_obj(h, SP_BUILTIN_POLY_POLY_HASH);
    return 1;
  }
  /* == and eql?: the same class, and members that are == or eql? */
  if (argc == 1 && (FX_IS("==") || FX_IS("eql?") || FX_IS("!="))) {
    sp_RbVal o = argv[0];
    int eq = r.v.p == o.v.p;
    if (!eq && fx_is_fx_object(o) && fx_class_of(o) == fx_class_of(r)) {
      eq = 1;
      int eql = FX_IS("eql?");
      for (int i = 0; i < rk->nmembers && eq; i++) {
        v = fx_rec_get(o, rk, i);
        sp_RbVal w = fx_rec_get(r, rk, i);
        eq = eql ? sp_poly_eql(w, v) : sp_poly_truthy(fx_send(w, "==", 1, &v));
      }
    }
    *out = sp_box_bool(name[0] == '!' ? !eq : eq);
    return 1;
  }
  if (argc == 0 && FX_IS("hash")) {
    uint64_t h = (uint64_t)fx_class_of(r)->cls_id * 0x9E3779B97F4A7C15ULL;
    for (int i = 0; i < rk->nmembers; i++) h = h * 31 + (uint64_t)sp_rbval_hash_key(fx_rec_get(r, rk, i));
    *out = sp_box_int((sp_int)(h >> 2));
    return 1;
  }
  if (argc == 0 && FX_IS("frozen?") && data) { *out = sp_box_bool(1); return 1; }
  /* dig answers nil for a member that does not exist */
  if (FX_IS("dig") && argc >= 1 && !data) {
    sp_RbVal k0 = argv[0];
    if (k0.tag != SP_TAG_INT && k0.tag != SP_TAG_SYM && !fx_is_string(k0)) fx_record_key(rk, k0);   /* raises */
    int i = fx_record_find(rk, k0);
    v = i < 0 ? sp_box_nil() : fx_rec_get(r, rk, i);
    *out = argc == 1 || v.tag == SP_TAG_NIL ? v : fx_send(v, "dig", argc - 1, argv + 1);
    return 1;
  }
  if (data && FX_IS("with")) {
    int kw = argc > 0 && fx_is_kwhash(argv[argc - 1]);
    if (argc > kw) sp_raise_cls("ArgumentError", sp_sprintf("wrong number of arguments (given %d, expected 0)", argc - kw));
    if (!kw) { *out = r; return 1; }
    v = fx_new_object(fx_class_of(r));
    for (int i = 0; i < rk->nmembers; i++) fx_rec_set(v, rk, i, fx_rec_get(r, rk, i));
    fx_record_unknown(rk, fx_record_keywords(v, rk, argv[0], NULL));
    sp_gc_freeze(v.v.p);
    *out = v;
    return 1;
  }
  if (data) return 0;
  if (argc == 1 && FX_IS("[]")) { *out = fx_rec_get(r, rk, fx_record_key(rk, argv[0])); return 1; }
  if (argc == 2 && FX_IS("[]=")) {
    fx_rec_set(r, rk, fx_record_key(rk, argv[0]), argv[1]);
    *out = argv[1];
    return 1;
  }
  if (argc == 0 && (FX_IS("size") || FX_IS("length"))) { *out = sp_box_int(rk->nmembers); return 1; }
  if (argc == 0 && blk && (FX_IS("each") || FX_IS("each_pair"))) {
    for (int i = 0; i < rk->nmembers; i++) {
      v = fx_rec_get(r, rk, i);
      if (name[4]) fx_y1(blk, fx_pair(sp_box_sym(rk->members[i]), v));
      else fx_y1(blk, v);
      FX_STOP();
    }
    *out = r;
    return 1;
  }
  if (FX_IS("values_at")) {
    sp_PolyArray *a = sp_PolyArray_new();
    SP_GC_ROOT(a);
    for (int i = 0; i < argc; i++) {
      if (fx_is_range(argv[i])) {
        sp_int beg, end;
        fx_range_span(argv[i], rk->nmembers, &beg, &end);
        for (sp_int j = beg; j < end; j++) sp_PolyArray_push(a, j < rk->nmembers ? fx_rec_get(r, rk, (int)j) : sp_box_nil());
        continue;
      }
      if (argv[i].tag != SP_TAG_INT)
        sp_raise_cls("TypeError", sp_sprintf("no implicit conversion of %s into Integer", sp_poly_class_name(argv[i])));
      sp_PolyArray_push(a, fx_rec_get(r, rk, fx_record_key(rk, argv[i])));
    }
    *out = sp_box_poly_array(a);
    return 1;
  }
  if (fx_record_enum(name)) {
    v = sp_box_poly_array(fx_record_values(r, rk));
    *out = fx_send_b(v, name, argc, argv, blk);
    return 1;
  }
  return 0;
}

/* Whether a record or a class answers name through this file or the class
   methods of object.c, for respond_to?. */
static int fx_record_responds(sp_RbVal r, const char *name) {
  static const char *const rec[] = { "members", "to_h", "deconstruct", "deconstruct_keys", "==", "eql?", "hash",
                                     "inspect", "to_s", NULL };
  static const char *const st[] = { "to_a", "values", "[]", "[]=", "each", "each_pair", "size", "length", "dig",
                                    "values_at", NULL };
  static const char *const cls[] = { "name", "to_s", "inspect", "superclass", "ancestors", "include", "extend",
                                     "attr_accessor", "attr_reader", "attr_writer", "const_get", "method_defined?",
                                     "instance_methods", NULL };
  FxClass *k = fx_class_value(r);
  if (k) {
    for (int i = 0; cls[i]; i++)
      if (!strcmp(cls[i], name)) return 1;
    const FxClass *rk = fx_record_of(k);
    if (rk && (FX_IS("members") || (rk->record == FX_REC_STRUCT && FX_IS("keyword_init?"))))
      return 1;
    if (rk && FX_IS("[]")) return 1;
    if (!k->is_module && (FX_IS("new") || FX_IS("allocate"))) return 1;
    if (k->cls_id < 0 && !strcmp(k->name, "Data") && FX_IS("define")) return 1;
    if (k->cls_id < 0 && (!strcmp(k->name, "Hash") || !strcmp(k->name, "Array")) && FX_IS("[]")) return 1;
    return 0;
  }
  if (!fx_is_fx_object(r)) return 0;
  const FxClass *rk = fx_record_of(fx_class_of(r));
  if (!rk) return 0;
  for (int i = 0; rec[i]; i++)
    if (!strcmp(rec[i], name)) return 1;
  if (rk->record == FX_REC_DATA) return FX_IS("with");
  for (int i = 0; st[i]; i++)
    if (!strcmp(st[i], name)) return 1;
  return fx_record_enum(name);
}

#undef FX_IS
#undef FX_STOP
