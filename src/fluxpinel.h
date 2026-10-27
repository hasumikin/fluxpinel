/* fluxpinel.h -- shared declarations of the Fluxpinel walker.

   Fluxpinel is one TU: main.c includes every other .c file (see fx_tu.c for
   why). This header holds the types that those files share. */

#ifndef FX_H
#define FX_H

#include "fx_tu.h"
#include "prism.h"

struct FxClass;

/* What a method does when it is called. FX_M_BUILTIN is the builtin
   method of the name, which an entry carries for its visibility only
   (`private :first` in a reopened Array, `private_class_method :new`). */
typedef enum { FX_M_DEF, FX_M_READER, FX_M_WRITER, FX_M_FFI, FX_M_ENTRY, FX_M_PROC, FX_M_BUILTIN } FxMethodKind;

/* The visibility of a method, and the default of a scope for a def. */
enum { FX_VIS_PUBLIC, FX_VIS_PRIVATE, FX_VIS_PROTECTED };

/* Why a call missed, for the words of NoMethodError (fx_no_method). */
enum { FX_MISS_METHOD, FX_MISS_VCALL, FX_MISS_PRIVATE, FX_MISS_PROTECTED };

/* The class id and the slot of the last access of one ivar. An object of
   that class id keeps the ivar at that slot, because a slot never moves.
   A cls_id below FX_CLS_BASE (0 at the start) never hits (FX_IC_HIT). */
typedef struct {
  int cls_id;
  int slot;
} FxIvarCache;

typedef struct {
  sp_sym name;
  FxMethodKind kind;
  int vis;                    /* FX_VIS_* */
  const pm_def_node_t *def;   /* FX_M_DEF */
  sp_sym ivar;                /* FX_M_READER / FX_M_WRITER: "@name" */
  FxIvarCache ic;             /* FX_M_READER / FX_M_WRITER: the slot of ivar */
  struct FxClass *owner;    /* the class or module that defines it */
  struct FxClass *lexical;  /* FX_M_DEF: the cref where the def is written, for
                                 the constants of the body; NULL for owner */
  int matches;                /* whether the body can set $~: 0 not known yet, 1 no, 2 yes */
  struct FxFfi *ffi;        /* FX_M_FFI: what ffi_func and its kin declared */
  int entry;                  /* FX_M_ENTRY: the row of fx_kernel_entries (host.c) */
  void *proc;                 /* FX_M_PROC: the FxProc of define_method; L.procs roots it */
} FxMethod;

typedef struct {
  FxMethod **items;
  int n, cap;
} FxMethodTable;

typedef struct {
  sp_sym name;
  sp_RbVal value;   /* marked by the globals hook */
} FxConst;

/* A class or a module. A builtin class (Integer, Object, ...) gets one
   too, on first use, so that a program can reopen it and so that constant
   lookup and is_a? see one hierarchy. */
typedef struct FxClass {
  sp_int cls_id;                    /* the runtime id: builtin < 0, Fluxpinel >= FX_CLS_BASE */
  sp_int inst_id;                   /* the id that an instance carries: cls_id for a Fluxpinel
                                       class; an alias >= FX_CLS_BASE for Object */
  const char *name;                 /* "A::B" */
  int is_module;
  struct FxClass *super;          /* NULL for BasicObject and for a module */
  struct FxClass *lexical;        /* the enclosing class or module, for constants */
  FxMethodTable methods;          /* instance methods */
  FxMethodTable smethods;         /* singleton methods (class methods) */
  struct FxClass **includes;      /* in include order */
  int nincludes, includes_cap;
  struct FxClass **prepends;      /* in prepend order */
  int nprepends, prepends_cap;
  struct FxClass **extends;       /* modules extended into the singleton */
  int nextends, extends_cap;
  FxConst *consts;
  int nconsts, consts_cap;
  FxConst *cvars;                   /* the class variables (@@x) of this class */
  int ncvars, cvars_cap;
  sp_sym *ivars;                    /* the slot names of an instance */
  int nivars, ivars_cap;
  int anon;                         /* made by Class.new or Struct.new and not yet
                                       assigned to a constant: name is "#<Class:0x...>" */
  int record;                       /* a class that Struct.new or Data.define made:
                                       FX_REC_STRUCT or FX_REC_DATA, else 0 */
  int keyword_init;                 /* Struct keyword_init: 1 true, -1 false, 0 nil */
  int frozen;                       /* the class value is frozen (C.freeze) */
  sp_sym *members;                  /* the members of a record class */
  int nmembers;
  int native;                       /* a native class of a package (native_struct): an
                                       instance is a C struct of the package, not a
                                       FxObject, and has no ivars */
} FxClass;

