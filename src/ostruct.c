/* ostruct.c -- OpenStruct.

   An OpenStruct is an sp_OpenStruct of the runtime: a table of members
   (lib/spinel_rt.h), which inspect, ==, to_h and JSON of the runtime
   already read. This file makes OpenStruct.new, the fixed methods of an
   OpenStruct, as emit_call_openstruct_arms of spinel's codegen gives them,
   and the reads and writes of a member by its name (o.k, o.k = v), which
   the walker tries last, as the method_missing of CRuby's does. */

#include "fluxpinel.h"

#define FX_IS(s) (!strcmp(name, (s)))

static int fx_is_ostruct(sp_RbVal v) { return v.tag == SP_TAG_OBJ && v.cls_id == SP_BUILTIN_OPENSTRUCT && v.v.p; }

/* The member that a Symbol or a String names; another key raises the
   NoMethodError of its to_sym, as CRuby's */
static sp_sym fx_ostruct_key(sp_RbVal k) {
  if (k.tag != SP_TAG_SYM) k = fx_send(k, "to_sym", 0, NULL);
  return (sp_sym)k.v.i;
}

/* OpenStruct.new, and OpenStruct.new(hash): the entries as members */
static int fx_ostruct_class(const FxClass *k, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (k->cls_id >= 0 || strcmp(k->name, "OpenStruct") || !FX_IS("new") || argc > 1) return 0;
  sp_OpenStruct *o;
  if (argc == 0 || argv[0].tag == SP_TAG_NIL) o = sp_OpenStruct_new();
  else if (fx_is_hash(argv[0])) o = sp_openstruct_from_poly(argv[0]);
  else sp_raise_cls("ArgumentError", "argument must be a Hash");
  *out = sp_box_openstruct(o);
  return 1;
}

