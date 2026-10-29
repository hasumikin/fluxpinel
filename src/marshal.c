/* marshal.c -- Marshal.dump and Marshal.load on the serializer of the
   runtime (lib/sp_marshal.c of spinel).

   The runtime writes and reads the builtin values. For an object of a
   class of the program it asks the vtable sp_marshal_v, which the code
   that spinel emits fills for its classes. Fluxpinel fills it here: it
   writes an FxObject as CRuby does (`o`, the class, the ivars), an
   instance of a subclass of Array or Hash as the runtime expects (`C`),
   and reads one back into an instance of the class by its name. In the
   host build, the kernel fills the vtable first for its own classes, and
   Fluxpinel chains to it for an object of the kernel. */

#include "fluxpinel.h"

#define FX_IS(s) (!strcmp(name, (s)))

static sp_marshal_vt fx_marshal_kernel;   /* the vtable of the kernel, or zero */

/* The slots of the ivars of an object of the program: o->iv, which has a
   slot for each ivar that a write made, as instance_variables lists. */
static long fx_marshal_nivars(const FxClass *c, const FxObject *o) {
  long n = o->iv ? (long)o->iv->len : 0;
  return n < c->nivars ? n : c->nivars;
}

static void fx_marshal_ivars(sp_mar_buf *b, const FxClass *c, const FxObject *o, long n) {
  for (long i = 0; i < n; i++) {
    sp_mar_sym(b, sp_sym_to_s(c->ivars[i]));
    sp_mar_w(b, o->iv->data[i]);
  }
}

/* An object of the program: `o`, the class and the ivars; an instance of
   a subclass of Array or Hash: `C`, the class and its elements, under `I`
   with the ivars when it has any. A native object and a record (Struct,
   Data) are not written: the runtime raises TypeError. An exception of a
   class of the program is an sp_Exception, which the runtime writes. */
static int fx_marshal_obj_dump(sp_mar_buf *b, int cls_id, void *p) {
  if (cls_id < FX_CLS_BASE) return fx_marshal_kernel.obj_dump ? fx_marshal_kernel.obj_dump(b, cls_id, p) : 0;
  FxClass *c = fx_class_by_id(cls_id);
  if (!c || c->native || fx_record_of(c)) return 0;
  if (c->anon) sp_raise_cls("TypeError", sp_sprintf("can't dump anonymous class %s", c->name));
  if (fx_is_array_class(c) || fx_is_hash_class(c)) {
    sp_RbVal v = sp_box_obj(p, fx_is_array_class(c) ? SP_BUILTIN_POLY_ARRAY : SP_BUILTIN_POLY_POLY_HASH);
    const FxSubTail *t = fx_bsub_tail(v);
    const FxObject *h = t && fx_is_fx_object(t->ivars) ? (const FxObject *)t->ivars.v.p : NULL;
    long n = h ? fx_marshal_nivars(c, h) : 0;
    if (n) sp_mar_b(b, 'I');
    sp_mar_b(b, 'C');
    sp_mar_sym(b, c->name);
    sp_mar_w_body(b, v);
    if (n) {
      sp_mar_long(b, n);
      fx_marshal_ivars(b, c, h, n);
    }
    return 1;
  }
  const FxObject *o = (const FxObject *)p;
  long n = fx_marshal_nivars(c, o);
  sp_mar_b(b, 'o');
  sp_mar_sym(b, c->name);
  sp_mar_long(b, n);
  fx_marshal_ivars(b, c, o, n);
  return 1;
}

/* Into nil: a new instance of the class of the program with this name.
   Into an object: its ivars from iv, a boxed Array of name and value
   pairs. A name that no class of the program has goes to the kernel. */
static sp_RbVal fx_marshal_obj_load(const char *name, sp_RbVal into, sp_RbVal iv, int *ok) {
  SP_GC_ROOT_RBVAL(into);
  SP_GC_ROOT_RBVAL(iv);
  *ok = 1;
  if (into.tag == SP_TAG_NIL) {
    FxClass *k;
    FX_HOOKED(k = fx_class_named(name));
    if (!k || k->cls_id < FX_CLS_BASE) {
      if (fx_marshal_kernel.obj_load) return fx_marshal_kernel.obj_load(name, into, iv, ok);
      *ok = 0;
      return sp_box_nil();
    }
    if (k->is_module || k->native || fx_record_of(k) || fx_is_exc_class(k)) {
      *ok = 0;
      return sp_box_nil();
    }
    sp_RbVal v;
    FX_HOOKED(v = fx_is_array_class(k) || fx_is_hash_class(k) ? fx_bsub_new(k) : fx_new_object(k));
    return v;
  }
  if (!fx_is_fx_object(into) && !fx_bsub_tail(into)) {
    if (fx_marshal_kernel.obj_load) return fx_marshal_kernel.obj_load(name, into, iv, ok);
    *ok = 0;
    return into;
  }
  const sp_PolyArray *a = (const sp_PolyArray *)iv.v.p;
  for (sp_int i = 0; i + 1 < a->len; i += 2)
    FX_HOOKED(fx_ivar_set(into, (sp_sym)a->data[i].v.i, a->data[i + 1]));
  return into;
}

