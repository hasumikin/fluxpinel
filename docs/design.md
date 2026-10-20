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

The walker tries four sources in order (`src/builtin.c`):

1. the core methods, written by hand. Most of them call one `sp_poly_*`
   function, for example `sp_poly_add` for `+`.
2. the `sp_poly_*` helpers that answer one method as a whole, for example
   `sp_poly_floor` for `floor`. A table maps the method name to the helper.
3. the generated wrappers.
4. `NoMethodError`.

A hand-written core method follows the C that codegen emits for the same
call. For example, `String#split(sep)` calls `sp_str_split_drop_trailing`,
as `src/codegen_call_recv.c` does.

### 3.3 Walker

The walker evaluates the prism AST directly (`src/walk.c`).

- **Frames.** A scope (the program or one method call) keeps its locals in
  a C array on the stack. Prism gives each scope a list of local names. The
  index in that list is the slot. Each slot is a GC root.
- **Temporaries.** A C local that holds a value across a call that can
  allocate is rooted with `SP_GC_ROOT_RBVAL`, as generated code is.
- **Jumps.** `return`, `break` and `next` set a pending jump and its value.
  Each statement list stops when a jump is pending. A loop or a method call
  takes the jump that belongs to it.
- **Methods.** A top-level `def` adds the method to a table of the walker.
  A call with no receiver looks in that table first, then in the Kernel
  methods (`puts`, `print`, `p`).
- **One TU.** `src/main.c` includes `lupe_tu.c`, the generated wrappers,
  `walk.c`, `builtin.c` and `iter.c`.

### 3.3.1 Blocks

- **Block value.** A call passes a `LupeBlock` to the callee as a C
  argument. A block literal lives in the C frame of its call. The block
  keeps a pointer to its home frame, the frame where the program wrote it.
- **Block frame.** A block call makes a frame whose parent is the home
  frame. A local with depth `d` is in the frame `d` parents up. The block
  frame takes `self`, the lexical class and the method block from the home
  frame. Thus `yield` in a block yields to the block of the method.
- **Parameters.** A block that is not a lambda spreads one Array argument
  over two or more parameters. A missing argument is nil, and an extra one
  is dropped. A lambda checks the count, as a method does. Numbered
  parameters (`_1`) and `it` are supported.
- **Jumps.** `next` ends the block call. `break` ends the call that passed
  the block: the block boundary changes it to `LUPE_CTL_BREAK_BLOCK` with
  that call as the target. `return` ends the method that holds the block,
  or the lambda. A loop or a method call takes only a jump with its own
  target.
- **Proc.** A `LupeProc` starts with a spinel `sp_Proc`, so the runtime
  reads `lambda?` and `frozen?` from it. Its `fn` is NULL: Lupe answers
  every call of a Proc. `proc`, `lambda`, `->`, `Proc.new` and a `&b`
  parameter make one. `&proc`, `&:sym` and an anonymous `&` pass a block.
- **Lifetime.** The home frame is on the C stack. Each frame has a serial
  number and an entry in a stack of serials on the heap (`L.fserial`). A
  block keeps the serial and the depth of its home frame. A frame pops
  its entry when its C scope ends (a `cleanup` attribute). A call of a
  block whose home frame has returned stops as a gap. The check reads
  only the heap stack, never the stack memory of a frame that ended. A
  raise skips the cleanup with `longjmp`, so a rescue cuts the heap stack
  back to its own depth. Thus a Proc works
  while its scope runs, but `define_method` and a Proc that a method
  returns do not. A heap frame for a scope that a Proc outlives is a
  later step.
- **Builtins with a block.** No runtime function calls a block, because
  codegen emits each builtin with a block as an inline C loop. `src/iter.c`
  writes these methods on the poly API: the Enumerable methods of Array
  and of an Integer Range, the Array mutators (`map!`, `select!`, ...),
  `Integer#times`/`upto`/`downto`/`step`, `String#each_char`, `tap`,
  `then`, `Array.new(n) { }` and `loop`. A builtin method with a block
  that `iter.c` does not have stops as a gap, since a generated wrapper
  would drop the block.

### 3.3.2 Exceptions

- **Value.** An exception is a spinel `sp_Exception`. It names its class
  (`cls_name`). For a class of the program, the name is the `LupeClass`
  name, and `sp_user_exc_parent_fn` answers its superclass. Thus
  `sp_exc_cls_matches` and the report of an uncaught exception see the
  classes of the program (`src/exc.c`).
