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

The main program of use 1 is a server whose framework and application
are written in the Ruby that spinel compiles. The same source runs in
three settings (section 5.5):

- **Development.** Fluxpinel alone runs the framework and the
  application. A change of a file needs no compile.
- **Deployment.** spinel compiles the framework and the application, and
  Fluxpinel is in the same binary. The interpreter makes the pages from
  the components of the user interface, and runs pure Ruby gems.
- **Test.** The same tests run on Fluxpinel and on the compiled binary.

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
(`lib/sp_gc.h`). An ENCODING value holds the name of the encoding. The
runtime tags a String as UTF-8 or ASCII-8BIT only, so an Encoding of
another name does not change the bytes. The walker roots each live value with
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

`fx_builtin_call` (`src/builtin.c`) tries these sources in order:

1. the methods of one kind of receiver, written by hand: Hash, Array,
   String, Exception, IO, Time, Complex, Encoding and others.
2. the methods that take a block (`fx_iter`). For a call with a block,
   the prelude follows. A miss is then a gap, or `NoMethodError` when
   CRuby lacks the method too (`src/responds.c`).
3. the core methods, written by hand (`fx_core`). Most of them call one
   `sp_poly_*` function, for example `sp_poly_add` for `+`.
4. the `sp_poly_*` helpers that answer one method as a whole, for example
   `sp_poly_floor` for `floor`. A table maps the method name to the helper.
5. the generated wrappers. A row of a library (`io/console`) answers after
   its `require` only, as the rows of spinel do. When the rows of a name
   take no count like the call's, the call is CRuby's `ArgumentError`,
   with the counts of the rows.
6. the methods of Object (`fx_object_core`), then `NoMethodError`.

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

### 3.2.2 The builtin layer stays thin

Fluxpinel has two layers. The dynamic layer (the walker, its frames and
blocks, the method tables, `eval`, `binding`, `method_missing`,
`respond_to?`, the GC roots, the lock, the host glue) is what spinel does
not do at run time. It stays. The builtin layer (the hand-written methods
of section 3.2, and `src/prelude.rb`) stands in for what spinel does not
give the walker yet: a runtime function, a row of `bop_rows[]`, or an
entry that calls a builtin method by name. It shrinks as spinel grows.

The rules:

- A hand-written method needs a reason that spinel cannot remove: the
  method calls the block of the walker, or spinel has no row and no
  runtime function for it. A method of the second kind is a candidate for
  a spinel pull request.
- When a row of spinel comes to cover a hand-written method, the method
  goes. `make regress` runs the corpus with `FX_TRACE_SHADOW=1`, and
  `tools/builtin_census.rb` fails on a call that a method written for its
  receiver (sources 1 and 2 of section 3.2) answered while a generated
  wrapper fits it (a shadow). A core method that calls one `sp_poly_*`
  function (sources 3 and 4) is no shadow: it is the runtime of spinel too.
  `tools/shadowed_known.txt` lists the shadows that are known, and the
  list only shrinks. A line with no reason is a method to remove. A line
  with a reason names what the row lacks for the walker (`$~`, the
  identity of the receiver, a form with more arguments): the method stays
  until spinel gives it, and the reason is the text of a spinel issue.
- `make census` prints the counts of the layer: the hand-written methods
  (`FX_IS` sites and prelude defs) and the generated wrappers. The first
  count goes down over time; the second follows spinel.

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
- **Method.** A `FxMethodValue` (`obj.method(:name)`) starts with a spinel
  `sp_BoundMethod`. Thus the runtime hashes it by the receiver and the
  name, as a Hash key needs. It keeps the method of the program that the
  name found, or no method for a builtin method, which a call sends by
  name. Its Proc (`to_proc`, `&m`) has a C block that calls the method.
  An UnboundMethod (`Mod.instance_method(:name)`, `unbind`) is the same
  value with the unbound flag of the runtime and no receiver. `bind`
  gives it one. A builtin method bound to a value of another class still
  sends by name: `Object.instance_method(:inspect).bind_call(nil)` runs
  `NilClass#inspect`.
- **Binding.** `binding` makes a `FxBinding` (`src/binding.c`), an
  instance of the native class `Binding`. It keeps the heap frame of the
  call, so the scope and the Binding share one set of variables, as a
  Proc does. `Binding#eval` and `Kernel#eval` with a Binding run the code
  in a new frame under the last frame of the Binding and keep that frame.
  Thus a later eval sees the locals of an earlier one, as the console does
  with its pieces (section 5.1). `local_variable_set` of a new name adds a
  frame of that one name: the scope of the call does not see it, as in
  CRuby. `local_variables` lists the frames from the newest, each name
  once. `dup` shares the frames and takes its own new ones from then on.
  `TOPLEVEL_BINDING` is the scope of the main program. The default file of
  an eval names its call, `(eval at FILE:LINE)`, as CRuby's does; `__dir__`
  is nil in such an eval and `require_relative` has no base path.
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
  `then`, `Array.new(n) { }` and `loop`. The prelude writes rarer ones
  in Ruby (section 3.2). A builtin method with a block that neither has
  stops as a gap, since a generated wrapper would drop the block. A
  collector with no block (`a.map`, `a.select`) answers an Enumerator
  that remembers the method, so `a.map.with_index { }` collects as `map`.

### 3.3.2 Exceptions

- **Value.** An exception is a spinel `sp_Exception`. It names its class
  (`cls_name`). For a class of the program, the name is the `FxClass`
  name, and `sp_user_exc_parent_fn` answers its superclass. Thus
  `sp_exc_cls_matches` and the report of an uncaught exception see the
  classes of the program (`src/exc.c`).
- **Backtrace.** A raise unwinds the frames on the C stack before a
  handler runs, so each frame stack keeps a record per depth on the heap
  (`FxTrace`): the statement or call under evaluation, the method or
  block, and the kind of frame. A landing (`FX_TRY_LANDED`) reads the
  records of the unwound frames before it cuts them, and puts the lines
  (`file:line:in 'A#m'`) on the exception, unless it has them already.
  `Kernel#caller` reads the same records, and `Kernel#caller_locations`
  makes a `Thread::Backtrace::Location` of each (`src/location.c`, an
  instance of a native class, as a Binding is), which holds the path,
  the line, the label and the base label of its line.
  `Exception#backtrace_locations` is nil: a landing keeps the lines only.
  A builtin method that the walker calls has a record too, with no frame
  (`fx_bt_builtin_enter`): the backtrace has `Array#each` and
  `Kernel#Integer` where CRuby has them, named by the class or module
  that owns the method in CRuby (`fx_rs_owner`). A method of Enumerable
  with a block has the `each` of its receiver above it. Two kinds of
  lines of CRuby are not there: the
  operators of the fast path (`Integer#/`), which make no record, and the
  methods that CRuby calls inside a builtin method (`File#initialize`
  under `IO.open`). The main program runs under
  a handler (`fx_run_main_source`) that gives an uncaught exception its
  backtrace and raises it again, with `sp_bt_enabled` set, so the report
  of the runtime prints the frames as CRuby does.
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
- **Catch.** `catch` pushes a catch entry of the runtime, in the shape of
  the code that spinel emits. `throw` calls `sp_throw_boxed`, which runs
  the ensure clauses between and lands at the entry of the tag. A catch
  with no tag makes a new Object as the tag. Each handler of the walker
  lets a throw that passes it go on (`sp_unwind_resume`). No rescue
  clause takes a throw.
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
- **`\X`.** The engine refuses a grapheme cluster: spinel carries no
  grapheme-break tables. Fluxpinel spells `\X` out before the compile,
  from the tables that the engine has and a few ranges (`src/re.c`), and
  `String#grapheme_clusters` scans with the same pattern. The answer is
  that of CRuby for combining marks, emoji ZWJ sequences, flags, skin
  tones, Hangul jamo and CR LF. The Prepend and Indic conjunct rules of
  UAX #29 are not in it: an accepted difference.
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

  An alias (`$-v`, `$PROGRAM_NAME`, `$:`, ...) names the same global, and
  so does one the program makes (`alias $new $old`, a table of
  `L.gvar_aliases` that `fx_gvar_canon` reads): a read, a write and
  `defined?` go to the name the alias stands for, a special global
  included, so `lib/English.rb` is the aliases of CRuby's library of
  the name. An assignment to a writable special global goes into the
  table, and a read prefers the table.
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
  end of the program. A `while` back-edge and a block call poll
  the safepoint flag, as AOT code does at a back-edge. A thread switch
  thus comes only where no jump is pending.