/* Fills the vtable of the serializer: the slots that the kernel did not
   fill take the wrappers of the runtime, and the object slots are those
   above, which chain to the kernel. */
static void fx_marshal_init(void) {
  fx_marshal_kernel = sp_marshal_v;
  if (!sp_marshal_v.sym_intern) sp_marshal_v.sym_intern = sp_sym_intern;
  if (!sp_marshal_v.arr_new) sp_marshal_v.arr_new = sp_marv_arr_new;
  if (!sp_marshal_v.arr_push) sp_marshal_v.arr_push = sp_marv_arr_push;
  if (!sp_marshal_v.hash_new) sp_marshal_v.hash_new = sp_marv_hash_new;
  if (!sp_marshal_v.hash_set) sp_marshal_v.hash_set = sp_marv_hash_set;
  if (!sp_marshal_v.hash_default) sp_marshal_v.hash_default = sp_marv_hash_default;
  if (!sp_marshal_v.hash_set_default) sp_marshal_v.hash_set_default = sp_marv_hash_set_default;
  if (!sp_marshal_v.any_push) sp_marshal_v.any_push = sp_marv_any_push;
  if (!sp_marshal_v.box_complex) sp_marshal_v.box_complex = sp_marv_box_complex;
  if (!sp_marshal_v.box_rational) sp_marshal_v.box_rational = sp_marv_box_rational;
  if (!sp_marshal_v.raise) sp_marshal_v.raise = sp_marv_raise;
  sp_marshal_v.obj_dump = fx_marshal_obj_dump;
  sp_marshal_v.obj_load = fx_marshal_obj_load;
}

/* Whether v is the keyword Hash of the call. */
static int fx_marshal_kw(sp_RbVal v) {
  return fx_is_hash(v) && v.v.p == L.kwhash;
}

/* Marshal.dump(obj, limit = -1), Marshal.dump(obj, io, limit = -1),
   Marshal.load(source, proc = nil, freeze: false) and restore. A limit
   is not modeled: the dump has no depth limit. A source that is no String
   is read with its read method. */
static int fx_marshal_fn(const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (FX_IS("dump")) {
    if (argc < 1 || argc > 3) fx_arity_error(argc, 1, 2, 0);
    SP_GC_ROOT_RBVAL(argv[0]);
    sp_RbVal io = argc >= 2 && !fx_is_integer(argv[1]) ? argv[1] : sp_box_nil();
    SP_GC_ROOT_RBVAL(io);
    sp_RbVal s = sp_box_str(sp_marshal_dump(argv[0]));
    if (io.tag == SP_TAG_NIL) {
      *out = s;
      return 1;
    }
    SP_GC_ROOT_RBVAL(s);
    fx_send(io, "write", 1, &s);
    *out = io;
    return 1;
  }
  if (FX_IS("load") || FX_IS("restore")) {
    int kw = argc >= 2 && fx_marshal_kw(argv[argc - 1]);
    int n = argc - kw;
    if (n < 1 || n > 2) fx_arity_error(argc, 1, 1, 0);
    if (n == 2 && argv[1].tag != SP_TAG_NIL) fx_gap("Marshal.load with a proc");
    if (kw) {
      sp_PolyPolyHash *h = sp_poly_as_poly_poly_hash(argv[argc - 1]);
      for (sp_int i = 0; i < h->len; i++)
        if (sp_poly_truthy(h->vals[h->order[i]])) fx_gap("Marshal.load with freeze: true");
    }
    sp_RbVal src = argv[0];
    SP_GC_ROOT_RBVAL(src);
    if (!fx_is_string(src)) src = fx_send(src, "read", 0, NULL);
    const char *s = sp_poly_arg_str_chk(src);
    *out = sp_marshal_load(s, (sp_int)sp_str_byte_len(s));
    return 1;
  }
  return 0;
}

static int fx_marshal_responds(const char *name) {
  return FX_IS("dump") || FX_IS("load") || FX_IS("restore");
}
