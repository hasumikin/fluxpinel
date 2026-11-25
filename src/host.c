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

/* Whether the kernel's class of the name is a builtin class the walker
   has of its own, which the kernel reopened (Time, Set): the walker's
   class answers the name, as a reopened builtin of a program does, and
   the kernel's entry would hide it (fx_builtin makes a builtin class at
   its first use, so the first class of the name in L.builtins wins). */
static int fx_host_builtin_name(const char *name) {
  /* the autoloads of Object (fx_global_init, which runs after this) */
  static const char *const autoloads[] = { "Set", "RbConfig", "Gem", "ThreadGroup", "ObjectSpace", "MonitorMixin" };
  for (size_t i = 0; i < sizeof autoloads / sizeof autoloads[0]; i++)
    if (!strcmp(name, autoloads[i])) return 1;
  return fx_builtin_id(name) != 0 || sp_exc_parent_of_name(name) != NULL;
}

static void fx_host_init(void) {
  int first = L.nbuiltins;
  for (sp_int id = 0; id < FX_CLS_BASE && *fx_host_class_name(id); id++) {
    if (fx_host_builtin_name(fx_host_class_name(id))) continue;
    sp_Class c = { id, NULL };
    int is_module = fx_kernel_hooks.class_is_module && fx_kernel_hooks.class_is_module(c);
    FxClass *k = fx_class_alloc(id, fx_name_dup(fx_host_class_name(id)), is_module, NULL, NULL);
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
  /* the methods of the table of --ext-class (sp_ext_methods): each class
     defines the rows it owns, and the ancestors of Fluxpinel reach the
     inherited ones, except a row of an ancestor that has no table of its
     own (a class the flag does not name), which the class defines itself;
     an entry of the name keeps its wrapper, which converts a Hash to the
     kind of the parameter */
  for (int i = first; i < L.nbuiltins; i++) {
    FxClass *k = L.builtins[i];
    for (const sp_ext_method *row = sp_ext_methods((int)k->cls_id); row && row->name; row++) {
      if (row->owner != (int)k->cls_id && sp_ext_methods(row->owner)) continue;
      /* a method that takes a block has its rows twice, for a call with no
         block and for a call with one: the first set gives the counts */
      if (row->block) continue;
      sp_sym name = sp_sym_intern(row->name);
      /* a method with optional or rest parameters has a row for each count
         of arguments, and a negative count for "that many or more": the
         rows of a name are one method, whose arity is -(n + 1) for the
         fewest arguments n, as Method#arity counts optional arguments */
      /* `new` of a class: the rows are the counts that its initialize
         takes, and Method#arity answers -1, as Class#new has in CRuby */
      int is_new = row->singleton && !strcmp(row->name, "new");
      FxMethod *had = fx_table_get(row->singleton ? &k->smethods : &k->methods, name);
      if (had && had->kind == FX_M_EXTM && had->owner == k) {
        int a = had->argc < 0 ? -had->argc - 1 : had->argc, b = row->argc < 0 ? -row->argc - 1 : row->argc;
        if (!is_new) had->argc = -((a < b ? a : b) + 1);
        continue;
      }
      if (row->singleton && had) continue;
      FxMethod *m = fx_method_new(FX_M_EXTM, name, NULL, 0, k);
      m->argc = is_new ? -1 : row->argc;
      fx_define_method(k, m, row->singleton);
    }
  }
}

/* The block of a script that a call of a method of the table passes. The
   kernel gets a Proc of the runtime whose fn yields to the block, and this
   record is the cap of that Proc.

   A jump out of the block (a break, or a return of the method around the
   call) must leave the frames of the kernel first. The call opens a break
   scope of the runtime (sp_brk_push). fn keeps the jump here and breaks to
   the scope, so the ensure clauses of the kernel run. fx_extm_invoke lands
   and puts the jump back, and the walker goes on with it as after a block
   of a builtin method. */
typedef struct {
  FxProc *proc;          /* the block of the script */
  sp_int scope;          /* the serial of the break scope of the call */
  FxCtl ctl;             /* the jump that left the block */
  const void *target;
} FxExtmBlock;

static void fx_extm_block_scan(void *p) { sp_gc_mark(((FxExtmBlock *)p)->proc); }

/* Whether the break scope of the call is live in the running thread. It
   is not when the kernel kept the Proc and calls it after the call, or
   calls it in another thread. */
static int fx_extm_scope_live(sp_int scope) {
  for (int i = sp_brk_top - 1; i >= 0; i--)
    if (sp_brk_serial[i] == scope) return 1;
  return 0;
}

/* The yield, under the walker lock. A jump that is left pending moves to
   the record: another thread of the walker runs while this one leaves the
   kernel. Only the thread of the call writes the record (live). Answers
   the value of the block or of the jump, and the jump in *ctl. */
static sp_RbVal fx_extm_block_yield(FxExtmBlock *xb, int live, int argc, sp_RbVal *argv, FxCtl *ctl) {
  sp_RbVal r = fx_yield(&xb->proc->b, argc, argv);
  *ctl = L.ctl;
  if (L.ctl == FX_CTL_NONE) return r;
  if (live) {
    xb->ctl = L.ctl;
    xb->target = L.ctl_target;
  }
  L.ctl = FX_CTL_NONE;
  return L.ctl_val;
}

/* The fn of the Proc: the kernel calls it with no walker lock, from the
   thread of the call or from any other. A jump breaks to the scope of the
   call. With no live scope the jump has no call to end, and the answer is
   LocalJumpError, as in CRuby. */
static sp_int fx_extm_block_fn(void *cap, sp_int argc, sp_int *args) {
  (void)args;
  SP_GC_SAVE();
  FxExtmBlock *xb = (FxExtmBlock *)cap;
  SP_GC_ROOT(xb);
  /* the channel of the arguments serves the next call of a Proc too */
  sp_RbVal argv[SP_PROC_ARG_SLOTS];
  int n = argc > SP_PROC_ARG_SLOTS ? SP_PROC_ARG_SLOTS : (int)argc;
  for (int i = 0; i < n; i++) {
    argv[i] = sp_proc_arg(i);
    _sp_gc_root_push((void **)((uintptr_t)&argv[i] | (uintptr_t)1));
  }
  const int live = fx_extm_scope_live(xb->scope);
  sp_RbVal r = sp_box_nil();
  FxCtl ctl = FX_CTL_NONE;
  FX_LOCKED(r = fx_extm_block_yield(xb, live, n, argv, &ctl));
  if (ctl == FX_CTL_RETURN && !live) sp_raise_cls("LocalJumpError", "unexpected return");
  /* the runtime raises LocalJumpError (break from proc-closure) for a
     scope that is not live */
  if (ctl != FX_CTL_NONE) sp_brk_throw(xb->scope, r);
  sp_proc_ret_set(r);
  return 0;
}

/* A call with a block: the method runs in a break scope, where a jump out
   of the block lands. */
static sp_RbVal fx_extm_invoke_block(sp_RbVal self, const char *name, int argc, sp_RbVal *argv,
                                     const FxBlock *blk) {
  SP_GC_SAVE();
  sp_RbVal pv = fx_block_proc(blk);
  SP_GC_ROOT_RBVAL(pv);
  FxExtmBlock *xb = (FxExtmBlock *)sp_gc_alloc(sizeof *xb, NULL, fx_extm_block_scan);
  xb->proc = (FxProc *)pv.v.p;
  xb->ctl = FX_CTL_NONE;
  SP_GC_ROOT(xb);
  sp_RbVal b = sp_box_proc(sp_proc_new((void *)fx_extm_block_fn, xb, sp_bm_cap_scan));
  SP_GC_ROOT_RBVAL(b);
  sp_RbVal r = sp_box_nil();
  SP_GC_ROOT_RBVAL(r);
  const int depth = fx_fs()->depth, lockmark = fx_lock_depth();
  const int exc_top = sp_exc_top, catch_top = sp_catch_top, nroots = sp_gc_nroots;
  void *const cause = sp_inflight_cause;
  const int rescue_sp = sp_rescue_sp;
  xb->scope = sp_brk_push();
  const int slot = sp_brk_top;
  if (setjmp(sp_brk_stack[slot - 1]) == 0) {
    int found;
    FX_UNLOCKED(found = sp_ext_send_block(self, name, argc, argv, b, &r));
    sp_brk_top = slot - 1;
    if (!found) fx_no_method_args(self, name, argc, argv, FX_MISS_METHOD);
    return r;
  }
  /* the landing of a jump: the state of the runtime and of the walker as
     at the call, as the landing of a catch has it. The value of the jump
     stays in the scope, which the GC marks, until the walker lock is back:
     a lock can park and collect. */
  sp_exc_top = exc_top;
  sp_catch_top = catch_top;
  sp_brk_top = slot;
  sp_gc_nroots = nroots;
  sp_inflight_cause = cause;
  sp_rescue_sp = rescue_sp;
  fx_lock_set_depth(lockmark);
  fx_fs()->depth = depth;
  L.kwhash = NULL;
  L.ctl = xb->ctl;
  L.ctl_target = xb->target;
  L.ctl_val = sp_brk_val[slot - 1];
  sp_brk_top = slot - 1;
  return sp_box_nil();
}

/* A call of a method of the table: the thunk converts self and the
   arguments (ArgumentError and TypeError are its), and the kernel runs
   without the walker lock, as an entry does. */
static sp_RbVal fx_extm_invoke(const FxMethod *m, sp_RbVal self, int argc, sp_RbVal *argv, const FxBlock *blk) {
  const char *name = sp_sym_to_s(m->name);
  /* a class of the script under a class of the kernel inherits the method,
     and the table has no row for it (`new` of such a class, a method of
     its object) */
  if ((self.tag == SP_TAG_OBJ || self.tag == SP_TAG_CLASS) && !fx_kernel_cls(self.cls_id))
    fx_gap(sp_sprintf("%s of a class of the kernel, called on a class of the script", name));
  if (blk) return fx_extm_invoke_block(self, name, argc, argv, blk);
  sp_RbVal r = sp_box_nil();
  int found;
  FX_UNLOCKED_ROOT(found = sp_ext_send(self, name, argc, argv, &r), SP_GC_ROOT_RBVAL(r));
  if (!found) fx_no_method_args(self, name, argc, argv, FX_MISS_METHOD);
  return r;
}

/* A call of an entry: the arguments go as they are, and the wrapper
   converts each one. */
static sp_RbVal fx_entry_invoke(int i, int argc, sp_RbVal *argv) {
  if (argc != fx_kernel_entries[i].argc) fx_arity_error(argc, fx_kernel_entries[i].argc, 0, 0);
  /* the kernel runs without the walker lock: an entry can run long (a
     server), and it calls Fluxpinel again from any of its threads */
  sp_RbVal r;
  FX_UNLOCKED_ROOT(r = fx_kernel_entries[i].fn(argv), SP_GC_ROOT_RBVAL(r));
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
  /* a path of the files in the binary (EMBED=1) is one of the table */
  char *real = fx_real_path(path);
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

static sp_RbVal fx_extm_invoke(const FxMethod *m, sp_RbVal self, int argc, sp_RbVal *argv, const FxBlock *blk) {
  (void)m, (void)self, (void)argc, (void)argv, (void)blk;
  return sp_box_nil();
}

#endif
