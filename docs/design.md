# Lupe design

## 1. Goals

Lupe gives spinel programs an interpreted mode. The interpreted code and the
AOT code share one value representation, one heap and one set of builtin
semantics.

Lupe serves two uses:

- **Oracle (use 2).** Lupe runs a program as a reference. It does no type
  inference and no optimization. A difference between Lupe and the AOT
  binary points to the analysis or the codegen of spinel.
- **Embedded REPL (use 1).** An AOT binary links Lupe. A user connects over
  a serial line or a socket. The REPL reads live objects, calls AOT methods
  and replaces methods that go through a method table.

The oracle comes first. It needs no change to spinel, and it gives the
walker a test harness. The embedded REPL reuses the same walker.

## 2. Constraints

- Lupe does not modify spinel. The spinel maintainer keeps a VM and `eval`
  out of the spinel design (matz/spinel#41, matz/spinel#36). Lupe lives
  outside that design, as a separate project.
- Lupe pins one spinel commit. The runtime API of spinel is internal and
  changes often. The pin makes each update an explicit step.
- Lupe links `libspinel_rt` (the C runtime in `spinel/lib/`). Lupe does not
  link the spinel compiler at run time.

## 3. Architecture

```
            +---------------------------+
  .rb  -->  | libprism  (parse)         |
            +---------------------------+
                        |
                        v
            +---------------------------+
            | Lupe walker               |
            |  - AST evaluator          |
            |  - method tables          |
            |  - builtin dispatch (gen) |
            +---------------------------+
                        |  sp_RbVal
                        v
            +---------------------------+      +----------------------+
            | libspinel_rt              | <--- | AOT code (spinel -c) |
            |  sp_poly_*, GC, heap      |      |  --ext-entry exports |
            +---------------------------+      +----------------------+
```

### 3.1 Values

The walker uses `sp_RbVal` as its only value type:

```c
typedef struct { int tag; int cls_id; union { sp_int i; const char *s; sp_float f; sp_int b; void *p; } v; } sp_RbVal;
```

The tags are INT, STR, FLT, BOOL, NIL, OBJ, SYM, CLASS, ENCODING and BIGINT
(`lib/sp_gc.h`). The walker roots each live value with
`SP_GC_ROOT_RBVAL`. The walker stack is then visible to the spinel GC.

### 3.2 Builtin dispatch

The spinel runtime has no `send` by name. The compiler owns the map from a
method name to C code. The map is `bop_rows[]` in `src/builtin_ops.c`.

Lupe generates part of its builtin dispatch from that table at build time
(`tools/bop_gen.c`):

- A `BOPE_TEMPLATE` row holds a C expression, for example
  `sp_str_center($r, $i0)`. The generator turns each such row into one
  wrapper per receiver variant:
  `sp_RbVal lupe_bop_N(sp_RbVal recv, sp_RbVal *argv)`.
- The wrapper unboxes and boxes with the spellings of `ty_traits[]`
  (`src/types.c`). This table gives the C type, the box and the unbox of
  each builtin kind. Thus the wrapper converts values as the AOT code does.
- A row for every Array kind or every Hash kind gives one wrapper for each
  concrete kind (`sp_IntArray`, `sp_StrIntHash` and others).
- A row with a special emitter (`BOPE_ARRAY_SORT` and others) or with no
  emitter (`BOPE_NONE`) has its logic in the codegen C code. Lupe writes
  these methods by hand.

The generator reads the spinel source at the pinned commit. This keeps the
builtin semantics of Lupe equal to the AOT output.

Several rows can share one receiver and one name. They differ in argument
count or argument kinds (`arg0`, `arg1`). The dispatcher selects a row by
`argc_min`/`argc_max` and by the tags of the arguments.

The generated wrappers cover the long tail: Rational, Complex, Time, IO,
MatchData, Range and others. The core of Array, Hash and String is mostly
outside the table. Lupe writes these methods by hand:

- element access and operators (`[]`, `[]=`, `==`, `+`, `length`, `push`),
- methods with a block (`each`, `map`, `select`, `inject`). The walker
  must call its own blocks, so these methods are walker-native anyway.
- bang methods on String (`sub!`, `upcase!`). They change the receiver
  through the STRBUF handle.

### 3.3 User classes

Spinel compiles each user class to a C struct. The `cls_id` of a class
indexes per-program tables that codegen emits (`sp_class_names[]` and
others). A class that the walker defines has no place in those tables.

Lupe gives walker classes their own object kind:

- a `cls_id` range that the AOT tables do not use,
- a generic ivar table per object,
- a method table per class.

### 3.4 Containers

Spinel has 12 typed containers, for example `sp_IntArray`, `sp_StrIntHash`
and `sp_PolyArray`. The walker creates only the poly forms. A value from AOT
code can hold any of the 12 forms. The walker reads all 12 forms.

### 3.5 Boundary to AOT code

- **Walker to AOT.** Spinel exports a method with `--ext-init NAME` and
  `--ext-entry Mod.m`. Lupe registers each export in its method table.
  An `untyped` parameter in RBS gives an `sp_RbVal` slot, so a value
  crosses by reference.
- **AOT to walker.** Out of scope. The poly dispatch of AOT code is a
  `switch` over the classes of that program. An unknown `cls_id` raises
  `NoMethodError`. This direction needs a hook in spinel.

## 4. Use 2: oracle

The oracle runs one program three ways and compares the output.

| CRuby vs Lupe | Lupe vs AOT | Probable cause |
|---|---|---|
| same | same | no defect |
| same | differ | spinel analysis or codegen |
| differ | same | spinel runtime builtin |
| differ | differ | Lupe, or more than one cause |

The test programs come from the spinel test suite. The harness also reports
each program that Lupe cannot run. That list measures the coverage of Lupe.

`spinel bisect` searches the optimization decisions inside one compile.
The oracle sits outside that: it gives a result with no decisions at all.

## 5. Use 1: embedded REPL

Requirements beyond the oracle:

- **Object layout.** The REPL must read the ivars of an AOT object. Spinel
  must give the struct layout of each class. Possible sources are a
  metadata file from spinel, or the generated C.
- **Transport.** A serial line on a microcontroller. A socket on a host.
- **Redefinition.** The REPL replaces a method only through a method table.
  AOT code that calls a method directly keeps the old method.
- **Footprint.** The walker and libprism must fit next to the AOT code on
  the target board.

## 6. Prior art

- **suppify** (bash0C7/suppify). It wraps spinel output as a C library, a
  CRuby gem or a PicoRuby mrbgem. Each library has its own spinel heap.
  Values cross by copy (scalars, or MessagePack). Lupe differs: one heap,
  values cross by reference.
- **R2P2-darwin PR #13** (bash0C7/R2P2-darwin). It calls a spinel kernel
  from the PicoRuby REPL through suppify.
- **`crystal i`.** Crystal keeps its semantic analysis and replaces LLVM
  codegen with a bytecode interpreter.
- **nicht.** Iced (AOT) and naked (walker) code meet at the method table
  and share one value word. Lupe applies the same idea to spinel.

## 7. Facts about the spinel runtime

These facts apply to spinel commit `ffac8340d`.

- `lib/` holds about 47k lines of C. `lib/spinel_rt.h` holds about 21k
  lines, mostly `static inline` functions.
- The headers declare about 785 functions that take or return `sp_RbVal`.
  264 of them are `sp_poly_*`.
- Many `sp_poly_*` names exist for codegen optimizations, for example
  `_aon_cold` and `_become_handle_enc`. They are not a public API.
- `bop_rows[]` has about 1692 rows. 931 rows are `BOPE_TEMPLATE`. About
  100 kinds use a special emitter.
- The runtime has function-pointer hooks into generated code, for example
  `sp_class_name_fn` and `sp_exc_bivar_copy_fn`.
- Spinel has no mode that forces every slot to poly.
- `libspinel_rt.a` links with no generated TU. A hand-written TU supplies
  the symbol table, the builtin class names, the user exception table and
  the init hooks (`poc/m0/lupe_tu.c`).
- `spinel_rt.h` declares hooks such as `sp_class_to_s` and `sp_sym_to_s`
  as `static`. Each TU that includes the header must define them. Thus all
  Lupe code that uses the runtime is one TU (a unity build).
- A poly String has two forms. A literal is `SP_TAG_STR`. A new String is a
  mutable handle: `SP_TAG_OBJ` with `cls_id` `SP_BUILTIN_STRBUF` (-40). The
  walker treats both forms as String.
- The integer overflow mode is a compile-time macro
  (`SP_INT_OVERFLOW_MODE_RAISE`, `_WRAP` or `_PROMOTE`). The default is
  `raise`. Lupe uses the mode of the AOT build that it runs with.
- Exceptions use the `setjmp` frame that spinel emits for `begin`/`rescue`
  (`sp_exc_stack`, `sp_exc_top`, `sp_last_exc_cls`). Lupe code can use the
  same frame.
- A host binary of the runtime alone is about 1.4 MB of text (x86_64,
  `-O2`). Most of it is `static inline` code from `spinel_rt.h`.
- `bop_rows[]` has 2035 rows: 958 `BOPE_TEMPLATE`, 183 with a special
  emitter and 894 `BOPE_NONE`.
- The 958 template rows give 1204 wrappers (row times receiver variant).
  `poc/m1` measures them:

  | result | wrappers | cause |
  |---|---|---|
  | compile and link | 1072 | |
  | skip | 123 | a fact of the compiler (see below) |
  | compile error | 6 | the result kind of the row does not match the C type of the template |
  | link error | 3 | Thread methods are in `libspinel_rt_mt.a` only |

- The skipped rows need one of these facts of the compiler:
  - an argument of no single kind (`$e` with `arg0` 0): 46,
  - a result kind that the analysis derives (`BOPR_*`, `TY_UNKNOWN`): 36,
  - a temp that the method family takes before its arms (`$T`): 31,
  - other placeholders (`$[`, `$R`, `$F`): 10.
- The wrappers cover 734 of the 1266 builtin methods (58%). By family:
  Rational 94%, Complex 96%, IO 73%, Integer 62%, String 44%, Array 22%,
  Hash 15%.
- Some runtime helpers answer `sp_oint` where the row says Integer
  (`IO#getbyte`). The wrapper boxes by the C type with `_Generic`.
- `String#crypt` needs `-lcrypt` at link time.

## 8. Open questions

- The smoke test in `poc/m1` checks 6 wrappers. How does Lupe check the
  other wrappers? The oracle (M3) is one answer.
- How does the walker call a block, and how does a block from the walker
  reach a builtin that yields?
- Which AOT build of a test program serves as the oracle reference: one
  build per program, or one shared build?
- How much of the 1.4 MB text does a walker keep after the linker drops
  unused code? The answer decides whether Lupe fits a microcontroller.
- Does the GC test in `poc/m0` detect a missing root? A negative test must
  show that an unrooted value does not survive.

## 9. Plan

1. **M0: runtime alone.** `poc/m0` links only `libspinel_rt`. It makes
   boxed values, calls `sp_poly_*` operations, rescues an exception and
   runs the GC. Run it with `make -C poc/m0 run`.
2. **M1: template survey.** `poc/m1` generates the wrappers, compiles and
   links them, and reports the coverage. A smoke test calls a sample.
   Run it with `make -C poc/m1`.
3. **M2: minimal walker.** Parse with libprism. Evaluate literals, locals,
   `def`, calls and `if`/`while` on Integer, String and Array.
4. **M3: oracle harness.** Run the spinel test suite under CRuby, Lupe and
   AOT. Report the table in section 4.
5. **M4: embedded REPL.** Link Lupe into an AOT binary on a host. Then move
   to a microcontroller board.