- **The walker lock.** The state of the walker (`L`) is not shared between
  OS threads. A green thread holds one lock while it runs walker code
  (`src/fx_tu.c`): the body of a Thread of the program, a call of the
  kernel into Fluxpinel, and a hook of the runtime on a value of the
  program. The lock is a Monitor of the runtime: a waiter parks and frees
  its worker. A wait in the runtime (I/O, `sleep`, a Queue, a safepoint
  poll, an `ffi_func` call) and a call of an entry of the kernel give the
  lock up for their time (`FX_UNLOCKED`), and a rescue landing takes it
  back (`FX_TRY_LANDED`). The way back can park too, when another thread
  holds the lock, and a collection runs in that park: a wait whose result
  is a value of the heap names it as a root until the lock is back
  (`FX_UNLOCKED_ROOT`). `tools/park_audit.rb` reads the runtime for the
  functions that park and checks each call of them (`make park-audit`).
  The host build thus keeps the workers of the kernel. The standalone command sets `SPINEL_WORKERS=1`, since the walker
  runs all its Ruby code, one thread at a time.
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
  current directory. After a file runs, `$LOADED_FEATURES` has its real
  path.
- **Library names.** spinel resolves `require "json"` at compile time
  (`src/spinel_parse.c`). Fluxpinel resolves the name at run time in the same
  order:
  - `lib/<name>.rb` of Fluxpinel comes first: a feature that spinel writes
    at compile time, whose package is a stub (`forwardable`), or a library
    that spinel has no package for (`delegate`).
  - `packages/<first>/<name>.rb` of spinel is the package. The first
    segment of the name selects the package. The file runs once, and its
    `<name>.share.rb` follows it when that file exists.
  - A feature of the runtime (`socket`, `ostruct`, `monitor`, `time`,
    `io/console`) and a core capability (`thread`, `fiber`, `rbconfig`,
    ...) load nothing. The require answers false. `io/console` turns on
    `IO#winsize`, `winsize=`, `raw`, `raw!` and `noecho` (`src/global.c`),
    which an IO does not answer before the require, as in CRuby.
  - Any other name is a LoadError. spinel refuses it at compile time under
    `--require-gate`, and ignores it with a warning otherwise.
- **The libraries in the binary.** The Ruby files of the libraries are in
  three directories: `packages/` and `builtins/` of spinel, and `lib/` of
  Fluxpinel. The make variable `EMBED` selects where a binary reads them.
  - `EMBED=0` (the default): the binary holds the paths of the three
    directories and reads the files at run time. It runs on a machine that
    has the checkouts at those paths. A change of a file in `lib/` needs
    no build.
  - `EMBED=1`: `tools/embed_gen.rb` writes the files as a table of C, and
    the binary holds them. The files have paths under `/@fluxpinel/`
    (`/@fluxpinel/packages/json/json.rb`), which no file system has. The
    loader reads a path under that root from the table only
    (`src/load.c`), so the binary reads the same libraries on each
    machine. `__FILE__`, `$LOADED_FEATURES` and a backtrace show those
    paths.
  - `make host EMBED=1` gives a host binary that runs with no checkout of
    spinel or Fluxpinel. `make test-embed` runs the tests on a build with
    `EMBED=1`.
  - `EMBED_EXTRA` adds directories of a program to the table, as
    `NAME=DIR` pairs. `DIR/a/b.rb` has the path `/@fluxpinel/NAME/a/b.rb`.
    A `require`, a `load` or `Fluxpinel.load` of that path reads the
    table, and a `require_relative` in such a file reads its neighbor.
    Each `.rb` file under `DIR` goes in (a directory named `test` also;
    the three directories of the libraries leave theirs out). The table
    has no listing: `Dir.glob` and `File.exist?` do not see its paths.
  - The gems of IRB (`--irb`, `--irb-port`) and each other file that a
    program names are files of the disk in each mode.
- **Warnings of the parser.** A source with no error can have warnings
  of prism (a duplicated Hash key, `= literal` in a conditional). Each
  one goes to `Warning.warn` as `path:line: warning: message`, as in
  CRuby: for the main program, a `require`, a `load`, an autoload and an
  `eval`. `$VERBOSE` false leaves out the ones that prism marks for `-w`,
  and `$VERBOSE` nil leaves out all of them. A `Warning.warn` of the
  program gets them, and one that raises ends the load of the file.
- **Autoload.** `autoload(name, path)` records the path on the module
  (`src/object.c`). The first read of the constant loads the file, as a
  require of the path does. `defined?` does not load it. `Set` is such a
  constant of Object: spinel loads its package when a program names `Set`,
  and so does Fluxpinel. Thus `require "set"` answers false. `RbConfig`
  and `Gem` are such constants too, from `builtins/*.rb` of spinel, which
  its parser splices when a program names them.
- **Enumerator::Lazy.** `Enumerator::Lazy` is such a constant of
  Enumerator, from `lib/fluxpinel/lazy.rb` of Fluxpinel. spinel compiles
  a chain of lazy calls to one loop and has no Lazy object in its runtime,
  so the class is Ruby code of Fluxpinel. `lazy` of a builtin collection
  or of an Enumerable object makes `Enumerator::Lazy.new(self)`
  (`src/iter.c`). The class is a subclass of the builtin Enumerator. It
  writes every method it answers, so its instances need no builtin part
  (`src/object.c`). Its helper methods have names that start with
  `__fx_`, and `instance_methods` shows them, which is a difference from
  CRuby.

### 3.3.7 FFI

- **Run time, not compile time.** spinel reads `ffi_func` and its kin at
  compile time and emits a direct C call for each function. Fluxpinel meets the
  same declarations in a module body at run time (`src/ffi.c`):
  - `ffi_lib` opens the library with `dlopen`: `libNAME.so`, or
    `libNAME.dylib` on macOS, or a versioned name when only that exists.
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
- **Callbacks.** `ffi_callback` declares a type for the arguments of the
  `ffi_func`s of its module. A call gives such an argument a Method or a
  Proc, and a closure of libffi calls it with the C arguments as values.
  The closure takes the walker lock, as the call gave it up. C keeps the
  function pointer, so each closure and its value stay for the run.
