/* random.c -- Random, and Kernel#rand and Kernel#srand.

   A Random is an sp_Random of the runtime (lib/sp_random.c). The generated
   wrappers give rand with no argument, seed and bytes; the forms with an
   argument call sp_rand_poly, the helper of spinel's codegen for a bound
   known only at run time. The default instance is the stream of
   Kernel#rand, so one srand governs both. */

#include "fluxpinel.h"

#define FX_IS(s) (!strcmp(name, (s)))

static sp_RbVal fx_random_box(sp_Random *r) { return sp_box_obj(r, SP_BUILTIN_RANDOM); }

/* The seed of Random.new and srand: no argument draws a fresh one */
static sp_int fx_random_seed_arg(int argc, sp_RbVal *argv) {
  return argc ? sp_poly_seed_arg(argv[0]) : sp_Random_new_seed();
}

/* Random.new, and the module functions of Random over the default instance */
static int fx_random_class(const FxClass *k, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (k->cls_id >= 0 || strcmp(k->name, "Random")) return 0;
  if (FX_IS("new") && argc <= 1) {
    sp_Random *r;
    if (argc == 0) r = sp_Random_new_auto();
    else if (argv[0].tag == SP_TAG_FLT) r = sp_Random_new_float(argv[0].v.f);
    else r = sp_Random_new(sp_poly_seed_arg(argv[0]));
    *out = fx_random_box(r);
    return 1;
  }
  if (FX_IS("new_seed") && argc == 0) { *out = sp_box_int(sp_Random_new_seed()); return 1; }
  if ((FX_IS("urandom") || FX_IS("random_bytes")) && argc == 1) {
    *out = sp_box_str_new(sp_Random_urandom(sp_poly_arg_int_chk(argv[0])));
    return 1;
  }
  if (FX_IS("srand") && argc <= 1) { *out = sp_box_int(sp_kernel_srand(fx_random_seed_arg(argc, argv))); return 1; }
  sp_Random *d = sp_random_default_get();
  if (FX_IS("rand") && argc <= 1) {
    *out = argc ? sp_rand_poly(d, argv[0], 0) : sp_box_float(sp_Random_rand_float(d));
    return 1;
  }
  if (FX_IS("bytes") && argc == 1) { *out = sp_box_str_new(sp_Random_bytes(d, sp_random_bytes_count(argv[0]))); return 1; }
  if (FX_IS("seed") && argc == 0) { *out = sp_box_int(sp_Random_seed(d)); return 1; }
  return 0;
}

/* Kernel#rand and Kernel#srand over the shared stream */
static int fx_random_kernel(const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (FX_IS("rand") && argc <= 1) {
    sp_Random *d = sp_random_default_get();
    *out = argc ? sp_rand_poly(d, argv[0], 1) : sp_box_float(sp_Random_rand_float(d));
    return 1;
  }
  if (FX_IS("srand") && argc <= 1) {
    *out = sp_box_int(sp_kernel_srand(fx_random_seed_arg(argc, argv)));
    return 1;
  }
  return 0;
}

/* The instance methods that the generated wrappers do not give */
static int fx_random_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (r.tag != SP_TAG_OBJ || r.cls_id != SP_BUILTIN_RANDOM || !r.v.p) return 0;
  sp_Random *g = (sp_Random *)r.v.p;
  if (FX_IS("rand") && argc == 1) { *out = sp_rand_poly(g, argv[0], 0); return 1; }
  if (FX_IS("rand") && argc > 1) fx_arity_error(argc, 0, 1, 0);
  if (FX_IS("seed") && argc > 0) fx_arity_error(argc, 0, 0, 0);
  if (FX_IS("bytes") && argc != 1) fx_arity_error(argc, 1, 0, 0);
  if ((FX_IS("dup") || FX_IS("clone")) && argc == 0) { *out = fx_random_box(sp_Random_dup(g)); return 1; }
  if (FX_IS("==") && argc == 1) {
    sp_RbVal o = argv[0];
    *out = sp_box_bool(o.tag == SP_TAG_OBJ && o.cls_id == SP_BUILTIN_RANDOM && o.v.p && sp_Random_eq(g, (sp_Random *)o.v.p));
    return 1;
  }
  return 0;
}