#define FX_REC_STRUCT 1
#define FX_REC_DATA 2

/* An instance of a Fluxpinel class. It starts like a spinel object (cls_id
   first), so the runtime reads its class id as it reads an AOT object's. */
typedef struct {
  sp_int cls_id;
  sp_PolyArray *iv;   /* slot i holds the ivar cls->ivars[i] */
} FxObject;

struct FxFrame;

/* The frames of one fiber that are live, outermost first, by serial. Each
   fiber that runs walker code has its own, since fibers interleave their
   frames. A stack is never freed: a block may still name it. */
typedef struct FxFrameStack {
  const void *fiber;              /* the sp_Fiber that runs these frames */
  uint64_t *serial;
  int depth, cap;
} FxFrameStack;

/* A block that a call passes: a block literal, a lambda, `&:sym` or the
   block of a Proc. A block literal lives in the C frame of its call. */
typedef struct FxBlock {
  const pm_node_t *node;          /* a block or a lambda node; NULL for &:sym */
  struct FxFrame *home;         /* the frame where the block was written */
  uint64_t serial;                /* home->serial then */
  FxFrameStack *fs;             /* the frame stack of home */
  int depth;                      /* home->depth; the home is live while
                                     fs->serial[depth] == serial */
  const struct FxBlock *site;   /* the call that break leaves; NULL for none */
  sp_sym sym;                     /* &:sym */
  int lambda;
  void *proc;                     /* the FxProc that carries the block, or NULL */
  /* a block of C: a yield calls cfn(ctx, argc, argv). It stops the call
     that it was given to with fx_cblock_stop. */
  sp_RbVal (*cfn)(void *ctx, int argc, sp_RbVal *argv);
  void *ctx;
} FxBlock;

/* A Proc value. It starts with a spinel sp_Proc, so that the runtime reads
   the fields that it knows (lambda?, frozen?). The runtime cannot call it:
   fn is NULL, and Fluxpinel answers every call of a Proc itself. */
typedef struct {
  sp_Proc p;
  FxBlock b;
} FxProc;

/* A Method value (obj.method(:name)). It starts with a spinel
   sp_BoundMethod, so that the runtime hashes and compares it by the
   receiver and the name, as a Hash key needs. Its fn is 0: Fluxpinel
   answers every call. m is the method of the program that the name
   found, or NULL for a builtin method, which a call sends by name. */
typedef struct {
  sp_BoundMethod bm;
  sp_RbVal recv;
  const FxMethod *m;
  sp_sym name;
  FxProc *proc;   /* the Proc of to_proc and of `&m`, made at the first */
} FxMethodValue;

/* The local variables and the context of one scope: the program, a class
   body, a method call or a block call. locals[i] holds the variable
   names->ids[i]; each slot is a GC root. */