- **The ffi gem.** The ffi package of spinel keeps the functions of
  `attach_function` in a table on the module, and spinel defines a method
  for each at compile time. Fluxpinel answers a missed call on a module
  that extends `FFI::Library` from that table (`fx_ffi_attached`), and a
  variable of `attach_variable` the same way. The fiddle package keeps the
  functions of `Fiddle::Importer#extern` in a table of its own and answers
  `__ffi_attached?`; a missed call on a module that extends the importer
  asks it.
- **Limits.** `ffi_source` does nothing: Fluxpinel cannot compile the
  fragment, so a function that only the fragment defines is not found at
  its first call. `ffi_struct`, a variadic function and an `IO::Buffer`
  argument are gaps. `FFI=0` builds Fluxpinel with no libffi, and then
  every FFI declaration and every native binding is a gap.

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
  `fx_fast_call` answers the commonest calls first, by the Symbol: the
  operators of two Integers or two Floats, `Integer#[]`, `==` and `===`
  of two Symbols, `Array#[]`, `[]=`, `<<`, `size`, `length` and `Hash#[]`.
  A program that defines one of the names for a builtin class turns the
  calls of that class off (`fx_fast_off`). The calls of the other classes
  stay on.
- **Lookup.** `fx_find_method` builds the list of the ancestors
  (`fx_ancestors`) and searches each method table. The record of a call
  node keeps the last answer for an object of the program (measure 3).
  One global cache keeps the answer for each (class, Symbol), a miss
  included. A change of a method table or of the ancestors increments one
  generation, and thus clears every entry.
- **The name.** `fx_sym` keeps the Symbol of each constant id of prism,
  so the walker interns a name once. A call still passes the name to the
  builtin dispatch as a C string. `fx_name_sym` turns the text of a Symbol
  back into the Symbol by its pointer, with no intern.
- **Instance variables.** The record of each node of `@x`, and each
  accessor, keeps the class id and the slot of its last access (measure
  6). A miss goes to one global cache of the slot for each (class,
  Symbol). A slot never moves, so an entry stays right: a slot that
  `remove_instance_variable` took, or that a write to a later slot passed
  over, holds a nil marked gone (`FX_IVAR_GONE`), which a read shows as
  nil and the lists, inspect and Marshal pass over.
- **Jumps.** After each evaluation, the caller reads `L.ctl` in memory to
  see a pending `return`, `break` or `next`.

A bytecode VM would remove the walk itself, but it keeps each cost above.
The walker removes them. The measures, in order:

1. **A side table of the nodes.** Each prism node has one record
   (`FxSide`) in a side table, and `fx_side` finds it by the node id. The
   parses number their nodes with one count, so a node id is unique in
   the process. A record keeps what the walker resolves once, such as
   the cache fields of the measures below. The prism tree stays as it
   is. [side-table.md](side-table.md) describes the table and its
   records.
2. **Symbols, no strings.** The walker interns each name once. A
   lookup compares Symbols and does no `strcmp` and no `sp_sym_intern`.
   Builtin dispatch finds a core method in one table by (receiver kind,
   Symbol), not by a chain of comparisons.
3. **A method cache for each call node.** The record of a call node keeps
   the class id of the last receiver, the method that it found and the
   generation (`FxCallCache`). The cache serves only an object of the
   program, as the receiver or as self. It hits when the class id and the
   generation are the same. A definition, an `include` or an `extend`
   increments the generation, and thus clears every cache. A change of
   visibility writes the method in place, and the call checks the
   visibility of the method each time. A program defines its methods
   mostly at the start, so the caches stay valid at run time. The global
   cache of `fx_find_method` stays for a miss. This measure is in place.
4. **Jumps in the result.** Each evaluation returns the value and a state
   (`return`, `break`, `next`) in two words, which stay in registers. A
   method boundary clears its state with one operation. An exception
   stays a raise of the runtime (`sp_raise`), so that a raise crosses to
   AOT code (section 3.6). This measure is on hold (side-table.md, section 4).
5. **Specialized operators.** The record of `a + b` and its kin keeps a
   function pointer for an evaluation of two Integers. When the node meets
   a Float, it changes its evaluation to the Float form, and then to the
   general form. A node that changes too often stays general. A program
   that redefines a builtin operator turns off each such node. This
   measure is on hold for the cost of maintenance (side-table.md,
   section 4).
6. **An instance variable cache.** The record of a node of `@x` keeps the
   class id and the slot that it found last (`FxIvarCache`). An accessor
   keeps the same cache in its `FxMethod`. The global slot cache stays
   for a miss. This measure is in place.
7. **Accessors.** A method that `attr_reader` or `attr_writer` defines
   runs with no frame (`fx_invoke_b`).
8. **Frames.** The slots of a frame are one root frame of the runtime
   (`SP_GC_ROOT_FRAME`), with one push (`FX_FRAME`).

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
  hierarchy, so that a program can reopen it. An alias of a builtin
  method is an entry that calls the builtin method by its name, with the
  visibility that Kernel gives it.
- **Object.** A `FxObject` is `{ cls_id, sp_PolyArray *iv }` on the spinel
  heap, allocated with `sp_gc_alloc` and a scan function. It starts with
  the class id, as an AOT object does. An instance of Object (`main`,
  `Object.new`) carries an alias id in the Fluxpinel range, so that the runtime
  sees a user object.
- **Hooks.** The runtime reaches a user object through function pointers.
  Fluxpinel sets `sp_obj_to_s_fn`, `sp_obj_inspect_fn`, `sp_obj_cls_name_fn`,
  `sp_class_is_module_fn`, `sp_user_binop_hook`, `sp_user_coerce_hook`,
  `sp_obj_conv_fn` and others. Thus `puts obj`, `p obj`,
  `[a, b].include?(c)`, `5 * money` and `format("%f", obj)` call the
  methods of the program. `sp_user_exc_to_s_fn` and
  `sp_user_exc_message_fn` do the same for an exception.
- **Reflection.** `respond_to?`, `method_defined?` and a miss of a method
  read a table of the public methods of CRuby (`src/responds.c`).
  `methods`, `public_methods`, `protected_methods` and `private_methods`
  list the methods of the program along the ancestors, then the builtin
  names from that table and the private methods of Kernel.
  `global_variables` lists the special globals that Fluxpinel reads and
  the globals that the program assigned, not in the order of CRuby.
- **Constants.** A bare name looks in the lexical scopes but the top
  level, then in the ancestors of the innermost one, then in Object, then
  in the builtin classes. A top-level `def` defines a method of Object.
- **Freeze.** `freeze` on an object sets the frozen bit of its heap
  header, and an ivar write to a frozen object raises FrozenError. A class
  keeps its own frozen flag.
- **A subclass of Array, Hash or String.** Its instance is an Array, a
  Hash or a String handle with the class and its ivars after it
  (`FxArraySub`, `FxHashSub`, `FxStrSub`), as spinel lays one out. The
  runtime sees an Array, a Hash or a String, and `sp_bsub_cls_fn` gives it
  the class; the walker finds the class by the scan function of the heap
  object. A call finds the methods of the class first, then the builtin
  ones; `super` reaches those. `dup` and `clone` keep the class, and so do
  `merge` and `compact` of a Hash; `to_a` and `to_h` are plain.
  - A method of String that changes its receiver (`<<`, `upcase!`)
    changes the instance. Any other one answers a plain String, as in
    CRuby, and so do `to_s` and `to_str`.
  - `==`, `!=`, `eql?`, `===` and `<=>` run on a plain String of the
    bytes: the comparisons of the runtime read the class of the receiver.
  - `Class.new(String)` and its kin for Array and Hash make such a class.
