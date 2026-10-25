# Fluxpinel design

## 1. Goals

Fluxpinel adds a dynamic layer to an AOT spinel program. The interpreted code
and the AOT code share one value representation, one heap and one set of
builtin semantics. Thus a value crosses between them by reference.

Fluxpinel serves two uses:

- **Dynamic layer (use 1).** An AOT binary links Fluxpinel. Section 5 gives the
  forms:
  - **Scripts.** The AOT program loads Ruby files at run time: plugins,
    configuration and glue code. A console is the interactive form: a user
    connects over a socket, reads live objects and calls AOT methods.
  - **eval.** AOT code evaluates a Ruby string.
  - **Dynamic dispatch.** A call that AOT code cannot resolve goes to Fluxpinel:
    `method_missing`, `define_method` and `send` with a computed name.
- **Oracle (use 2).** Fluxpinel runs a program as a reference. It does no type
  inference and no optimization. A difference between Fluxpinel and the AOT
  binary points to the analysis or the codegen of spinel. The oracle also
  keeps the two sides of use 1 equal: a method gives the same answer in
  Fluxpinel and in AOT code.

The oracle needs no change to spinel, and it gives the walker a test
harness. Use 1 reuses the same walker.

## 2. Constraints

- Fluxpinel does not modify spinel on its own. The spinel maintainer keeps a VM
  and `eval` out of the spinel design (matz/spinel#41, matz/spinel#36).
  Fluxpinel lives outside that design, as a separate project. A change that
  Fluxpinel needs in spinel goes to the maintainer as an issue, after a demo
  shows the use.
- Fluxpinel is a tool for a host. A microcontroller is not a target of the
  design.
- Fluxpinel pins one spinel commit. The runtime API of spinel is internal and
  changes often. The pin makes each update an explicit step.
  - `SPINEL_COMMIT` holds the pin. A change of the pin is a commit of its
    own.
  - `make deps` fetches the pinned commit into `deps/spinel` and builds it.
    `SPINEL` names that checkout by default.
  - `SPINEL` can name another checkout, on the command line or in
    `config.mk`. `make` stops when the checkout is not at the pin.
  - The oracle checks a spinel tree at any commit with `SPINEL_PIN=0`. Its
    report tells whether the tree is the pin.
  - The dynamic layer links the kernel and Fluxpinel with one runtime, so
    both must use the pinned commit.
- Fluxpinel links `libspinel_rt` (the C runtime in `spinel/lib/`). Fluxpinel does not
  link the spinel compiler at run time.

## 3. Architecture

```
            +---------------------------+
  .rb  -->  | libprism  (parse)         |
            +---------------------------+
                        |
                        v
            +---------------------------+
            | Fluxpinel walker               |
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

Fluxpinel generates part of its builtin dispatch from that table at build time
(`tools/bop_gen.c`):

- A `BOPE_TEMPLATE` row holds a C expression, for example
  `sp_str_center($r, $i0)`. The generator turns each such row into one
  wrapper per receiver variant:
  `sp_RbVal fx_bop_N(sp_RbVal recv, sp_RbVal *argv)`.
- The wrapper unboxes and boxes with the spellings of `ty_traits[]`
  (`src/types.c`). This table gives the C type, the box and the unbox of
  each builtin kind. Thus the wrapper converts values as the AOT code does.
- A row for every Array kind or every Hash kind gives one wrapper for each
  concrete kind (`sp_IntArray`, `sp_StrIntHash` and others).
- A row with a special emitter (`BOPE_ARRAY_SORT` and others) or with no
  emitter (`BOPE_NONE`) has its logic in the codegen C code. Fluxpinel writes
  these methods by hand.

The generator reads the spinel source at the pinned commit. A generated
wrapper calls the runtime function that the AOT output calls, so it gives
the same answer. A method that Fluxpinel writes by hand has no such tie
(section 3.2.1).

Several rows can share one receiver and one name. They differ in argument
count or argument kinds (`arg0`, `arg1`). The dispatcher selects a row by
`argc_min`/`argc_max` and by the tags of the arguments.

The generated wrappers cover the long tail: Rational, Complex, Time, IO,
MatchData, Range and others. The core of Array, Hash and String is mostly
outside the table. Fluxpinel writes these methods by hand:

- element access and operators (`[]`, `[]=`, `==`, `+`, `length`, `push`),
- methods with a block (`each`, `map`, `select`, `inject`). The walker
  must call its own blocks, so these methods are walker-native anyway.
  `src/prelude.rb` writes the rarer ones in Ruby (`each_index`, `fill`,
  `combination`, `step`). A call takes the prelude only when no C method
  takes the block (`src/prelude.c`). The prelude does not reopen the
  builtin class, so the other calls keep their speed.
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

### 3.2.1 Risks of the builtin dispatch

The rows of `bop_rows[]` fall into four groups, and only one of them gives
a runtime function to call:

| Group | Example | Runtime function | Fluxpinel |
|---|---|---|---|
| gen | `String#upcase` (`sp_str_upcase($r)`) | yes | a generated wrapper |
| emitter | `Array#any?` (`BOPE_ARRAY_PRED0`) | no: codegen writes the C at the call | written by hand |
| none | `Time#utc` (`BOPE_NONE`) | the row only gives the type; codegen writes the C elsewhere | written by hand, or a gen row of the same method |
| skip | `Array#slice!(i, n)` (result `BOPR_SELF`) | yes | none yet: `bop_gen` cannot expand the row |

This gives these risks:

- **The same semantics holds only for the generated wrappers.** A method
  that Fluxpinel writes by hand is a second implementation. When spinel
  changes the C that it emits for the method, the hand-written method does
  not follow. The oracle finds such a difference only where a spinel test
  covers it.
- **A change of spinel can remove a wrapper.** When spinel moves a method
  from a template to an emitter, its wrapper goes, and the method becomes a
  gap until Fluxpinel writes it. `make regress` finds this for the methods
  that its tests use, and for no other method.
- **The build depends on the internals of spinel.** `bop_gen` links
  `src/types.c` and `src/builtin_names.c` of spinel. When these files use
  more of the analysis, `bop_gen` needs a stand-in for each new symbol
  (`builtin_class_id`, `g_one_string`). The console depends on the internal
  structs of prism (`src/load.c`). `make upgrade_spinel` finds each break.
- **The binary keeps every wrapper.** The table of wrappers names each
  one, so the linker keeps each wrapper and the runtime functions that it
  calls, whatever the kernel uses. Fluxpinel alone is about 6 MB.

Ways to reduce the risks:

- Ask spinel for a runtime function for each emitter row, or for one
  entry that calls a builtin method by name. Either removes most of the
  hand-written methods. It is the same direction as section 5.3.
- Teach `bop_gen` the rows that it skips now (`$T`, a result of no single
  kind).
- Name the spinel row that each hand-written method follows, so that a
  change of the row shows at an update of the pin.

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
- **One TU.** `src/main.c` includes `fx_tu.c`, the generated wrappers,
  `walk.c`, `builtin.c` and `iter.c`.

### 3.3.1 Blocks

- **Block value.** A call passes a `FxBlock` to the callee as a C
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
  the block: the block boundary changes it to `FX_CTL_BREAK_BLOCK` with
  that call as the target. `return` ends the method that holds the block,
  or the lambda. A loop or a method call takes only a jump with its own
  target.
- **Proc.** A `FxProc` starts with a spinel `sp_Proc`, so the runtime
  reads `lambda?` and `frozen?` from it. Its `fn` is NULL: Fluxpinel answers
  every call of a Proc. `proc`, `lambda`, `->`, `Proc.new` and a `&b`
  parameter make one. `&proc`, `&:sym` and an anonymous `&` pass a block.
- **Lifetime.** The home frame is on the C stack. Each frame has a serial
  number and an entry in a stack of serials on the heap
  (`FxFrameStack`). Each fiber that runs walker code has its own stack
  (section 3.3.5). A block keeps the stack, the serial and the depth of
  its home frame. A frame pops its entry when its C scope ends (a
  `cleanup` attribute). The check reads only the heap stack, never the
  stack memory of a frame that ended. A raise skips the cleanup with
  `longjmp`, so a rescue cuts the heap stack back to its own depth.
- **Heap frames.** A Proc can outlive the scope of its block, and every
  Proc comes from `fx_block_proc`. There the home frame moves to the
  heap (`FxEnv`), as an env of mruby does:
  - The env takes a copy of the frame and of its slots.
  - The frame on the C stack runs on with the slots of the env. Thus the
    scope and the Proc see one set of variables.
  - The parent frames move too, since a block reads their locals.
  - The block of the frame moves as a Proc, so `yield` in the Proc works
    after the method returns.
  - The collector is generational, so a store into a slot of an env takes
    the write barrier (`fx_local`).
- **Dead homes.** A Proc runs after its scope ends. A `return` or a
  `break` in it then raises LocalJumpError, as in CRuby. A block literal
  that is not a Proc never outlives its call. A call of one whose home
  frame on the C stack has ended is still a gap, as a safety net.
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
  (`cls_name`). For a class of the program, the name is the `FxClass`
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
  frame stack of Fluxpinel (section 3.3.1).
- **Missing methods.** A program that rescues `NoMethodError` can hide a
  method that Fluxpinel lacks. The oracle still sees the trace of the missing
  method when the output differs.

### 3.3.3 Regexp

- **Value.** A Regexp is a compiled pattern of the runtime
  (`mrb_regexp_pattern`), boxed with `SP_BUILTIN_REGEX`. The runtime
  allocates it with `malloc` and never frees it, as in AOT code. A literal
  compiles once, at its first evaluation, and a table keyed by the node
  keeps the pattern (`src/re.c`). An interpolated literal compiles at each
  evaluation, but once with `/o`.
- **Methods.** spinel emits the String methods that take a Regexp from
  codegen code, so Fluxpinel writes them on the runtime helpers:
  - `=~`, `!~`, `match`, `match?` and `===`.
  - `sub`, `gsub` and their bang forms, with a String, a Hash or a block.
  - `scan`, `split`, `index`, `rindex`, `[]`, `slice!`, `[]=`,
    `partition`, `rpartition` and `start_with?`.

  A MatchData answers most methods through the generated wrappers.
- **Registers.** `$~`, `$1` and the other match globals read the registers
  of the runtime. A method whose body has a Regexp literal or a call of the
  match family saves the registers of its caller and puts them back on
  exit (`sp_re_frame`). Fluxpinel finds these methods by a walk of the body at
  the first call.
- **Subject.** A match reads a copy of a String that can change. `$~`
  keeps the subject, so a later `slice!` or `gsub!` does not change it.

### 3.3.4 Global variables

- **Store.** A global variable of the program lives in a table of Fluxpinel
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
  from codegen code, so Fluxpinel writes them on `sp_File_puts_val` and
  `sp_File_write_poly`. The other IO methods go through the generated
  wrappers.

### 3.3.5 Fibers, threads and generators

- **One body.** The runtime runs a fiber body on a C stack of its own,
  and a green thread and a generator Enumerator are fibers too. Fluxpinel
  gives all of them one body, `fx_fiber_body`, which calls a block of
  the program on that stack (`src/fiber.c`). The fiber carries the block
  in its `user_data`, which the collector marks.
- **Arguments.** A Fiber block takes what the first `resume` passes. A
  Thread block takes the arguments of `Thread.new`. A generator block
  takes the yielder of the runtime (`SP_BUILTIN_YIELDER`), whose `<<` is
  `Fiber.yield`.
- **Frame stacks.** Fibers interleave their frames, so each fiber that
  runs walker code has its own frame stack. `fx_fiber_body` makes it,
  and `fx_fs` finds it again after a switch, by the running fiber
  (`sp_fiber_current`). A fiber that Fluxpinel did not start is a root fiber,
  and it shares the frame stack of the main program. The runtime swaps
  the GC roots and the rescue handlers itself.
- **Stack size.** Fluxpinel asks the runtime for fiber stacks of 1 MB, the size
  that codegen asks for an unoptimised build. The walker uses more C stack
  for each Ruby call than AOT code does.
- **Runtime variant.** spinel links a program that uses threads with the
  threaded runtime (`libspinel_rt_mt.a`, `-DSP_THREADS`). Only that
  variant has the full scheduler: `sleep` and I/O park a green thread,
  and `Thread#wakeup` and `Mutex#sleep` exist. Fluxpinel links it by default
  (`THREADS=1`); `THREADS=0` links the single-threaded runtime.
- **Scheduling.** Fluxpinel starts the scheduler at startup, since the threaded
  runtime needs its root fiber before any fiber runs, and drains it at the
  end of the program. Fluxpinel sets `SPINEL_WORKERS=1`: the state of the
  walker is not shared between OS threads, so green threads take turns on
  one OS thread, which is main. A `while` back-edge and a block call poll
  the safepoint flag, as AOT code does at a back-edge. A thread switch
  thus comes only where no jump is pending.
- **Blocking calls.** I/O, `sleep`, and a wait on a Queue or a Mutex park
  a green thread in the scheduler. A call that blocks in the OS stops
  every thread until it returns. An example is an `ffi_func` call that
  blocks.
- **Iteration.** A generator Enumerator is the runtime's, so the generated
  wrappers answer `next`, `peek`, `take`, `size` and `rewind`. A method
  with a block reads the items one at a time through a walker of the
  runtime (`sp_enum_walker_boxed`). Thus `find` on an endless generator
  stops at the hit. `loop` ends at a StopIteration and answers its
  result.
- **Home frames.** A fiber runs a Proc of its block, so the home frame is
  on the heap (section 3.3.1). A Thread or a generator made in a block or
  a method that returns before it runs works: for example
  `3.times.map { Thread.new { } }`.

### 3.3.6 Source files

- **Load at run time.** spinel splices a required file into one source at
  compile time. Fluxpinel loads it at run time, as CRuby does: the file runs
  once, at the first `require_relative`, as a top-level program of its
  own. Self is main, the definee is Object, and its locals are its own
  (`src/load.c`).
- **One constant pool.** Each file has a prism parser of its own. A node
  names a local, a method or a constant by an id in the constant pool of
  its parser, so all the parsers share one pool (`L.pool`). The pool only
  grows, and an id stays valid.
- **File of a node.** A source keeps its bytes, and a node points into
  them. Thus Fluxpinel finds the file of a node by the address. `__FILE__`,
  `__LINE__`, `__dir__`, `require_relative` and the message of an
  unsupported node read it.
- **Paths.** `require_relative` resolves from the real path of the file of
  the call, and adds `.rb`. A file with a real path that is loaded already
  answers false. `require` of a path (`./x`, `../x`, `/x`) resolves from the
  current directory.
- **Library names.** spinel resolves `require "json"` at compile time
  (`src/spinel_parse.c`). Fluxpinel resolves the name at run time in the same
  order:
  - `packages/<first>/<name>.rb` of spinel is the package. The first
    segment of the name selects the package. The file runs once, and its
    `<name>.share.rb` follows it when that file exists.
  - A feature of the runtime (`socket`, `ostruct`, `monitor`, `time`,
    `io/console`) and a core capability (`thread`, `fiber`, `rbconfig`,
    ...) load nothing. The require answers false.
  - Any other name is a LoadError. spinel refuses it at compile time under
    `--require-gate`, and ignores it with a warning otherwise.
- **Set.** CRuby loads Set before the program starts, and spinel loads
  its package when a program names `Set`. Fluxpinel loads the package at the
  first lookup of the constant `Set`. Thus `require "set"` answers false.

### 3.3.7 FFI

- **Run time, not compile time.** spinel reads `ffi_func` and its kin at
  compile time and emits a direct C call for each function. Fluxpinel meets the
  same declarations in a module body at run time (`src/ffi.c`):
  - `ffi_lib` opens the library with `dlopen`: `libNAME.so`, or a
    versioned name when only that exists.
  - `ffi_func` declares a singleton method of the module (`FX_M_FFI`).
    Its first call finds the symbol with `dlsym`, and each call goes
    through libffi with the C types that the declaration names.
  - `ffi_const`, `ffi_buffer`, `ffi_read_<width>`, `ffi_read_ptr` and
    `ffi_write_*` follow `docs/FFI.md` of spinel.
- **Values.** The argument and return conversions follow the type table
  of `docs/FFI.md`. A `:ptr` is an `SP_BUILTIN_FOREIGN_PTR` box, which the
  collector does not follow, and NULL is nil. An `:int_array` or a
  `:float_array` argument is a C copy of the Array for the call.
- **Runtime functions.** An `ffi_func` with no `ffi_lib` can name a
  function of the runtime (`sp_net_shell_capture`). Fluxpinel links with
  `-rdynamic`, so `dlsym` finds the runtime functions in the binary.
- **native_func.** A package binds a C function with `native_func :name,
  [types], ret, "csym"`. Fluxpinel calls it through libffi, as it calls an
  `ffi_func`. The types follow `native_c_type` in `codegen_util.c` of
  spinel:
  - `:any` passes and returns an `sp_RbVal` by value, as a libffi struct.
  - `:string` is a String of the runtime, and `:string?` is one or NULL.
  - A `:cstring` or `:cbinstr` return is a buffer of the callee. Fluxpinel
    copies it, and a `:cbinstr` copy is binary.
  - `:int` is an `sp_int`, `:float` a double and `:bool` an `sp_bool`.
- **Package objects.** The Makefile links the object of each package
  (`sp_json_mt.o`, ...) and the libraries of its `ffi_lib`. It also keeps
  each bound function of the runtime archive in the link with `-Wl,-u`.
  `tools/native_link.rb` reads these from the packages.
- **Native classes.** A package binds a C struct as a class with
  `native_struct` (StringIO, StringScanner, `OpenSSL::X509::Store`):
  - The struct starts with the class id, so an instance is an object of
    the class for the dispatch of Fluxpinel. It has no ivars.
  - `native_new` binds a constructor, which takes the class id first.
    `native_method` binds a method, which takes the struct first.
  - The bindings of one name form one method of the class. A call takes
    the binding that spinel would pick at compile time: of the bindings
    of its arity, the first that takes each argument as is, else the
    first of the arity, else one with a `:rest`.
  - A subclass of a native class (`native_subclass`) is a gap at its
    first call. `native_share` has no effect: a StringIO does not write
    into the String that it was made on, as it does under
    `--share-strings`.
- **Limits.** `ffi_callback`, `ffi_struct`, `ffi_source`, a variadic
  function and an `IO::Buffer` argument are gaps. `FFI=0` builds Fluxpinel
  with no libffi, and then every FFI declaration and every native binding
  is a gap.

### 3.3.8 Speed

The walker is slower than CRuby. The target is the speed of the CRuby
interpreter with no JIT. A tree-walking interpreter on a runtime that it
borrows reaches it: abruby of ASTro (section 6) walks its tree on the
values, the GC and the parser of CRuby, and runs `fib` in 0.85 s where
CRuby takes 1.33 s.

Most of the cost of the walker is not the walk over the AST. It is the
work that each call does again on each run:

- **Dispatch by String.** A call finds a builtin method by its name: a
  chain of `strcmp` in each core file (about 500 comparisons in `src/`),
  then a search of the generated rows (`fx_bop_find`). A frequent method
  at the end of a chain pays for each comparison before it.
- **Lookup with no cache.** `fx_find_method` builds the list of the
  ancestors (`fx_ancestors`) and searches each method table, at each call.
- **The name.** A call turns the constant id of prism into a C string, and
  `sp_sym_intern` turns it back into a Symbol for each lookup.
- **Instance variables.** `fx_ivar_slot` searches the names of the class
  for the slot at each read and each write.
- **Jumps.** After each evaluation, the caller reads `L.ctl` in memory to
  see a pending `return`, `break` or `next`.
- **Frames.** A frame roots each slot with its own push to the GC root
  stack.

A bytecode VM would remove the walk itself, but it keeps each cost above.
The walker removes them. The measures, in order:

1. **A tree of the walker.** Translate the prism AST once, at load, into
   nodes of Fluxpinel. A node keeps what the translation resolves: the
   Symbol of a name, the slot of a local, and the cache fields of the
   measures below. The walker cannot add a field to a prism node, so each
   other measure needs this tree. A node can also carry a function
   pointer for its evaluation, so that a measure can replace the
   evaluation of one node (measure 5).
2. **Symbols, no strings.** The translation interns each name once. A
   lookup compares Symbols and does no `strcmp` and no `sp_sym_intern`.
   Builtin dispatch finds a core method in one table by (receiver kind,
   Symbol), not by a chain of comparisons.
3. **A method cache for each call node.** A call node keeps the class of
   the last receiver, the method that it found and a serial number. The
   cache hits when the class and the serial are the same. A definition, an
   `include`, an `extend` or a change of visibility increments one global
   serial, and thus clears every cache. A program defines its methods
   mostly at the start, so the caches stay valid at run time. The code of
   a miss stays out of line (`noinline`), but not in a cold section: a
   call site with more than one class takes the miss path often.
4. **Jumps in the result.** Each evaluation returns the value and a state
   (`return`, `break`, `next`) in two words, which stay in registers. A
   method boundary clears its state with one operation. An exception
   stays a raise of the runtime (`sp_raise`), so that a raise crosses to
   AOT code (section 3.6).
5. **Specialized operators.** The translation gives `a + b` and its kin a
   node for two Integers. When the node meets a Float, it changes its
   evaluation to the Float form, and then to the general form. A node
   that changes too often stays general. A program that redefines a
   builtin operator turns off each such node.
6. **An instance variable cache.** A node of `@x` keeps the class and the
   slot that it found last.
7. **Accessors.** A method that `attr_reader` or `attr_writer` defines
   runs with no frame.
8. **Frames.** Root the slots of a frame as one range, with one push.

Two parts of the work stay as they are:

- **Calls into the runtime.** A builtin method is a function of the spinel
  runtime, and the walker does not make it faster. abruby has the same
  limit: the functions of CRuby take about 30% of its profile.
- **Code generation.** ASTro gets most of its speed from C code that it
  generates for each node and compiles. A part of a program that needs
  that speed moves to AOT code in Fluxpinel, and spinel compiles it.

Each measure keeps the semantics. The oracle (section 4) checks this: the
same tests must agree after each step. A step must also show its gain on
a benchmark: `fib`, and `examples/logq/queries/per_row_block.rb`, which calls into
the walker once for each row of the log.

### 3.4 User classes

Spinel compiles each user class to a C struct. The `cls_id` of a class
indexes per-program tables that codegen emits (`sp_class_names[]` and
others). A class that the walker defines has no place in those tables.

Fluxpinel gives walker classes their own object kind (`src/object.c`):

- **Class.** A `FxClass` holds the method table, the singleton method
  table, the included and extended modules, the constants and the ivar
  slot names. Its runtime id starts at `FX_CLS_BASE` (0x100000), far
  above the ids of AOT classes. A builtin class (Integer, Object,
  StandardError, ...) gets a `FxClass` on first use, with the builtin
  hierarchy, so that a program can reopen it.
- **Object.** A `FxObject` is `{ cls_id, sp_PolyArray *iv }` on the spinel
  heap, allocated with `sp_gc_alloc` and a scan function. It starts with
  the class id, as an AOT object does. An instance of Object (`main`,
  `Object.new`) carries an alias id in the Fluxpinel range, so that the runtime
  sees a user object.
- **Hooks.** The runtime reaches a user object through function pointers.
  Fluxpinel sets `sp_obj_to_s_fn`, `sp_obj_inspect_fn`, `sp_obj_cls_name_fn`,
  `sp_class_is_module_fn`, `sp_user_binop_hook` and `sp_user_coerce_hook`.
  Thus `puts obj`, `p obj`, `[a, b].include?(c)` and `5 * money` call the
  methods of the program.
- **Constants.** A bare name looks in the lexical scopes but the top
  level, then in the ancestors of the innermost one, then in Object, then
  in the builtin classes. A top-level `def` defines a method of Object.
- **Freeze.** `freeze` on an object sets the frozen bit of its heap
  header, and an ivar write to a frozen object raises FrozenError. A class
  keeps its own frozen flag.
- **Gaps.** A semantics that Fluxpinel does not model stops the program as an
  unsupported node does (`fx_gap`): an instance of a subclass of a
  builtin class other than Object, Exception, Struct and Data, an ivar on
  a builtin value other than an exception, `extend` on an object, and the
  reopening of a CRuby core class that spinel's class table does not name
  (Random, Set, ...).

`sp_cext_dispatch_fn` sends a method by name to a foreign object. It is a
candidate for the AOT to walker direction of section 3.6.

### 3.4.1 Struct, Data and anonymous classes

- **Record class.** spinel compiles `Struct.new` and `Data.define` to a
  class of the program. Fluxpinel makes a `FxClass` at run time, with the
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
  `FxProc` through `dproc_self` (`src/hash.c`).
- **Typed Hash.** A method that reads a typed Hash takes a poly copy
  (`sp_poly_as_poly_poly_hash`). A method that changes one stops as a gap.
- **Array.** Codegen emits most methods of Array at the call site, so the
  table has no wrapper for them. `src/array.c` calls the helpers that the
  AOT code calls on a boxed Array (`sp_poly_sort`, `sp_poly_splice` and
  others). A mutator works on a poly array and writes the result back
  into a typed one (`sp_poly_arr_writeback`).
- **String.** `src/string.c` does the same for String. A mutator reads
  the bytes, calls the C function of the typed String and writes the new
  bytes back into the handle (`sp_poly_str_become`). A bang method with a
  plain form calls the plain form and answers nil when nothing changes.
  A String of the tag `SP_TAG_STR` comes from the runtime, so a mutator
  raises `FrozenError` on a frozen one and stops as a gap on another.
- **User objects in an Array.** The helpers reach the program through
  hooks: `sp_obj_cmp_hook` calls `<=>`, and `sp_obj_to_ary_fn`,
  `sp_obj_to_a_fn`, `sp_obj_to_int_fn` and `sp_obj_to_str_fn` call the
  conversion methods.
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

Spinel emits a program as a library with `--ext-init NAME` and
`--ext-entry Mod.m,...` (with `-c`). The output has two parts:

- The C file is the **kernel**. It holds the global state of the runtime
  (`ARGV`, the signal state), the symbol table and the class names of the
  program. Its `NAME()` takes the place of `main`.
- The header is the contract for a **host**: C code that calls into the
  kernel. A host is built with `SPINEL_EXT_HOST`, so that `spinel_rt.h`
  declares that global state `extern` and does not define it.

Fluxpinel is the host of use 1. The program that a user compiles is the kernel.
Fluxpinel does not use the header form of a host. `spinel_rt.h` keeps much state
`static` in each TU: the exception stack, the catch and break stacks and
the state of `ensure`. A raise of the kernel jumps to the handlers of the
kernel's TU, so a walker in another TU never catches it. Thus the host
build (`make host`) puts the kernel and Fluxpinel into one TU:

```
src/kernel_prelude.h   spinel_rt.h, with sp_class_to_s renamed
build/host/kernel.c    the kernel (spinel -c --ext-init fx_kernel_init)
src/main.c             Fluxpinel, with FX_KERNEL=fx_kernel_init
```

- **One state.** The walker and the kernel use the same handler stacks,
  the same symbol table and the same heap.
- **The class names.** The runtime names a class value through
  `sp_class_to_s`, and the kernel defines it for its own classes only.
  `kernel_prelude.h` includes `spinel_rt.h` with that name changed to
  `fx_class_to_s_any`. The calls inside the runtime thus go to Fluxpinel,
  which calls the function of the kernel for a class of the kernel.
- **The types.** The struct of each class of the kernel is in the TU, so
  Fluxpinel can read an ivar of an AOT object by its field.
- **The defines.** The kernel gives some defines before its include of
  `spinel_rt.h`, and they change what that header declares. Some depend
  on the program: `SP_TU_REOPENS_BUILTIN_CLASS` for a program that reopens
  `File`, as `File.foreach` does. The build copies them to
  `kernel_defines.h`, and `kernel_prelude.h` includes that file, so both
  sides see one `spinel_rt.h`.

- **The entries.** Each export is a C function, for example
  `sp_App_s_add`. A parameter with no caller in the program is an
  `sp_RbVal`. A caller in the program narrows the parameter to its type
  (`sp_int`). An `untyped` parameter in RBS keeps the `sp_RbVal` slot.
  An entry that only a script calls types its parameters with inline RBS
  (`#: (Array[Integer], String) -> Array[Integer]`). Otherwise each
  operation on a parameter goes through the poly dispatch.
- **The results.** An entry answers a C type: `sp_RbVal`, `sp_int`,
  `sp_float`, `sp_sym`, `const char *`, an Array, or a pointer to an
  object of the program. An object starts with its `cls_id`, so the host
  boxes it as an `sp_RbVal`.
- **Exceptions.** `NAME_try(fn, ctx, &cls, &msg)` runs `fn` under a rescue
  handler. It answers 0, or 1 and the class name and message of a raise.
  In the host build, a raise of the kernel reaches the rescue of a script
  directly.
- **In Fluxpinel.** `tools/entry_gen.rb` reads the header and writes a wrapper
  for each entry: it converts each argument to the C type of its parameter
  and boxes the result. An Array of the kind of the parameter (an Array of
  Integer that an entry answered) passes as it is. Any other Array passes
  as a copy of that kind. A Hash must be of the kind of the parameter, or
  the wrapper raises `TypeError`. Each class of the kernel is a `FxClass` with the
  id of the kernel, so a constant names it and an AOT object has it as its
  class. An entry is a singleton method of its module (`FX_M_ENTRY`).
  `new` of a class of the kernel is a gap, as the constructor is no entry.
- **The hooks.** The kernel sets the hooks of the runtime (`sp_obj_inspect_fn`,
  `sp_obj_cmp_hook` and their kin) to the functions that know its classes.
  Fluxpinel keeps each hook of the kernel and sets its own. The hook of Fluxpinel
  handles a class of Fluxpinel (`cls_id >= FX_CLS_BASE`) and calls the hook
  of the kernel for a class of the kernel (`0 <= cls_id < FX_CLS_BASE`).
  With no kernel, the kept hooks are NULL, so one source serves both
  builds.
- **Limits of the exports.** Fluxpinel works around them, and the spinel
  maintainer gets them as issues after a demo:
  - An entry that answers a String and has no caller in the program
    answers `NULL`.
  - Only the singleton methods of a module export. A method of an instance
    does not.
  - The kernel names only its own classes in `sp_class_to_s`.
    `kernel_prelude.h` works around this.

The other direction is AOT to walker. A `native_func` of spinel binds a
method of a module to a C function by its name, and an `:any` argument or
result is an `sp_RbVal` as it is. AOT code thus calls a C function of Fluxpinel
in the same TU, with no change to spinel. `kernel/fluxpinel.rb` declares these
functions (`src/host.c`), and a kernel does `require_relative` of it:

- `Fluxpinel.load(path)` runs a Ruby file at the top level and answers the
  value of its last statement.
- `Fluxpinel.eval(src)` does the same for a String.
- `Fluxpinel.call(obj, name, args)` calls a method of a value of Fluxpinel. The poly
  dispatch of AOT code does not know the classes of Fluxpinel.

A raise in Fluxpinel reaches a rescue of AOT code, and the reverse, through the
one exception stack. `kernel/fluxpinel_cruby.rb` gives the same functions under
CRuby, so the same program runs there for the answer of a test.

## 4. Use 2: oracle

The oracle runs one program three ways and compares the output.

| CRuby vs Fluxpinel | Fluxpinel vs AOT | Probable cause |
|---|---|---|
| same | same | no defect |
| same | differ | spinel analysis or codegen |
| differ | same | spinel runtime builtin |
| differ | differ | Fluxpinel, or more than one cause |

The test programs come from the spinel test suite. The harness also reports
each program that Fluxpinel cannot run. That list measures the coverage of Fluxpinel.

`spinel bisect` searches the optimization decisions inside one compile.
The oracle sits outside that: it gives a result with no decisions at all.

### 4.1 The harness

`make oracle` runs `tools/oracle.rb` on `$(SPINEL)/test/*.rb`.

- The CRuby answer is the `.expected` file of each test. Spinel snapshots it
  from CRuby.
- Fluxpinel runs first. Spinel compiles a test only when Fluxpinel runs it, so a run
  of the whole corpus costs little while the coverage of Fluxpinel is low.
- The harness compares stdout. It does not compare stderr or the exit
  status, as `.expected` holds stdout only.
- `FX_TRACE_MISSING=1` makes Fluxpinel report a missing builtin method. The
  harness then counts the test as a gap of Fluxpinel, not as a wrong answer.
- The report (`build/oracle/report.md`) names the spinel commit of the tree
  and of the Fluxpinel build. It warns when they differ.

Classes beyond the table above: `fx_only` (spinel refuses the program,
Fluxpinel gives the CRuby answer), `fx_unsupported`, `fx_missing` and
`fx_crash`.

Most of the time of the oracle goes to the compile of each test by spinel
and the C compiler. `make regress` skips this part:

- `make oracle` writes `tools/oracle_agree.txt`. The file names the tests
  that Fluxpinel answers right.
- `make regress` runs only these tests, under Fluxpinel only. It fails when
  a test no longer gives the CRuby answer.
- The run takes about 10 seconds, so it can follow each change of spinel.

### 4.2 The overflow mode

The oracle runs Fluxpinel and AOT in promote mode. Spinel's raise mode is not a
property of the runtime alone: the analysis still promotes an Integer where
it sees growth, for example `10 ** 30`. A walker has no analysis, so it
cannot make the same choice. In raise mode, 56 tests that Fluxpinel ran gave
`RangeError` where AOT gave a Bignum.

The corpus follows spinel's own promote lane: it leaves out the tests in
`RAISE_MODE_PINS` of spinel's Makefile.

### 4.3 A defect only Fluxpinel reaches

`"%c" % s` answers `""` when `s` is a mutable String handle in a poly slot.
CRuby answers the first character. The `%c` branch of
`sp_str_format_polyarr` (`lib/sp_poly_cold.c`) tests `SP_TAG_STR` only and
does not dereference a STRBUF handle. The AOT tests pass a literal there,
which is `SP_TAG_STR`. Fluxpinel makes every String literal a new handle, so it
takes the handle path on every run.

Fluxpinel does not work around such a defect, as the oracle must show it.
`tools/oracle_known.tsv` lists the known defects. The oracle counts a
listed test as `known` while it differs, and as `known_fixed` when it
agrees again, so that the line can go.

Thus a difference in the `fx_differs` class is not always a defect of
Fluxpinel. Fluxpinel holds every value in its most general form, and a runtime path
for that form can have a defect that the specialized AOT code never
reaches.

## 5. Use 1: dynamic layer

The three forms need different amounts from spinel. Each form builds on
the one before it.

| Form | Direction | Change to spinel |
|---|---|---|
| Scripts and the console | AOT calls Fluxpinel; Fluxpinel calls the entries | none |
| eval | AOT calls Fluxpinel with a String | none with no binding; a change for a binding |
| Dynamic dispatch | AOT hands an unresolved call to Fluxpinel | a change to the analysis |

Requirements for all forms:

- **One walker lock.** The walker is not shared between OS threads
  (section 3.3.5). AOT code on more than one thread calls Fluxpinel under one
  lock.
- **Speed.** The walker is slower than CRuby (sections 3.3.8 and 7).
  Fluxpinel is for the glue of a program, not for its hot paths.
- **The same semantics.** A value has the same builtin methods in Fluxpinel and
  in AOT code. A generated wrapper calls the runtime function of the AOT
  code. A hand-written method can differ (section 3.2.1). The oracle
  (section 4) checks both where a spinel test covers them.

### 5.1 Scripts and the console

The AOT program loads a Ruby file with `Fluxpinel.load` (section 3.6). The
script calls the entries of the program and gets AOT objects as values.
The program calls a method of the script with `Fluxpinel.call`.

The console is a session of the walker (`src/console.c`). The AOT program
owns the socket, and gives each line to the session.

- `Fluxpinel.session` makes a session. `Fluxpinel.feed(session, line)`
  adds the line to the piece, and answers the reply and the next prompt.
- A piece runs when prism parses it with no error. A piece is incomplete
  when an error is at its end or names a missing close (`*_term`, `*_eof`),
  and no stray token comes before its end. Then the session waits for more
  lines. Any other error is a syntax error.
- Each piece runs in a new frame whose parent is the frame of the piece
  before. The session keeps these frames on the heap. prism gets their
  names as the scopes around the piece, so a later piece reads an earlier
  local as a local of an outer scope. A Proc of an earlier piece thus sees
  a later write to its locals. The first frame holds `_`.
- The session catches an exception of a piece and answers it. A
  SystemExit (`exit`, `abort`) closes the session: `feed` answers nil from
  then on, and the AOT program closes the socket. The program runs on. A
  throw goes on.
- `puts` in a piece writes to the stdout of the process, not to the
  socket.
- `fluxpinel --console` runs a session on stdin. `kernel/fluxpinel_cruby.rb`
  gives the same session under CRuby, and `test/console.rb` compares the
  two.

- **Object layout.** A script reads the ivars of an AOT object with
  `instance_variable_get` and `instance_variables`.
  - spinel writes a table for the default `inspect` into the kernel
    (`sp_insp_tab`). The table gives the field, the nil byte and the kind
    of each ivar. The table is in the TU of the host build (section 3.6).
  - `tools/ivar_gen.rb` writes a box for each kind (`ivars.c`). A scalar
    kind takes the box from the `inspect` function of the kind. A
    container kind takes the box from the C type of its field.
  - An ivar that the object has not set reads as nil, as in CRuby.
  - A class that defines its own `inspect` has no row, so its ivars are a
    gap. A write of an ivar is a gap too: it needs a conversion to the
    type of the field.
- **Redefinition.** A script replaces a method only through a method table.
  AOT code that calls a method directly keeps the old method.

### 5.2 eval

AOT code calls `Fluxpinel.eval` with a String, and Fluxpinel evaluates it at the top
level (section 3.6). The string sees the globals, the constants and the
classes of the kernel. It does not see the locals of the AOT method: they
are C variables. `eval` with a binding needs those locals, so it needs a
change to spinel.

### 5.3 Dynamic dispatch

Spinel compiles a call of a method that no class defines, and the call
raises `NoMethodError` at run time. It does not call `method_missing`, and
it warns when a class defines one. Spinel refuses `define_method` with a
name that the program makes at run time. For dynamic dispatch, spinel must
emit code that hands such a call to Fluxpinel at run time. Then a class of
the program can use `method_missing` and `define_method` with a computed
name, and the static part of the program stays AOT code. A port of a CRuby application
can start with the dynamic parts in Fluxpinel, and move them to AOT code one by
one.

## 6. Prior art

- **suppify** (bash0C7/suppify). It wraps spinel output as a C library, a
  CRuby gem or a PicoRuby mrbgem. Each library has its own spinel heap.
  Values cross by copy (scalars, or MessagePack). Fluxpinel differs: one heap,
  values cross by reference.
- **R2P2-darwin PR #13** (bash0C7/R2P2-darwin). It calls a spinel kernel
  from the PicoRuby REPL through suppify.
- **`crystal i`.** Crystal keeps its semantic analysis and replaces LLVM
  codegen with a bytecode interpreter. The interpreter cannot run inside a
  compiled program: its type ids do not match the type ids of the compiled
  code, so objects cannot cross. (See the forum topic "Will it be possible
  to embed the upcoming Crystal interpreter?".) Fluxpinel avoids this: the
  host build gives each class of the kernel its id from the kernel
  (section 3.6).
- **nicht.** Iced (AOT) and naked (walker) code meet at the method table
  and share one value word. Fluxpinel applies the same idea to spinel.
- **ASTro** (ko1/astro). A framework for AST interpreters. It generates
  the evaluator of each node from one definition, and it specializes a
  tree into C code that a C compiler makes fast. Its sample abruby walks a
  tree on the runtime of CRuby, as Fluxpinel does on the runtime of
  spinel. The measures of section 3.3.8 come from abruby: a tree made
  from the prism AST, a method cache keyed by a serial, specialized
  operators and jumps in the result.

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
  the init hooks (`src/fx_tu.c`).
- `spinel_rt.h` declares hooks such as `sp_class_to_s` and `sp_sym_to_s`
  as `static`. Each TU that includes the header must define them. Thus all
  Fluxpinel code that uses the runtime is one TU (a unity build).
- A poly String has two forms. A literal is `SP_TAG_STR`. A new String is a
  mutable handle: `SP_TAG_OBJ` with `cls_id` `SP_BUILTIN_STRBUF` (-40). The
  walker treats both forms as String.
- The integer overflow mode is a compile-time macro
  (`SP_INT_OVERFLOW_MODE_RAISE`, `_WRAP` or `_PROMOTE`). The default is
  `raise`. Fluxpinel uses the mode of the AOT build that it runs with.
- Exceptions use the `setjmp` frame that spinel emits for `begin`/`rescue`
  (`sp_exc_stack`, `sp_exc_top`, `sp_last_exc_cls`). Fluxpinel code can use the
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
- The oracle on spinel `7ea20089b` (7824 tests, promote mode): Fluxpinel runs
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
  classes and modules: Fluxpinel runs 491 tests and 474 agree. The largest
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
  blocks: Fluxpinel runs 873 tests and 854 agree. 19 differ. The largest
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
  forwarding: Fluxpinel runs 1548 tests and 1519 agree. 29 differ, most of them
  in `respond_to?` and in class semantics. The largest blockers now:

  | blocker | tests |
  |---|---|
  | `begin` / `rescue` | 775 |
  | a Regexp literal | 247 |
  | `Class#new` with a block | 174 |
  | a global variable write | 171 |
  | a `rescue` modifier | 149 |
  | a multiple assignment | 145 |

- The oracle on spinel `b5d3820ad` (7910 tests), with exceptions: Fluxpinel
  runs 2023 tests and 1909 agree. 111 differ; about 30 of them check
  `private` or `freeze`, which Fluxpinel does not model. The largest blockers
  now:

  | blocker | tests |
  |---|---|
  | a Regexp literal | 262 |
  | a global variable write | 189 |
  | `Class#new` with a block | 181 |
  | a multiple assignment | 151 |
  | `case` / `when` | 132 |
  | a Range of values other than Integer | 101 |

- The oracle on spinel `b5d3820ad`, with Regexp: Fluxpinel runs 2210 tests and
  2095 agree. 112 differ. The largest blockers now:

  | blocker | tests |
  |---|---|
  | a global variable write | 191 |
  | `Class#new` with a block | 184 |
  | a multiple assignment | 151 |
  | `case` / `when` | 138 |
  | a Range of values other than Integer | 102 |
  | `case` / `in` | 73 |

- The oracle on spinel `b5d3820ad`, with global variables: Fluxpinel runs 2311
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
  classes: Fluxpinel runs 2509 tests and 2386 agree. 120 differ. The largest
  blockers now:

  | blocker | tests |
  |---|---|
  | a multiple assignment | 160 |
  | `case` / `when` | 146 |
  | `Thread.new`, `Fiber.new` or `Enumerator.new` with a block | 145 |
  | a Range of values other than Integer | 109 |
  | `case` / `in` | 90 |
  | a call of a block whose scope has returned | 67 |

- The oracle on spinel `89f0f421b` (7933 tests), with Fiber, Thread and
  Enumerator.new on the threaded runtime: Fluxpinel runs 2636 tests and 2503
  agree. A run of the previous Fluxpinel on the same corpus agreed on 2379. 133
  differ. The largest blockers now:

  | blocker | tests |
  |---|---|
  | a multiple assignment | 172 |
  | `case` / `when` | 149 |
  | a call of a block whose scope has returned | 112 |
  | a Range of values other than Integer | 110 |
  | `case` / `in` | 90 |
  | `alias` | 62 |

  The block whose scope has returned went from 67 to 112 tests: a Thread
  or a generator that a block makes (`3.times.map { Thread.new { } }`)
  now runs far enough to meet it.
- The single-threaded runtime archive lacks the parts of the scheduler
  that park a green thread: `sp_sched_sleep`, `sp_Thread_stop` and
  `sp_Mutex_sleep`. Only `libspinel_rt_mt.a` has them.
- A Thread name that is a heap String is freed by a minor collection when
  the Thread is the main one: `Thread.current.name = "main-" + x` and a
  collection, then `Thread.current.name` reads a freed slot. AOT code and
  Fluxpinel both fail under `SPINEL_GC_STRESS=2`. AOT code meets it less often,
  since a literal there is a static String (`gc_minor_thread_local_slot`
  passes AOT and fails Fluxpinel under the stress mode).
- A class keeps one lexical parent, the scope of its first definition.
  In CRuby, the lexical scope belongs to each class body: the body of
  `class A::B` does not search `A`. Fluxpinel searches `A` there
  (`class_new_superclass_order`).
- The runtime shows a class value by its name, through
  `fx_class_name_hook`. Thus `p K` for a Struct class with
  `keyword_init: true` shows `K`, but CRuby shows `K(keyword_init: true)`.
- `sp_re_matchdata` sets the captures of a match but not the positions
  (`sp_re_caps`) that `sp_re_last_matchdata` reads. Fluxpinel sets the
  registers again from the MatchData, so `$~` after `str.match(re)` reads
  this match.
- Fluxpinel makes a mutable String for each String literal. Thus
  `"ab12".slice!(/\d+/)` changes a String in Fluxpinel, but CRuby and AOT raise
  FrozenError (`string_slice_bang_regexp_arm`).
- `rescue` lets a program reach two old defects of Fluxpinel. A generated
  wrapper whose row has no argument kinds takes any argument: `"s".include?(:x)`
  passes a Symbol to `sp_str_include`, which crashes
  (`param_include_body_widen`). A rescue with `retry` around a method that
  Fluxpinel lacks retries forever (`string_split_local_each_retry`).
- A comparator block that does not compare (`sort { |x| x.length }`) gives
  an order that depends on the sort algorithm. Fluxpinel uses a stable merge
  sort, and CRuby uses its own quicksort. `comparator_block_arity` differs
  for this reason.
- The M2 walker runs `fib(32)` in 0.85 s. CRuby 4.0.6 runs it in 0.24 s,
  and CRuby with YJIT in 0.06 s. The walker compares each method name with
  `strcmp` and looks up methods by a linear search.

## 8. Open questions

- The smoke test in `poc/m1` checks 6 wrappers. How does Fluxpinel check the
  other wrappers? The oracle (M3) is one answer.
- Which AOT build of a test program serves as the oracle reference: one
  build per program, or one shared build?
- Which entries does a host get? An export names each method. A program
  that loads scripts wants a wide surface. Candidates are a list in the
  build, or a mark on a method in the source.
- Can a generator map the `sp_poly_*` helpers to method names, as
  `tools/bop_gen.c` does for `bop_rows[]`? Codegen names the helper that it
  emits for each method, so the map exists in the codegen source.

## 9. Plan

1. **M0: runtime alone.** `poc/m0` links only `libspinel_rt`. It makes
   boxed values, calls `sp_poly_*` operations, rescues an exception and
   runs the GC. Run it with `make -C poc/m0 run`.
2. **M1: template survey.** `poc/m1` generates the wrappers, compiles and
   links them, and reports the coverage. A smoke test calls a sample.
   Run it with `make -C poc/m1`.
3. **M2: minimal walker.** `build/fluxpinel FILE.rb` evaluates literals,
   locals, `def`, calls, `if`/`unless`, `while`/`until`, `return`, `break`
   and `next` on Integer, Float, String, Symbol and Array. `make test` runs
   `test/m2` under CRuby and Fluxpinel, in a plain run and in the GC stress run.
4. **M3: oracle harness.** `make oracle` runs the spinel corpus under
   CRuby, Fluxpinel and AOT in promote mode, and reports the classes of
   section 4 and the blockers of Fluxpinel.
5. **M3+: coverage.** Take the blockers in the order of the oracle report.
   Each step runs the oracle again. From M4 on, coverage is not a step of
   its own: take first the blockers that a demo meets, and then the
   blockers of the oracle report.
   - Classes, modules and constants (section 3.4): `test/m3`.
   - Blocks, Proc and Integer Range (section 3.3.1): `test/m3`.
   - Hash and keyword arguments (section 3.5): `test/m3`.
   - Exceptions: `raise`, `begin`/`rescue`/`else`/`ensure`, `retry` and
     the `rescue` modifier (section 3.3.2): `test/m3`.
   - Regexp, MatchData and the match globals (section 3.3.3): `test/m3`.
   - Global variables and the standard streams (section 3.3.4): `test/m3`.
   - Struct, Data and anonymous classes (section 3.4.1): `test/m3`.
   - Fiber, Thread and Enumerator.new on the threaded runtime (section
     3.3.5): `test/m3`.
   - `require_relative`, `require` of a path, `__FILE__`, `__LINE__` and
     `__dir__` (section 3.3.6): `test/m3`.
   - The FFI at run time through libffi (section 3.3.7): `test/m3`, whose
     `.expected` is the output of AOT code.
   - `require` of a library name, the packages of spinel and their
     `native_func` bindings (sections 3.3.6 and 3.3.7): `test/m3`.
   - Heap frames for a scope that a Proc outlives (section 3.3.1):
     `test/m3`.
   - Next, the tests of a Roundhouse output (`spin test`) under Fluxpinel and
     AOT. Roundhouse lowers a Rails app to Ruby that spinel compiles.
   - Then the largest blockers of the oracle report.
6. **M4: Fluxpinel as a host.** Build the kernel and Fluxpinel in one TU (section
   3.6). Fluxpinel uses the symbol table and the class names of the kernel, and
   chains each hook to the hook of the kernel. `make test-host` runs
   `test/m2` and `test/m3` on the host build, with `test/kernel/app.rb` as
   the kernel.
7. **M5: walker to AOT.** A tool reads the entries of a kernel and
   generates a wrapper for each one. The wrapper boxes the result.
   A script calls an entry as a singleton method of its module, and holds
   an AOT object as a value. Fluxpinel works around the limits of the exports.
8. **M6: scripts.** AOT code calls Fluxpinel through `kernel/fluxpinel.rb` (section
   3.6), and `test/host` checks it. The demo `examples/textkit`: an AOT
   program loads `plugins/*.rb` at start, and each plugin adds a filter and
   calls the AOT code.
9. **M7: console.** A REPL over a socket on the base of M6. It keeps the
   locals from one line to the next, and Prism tells an incomplete line.
10. **M8: eval with a binding and dynamic dispatch.** With the demos of M6
    and M7, ask the spinel maintainer for the changes of sections 5.2 and
    5.3.