typedef struct FxFrame {
  sp_RbVal *locals;
  const pm_constant_id_list_t *names;
  sp_RbVal self;              /* rooted by the caller */
  FxClass *cref;            /* the lexical class: def target and constant scope */
  FxClass *definee;         /* the def target when it is not cref: the class of
                                 class_eval or instance_eval; NULL for cref */
  const FxMethod *method;   /* the running method, for super; NULL outside one */
  int singleton_defs;         /* 1: inside class << self; 2: after module_function;
                                 3: instance_eval of an object, whose singleton
                                 methods Fluxpinel does not model; 4: instance_eval
                                 of a class, where only a def is a class method */
  int vis;                    /* the default visibility of a def here (FX_VIS_*): a
                                 block frame uses that of its scope (fx_vis_frame) */
  int vis_scope;              /* a block frame with a scope of its own: a class
                                 body of Class.new, or a define_method body */
  struct FxFrame *parent;   /* a block frame: the frame of the enclosing scope */
  const FxBlock *block;     /* the block of the method, for yield; NULL for none */
  sp_RbVal *it;               /* the slot of `it` (a rooted slot after the locals) */
  sp_RbVal *anon;             /* [0]: anonymous *rest, [1]: anonymous **kwrest, also
                                 what `...` takes (rooted slots after `it`) */
  uint64_t serial;            /* unique to this frame, for FxBlock.serial */
  int lambda;                 /* a lambda frame: return leaves it */
  int depth;                  /* the index of this frame in its frame stack */
  FxFrameStack *fs;         /* the frame stack of the fiber that runs it */
  int heap;                   /* the frame of a FxEnv */
  struct FxEnv *env;        /* a frame on the C stack: its heap copy, once a
                                 Proc keeps it; a GC root */
} FxFrame;

/* A frame that a Proc keeps, on the heap, since the Proc can outlive the
   scope. The frame on the C stack moves its slots here when the first Proc
   takes it, and runs on with them, so both see one set of variables. */
typedef struct FxEnv {
  FxFrame frame;            /* first: a pointer to the frame is one to the env */
  FxFrame *stack;           /* the frame on the C stack; valid while that is live */
  size_t nslots;
  sp_RbVal slots[];           /* the locals, `it` and the two anonymous slots */
} FxEnv;

/* A pending jump out of the statement under evaluation. A break in a
   block becomes FX_CTL_BREAK_BLOCK at the block boundary; it ends the
   call that passed the block (L.ctl_target). A return ends the frame
   L.ctl_target. A redo runs the body of the loop or the block again. */
typedef enum {
  FX_CTL_NONE, FX_CTL_RETURN, FX_CTL_BREAK, FX_CTL_NEXT, FX_CTL_BREAK_BLOCK, FX_CTL_RETRY, FX_CTL_REDO
} FxCtl;

/* A source file that Fluxpinel parsed: the main program or a file that
   require_relative loaded. Its AST and its bytes live until the end, since
   methods keep its nodes. */
typedef struct {
  const char *path;          /* as given for the main program; real path otherwise */
  const char *real;          /* the real path, by which a file loads once */
  const uint8_t *start, *end;   /* the bytes, to find the file of a node */
  pm_options_t options;      /* the parser reads its filepath from here */
  pm_parser_t parser;
  pm_node_t *root;
} FxSource;

/* The record of a prism node in the side table (docs/side-table.md). A
   node kind that keeps fields adds its struct to the union. Only the case
   of the walker for that kind reads or writes the field, so the case
   makes sure of the kind. */
/* The record of a call node: the shape of its arguments, and the method
   that the last call on an object of the program found. */
typedef struct {
  uint8_t ready;    /* dyn and fwd are set */
  uint8_t dyn;      /* fx_args_dynamic of the arguments */
  uint8_t fwd;      /* fx_has_forwarding of the arguments */
  int cls_id;       /* the class id of the last object of the program that
                       the call took as its receiver or self */
  unsigned gen;     /* fx_methods_gen of the lookup; 0 never hits */
  FxMethod *m;      /* the method of cls_id, or NULL */
} FxCallCache;

typedef struct {
  union {
    FxIvarCache ivar;   /* a node of @x: read, write, op-write, target */
    FxCallCache call;   /* a call node */
  };
} FxSide;