/* The methods of an OpenStruct that are not a member of it */
static int fx_ostruct_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                           sp_RbVal *out) {
  if (!fx_is_ostruct(r)) return 0;
  sp_OpenStruct *o = (sp_OpenStruct *)r.v.p;
  SP_GC_ROOT(o);
  if (blk) {
    if (FX_IS("each_pair") && argc == 0) {
      sp_SymPolyHash *t = sp_OpenStruct_to_h(o);
      SP_GC_ROOT(t);
      for (sp_int i = 0; i < t->len; i++) {
        sp_RbVal pair[2] = { sp_box_sym(t->order[i]), sp_SymPolyHash_get(t, t->order[i]) };
        fx_yield(blk, 2, pair);
      }
      *out = r;
      return 1;
    }
    /* delete_field(name) { default }: the block answers for a member the
       OpenStruct lacks */
    if (FX_IS("delete_field") && argc == 1) {
      sp_sym k = fx_ostruct_key(argv[0]);
      if (!sp_OpenStruct_has(o, k)) { *out = fx_yield(blk, 0, NULL); return 1; }
      *out = sp_OpenStruct_get(o, k);
      sp_SymPolyHash_delete(o->tbl, k);
      return 1;
    }
    return 0;
  }
  if (argc == 0) {
    if (FX_IS("to_h")) { *out = sp_box_obj(sp_OpenStruct_to_h(o), SP_BUILTIN_SYM_POLY_HASH); return 1; }
    /* each_pair with no block: an Enumerator over the pairs of the table */
    if (FX_IS("each_pair")) {
      sp_RbVal h = sp_box_obj(sp_OpenStruct_to_h(o), SP_BUILTIN_SYM_POLY_HASH);
      SP_GC_ROOT_RBVAL(h);
      *out = sp_box_obj(sp_Enumerator_new_from(h), SP_BUILTIN_ENUMERATOR);
      return 1;
    }
    if (FX_IS("dup") || FX_IS("clone")) { *out = sp_box_openstruct(sp_OpenStruct_dup(o, FX_IS("clone"))); return 1; }
    if (FX_IS("freeze")) { sp_gc_freeze(o); *out = r; return 1; }
    if (FX_IS("to_s") || FX_IS("inspect")) { *out = sp_box_str_new(sp_OpenStruct_inspect(o)); return 1; }
    return 0;
  }
  if (argc == 1) {
    sp_RbVal a = argv[0];
    if (FX_IS("[]")) { *out = sp_OpenStruct_get(o, fx_ostruct_key(a)); return 1; }
    if (FX_IS("respond_to?")) {
      *out = sp_box_bool(sp_OpenStruct_has(o, fx_ostruct_key(a)) || fx_builtin_responds(r, fx_name_arg(a)));
      return 1;
    }
    if (FX_IS("==") || FX_IS("===")) {
      *out = sp_box_bool(fx_is_ostruct(a) && sp_OpenStruct_eq(o, (sp_OpenStruct *)a.v.p));
      return 1;
    }
    if (FX_IS("eql?")) {
      *out = sp_box_bool(fx_is_ostruct(a) && sp_OpenStruct_eql(o, (sp_OpenStruct *)a.v.p));
      return 1;
    }
    /* delete_field(name): the value, and NameError for a member the
       OpenStruct lacks, as CRuby's */
    if (FX_IS("delete_field")) {
      sp_sym k = fx_ostruct_key(a);
      if (!sp_OpenStruct_has(o, k))
        sp_raise_cls("NameError", sp_sprintf("no field '%s' in %s", sp_sym_to_s(k), sp_OpenStruct_inspect(o)));
      *out = sp_OpenStruct_get(o, k);
      sp_SymPolyHash_delete(o->tbl, k);
      return 1;
    }
  }
  if (argc == 2 && FX_IS("[]=")) {
    sp_OpenStruct_set(o, fx_ostruct_key(argv[0]), argv[1]);
    *out = argv[1];
    return 1;
  }
  /* dig(name, *rest): the member, and dig of it with the rest */
  if (argc >= 1 && FX_IS("dig")) {
    sp_RbVal v = sp_OpenStruct_get(o, fx_ostruct_key(argv[0]));
    SP_GC_ROOT_RBVAL(v);
    *out = fx_dig_rest(v, argc - 1, argv + 1);
    return 1;
  }
  return 0;
}

/* A name that is not a method of Object: a member read (o.k) or write
   (o.k = v), ahead of the methods of the runtime for a boxed value (sum,
   empty?), which an OpenStruct does not have. A read of a member the
   OpenStruct lacks is nil, as CRuby's. */
static int fx_ostruct_member(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (!fx_is_ostruct(r) || fx_object_method_name(name)) return 0;
  sp_OpenStruct *o = (sp_OpenStruct *)r.v.p;
  size_t n = strlen(name);
  if (argc == 1 && n > 1 && name[n - 1] == '=' && name[n - 2] != '=' && name[n - 2] != '!' && name[n - 2] != '<' &&
      name[n - 2] != '>') {
    const char *mem = sp_str_from_bytes(name, n - 1);
    sp_OpenStruct_set(o, sp_sym_intern(mem), argv[0]);
    *out = argv[0];
    return 1;
  }
  if (argc == 0 && name[n - 1] != '=') {
    *out = sp_OpenStruct_get(o, sp_sym_intern(name));
    return 1;
  }
  return 0;
}

/* respond_to?: a member of the OpenStruct, or a method of it */
static int fx_ostruct_responds(sp_RbVal r, const char *name) {
  static const char *const names[] = { "to_h", "[]", "[]=", "dig", "each_pair", "delete_field", "respond_to?",
                                       "==", "eql?", "dup", "clone", "freeze", "frozen?", "inspect", "to_s", "hash",
                                       NULL };
  if (!fx_is_ostruct(r)) return 0;
  if (sp_OpenStruct_has((sp_OpenStruct *)r.v.p, sp_sym_intern(name))) return 1;
  for (int i = 0; names[i]; i++)
    if (!strcmp(names[i], name)) return 1;
  return 0;
}