- **Creation.** `k.new` for a subclass of Exception makes an
  `sp_Exception` and calls the `initialize` of the program, if any.
  `super(msg)` from it reaches the builtin `initialize`, which sets the
  message. An exception keeps its ivars in a hidden object, as a class
  does.
- **Handlers.** `begin`/`rescue` pushes a handler of the runtime with
  `setjmp`, in the shape of the code that spinel emits. A rescue clause
  runs outside the handler, so a raise in it goes to the next handler out.
  A clause matches by `sp_exc_cls_matches`, and a module by `is_a?`.
- **Ensure.** `ensure` is a second handler around the rescue part. The
  ensure clause runs after the body, after a jump (`return`, `break`)
  and after a raise, which then goes on. A jump in the ensure clause
  takes the place of the passing jump or raise.
- **Landing.** A landing restores the GC roots and the rescue stack from
  the marks of the handler, as generated code does. It also cuts the
  frame stack of Lupe (section 3.3.1).
- **Missing methods.** A program that rescues `NoMethodError` can hide a
  method that Lupe lacks. The oracle still sees the trace of the missing
  method when the output differs.

### 3.3.3 Regexp

- **Value.** A Regexp is a compiled pattern of the runtime
  (`mrb_regexp_pattern`), boxed with `SP_BUILTIN_REGEX`. The runtime
  allocates it with `malloc` and never frees it, as in AOT code. A literal
  compiles once, at its first evaluation, and a table keyed by the node
  keeps the pattern (`src/re.c`). An interpolated literal compiles at each
  evaluation, but once with `/o`.
- **Methods.** spinel emits the String methods that take a Regexp from
  codegen code, so Lupe writes them on the runtime helpers:
  - `=~`, `!~`, `match`, `match?` and `===`.
  - `sub`, `gsub` and their bang forms, with a String, a Hash or a block.
  - `scan`, `split`, `index`, `rindex`, `[]`, `slice!`, `[]=`,
    `partition`, `rpartition` and `start_with?`.

  A MatchData answers most methods through the generated wrappers.
- **Registers.** `$~`, `$1` and the other match globals read the registers
  of the runtime. A method whose body has a Regexp literal or a call of the
  match family saves the registers of its caller and puts them back on
  exit (`sp_re_frame`). Lupe finds these methods by a walk of the body at
  the first call.
- **Subject.** A match reads a copy of a String that can change. `$~`
  keeps the subject, so a later `slice!` or `gsub!` does not change it.

### 3.3.4 Global variables

- **Store.** A global variable of the program lives in a table of Lupe
  (`L.globals`), and the mark hook marks it. A global that no code has
  assigned reads nil (`src/global.c`).
- **Special globals.** A special global reads the state of the runtime, as
  the code that spinel emits does:
  - `$stdin`, `$stdout` and `$stderr` read the IO handles of the runtime.
  - `$?`, `$$`, `$0`, `$/`, `$LOAD_PATH` and `$LOADED_FEATURES` read
    their runtime values.
  - `$VERBOSE` and `$DEBUG` read false until the program assigns them.

  An alias (`$-v`, `$PROGRAM_NAME`, `$:`, ...) names the same global. An
  assignment to a writable special global goes into the table, and a read
  prefers the table.
- **Output.** After `$stdout = obj`, Kernel#puts, #print and #p write to
  obj, as in CRuby. An assignment of an object with no `write` method
  raises TypeError.
- **IO methods.** spinel emits `IO#puts`, `#print`, `#write` and `#<<`
  from codegen code, so Lupe writes them on `sp_File_puts_val` and
  `sp_File_write_poly`. The other IO methods go through the generated
  wrappers.

### 3.4 User classes

Spinel compiles each user class to a C struct. The `cls_id` of a class
indexes per-program tables that codegen emits (`sp_class_names[]` and
others). A class that the walker defines has no place in those tables.

Lupe gives walker classes their own object kind (`src/object.c`):

- **Class.** A `LupeClass` holds the method table, the singleton method
  table, the included and extended modules, the constants and the ivar
  slot names. Its runtime id starts at `LUPE_CLS_BASE` (0x100000), far
  above the ids of AOT classes. A builtin class (Integer, Object,
  StandardError, ...) gets a `LupeClass` on first use, with the builtin
  hierarchy, so that a program can reopen it.