/* The side table is a list of chunks of FX_SIDE_CHUNK records, so that a
   record never moves when the table grows. */
#define FX_SIDE_BITS 16
#define FX_SIDE_CHUNK ((uint32_t)1 << FX_SIDE_BITS)

typedef struct {
  const char *path;
  char **args;           /* the arguments after the path, for ARGV */
  int nargs;
  pm_constant_pool_t pool;   /* the constants of every file: a node's
                                constant id indexes this one pool */
  FxSource **sources;      /* the parsed files; sources[0] is the main one */
  int nsources, sources_cap;
  uint32_t node_ids;     /* the last node id of all parses: each parse
                            continues the count of the one before */
  FxSide **side;         /* the side table: chunk i holds the records of the
                            node ids from i << FX_SIDE_BITS */
  uint32_t side_chunks;
  FxClass **classes;   /* Fluxpinel classes; classes[i] has cls_id FX_CLS_BASE + i */
  int nclasses, classes_cap;
  FxClass **builtins;  /* builtin classes made so far */
  int nbuiltins, builtins_cap;
  FxClass *object;     /* Object: top-level def and constants go here */
  sp_RbVal main;         /* the top-level self; a GC root */
  FxCtl ctl;
  sp_RbVal ctl_val;      /* the value that the jump carries; a GC root */
  const void *ctl_target;   /* FX_CTL_RETURN: a FxFrame; FX_CTL_BREAK_BLOCK: a FxBlock */
  uint64_t serial;       /* the last FxFrame.serial */
  const void *kwhash;    /* the Hash that a call passes as keywords (`m(a: 1)`), so
                            that a Hash literal in the last place stays positional */
  int vcall;             /* the next fx_call_self is a bare name (`foo`), whose
                            miss is a NameError */
  FxFrameStack *fs;    /* the frame stack of the running fiber. A frame pops
                            its entry when it ends; a rescue cuts the entries of
                            the frames that a raise unwound */
  FxFrameStack *main_fs;   /* the stack of the frames of a root fiber */
  struct { const void *fiber; FxFrameStack *fs; } *fstacks;   /* by fiber */
  int nfstacks, fstacks_cap;
  FxConst *globals;    /* the global variables that the program assigned;
                            marked by the mark hook */
  int nglobals, globals_cap;
  void **procs;          /* the FxProc of each define_method, also of a method
                            that a later definition replaced, as a call of it
                            can still run; marked by the mark hook */
  int nprocs, procs_cap;
  int mm_why;            /* why the call of the running method_missing hook
                            missed (FX_MISS_*), for a super from the hook */
} Fluxpinel;

static Fluxpinel L;

static __attribute__((noreturn)) void fx_side_broken(uint32_t id);

/* The record of the node n in the side table. */
static inline FxSide *fx_side(const pm_node_t *n) {
  uint32_t id = n->node_id;
  if (__builtin_expect(id == 0 || id > L.node_ids, 0)) fx_side_broken(id);
  return &L.side[id >> FX_SIDE_BITS][id & (FX_SIDE_CHUNK - 1)];
}

