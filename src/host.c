/* host.c -- the classes and the entries of the kernel, in the host build.

   The kernel is the AOT program in the TU of the host build
   (docs/design.md, section 3.6). Each class of the kernel becomes a
   LupeClass with the id of the kernel, so a constant names it and an AOT
   object has it as its class. Each entry (tools/entry_gen.rb) becomes a
   singleton method of its module (LUPE_M_ENTRY).

   With no kernel, lupe_host_init does nothing. */

#include "lupe.h"

#ifdef LUPE_KERNEL

/* The name that the kernel gives the class id, or "" past its classes. */
static const char *lupe_host_class_name(sp_int id) {
  return lupe_kernel_hooks.class_name((sp_Class){ id, NULL });
}

/* The class of the kernel that an entry spells by the last part of its
   name (D for C::D), or NULL. */
static LupeClass *lupe_host_module(const char *mod) {
  size_t n = strlen(mod);
  for (int i = 0; i < L.nbuiltins; i++) {
    LupeClass *k = L.builtins[i];
    if (!lupe_kernel_cls(k->cls_id)) continue;
    size_t kn = strlen(k->name);
    if (!strcmp(k->name, mod) || (kn > n + 2 && !strcmp(k->name + kn - n, mod) && k->name[kn - n - 1] == ':'))
      return k;
  }
  return NULL;
}

static void lupe_host_init(void) {
  int first = L.nbuiltins;
  for (sp_int id = 0; id < LUPE_CLS_BASE && *lupe_host_class_name(id); id++) {
    sp_Class c = { id, NULL };
    int is_module = lupe_kernel_hooks.class_is_module && lupe_kernel_hooks.class_is_module(c);
    LupeClass *k = lupe_class_alloc(id, strdup(lupe_host_class_name(id)), is_module, NULL, NULL);
    lupe_push_class(&L.builtins, &L.nbuiltins, &L.builtins_cap, k);
  }
  /* the superclasses, once each class of the kernel has its LupeClass */
  for (int i = first; i < L.nbuiltins; i++) {
    LupeClass *k = L.builtins[i];
    if (k->is_module) continue;
    sp_Class s = sp_class_superclass((sp_Class){ k->cls_id, NULL });
    LupeClass *sk = s.cls_id == k->cls_id ? NULL : lupe_class_by_id(s.cls_id);
    k->super = sk ? sk : L.object;
  }
  for (int i = 0; i < LUPE_NENTRIES; i++) {
    LupeClass *k = lupe_host_module(lupe_kernel_entries[i].module);
    if (!k) continue;
    LupeMethod *m = lupe_method_new(LUPE_M_ENTRY, sp_sym_intern(lupe_kernel_entries[i].name), NULL, 0, k);
    m->entry = i;
    lupe_define_method(k, m, 1);
  }
}

/* A call of an entry: the arguments go as they are, and the wrapper
   converts each one. */
static sp_RbVal lupe_entry_invoke(int i, int argc, sp_RbVal *argv) {
  if (argc != lupe_kernel_entries[i].argc) lupe_arity_error(argc, lupe_kernel_entries[i].argc, 0, 0);
  return lupe_kernel_entries[i].fn(argv);
}

#else

static void lupe_host_init(void) {}

static sp_RbVal lupe_entry_invoke(int i, int argc, sp_RbVal *argv) {
  (void)i, (void)argc, (void)argv;
  return sp_box_nil();
}

#endif
