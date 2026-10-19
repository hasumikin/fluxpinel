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

Lupe generates its builtin dispatch from that table at build time:

- A `BOPE_TEMPLATE` row holds a C expression, for example
  `sp_poly_case_conv($r, sp_str_upcase, "upcase")`. A generator turns each
  such row into one wrapper function. The wrapper unboxes by tag and calls
  the expression.
- A row with a special emitter (`BOPE_ARRAY_SORT` and others) has its logic
  in the codegen C code. Lupe implements these rows by hand, or leaves them
  out.

The generator reads the spinel source at the pinned commit. This keeps the
builtin semantics of Lupe equal to the AOT output.

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

## 8. Open questions

- Does `libspinel_rt` link without a generated TU? Some symbols, such as
  `sp_exc_subclass_ids`, come from the generated TU.
- What fraction of the `BOPE_TEMPLATE` rows can a generator convert with no
  manual work?
- How does a template refer to compiler state beyond `$r` and the
  arguments?
- How does the walker call a block, and how does a block from the walker
  reach a builtin that yields?
- Which AOT build of a test program serves as the oracle reference: one
  build per program, or one shared build?

## 9. Plan

1. **M0: runtime alone.** Write a C program that links only
   `libspinel_rt`. Make Integer, String and Array values. Call
   `sp_poly_add` and Array operations. Run the GC.
2. **M1: template survey.** Count the `BOPE_TEMPLATE` rows that a generator
   converts with no manual work.
3. **M2: minimal walker.** Parse with libprism. Evaluate literals, locals,
   `def`, calls and `if`/`while` on Integer, String and Array.
4. **M3: oracle harness.** Run the spinel test suite under CRuby, Lupe and
   AOT. Report the table in section 4.
5. **M4: embedded REPL.** Link Lupe into an AOT binary on a host. Then move
   to a microcontroller board.
