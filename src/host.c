/* host.c -- the classes and the entries of the kernel, in the host build.

   The kernel is the AOT program in the TU of the host build
   (docs/design.md, section 3.6). Each class of the kernel becomes a
   FxClass with the id of the kernel, so a constant names it and an AOT
   object has it as its class. Each entry (tools/entry_gen.rb) becomes a
   singleton method of its module (FX_M_ENTRY).

   With no kernel, fx_host_init does nothing. */

#include "fluxpinel.h"

#ifdef FX_KERNEL

/* The name that the kernel gives the class id, or "" past its classes. */
static const char *fx_host_class_name(sp_int id) {
  return fx_kernel_hooks.class_name((sp_Class){ id, NULL });
}

/* The class of the kernel that an entry spells by the last part of its
   name (D for C::D), or NULL. */
static FxClass *fx_host_module(const char *mod) {
  size_t n = strlen(mod);
  for (int i = 0; i < L.nbuiltins; i++) {
    FxClass *k = L.builtins[i];
    if (!fx_kernel_cls(k->cls_id)) continue;
    size_t kn = strlen(k->name);
    if (!strcmp(k->name, mod) || (kn > n + 2 && !strcmp(k->name + kn - n, mod) && k->name[kn - n - 1] == ':'))
      return k;
  }
  return NULL;
}

static void fx_host_init(void) {
  int first = L.nbuiltins;
  for (sp_int id = 0; id < FX_CLS_BASE && *fx_host_class_name(id); id++) {
    sp_Class c = { id, NULL };
    int is_module = fx_kernel_hooks.class_is_module && fx_kernel_hooks.class_is_module(c);
    FxClass *k = fx_class_alloc(id, strdup(fx_host_class_name(id)), is_module, NULL, NULL);
    fx_push_class(&L.builtins, &L.nbuiltins, &L.builtins_cap, k);
  }
  /* the superclasses, once each class of the kernel has its FxClass */
  for (int i = first; i < L.nbuiltins; i++) {
    FxClass *k = L.builtins[i];
    if (k->is_module) continue;
    sp_Class s = sp_class_superclass((sp_Class){ k->cls_id, NULL });
    FxClass *sk = s.cls_id == k->cls_id ? NULL : fx_class_by_id(s.cls_id);
    k->super = sk ? sk : L.object;
  }
  fx_methods_gen++;   /* the ancestors changed */
  for (int i = 0; i < FX_NENTRIES; i++) {
    FxClass *k = fx_host_module(fx_kernel_entries[i].module);
    if (!k) continue;
    FxMethod *m = fx_method_new(FX_M_ENTRY, sp_sym_intern(fx_kernel_entries[i].name), NULL, 0, k);
    m->entry = i;
    fx_define_method(k, m, 1);
  }
}

/* A call of an entry: the arguments go as they are, and the wrapper
   converts each one. */
static sp_RbVal fx_entry_invoke(int i, int argc, sp_RbVal *argv) {
  if (argc != fx_kernel_entries[i].argc) fx_arity_error(argc, fx_kernel_entries[i].argc, 0, 0);
  /* the kernel runs without the walker lock: an entry can run long (a
     server), and it calls Fluxpinel again from any of its threads */
  sp_RbVal r;
  FX_UNLOCKED(r = fx_kernel_entries[i].fn(argv));
  return r;
}

/* ---- the calls of the kernel into Fluxpinel (kernel/fluxpinel.rb) ----

   AOT code calls these by name (native_func), from any of its threads:
   each runs under the walker lock (FX_LOCKED). The kernel runs its top
   level before Fluxpinel starts, so a call from there is an error. */

static void fx_api_check(void) {
  if (!L.object) sp_raise_cls("RuntimeError", "Fluxpinel has not started: call Fluxpinel from an entry");
}

/* Runs a new source at the top level and answers the value of its last
   statement. The source stays: a method of it can run later. */
static sp_RbVal fx_api_run(const char *path, char *buf, size_t len) {
  FxSource *s;
  char *real = realpath(path, NULL);
  const char *err = fx_parse(strdup(path), real ? real : strdup(path), buf, len, &s);
  if (err) sp_raise_cls("SyntaxError", err);
  return fx_run_source_as(s, 1);
}

/* Fluxpinel.load(path): Kernel#load, but it answers the value of the file. */
sp_RbVal fx_api_load(const char *path) {
  fx_api_check();
  size_t len;
  char *buf = fx_read_file(path, &len);
  if (!buf) sp_raise_load_error(path);
  sp_RbVal r;
  FX_LOCKED(r = fx_api_run(path, buf, len));
  return r;
}

/* Fluxpinel.eval(src): with no binding, so the locals of the caller are not
   seen. */
sp_RbVal fx_api_eval(const char *src) {
  fx_api_check();
  size_t len = sp_str_byte_len(src);
  char *buf = malloc(len + 1);
  memcpy(buf, src, len);
  buf[len] = 0;
  sp_RbVal r;
  FX_LOCKED(r = fx_api_run("(eval)", buf, len));
  return r;
}

/* Fluxpinel.call(obj, name, args): args is an Array, or nil for none. */
sp_RbVal fx_api_call(sp_RbVal obj, const char *name, sp_RbVal args) {
  fx_api_check();
  SP_GC_ROOT_RBVAL(obj);
  SP_GC_ROOT_RBVAL(args);
  SP_GC_ROOT_STR(name);
  int argc = args.tag == SP_TAG_NIL ? 0 : (int)sp_poly_length(args);
  if (args.tag != SP_TAG_NIL && !fx_is_array(args))
    sp_raise_cls("TypeError", sp_sprintf("no implicit conversion of %s into Array", sp_poly_class_name(args)));
  sp_RbVal argv[argc > 0 ? argc : 1];
  for (int i = 0; i < argc; i++) argv[i] = sp_poly_arr_get(args, i);
  sp_RbVal r;
  FX_LOCKED(r = fx_send(obj, name, argc, argv));
  return r;
}

#else

static void fx_host_init(void) {}

static sp_RbVal fx_entry_invoke(int i, int argc, sp_RbVal *argv) {
  (void)i, (void)argc, (void)argv;
  return sp_box_nil();
}

#endif