- **Marshal.** `Marshal.dump` and `Marshal.load` run the serializer of
  the runtime (`src/marshal.c`). The runtime writes and reads the builtin
  values, and asks its vtable for an object of the program: Fluxpinel
  writes an object as CRuby does (`o`, the class, the ivars that are set),
  an instance of a subclass of Array, Hash or String as `C`, and reads one back
  into an instance of the class by its name. In the host build, the kernel
  fills the vtable for its classes first, and Fluxpinel chains to it. A
  record (Struct, Data), a native object, a Proc and a Binding raise
  TypeError. A depth limit, a proc and `freeze: true` are gaps. The
  runtime writes a Complex with Float parts, so `Complex(1.0, 2.0)` loads
  as `(1+2i)`.
- **Singleton class.** `def obj.m`, `class << obj`, `extend` and
  `define_singleton_method` on a heap value give it a singleton class: an
  anonymous class under its class, in a table keyed by the heap pointer
  (`fx_singleton_of`). The object keeps its class id, so `class`,
  `inspect` and the ivar slots do not change; the lookup of a method of
  a value asks the singleton class first (`fx_lookup_class`), and a call
  node whose receiver has one takes the general path instead of its
  cache. A value with no heap pointer (an Integer, a Symbol, nil) raises
  TypeError, as in CRuby. The runtime does not see the singleton class:
  `p obj` prints as the class does. The singleton class of a class M
  (`M.singleton_class`, `fx_meta_of`) is a marker class: a definition
  through it, and a def in its `class_eval`, go to the class methods of
  M (`smethods`), as `class << self` does, and `instance_methods` lists
  them.
- **Refinements.** `refine C do ... end` in a module makes a refinement: an
  anonymous class under C, whose methods the block defines, kept in the
  module. `using M` makes the refinements of M active in a range of the
  file: from the call to the end of the file, or to the end of the class
  body around it. A call node in that range asks the active refinements
  of a class of its receiver first (`fx_refined_call`); `super` in a
  refined method starts at C. `send`, `respond_to?` and `method` at such
  a node see the refinement, as in CRuby. `Symbol#to_proc` does not. A
  refinement is a Class, not a Refinement; `Module#refinements` and
  `import_methods` are not defined.
- **Gaps.** A semantics that Fluxpinel does not model stops the program as an
  unsupported node does (`fx_gap`): an instance of a subclass of a
  builtin class other than Object, Exception, Struct, Data, Array and
  Hash, an ivar on a builtin value other than an exception, and the
  reopening of a CRuby core class that spinel's class table does not name
  (Random, Method, ...).

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
  holds it. The superclass is a class of the program, Object, BasicObject
  or an exception class. Another builtin class is a gap.

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
  A mutator raises `FrozenError` on a frozen String.
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

Spinel emits a program as a library with `--ext-init NAME`,
`--ext-entry Mod.m,...` and `--ext-class C,...` (with `-c`). The output
has two parts:

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
build/host/entries.c   the wrappers of the entries (tools/entry_gen.rb)
build/host/ivars.c     the boxes of the ivars (tools/ivar_gen.rb)
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

A script reaches the kernel in two ways: the entries, and the methods of
the classes. Both are flags of spinel, with no change to it.

### 3.6.1 The classes and the objects

Each class of the kernel is a `FxClass` with the id of the kernel
(`fx_host_init` in `src/host.c`). A constant of the script names the
class, and an AOT object has it as its class. A script reads the ivars of
an AOT object (section 5.1).

### 3.6.2 The entries

`--ext-entry Mod.m,...` exports each method as a C function with typed
parameters, for example `sp_App_s_add`. A kernel can have no entry:
`make host` with no `KERNEL_ENTRIES` passes no `--ext-entry`. A script
then calls the methods of the table only (section 3.6.3).

- **The parameters.** A parameter with no caller in the program is an
  `sp_RbVal`. A caller in the program narrows the parameter to its type
  (`sp_int`). An `untyped` parameter in RBS keeps the `sp_RbVal` slot.
  An entry that only a script calls types its parameters with inline RBS
  (`#: (Array[Integer], String) -> Array[Integer]`). Otherwise each
  operation on a parameter goes through the poly dispatch.
- **The results.** An entry answers a C type: `sp_RbVal`, `sp_int`,
  `sp_float`, `sp_sym`, `const char *`, an Array, or a pointer to an
  object of the program. An object starts with its `cls_id`, so the host
  boxes it as an `sp_RbVal`.
- **The wrappers.** `tools/entry_gen.rb` reads the header and writes a
  wrapper for each entry (`entries.c`). The wrapper converts each argument
  to the C type of its parameter and boxes the result. An entry is a
  singleton method of its module (`FX_M_ENTRY`).
- **The checks of a wrapper.** A value of a wrong kind raises `TypeError`
  before the entry reads it.
  - An Array of the kind of the parameter (an Array of Integer that an
    entry answered) passes as it is. Any other Array passes as a copy of
    that kind.
  - A Hash must be of the kind of the parameter. A Hash parameter whose
    values are of any kind takes a copy of any Hash.
  - A String of either form passes: the handle as it is, and the literal
    form (the String of `Symbol#to_s`) as a handle of its own.
  - An object must be of the class of the parameter or of a class under
    it. The wrapper reads the class id from the kernel, where each
    constructor sets it. nil does not pass: the header does not say
    whether the parameter takes nil.

### 3.6.3 The methods of the classes

`--ext-class C,...` (or `all`) exports the public methods of the classes
by name. `make host` passes `KERNEL_EXT_CLASS`, which is `all` by default.

- **The table.** spinel writes a table for each class
  (`sp_ext_methods`): a row for each method of a `def` or a `def self.`,
  for an inherited one too, and for each `attr_*` reader and writer.
  `docs/spin.md` of spinel has the rules. These rows matter to a script:
  - A method with optional or rest parameters has a row for each count
    of arguments.
  - A method that takes a block has its rows twice: with the block and
    with none.
  - `new` has a row for each count of arguments that the `initialize` of
    the class takes.
- **The methods in Fluxpinel.** At its start, Fluxpinel makes each row a
  method of the `FxClass` of its class (`FX_M_EXTM`). A class defines the
  rows that it owns, and its ancestors give the inherited ones. The rows
  of one name are one method, whose arity is the one that `Method#arity`
  counts. An entry of the same name keeps its wrapper.
- **A call.** A call goes to `sp_ext_send` of the kernel
  (`fx_extm_invoke`). The thunk of the row converts the receiver and the
  arguments. It raises `ArgumentError` for a count that no row takes, and
  `TypeError` for a value of a wrong kind.
- **`new`.** `new` of a class of the kernel is a singleton method of the
  table, so a script makes an object of the kernel. `Method#arity` of it
  is -1, as `Class#new` has in CRuby. A class with no row of `new`
  (`docs/spin.md` of spinel names them) and `allocate` are a gap. A class
  of the script under a class of the kernel inherits the methods and has
  no row, so a call of one is a gap too.