- **Object.** A `LupeObject` is `{ cls_id, sp_PolyArray *iv }` on the spinel
  heap, allocated with `sp_gc_alloc` and a scan function. It starts with
  the class id, as an AOT object does. An instance of Object (`main`,
  `Object.new`) carries an alias id in the Lupe range, so that the runtime
  sees a user object.
- **Hooks.** The runtime reaches a user object through function pointers.
  Lupe sets `sp_obj_to_s_fn`, `sp_obj_inspect_fn`, `sp_obj_cls_name_fn`,
  `sp_class_is_module_fn`, `sp_user_binop_hook` and `sp_user_coerce_hook`.
  Thus `puts obj`, `p obj`, `[a, b].include?(c)` and `5 * money` call the
  methods of the program.
- **Constants.** A bare name looks in the lexical scopes but the top
  level, then in the ancestors of the innermost one, then in Object, then
  in the builtin classes. A top-level `def` defines a method of Object.
- **Freeze.** `freeze` on an object sets the frozen bit of its heap
  header, and an ivar write to a frozen object raises FrozenError. A class
  keeps its own frozen flag.
- **Gaps.** A semantics that Lupe does not model stops the program as an
  unsupported node does (`lupe_gap`): an instance of a subclass of a
  builtin class other than Object, Exception, Struct and Data, an ivar on
  a builtin value other than an exception, `extend` on an object, and the
  reopening of a CRuby core class that spinel's class table does not name
  (Random, Set, ...).

`sp_cext_dispatch_fn` sends a method by name to a foreign object. It is a
candidate for the AOT to walker direction of section 3.6.

### 3.4.1 Struct, Data and anonymous classes

- **Record class.** spinel compiles `Struct.new` and `Data.define` to a
  class of the program. Lupe makes a `LupeClass` at run time, with the
  members, the kind (Struct or Data) and `keyword_init` (`src/struct.c`).
  The block of `Struct.new` runs as the class body.
- **Members.** An instance keeps each member in an ivar slot whose name
  has no `@`, so `instance_variables` does not list it. An accessor is an
  attr method on that slot. Thus a `def` of the program can replace an
  accessor, and `super` from its `initialize` reaches the builtin one.
- **Data.** `Data.new` turns positional arguments into keywords before it
  calls `initialize`, as CRuby does. A missing keyword raises before an
  unknown one, and the builtin `initialize` freezes the instance.
- **Anonymous class.** `Class.new(Super) { }` and `Module.new { }` run the
  block with the new class as self and as the definee. A class from
  `Class.new` or `Struct.new` takes the name of the first constant that
  holds it.

### 3.5 Containers

Spinel has 12 typed containers, for example `sp_IntArray`, `sp_StrIntHash`
and `sp_PolyArray`. The walker creates only the poly forms. A value from AOT
code can hold any of the 12 forms. The walker reads all 12 forms.

- **Hash.** A Hash literal makes an `sp_PolyPolyHash`. Its entries keep the
  insertion order. `Hash.new(v)` sets the default value. `Hash.new { }`
  sets a C default proc that calls the block, and the hash marks the
  `LupeProc` through `dproc_self` (`src/hash.c`).
- **Typed Hash.** A method that reads a typed Hash takes a poly copy
  (`sp_poly_as_poly_poly_hash`). A method that changes one stops as a gap.
- **Enumerable.** `src/iter.c` reads a Hash as a sequence of `[key, value]`
  pairs. A block with two parameters spreads each pair. `select`,
  `reject`, `transform_values` and the mutators answer a Hash, so
  `src/hash.c` has them.
- **Keys.** A user object as a key reaches the program through two hooks:
  `sp_obj_hash_hook` calls `hash`, and `sp_obj_eql_hook` calls `eql?`.
  Without these methods, a key compares by identity.
- **Keyword arguments.** `m(a: 1)` passes a Hash as the last argument,
  and the call marks that Hash (`L.kwhash`). A method with keyword
  parameters takes only the marked Hash, so a Hash literal in the last
  place stays positional, as in Ruby 3. The binder binds each keyword and
  puts the rest into `**rest`. A missing or unknown keyword raises
  `ArgumentError`, as CRuby does. A bare `super` passes the keywords
  with their current values.