static sp_RbVal fx_eval(FxFrame *f, const pm_node_t *n);
static sp_RbVal fx_send(sp_RbVal recv, const char *name, int argc, sp_RbVal *argv);
static sp_RbVal fx_send_b(sp_RbVal recv, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk);
static sp_RbVal fx_send_v(sp_RbVal recv, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                            int check, const sp_RbVal *caller);
static sp_RbVal fx_call_self(FxFrame *f, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk);
static sp_RbVal fx_yield(const FxBlock *b, int argc, sp_RbVal *argv);
static sp_RbVal fx_block_proc(const FxBlock *b);
static __attribute__((noreturn)) void fx_arity_error(int argc, int nreq, int nopt, int rest);
static int fx_iter(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk, sp_RbVal *out);
static int fx_proc_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out);
static int fx_range_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out);
static int fx_hash_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                          sp_RbVal *out);
static int fx_array_hash(sp_RbVal r, const char *name, int argc, sp_RbVal *out);
static int fx_array_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                           sp_RbVal *out);
static int fx_string_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                            sp_RbVal *out);
static int fx_builtin_call(sp_RbVal recv, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                             sp_RbVal *out);
static FxClass *fx_class_named(const char *name);
static int fx_prelude_call(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                           sp_RbVal *out);   /* prelude.c */
static int fx_prelude_has(sp_RbVal r, const char *name);
static void fx_prelude_gap(const char *name, int argc, sp_RbVal *argv);
static int fx_is_string(sp_RbVal v);
static int fx_fast_call(sp_RbVal r, sp_sym s, int argc, sp_RbVal *argv, sp_RbVal *out);   /* builtin.c */
static int fx_integer_class(const FxClass *k, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out);
static int fx_fast_eqq(sp_RbVal v, sp_RbVal s, sp_RbVal *out);
/* iter.c: Method values */
static int fx_method_value_new(sp_RbVal r, sp_RbVal name, int public_only, sp_RbVal *out);
static int fx_is_method_value(sp_RbVal v);
static const FxBlock *fx_method_value_block(sp_RbVal v);
static sp_RbVal fx_method_value_step(void *ctx, int argc, sp_RbVal *argv);
static int fx_method_value_arity(const FxMethodValue *mv);
static int fx_method_value_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                                  sp_RbVal *out);

/* exc.c */
static int fx_is_exc(sp_RbVal v);
static sp_RbVal fx_exc_box(sp_Exception *e);
static int fx_is_exc_class(const FxClass *k);
static sp_RbVal fx_exc_new(FxClass *k, int argc, sp_RbVal *argv, const FxBlock *blk);
static void fx_exc_init(sp_RbVal v, int argc, sp_RbVal *argv);
static int fx_exc_kernel(const char *name, int argc, sp_RbVal *argv, sp_RbVal *out);
static int fx_exc_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out);
static int fx_exc_matches(const char *cls, sp_RbVal ev, sp_RbVal target);

/* re.c */
static sp_RbVal fx_re_literal(FxFrame *f, const pm_node_t *n);
static sp_RbVal fx_re_last(void);
static const char *fx_gvar_canon(const char *name);
static FxConst *fx_gvar_slot(sp_sym s);
static sp_RbVal fx_re_nth(int n);
static sp_RbVal fx_re_backref(const char *g);
static sp_RbVal fx_re_match_write(FxFrame *f, const pm_match_write_node_t *w);
static int fx_re_frame_enter(const FxMethod *m, sp_re_frame *rf);
static int fx_re_call(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                        sp_RbVal *out);

/* struct.c */
static const FxClass *fx_record_of(const FxClass *k);
static void fx_record_init(sp_RbVal self, int argc, sp_RbVal *argv);
static const char *fx_record_inspect(sp_RbVal self);
static int fx_struct_class(FxFrame *f, FxClass *k, const char *name, int argc, sp_RbVal *argv,
                             const FxBlock *blk, sp_RbVal *out);
static int fx_record_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                            sp_RbVal *out);
static int fx_record_responds(sp_RbVal r, const char *name);

/* fiber.c */
static int fx_fiber_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                           sp_RbVal *out);
static int fx_fiber_kernel(const char *name, int argc, sp_RbVal *argv, sp_RbVal *out);
static void fx_sched_end(void);

/* ffi.c */
static int fx_ffi_class(FxClass *k, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out);
static sp_RbVal fx_ffi_invoke(const struct FxFfi *x, sp_RbVal self, int argc, sp_RbVal *argv);

/* process.c */
static void fx_process_consts(FxClass *c);
static int fx_process_class(const FxClass *k, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out);
static int fx_process_responds(sp_RbVal r, const char *name);

/* file.c */
static int fx_file_class(const FxClass *k, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                         sp_RbVal *out);
static int fx_file_responds(sp_RbVal r, const char *name);