- **A block.** A script passes a block to a method that takes one (the
  rows of `block` 1). The kernel gets a Proc of the runtime, and the Proc
  yields to the block under the walker lock (`fx_extm_invoke_block`).
  - A `break` in the block, or a `return` of the method around the call,
    leaves the frames of the kernel through a break scope of the runtime
    (`sp_brk_push`). The `ensure` clauses of the kernel run on the way.
  - A `throw` and a raise in the block pass the kernel in the same way.
  - A block that the kernel keeps can run after the call. A `break` in
    it then raises `LocalJumpError`, as in CRuby.
  - A method that takes no block does not use the block.

### 3.6.4 The state that both sides share

- **Exceptions.** A raise of the kernel reaches a rescue of a script, and
  the reverse, through the one exception stack. A `throw` reaches its
  `catch` across the frames of the other side too. (For a host in a TU of
  its own, the header has `NAME_try(fn, ctx, &cls, &msg)`, which runs `fn`
  under a rescue handler. Fluxpinel does not use it.)
- **The walker lock.** The kernel runs without the walker lock: a call of
  an entry or of a method of the table gives the lock up, and a call of
  the kernel into Fluxpinel takes it (section 3.3.5). The kernel thus
  calls Fluxpinel from any of its threads.
- **The hooks.** The kernel sets the hooks of the runtime (`sp_obj_inspect_fn`,
  `sp_obj_cmp_hook` and their kin) to the functions that know its classes.
  Fluxpinel keeps each hook of the kernel and sets its own. The hook of Fluxpinel
  handles a class of Fluxpinel (`cls_id >= FX_CLS_BASE`) and calls the hook
  of the kernel for a class of the kernel (`0 <= cls_id < FX_CLS_BASE`).
  With no kernel, the kept hooks are NULL, so one source serves both
  builds.

### 3.6.5 From the kernel to a script

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
- `Fluxpinel.session` and `Fluxpinel.feed(session, line)` run a console
  session (section 5.1).

`kernel/fluxpinel_cruby.rb` gives the same functions under CRuby, so the
same program runs there for the answer of a test.

### 3.6.6 Limits of the exports

- A method with a keyword parameter has no row in the table, so a script
  does not call it by name.
- `--ext-class all` makes each public method a root of the program.
  spinel can then refuse a program that it compiles with no flag. Name
  the classes in that case (`KERNEL_EXT_CLASS`, see section 5.4.5).
- The kernel names only its own classes in `sp_class_to_s`.
  `kernel_prelude.h` works around this.
- A script does not replace a method that AOT code calls directly
  (section 5.1).

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
  from CRuby. A test marked `# spinel: not-cruby` records the answer of
  spinel instead, and the oracle skips it.
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

A syntax or a call that Fluxpinel does not have stops the program with
status 2 (`fx_unsupported`, `fx_gap`), which the oracle reads. A process
with a session of a user is different: `--irb`, `--irb-port` and
`Fluxpinel::Session` make both raise `NotImplementedError`. A line of the
session then shows the error, and the program runs on.

Most of the time of the oracle goes to the compile of each test by spinel
and the C compiler. `make regress` skips this part:

- `make oracle` writes `tools/oracle_agree.txt`. The file names the tests
  that Fluxpinel answers right.
- `make regress` runs only these tests, under Fluxpinel only. It fails when
  a test no longer gives the CRuby answer.
- The run takes about 10 seconds, so it can follow each change of spinel.

### 4.2 The gems of IRB

The goal of these two checks is IRB on Fluxpinel. The gems come from the
CRuby that runs make.

- **The gem oracle.** `make gem-oracle` runs `tools/gem_oracle.rb`. It
  loads each entry file of the packages of spinel, and each file of the
  gems that IRB needs (reline, irb, prism, rdoc), under Fluxpinel. A file
  that CRuby cannot load alone is skipped. The report
  (`build/gem_oracle/report.md`) gives the first failure of each item, and
  groups the failures by reason.
- **The sessions.** A file that loads can still give a wrong answer. `make
  test` runs `test/irb.rb`: each session in `test/irb` is the input of an
  IRB with no terminal. The test runs it under the IRB of CRuby and under
  `fluxpinel --irb`, and compares the output. It reads an address and a
  measured time as equal, and drops each line of a backtrace after the
  first. A session holds only the lines that give the same output.
- **The terminal.** A session with no terminal does not use the line
  editor of Reline. `make test` also runs `test/irb_tty.rb`, which starts
  `fluxpinel --irb` on a pseudo terminal and types as a user does. It
  checks the prompt, a def of three lines, the completion of Tab, Ctrl-C,
  the history of the up arrow, an error and `exit`. It waits for a text
  on the screen at each step, and does not compare with CRuby: the two
  draw the same screen with different sequences.

### 4.3 The overflow mode

The oracle runs Fluxpinel and AOT in promote mode. Spinel's raise mode is not a
property of the runtime alone: the analysis still promotes an Integer where
it sees growth, for example `10 ** 30`. A walker has no analysis, so it
cannot make the same choice. In raise mode, 56 tests that Fluxpinel ran gave
`RangeError` where AOT gave a Bignum.

The corpus follows spinel's own promote lane: it leaves out the tests in
`RAISE_MODE_PINS` of spinel's Makefile.

Fluxpinel alone is built in promote mode by default (`INT_OVERFLOW`), so
`build/fluxpinel` is the binary of the oracle. No code of spinel is in
that binary, and the walker holds each value in a box: a Bignum can go
where an Integer goes. A host build is different. The mode is a macro of
`spinel_rt.h`, and Fluxpinel and the kernel are one TU, so they have one
mode (`HOST_INT_OVERFLOW`). The C code of the kernel also has the shape
of its mode: in raise mode, a local of spinel is a machine integer with no
room for a Bignum. The default there is raise, the default of spinel.

### 4.4 A defect only Fluxpinel reaches

Fluxpinel holds every value in a box, and calls the functions of the
runtime for a boxed value. Code that spinel compiles holds most values in
a typed form, and reaches those functions less. A defect in such a
function can show in Fluxpinel only.

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
| Scripts and the console | AOT calls Fluxpinel; Fluxpinel calls the entries and the methods of the classes | none |
| eval | AOT calls Fluxpinel with a String | none with no binding; a change for a binding |
| Dynamic dispatch | AOT hands an unresolved call to Fluxpinel | a change to the analysis |

Requirements for all forms:

- **One walker lock.** The walker is not shared between OS threads
  (section 3.3.5). AOT code on more than one thread calls Fluxpinel under one
  lock, which Fluxpinel takes itself.
- **Speed.** The walker is slower than CRuby (sections 3.3.8 and 7).
  Fluxpinel is for the glue of a program, not for its hot paths.
- **The same semantics.** A value has the same builtin methods in Fluxpinel and
  in AOT code. A generated wrapper calls the runtime function of the AOT
  code. A hand-written method can differ (section 3.2.1). The oracle
  (section 4) checks both where a spinel test covers them.

### 5.1 Scripts and the console

The AOT program loads a Ruby file with `Fluxpinel.load` (section 3.6). The
script calls the entries of the program and gets AOT objects as values.
It calls the methods of those objects by name, passes a block to them, and
makes an object with `new` (section 3.6.3). The program calls a method of
the script with `Fluxpinel.call`.

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
- `--irb` and `--irb-port PORT` run IRB of the gems of CRuby in place of
  this console: on the terminal, or on each telnet connection to the
  running program (`lib/fluxpinel/remote_irb.rb`, and the README).

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