- **Forwarding.** A call with `*a`, `...` or a keyword Hash of only `**h`
  evaluates its arguments into an Array, since the count is known at run
  time only. An anonymous `*`, `**` or `...` parameter keeps its values
  in two slots of the frame, after the slot of `it`.
- **Write barrier.** The walker stores a pointer into a GC object only
  through a runtime setter, or after `sp_gc_wb`. A young object that an
  old one holds without the barrier is freed by a minor GC. The GC stress
  run showed this with the default proc of `Hash.new { }`.

### 3.6 Boundary to AOT code

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

### 4.1 The harness

`make oracle` runs `tools/oracle.rb` on `$(SPINEL)/test/*.rb`.

- The CRuby answer is the `.expected` file of each test. Spinel snapshots it
  from CRuby.
- Lupe runs first. Spinel compiles a test only when Lupe runs it, so a run
  of the whole corpus costs little while the coverage of Lupe is low.
- The harness compares stdout. It does not compare stderr or the exit
  status, as `.expected` holds stdout only.
- `LUPE_TRACE_MISSING=1` makes Lupe report a missing builtin method. The
  harness then counts the test as a gap of Lupe, not as a wrong answer.
- The report (`build/oracle/report.md`) names the spinel commit of the tree
  and of the Lupe build. It warns when they differ.

Classes beyond the table above: `lupe_only` (spinel refuses the program,
Lupe gives the CRuby answer), `lupe_unsupported`, `lupe_missing` and
`lupe_crash`.

### 4.2 The overflow mode

The oracle runs Lupe and AOT in promote mode. Spinel's raise mode is not a
property of the runtime alone: the analysis still promotes an Integer where
it sees growth, for example `10 ** 30`. A walker has no analysis, so it
cannot make the same choice. In raise mode, 56 tests that Lupe ran gave
`RangeError` where AOT gave a Bignum.

The corpus follows spinel's own promote lane: it leaves out the tests in
`RAISE_MODE_PINS` of spinel's Makefile.

### 4.3 A defect only Lupe reaches

`"%c" % s` answers `""` when `s` is a mutable String handle in a poly slot.
CRuby answers the first character. The `%c` branch of
`sp_str_format_polyarr` (`lib/sp_poly_cold.c`) tests `SP_TAG_STR` only and
does not dereference a STRBUF handle. The AOT tests pass a literal there,
which is `SP_TAG_STR`. Lupe makes every String literal a new handle, so it
takes the handle path on every run.

Lupe does not work around such a defect, as the oracle must show it.
`tools/oracle_known.tsv` lists the known defects. The oracle counts a
listed test as `known` while it differs, and as `known_fixed` when it
agrees again, so that the line can go.

Thus a difference in the `lupe_differs` class is not always a defect of
Lupe. Lupe holds every value in its most general form, and a runtime path
for that form can have a defect that the specialized AOT code never
reaches.

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
  the init hooks (`src/lupe_tu.c`).
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
- Some core methods have no row in `bop_rows[]` at all. `Float#floor`,
  `Integer#gcd` and `Array#sum` are examples. Codegen writes them, and a
  `sp_poly_*` helper answers them on a boxed value.
- `spinel_rt.h` defines 75 `sp_poly_*` helpers of the form
  `sp_RbVal f(sp_RbVal)` and 49 of the form `sp_RbVal f(sp_RbVal, sp_RbVal)`.
  Their names do not always equal the method name (`sp_poly_int_gcd` for
  `gcd`).
- The GC modes of spinel catch a missing root in the walker. With the root
  of an Array literal removed, `test/m2/gc_stress.rb` gives the correct
  output in a plain run. It gives a wrong output with `SPINEL_GC_STRESS=1`,
  and it aborts with `SPINEL_GC_STRESS=2 SPINEL_GC_VERIFY=1`.
- The oracle on spinel `7ea20089b` (7824 tests, promote mode): Lupe runs
  96 tests. 95 agree with CRuby and AOT, and 1 differs (section 4.3). The
  largest blockers, by tests:

  | blocker | tests |
  |---|---|
  | `class` | 2457 |
  | a call with a block | 1032 |
  | a constant read | 618 |
  | `module` | 546 |
  | a constant write | 385 |
  | a Hash literal | 361 |
  | `Kernel#require` | 335 |
  | `String#+@` | 219 |
  | `begin` / `rescue` | 204 |