/* time.c */
static int fx_time_class(const FxClass *k, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out);
static int fx_time_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out);

/* complex.c */
static sp_RbVal fx_kernel_complex(int argc, sp_RbVal *argv, int raise);
static int fx_complex_class(const FxClass *k, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out);
static int fx_complex_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out);
static int fx_to_c(sp_RbVal r, const char *name, int argc, sp_RbVal *out);

/* encoding.c */
static int fx_encoding_const(FxClass *enc, sp_sym name, sp_RbVal *out);
static int fx_encoding_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out);
static int fx_encoding_class(const FxClass *k, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out);

/* host.c */
static void fx_host_init(void);
static sp_RbVal fx_entry_invoke(int i, int argc, sp_RbVal *argv);

/* load.c */
static int fx_load_call(const pm_node_t *n, sp_RbVal self, const char *name, int argc, sp_RbVal *argv,
                          sp_RbVal *out);
static int fx_autoload(sp_sym name);

/* RUBY_DESCRIPTION of Fluxpinel, which `fluxpinel -v` prints: the version
   of Fluxpinel, the commit of spinel and the version of Ruby that spinel
   gives (the Makefile). */
#define FX_DESCRIPTION \
  "fluxpinel " FX_VERSION " (spinel " FX_SPINEL_REV ", ruby " FX_RUBY_VERSION ") [" SP_RUBY_ARCH "-" SP_RUBY_OS "]"

/* responds.c */
static int fx_rs_responds(sp_RbVal r, const char *mn);
static int fx_rs_known(sp_RbVal r, const char *mn);
static int fx_rs_own(sp_RbVal r, const char *mn);
static int fx_rs_instance(const char *cls, const char *mn);
static int fx_rs_defined(const FxClass *k, const char *mn, int inherit);

/* global.c */
static sp_RbVal fx_gvar_eval(FxFrame *f, const pm_node_t *n);
static int fx_gvar_stdout(sp_RbVal *out);
static sp_RbVal fx_gvar_get(const char *name);
static sp_RbVal fx_io_select(int argc, sp_RbVal *argv);
static void fx_gvar_set(const char *name, sp_RbVal v);
static int fx_io_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out);
static int fx_env_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                         sp_RbVal *out);
static int fx_is_proc(sp_RbVal v);
static int fx_is_range(sp_RbVal v);
static int fx_is_hash(sp_RbVal v);
static int fx_kernel(const char *name, int argc, sp_RbVal *argv, sp_RbVal *out);
static void fx_trace_missing(const char *owner, const char *name, int argc);
static int fx_builtin_responds(sp_RbVal r, const char *name);
static int fx_enum_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                        sp_RbVal *out);
static __attribute__((noreturn)) void fx_unsupported(const pm_node_t *n, const char *what);
static __attribute__((noreturn)) void fx_gap(const char *what);
static sp_sym fx_sym(pm_constant_id_t id);

/* object.c */
static FxClass *fx_class_of(sp_RbVal v);
static FxClass *fx_class_value(sp_RbVal v);
static sp_RbVal fx_class_box(const FxClass *c);
static FxMethod *fx_find_method(const FxClass *c, sp_sym name);
static FxMethod *fx_find_smethod(const FxClass *c, sp_sym name);
static FxMethod *fx_find_super(const FxClass *c, const FxMethod *m);
static sp_RbVal fx_invoke(const FxMethod *m, sp_RbVal self, int argc, sp_RbVal *argv);
static sp_RbVal fx_invoke_b(const FxMethod *m, sp_RbVal self, int argc, sp_RbVal *argv, const FxBlock *blk);
static int fx_object_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out);
static int fx_class_core(FxFrame *f, sp_RbVal r, const char *name, int argc, sp_RbVal *argv,
                           const FxBlock *blk, sp_RbVal *out);
static int fx_is_fx_object(sp_RbVal v);
static int fx_unknown_builtin(const char *name);

#endif