### 5.4 The console of a compiled Rails app (Roundhouse)

Roundhouse lowers a Rails app to Ruby that Spinel compiles. Fluxpinel
adds a console to the blog of Roundhouse (`fixtures/real-blog`, a Rails 8
scaffold of articles and comments). A client connects to the running
server over a socket:

```
$ nc 127.0.0.1 4000
fx> Console.count("Article")
=> 3
fx> a = Console.find("Article", 1)
fx> a.instance_variable_get(:@title)
=> "Getting Started with Rails"
fx> c = Console.create("Comment", {article_id: 1, commenter: "fx", body: "from the console"})
fx> Console.count("Comment")
=> 4
fx> exit
```

The server serves its pages all the time. `exit` closes the session only.

The blog and Campfire (section 5.4.7) are checks of the host build on
large kernels: a move of the pin of spinel runs them
(docs/development.md). The plan (section 9) has no step for Roundhouse.

### 5.4.1 The files

| File | What it does |
|---|---|
| `ROUNDHOUSE_COMMIT` | holds the pin: the Roundhouse commit that Fluxpinel uses |
| `roundhouse/roundhouse.mk` | holds the make targets of this section |
| `CAMPFIRE_COMMIT` | holds the commit of Campfire (section 5.4.7) |
| `roundhouse/apply.rb` | changes the Spinel output of an application |
| `roundhouse/spin_flags.rb` | writes the flags of the packages of the output |
| `roundhouse/console.rb` | is the console and its entries |
| `test/roundhouse/test.rb` | runs the host build of the blog and checks it |
| `test/roundhouse/campfire.rb` | runs the host build of Campfire and checks it (section 5.4.7) |
| `test/roundhouse/helper.rb` | holds the helpers of the two tests |

For the update of the pin, see [development.md](development.md).

### 5.4.2 Changes to the output of Roundhouse

The output of Roundhouse runs the server at its top level. `apply.rb`
changes four things:

- It moves the start of the server in `main.rb` into an entry,
  `Main.start`. Fluxpinel starts after the top level of the kernel, so the
  host build calls this entry. With `FLUXPINEL_CONSOLE=PORT`, `Main.start`
  also starts the console.
- It writes `console.rb`. This file accepts the connections of the console
  and gives each line to `Fluxpinel.feed`. It also has the entries
  `Console.count`, `Console.find` and `Console.create`. Each entry has a
  `when` for each model of the application.
- It copies `kernel/fluxpinel.rb` of Fluxpinel beside `console.rb`.
- It writes `.fx-ext-class`, the classes of `--ext-class` for the host
  build.

`apply.rb` reads the classes from `app/models` of the output:

- A class of records is a class whose superclasses go up to
  `ActiveRecord::Base`. The class right under `ActiveRecord::Base`
  (`ApplicationRecord`) is not one. These classes go in `.fx-ext-class`.
- A model with a table is a class of records with a file `NAME_row.rb`
  beside its `NAME.rb`. Roundhouse writes that file for each table. These
  models get a `when` in `console.rb`.

A class of single table inheritance (`Rooms::Open < Room`) is a class of
records with no table of its own.

`spin_flags.rb` runs `spin flags` in the output, which is a spin project.
It writes `.fx-kernel-flags` (the `-I` of each package, `--rbs`) and
`.fx-host-libs` (the objects and libraries of the packages). The link
also reads the libraries that the kernel binds with `ffi_lib` from the
kernel C, so no file names them.

Each change of `main.rb` finds an anchor line. When an anchor is not in
`main.rb` exactly once, `apply.rb` stops and changes nothing.

### 5.4.3 Build

You need these tools:

- Rust
- Rails 8.1.4, Bundler, Node.js with npm
- the SQLite CLI and headers (`libsqlite3-dev`)
- the jemalloc headers (`libjemalloc-dev`)

Run `make roundhouse` in the top directory of Fluxpinel. It does these
steps:

1. It fetches Roundhouse at the pin into `deps/roundhouse` and builds it.
2. It makes the Rails fixture with `bin/rh fixture` of Roundhouse.
3. It makes the Spinel output of the blog in `build/roundhouse/blog` and
   runs `apply.rb` and `spin_flags.rb` on it.
4. It builds the assets of the blog: the JavaScript of Turbo and
   Stimulus, and the Tailwind CSS. Without them, the browser does not
   connect to Action Cable.
5. It builds the host in `build/roundhouse/host/blog`.

Each step runs again only when its input changes. A new pin starts again
from step 1. A change of `apply.rb`, `spin_flags.rb`, `console.rb`,
`kernel/fluxpinel.rb` or the spinel pin starts again from step 3.

rh and Bundler use the Ruby of `.ruby-version` of Fluxpinel. Rails must be
installed for that Ruby.

### 5.4.4 Run

Seed the database and run the server with the console:

```sh
cd build/roundhouse/blog
sqlite3 storage/development.sqlite3 < db/seed.sql
FLUXPINEL_CONSOLE=4000 ../host/blog
```

The server serves the blog on http://localhost:3000. Connect to the
console with `nc 127.0.0.1 4000`.

Open http://localhost:3000/articles/1, and type this in the console. The
comment shows up on the open page with no reload:

```ruby
Console.create("Comment", {article_id: 1, commenter: "fx", body: "live"})
```

### 5.4.5 IRB over telnet

The host also takes `--irb-port PORT` and opens IRB, with the line editor
of Reline, to each telnet connection on 127.0.0.1:PORT (see `--irb-port`
in the [README](../README.md)). The two arguments can be at any place
before a `--`, and the entry does not get them.
The kernel is compiled with `--ext-class` for the models and for
`ActiveRecord::Base` and `ActiveRecord::Relation` (`.fx-ext-class`), so IRB
calls the methods of the models and of a `Relation` by name:

```sh
cd build/roundhouse/blog
../host/blog --irb-port 4001
```

```
$ telnet 127.0.0.1 4001
irb(main):001> Article.count
=> 3
irb(main):002> Article.find(1).title
=> "Getting Started with Rails"
irb(main):003> Article.all.limit(2).to_a.size
=> 2
irb(main):004> Article.where(id: [1, 3]).order(:id).map(&:id)
=> [1, 3]
irb(main):005> Article.all.each { |a| break a.title if a.id == 2 }
=> "Understanding MVC Architecture"
irb(main):006> Article.new(title: "from new").persisted?
=> false
```

A block of the session goes to a method of the kernel that takes one,
such as `each` and `map` of a `Relation`. A `break` in the block ends the
call. `new` of a model makes a record, as the table of `--ext-class` has
the rows of `new`.

The table of `--ext-class` does not hold a method with a keyword
parameter. `select` and `find` of a `Relation` are the query methods of
Roundhouse, which take no block. Use `to_a` first:
`Article.all.to_a.select { |a| a.id > 1 }`.

A method that answers an object of a class the list does not name comes
back as an object with no methods.
`--ext-class all` is not an option here: it makes every public method of
the blog a root, and spinel then refuses `runtime/logger_spinel.rb`.

### 5.4.6 Check

`make test-roundhouse` runs `test/roundhouse/test.rb` on the host build.
The test starts the server on free ports. It uses a copy of `db/seed.sql`
in a temporary database, so `storage/` of the blog does not change. It
checks these things:

- `GET /articles` answers 200 with an article of the seed.
- The console counts, finds and creates records, and keeps a local.
- The console calls the methods of a model and of a `Relation` by name.
- The console passes a block to a method of a `Relation`.
- The console makes a record with `new`.
- A call that Fluxpinel does not have raises, and the session goes on.
- `exit` closes the session, and the server runs after it.
- The page of the article shows the new comment.

The test then starts the server with `--irb-port`. With no Reline that
runs on a socket, it skips this part. It checks these things:

- A session of IRB calls a model.
- `exit` as Ruby code (`(exit)`) and the `exit` command of IRB close the
  session, and the server runs after each.

### 5.4.7 Campfire

Campfire (basecamp/once-campfire) is a Rails application that is not a
fixture of Roundhouse. `make campfire` builds its host with the same
steps as the blog. Nothing in the make rules names a model or a package
of Campfire.

You need the tools of section 5.4.3 and the headers of libvips
(`libvips-dev`). The build takes about 30 minutes.

`make campfire` does these steps:

1. It fetches Campfire at the commit in `CAMPFIRE_COMMIT` into
   `deps/once-campfire`.
2. It makes the Spinel output in `build/roundhouse/campfire` with
   `--allow-unsupported`. Roundhouse does not lower each part of Campfire,
   and the flag writes the output with those parts left out.
3. It runs `apply.rb` and `spin_flags.rb` on the output.
4. It builds the host in `build/roundhouse/campfire-host/campfire`.