- The oracle on spinel `1b789548d` (7840 tests, promote mode), with
  classes and modules: Lupe runs 491 tests and 474 agree. The largest
  blockers now:

  | blocker | tests |
  |---|---|
  | a call with a block | 1924 |
  | a Hash literal | 702 |
  | `begin` / `rescue` | 361 |
  | `String#+@` | 362 |
  | `Kernel#require` | 338 |
  | `ARGV` | 220 |

  One test times out: 2^24 calls of class methods
  (`cmethod_chain_reachability`).
- The oracle on spinel `6ab3b2e20` (7857 tests, promote mode), with
  blocks: Lupe runs 873 tests and 854 agree. 19 differ. The largest
  blockers now:

  | blocker | tests |
  |---|---|
  | a Hash literal | 854 |
  | `begin` / `rescue` | 629 |
  | a Regexp literal | 208 |
  | `Class#new` with a block | 174 |
  | a keyword argument | 152 |
  | a global variable write | 144 |

- The oracle on spinel `6ab3b2e20`, with Hash, keyword arguments and
  forwarding: Lupe runs 1548 tests and 1519 agree. 29 differ, most of them
  in `respond_to?` and in class semantics. The largest blockers now:

  | blocker | tests |
  |---|---|
  | `begin` / `rescue` | 775 |
  | a Regexp literal | 247 |
  | `Class#new` with a block | 174 |
  | a global variable write | 171 |
  | a `rescue` modifier | 149 |
  | a multiple assignment | 145 |

- The oracle on spinel `b5d3820ad` (7910 tests), with exceptions: Lupe
  runs 2023 tests and 1909 agree. 111 differ; about 30 of them check
  `private` or `freeze`, which Lupe does not model. The largest blockers
  now:

  | blocker | tests |
  |---|---|
  | a Regexp literal | 262 |
  | a global variable write | 189 |
  | `Class#new` with a block | 181 |
  | a multiple assignment | 151 |
  | `case` / `when` | 132 |
  | a Range of values other than Integer | 101 |

- The oracle on spinel `b5d3820ad`, with Regexp: Lupe runs 2210 tests and
  2095 agree. 112 differ. The largest blockers now:

  | blocker | tests |
  |---|---|
  | a global variable write | 191 |
  | `Class#new` with a block | 184 |
  | a multiple assignment | 151 |
  | `case` / `when` | 138 |
  | a Range of values other than Integer | 102 |
  | `case` / `in` | 73 |

- The oracle on spinel `b5d3820ad`, with global variables: Lupe runs 2311
  tests and 2188 agree. 120 differ. The largest blockers now:

  | blocker | tests |
  |---|---|
  | `Class#new` with a block | 189 |
  | a multiple assignment | 155 |
  | `case` / `when` | 140 |
  | a Range of values other than Integer | 103 |
  | `case` / `in` | 75 |
  | a call of a block whose scope has returned | 65 |

- The oracle on spinel `b5d3820ad`, with Struct, Data and anonymous
  classes: Lupe runs 2509 tests and 2386 agree. 120 differ. The largest
  blockers now:

  | blocker | tests |
  |---|---|
  | a multiple assignment | 160 |
  | `case` / `when` | 146 |
  | `Thread.new`, `Fiber.new` or `Enumerator.new` with a block | 145 |
  | a Range of values other than Integer | 109 |
  | `case` / `in` | 90 |
  | a call of a block whose scope has returned | 67 |

- A class keeps one lexical parent, the scope of its first definition.
  In CRuby, the lexical scope belongs to each class body: the body of
  `class A::B` does not search `A`. Lupe searches `A` there
  (`class_new_superclass_order`).
- The runtime shows a class value by its name, through
  `lupe_class_name_hook`. Thus `p K` for a Struct class with
  `keyword_init: true` shows `K`, but CRuby shows `K(keyword_init: true)`.
- `sp_re_matchdata` sets the captures of a match but not the positions
  (`sp_re_caps`) that `sp_re_last_matchdata` reads. Lupe sets the
  registers again from the MatchData, so `$~` after `str.match(re)` reads
  this match.
- Lupe makes a mutable String for each String literal. Thus
  `"ab12".slice!(/\d+/)` changes a String in Lupe, but CRuby and AOT raise
  FrozenError (`string_slice_bang_regexp_arm`).
- `rescue` lets a program reach two old defects of Lupe. A generated
  wrapper whose row has no argument kinds takes any argument: `"s".include?(:x)`
  passes a Symbol to `sp_str_include`, which crashes
  (`param_include_body_widen`). A rescue with `retry` around a method that
  Lupe lacks retries forever (`string_split_local_each_retry`).
- A comparator block that does not compare (`sort { |x| x.length }`) gives
  an order that depends on the sort algorithm. Lupe uses a stable merge
  sort, and CRuby uses its own quicksort. `comparator_block_arity` differs
  for this reason.
- The M2 walker runs `fib(32)` in 0.85 s. CRuby 4.0.6 runs it in 0.24 s,
  and CRuby with YJIT in 0.06 s. The walker compares each method name with
  `strcmp` and looks up methods by a linear search.

## 8. Open questions

- The smoke test in `poc/m1` checks 6 wrappers. How does Lupe check the
  other wrappers? The oracle (M3) is one answer.
- How does a Proc outlive its scope? Candidates are a heap frame for each
  scope that holds a block literal, and a copy of the frame when a Proc
  escapes.
- Which AOT build of a test program serves as the oracle reference: one
  build per program, or one shared build?
- How much of the 1.4 MB text does a walker keep after the linker drops
  unused code? The answer decides whether Lupe fits a microcontroller.
- Can a generator map the `sp_poly_*` helpers to method names, as
  `tools/bop_gen.c` does for `bop_rows[]`? Codegen names the helper that it
  emits for each method, so the map exists in the codegen source.
- How does the walker dispatch fast? Candidates are interned method names
  (`sp_sym`) and an inline cache per call node.
- How does Lupe hold its spinel dependency? Lupe uses the spinel source
  (`bop_rows[]`, `ty_traits[]`), `libspinel_rt.a` and `libprism.a` from
  `$(SPINEL)`, an external checkout. The two uses want different things:
  - The oracle checks the spinel tree under test, at any commit. An
    external path fits it.
  - The embedded REPL links with one application. It needs a fixed spinel
    commit and a reproducible build. On a microcontroller, it also builds
    the runtime and prism from source with the target toolchain.

  A candidate: a spinel submodule as the default, and `make SPINEL=...` to
  use another tree. The oracle (M3) needs at least a recorded commit. The
  device REPL (M4) needs the source build.

## 9. Plan

1. **M0: runtime alone.** `poc/m0` links only `libspinel_rt`. It makes
   boxed values, calls `sp_poly_*` operations, rescues an exception and
   runs the GC. Run it with `make -C poc/m0 run`.
2. **M1: template survey.** `poc/m1` generates the wrappers, compiles and
   links them, and reports the coverage. A smoke test calls a sample.
   Run it with `make -C poc/m1`.
3. **M2: minimal walker.** `build/lupe FILE.rb` evaluates literals,
   locals, `def`, calls, `if`/`unless`, `while`/`until`, `return`, `break`
   and `next` on Integer, Float, String, Symbol and Array. `make test` runs
   `test/m2` under CRuby and Lupe, in a plain run and in the GC stress run.
4. **M3: oracle harness.** `make oracle` runs the spinel corpus under
   CRuby, Lupe and AOT in promote mode, and reports the classes of
   section 4 and the blockers of Lupe.
5. **M3+: coverage.** Take the blockers in the order of the oracle report.
   Each step runs the oracle again.
   - Classes, modules and constants (section 3.4): `test/m3`.
   - Blocks, Proc and Integer Range (section 3.3.1): `test/m3`.
   - Hash and keyword arguments (section 3.5): `test/m3`.
   - Exceptions: `raise`, `begin`/`rescue`/`else`/`ensure`, `retry` and
     the `rescue` modifier (section 3.3.2): `test/m3`.
   - Regexp, MatchData and the match globals (section 3.3.3): `test/m3`.
   - Global variables and the standard streams (section 3.3.4): `test/m3`.
   - Struct, Data and anonymous classes (section 3.4.1): `test/m3`.
   - Next: the largest blockers of the oracle report.
6. **M4: embedded REPL.** Link Lupe into an AOT binary on a host. Then move
   to a microcontroller board.