`RH_CAMPFIRE_FLAGS` holds more flags of spinel for the kernel, and is
empty. Campfire has `require` calls in class bodies, and files that
require each other. The load graph of spinel places such files since
spinel 4cc507c2b (matz/spinel#12524); with an older spinel the start
raises `NameError`.

Run the server with the console:

```sh
cd build/roundhouse/campfire
FLUXPINEL_CONSOLE=4000 ../campfire-host/campfire
```

The server serves Campfire on http://localhost:3000. With no account, each
page goes to `/first_run`.

`make test-campfire` runs `test/roundhouse/campfire.rb` on the host build.
The test starts the server on a new database and checks these items:

- The pages go to `/first_run` with no account, and to the sign in with
  an account.
- The console creates and reads records of Account, User, Room,
  Membership, Message, Boost and Session.
- The console reads associations, the search of messages and the users
  that a message mentions.
- The IRB of `--irb-port` reads the records.

`make campfire` and `make test-campfire` are not a part of `make
roundhouse` or of `make upgrade_roundhouse`. Run them by hand after a
change of a pin.

### 5.5 A server in three settings

A server has a thread for each connection, over `TCPServer`. Its
framework and its application are one source, written in the Ruby that
spinel compiles. The source runs under Fluxpinel alone, and as the kernel
of a host build.

### 5.5.1 Development: Fluxpinel alone

`fluxpinel server.rb` runs the whole server in the walker.

- **Threads.** The walker runs one thread at a time (section 3.3.5). A
  thread that waits (in `accept`, a read, `Queue#pop`) gives its turn up.
  A thread that computes keeps its turn for 10 ms, then the next thread
  runs. `RUBY_THREAD_TIMESLICE` sets that time in milliseconds, as in
  CRuby.
- **Many connections.** A server with 5,000 open event streams answers a
  short request in 0.2 ms, and a write to each stream takes 0.3 s. The
  memory is 36 kB for a stream. These numbers are those of CRuby within
  a factor of two.
- **Requests that compute.** A short request beside k requests that
  compute waits about (k + 1) times the time of a turn: 20 ms for one,
  173 ms for sixteen. A compiled binary does not wait, as its threads run
  on more than one core.
- **A file that changes.** `Kernel#load` runs a file again, and the
  server goes on. A server loads each file whose time changed before it
  takes a request. A file with a syntax error raises SyntaxError, with
  the file, the line and the lines of the source, and the code of before
  stays. As in CRuby, a method that the file no longer has stays too.
- **Errors.** A backtrace has the file and the line of each frame. The
  top level of a loaded file is `<top (required)>`, under a frame of
  `Kernel#load`.
- **Signals.** SIGINT and SIGTERM with no trap raise Interrupt and
  SignalException in the main thread, as in CRuby. With a second thread,
  the main thread can wait where the raise does not reach it (`accept`).
  A watch then ends the process with the default of the system after one
  second (`src/walk.c`). CRuby raises there too; that needs the runtime.
- **Tests.** minitest runs: `require "minitest/autorun"` runs the tests
  of a file at the exit. Fluxpinel has no RubyGems, so `-I` names the
  directory of a gem.

### 5.5.2 Deployment: a host build

`make host KERNEL=server.rb KERNEL_MAIN=Server.start EMBED=1` gives one
binary. spinel compiles the server, and Fluxpinel is in the binary for
the code that stays Ruby at run time.

- **The entry.** `KERNEL_MAIN` names a method that takes the arguments of
  the command (`def self.start(argv)`). Fluxpinel starts after the top
  level of the kernel, so the entry is the first place that calls it.
- **Pages from the interpreter.** The entry calls `Fluxpinel.load` one
  time for the components of the user interface. The answer is a value
  of the walker (a module). For each request, compiled code makes the
  data of the page (a Hash and Arrays, with no conversion) and calls
  `Fluxpinel.call(module, "render", [data])`. The answer is the String of
  the page.
- **Threads.** The compiled threads run on more than one core. A call of
  Fluxpinel takes the walker lock, so one page is made at a time. A small
  page takes about 2 ms, and the binary makes about 500 of them in a
  second from any number of threads.
- **The files in the binary.** `EMBED=1` puts the libraries in the
  binary, and `EMBED_EXTRA` adds the Ruby files of the program (section
  3.3.6). The binary then reads no Ruby file from the disk. It links
  shared libraries (libffi, libcrypt, and each one that `ffi_lib`
  names), which the machine must have.
- **What does not change at run time.** A file that `Fluxpinel.load` runs
  again changes the code of the walker. Compiled code does not change.

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
- A boxed String is its handle: `SP_TAG_OBJ` with `cls_id`
  `SP_BUILTIN_STRBUF` (-40), frozen or not.
- The integer overflow mode is a compile-time macro
  (`SP_INT_OVERFLOW_MODE_RAISE`, `_WRAP` or `_PROMOTE`). The default of
  spinel is `raise`. A host build uses the mode of its kernel
  (section 4.3).
- Exceptions use the `setjmp` frame that spinel emits for `begin`/`rescue`
  (`sp_exc_stack`, `sp_exc_top`, `sp_last_exc_cls`). Fluxpinel code can use the
  same frame.
- A host binary of the runtime alone is about 1.4 MB of text (x86_64,
  `-O2`). Most of it is `static inline` code from `spinel_rt.h`.
  `poc/m0` is such a host: it links only `libspinel_rt`, makes boxed
  values, calls `sp_poly_*` operations, rescues an exception and runs the
  GC. Run it with `make -C poc/m0 run`.
- `bop_rows[]` has 2035 rows: 958 `BOPE_TEMPLATE`, 183 with a special
  emitter and 894 `BOPE_NONE`.
- The 958 template rows give 1204 wrappers (row times receiver variant).
  `poc/m1` generates the wrappers, compiles and links them, and measures
  them (`make -C poc/m1`):

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
- `make oracle` gives the coverage of Fluxpinel now. `build/oracle/report.md`
  counts each class and names the largest blockers.
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
- Fluxpinel makes a mutable String for each String literal, as CRuby does
  with no `# frozen_string_literal: true`. spinel freezes each literal, and
  the `.expected` of a test is the answer of CRuby with frozen literals.
  Thus `"ab12".slice!(/\d+/)` changes a String in Fluxpinel, where the
  `.expected` has FrozenError (`string_slice_bang_regexp_arm`), and two
  literals of the same text are two objects
  (`compare_by_identity_string_key`). With the comment at the top of a
  file, Fluxpinel freezes the literals of that file.
- `rescue` lets a program reach two old defects of Fluxpinel. A generated
  wrapper whose row has no argument kinds takes any argument: `"s".include?(:x)`
  passes a Symbol to `sp_str_include`, which crashes
  (`param_include_body_widen`). A rescue with `retry` around a method that
  Fluxpinel lacks retries forever (`string_split_local_each_retry`).
- A comparator block that does not compare (`sort { |x| x.length }`) gives
  an order that depends on the sort algorithm. Fluxpinel uses a stable merge
  sort, and CRuby uses its own quicksort. `comparator_block_arity` differs
  for this reason.

## 8. Open questions

- Can a generator map the `sp_poly_*` helpers to method names, as
  `tools/bop_gen.c` does for `bop_rows[]`? Codegen names the helper that it
  emits for each method, so the map exists in the codegen source.

## 9. Plan

The plan has six parts, in the order of their priority. The steps of a
part are in order too. A step that needs a change of spinel or of
Roundhouse says so. Such a change is proposed with a demo that shows its
use.

### 9.1 A server in three settings

The server of section 5.5 is the main use of the dynamic layer. The gaps
that it meets come first in the parts 9.2 to 9.5.

1. **The gems of pure Ruby.** A gem that the server needs must load and
   run in the walker. Take the gaps in the order that the gems meet
   them. The known ones:
   - rexml stops at its first read (a `StringIO` under its source).
   - `ObjectSpace::WeakMap` is missing (connection_pool).
   - tzinfo and concurrent-ruby stop after a warning of their engine
     check; the cause is not known.
   - A character property (`\p{Han}`) and a pattern with many capture
     groups stop in the regexp engine of spinel (kramdown, addressable,
     rouge). They are issues of spinel.
2. **The same tests on the two sides.** Run the tests of a framework and
   an application under Fluxpinel and on the compiled binary, as the
   oracle does for the tests of spinel (section 4). A difference points
   to spinel or to Fluxpinel before the server runs.
3. **Pages from more than one walker.** One page is made at a time
   (section 5.5.2). Measure a large page. When the time of the pages
   limits a server, the choices are a cache of pages, more processes, or
   more than one walker in a process (which the one walker lock of
   section 3.3.5 does not allow now).
4. **A raise for a signal in each wait.** With a second thread, a signal
   that finds the main thread in `accept` ends the process after a
   second (section 5.5.1). A raise there, as in CRuby, needs the runtime
   to wake the main thread; its trap thread stops the deadlock check of
   `Thread#join`. Propose the change to spinel.
5. **The other files of a program in the binary.** `EMBED_EXTRA` takes
   Ruby files. A program also has files for the browser. Take them when a
   program asks for one binary with all its files.
6. **A client of PostgreSQL.** A server binds SQLite with `ffi_func` now.
   A client of PostgreSQL over a socket, in the Ruby that spinel
   compiles, is a later step.

### 9.2 The boundary to AOT code

Each step needs a change of spinel.

1. **A hook at the waits of the runtime.** Fluxpinel gives its lock up
   around each wait of the runtime by hand (`FX_UNLOCKED`, section 3.3.5).
   Propose two hooks, which the runtime calls when a public wait starts
   and when it ends. An embedder then frees its lock in one place.
   Fluxpinel drops the `FX_UNLOCKED` sites of the waits and the table of
   the wrappers that park (`tools/bop_gen.c`).
2. **eval with a binding (section 5.2).** At a call of `binding`, AOT
   code gives the locals of its method: the name, the type and the address
   of each one. A Binding of the walker takes such a frame, reads the
   locals and writes them back.
3. **A method with a keyword parameter.** The table of `--ext-class` has
   no row for it (section 3.6.6), so a script does not call it by name.
4. **Dynamic dispatch (section 5.3).** AOT code hands a call that it
   cannot resolve to Fluxpinel.

A script does not replace a method that AOT code calls directly (section
5.1). The plan has no step for that.

### 9.3 IRB and the console

1. **The gems with no change.** Two pull requests remove what Fluxpinel
   adds to the gems:
   - `--irb-port` needs a Reline that runs on a socket (ruby/reline#932).
     When a release of Reline has it, `GEM_DIRS_FIRST` goes from
     `docs/development.md`, and the CI runs the case of `--irb-port`.
   - `lib/fluxpinel/irb_ri_guard.rb` rescues the LoadError of the ri
     driver for IRB (ruby/irb#1272). When a release of IRB has it, the
     file goes.
2. **The sessions of IRB (section 4.2).** Run a session with more of the
   commands and the input of a user, and take its first differences from
   the IRB of CRuby. When a difference goes, its line goes into a session
   of `test/irb`.
3. **The differences that the sessions leave out.** Each one needs a
   decision:
   - A backtrace has no line for an operator of the fast path
     (`'Integer#/'`), and none for the methods that CRuby calls inside a
     builtin method (section 3.3.2).
   - An error in the input of IRB has no mark under its source. The IRB
     of CRuby 4.1 keeps the lines of its input (`RubyVM.keep_script_lines`)
     for error_highlight to mark them; Fluxpinel has no RubyVM, and marks
     an error in a file (`fx_mark_text`), as CRuby does without the lines.
   - `10 ** 30` is a RangeError in the IRB of a host build, which is in
     raise mode as its kernel (section 4.3).

### 9.4 The builtin layer

The layer shrinks (section 3.2.2).

1. **The known shadows.** Each line of `tools/shadowed_known.txt` has a
   reason: what the row of spinel lacks for the walker. Report each reason
   to spinel as an issue or a pull request. When the pin has the row, the
   hand-written method and the line go.
2. **A reason for each method.** Each hand-written method names its
   reason in a comment: the method calls a block of the walker, or spinel
   has no row and no runtime function. `make census` then lists the
   methods of the second kind, which are the candidates for spinel.
3. **The helpers of the runtime.** Answer the question of section 8. A
   generated map from the `sp_poly_*` helpers to method names replaces the
   core methods that call one helper by hand.

### 9.5 Compatibility with CRuby

Coverage is not a step of its own. Take first the gaps that a demo or a
user meets. Then take the largest items of the oracle report
(`build/oracle/report.md`):

- the reasons of "Unsupported in the walker",
- the methods of "Missing builtin methods",
- the tests in which only Fluxpinel differs from CRuby. Group them by
  cause first.

Each step runs `make oracle` and commits `tools/oracle_agree.txt`.

### 9.6 Speed

Speed must not damage the dynamic layer, the oracle or the ease of
maintenance. [side-table.md](side-table.md) has the stop conditions
(section 6) and the measures on hold (section 4).

1. **Builtin calls beside the fast path.** A call of a builtin method
   that `fx_fast_call` does not answer is slower in a program that reopens
   builtin classes, as a gem does. Measure it under IRB, and cut the
   lookups that such a call does again each time.
2. **The measures on hold.** Start one only when its stop condition
   allows it.
