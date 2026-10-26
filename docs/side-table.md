# Side table of nodes

The walker keeps one record next to each prism node, in a side table. A
record holds what the walker finds once and uses again at each run of
the node: the shape of the arguments of a call, and the caches of a call
and of an instance variable. This is measure 1 of
[design.md](design.md), section 3.3.8. The prism tree stays as it is.

## 1. Background

Fluxpinel runs a Ruby script on its own, and mame/optcarrot (an NES
emulator, a common benchmark of Ruby) is the target program. The target
of speed is the CRuby interpreter with no JIT (design.md, section 3.3.8).

Most of the cost of the walker is work that each run of a node does
again: the shape of the arguments, the lookup of the method, the slot of
an instance variable. For one node, these answers are almost always the
same. A record of the node keeps them.

A new tree of the walker, translated from the prism AST, could keep the
same fields. But it needs a rewrite of most of the walker:

- `walk.c` names a prism node struct (`pm_*_node_t *`) in about 200
  places, and `fx_eval` has about 170 cases.
- `FxMethod.def`, `FxBlock.node`, `defined?`, the prelude and the console
  keep prism nodes too.

The side table moves one node kind at a time, and the oracle checks each
step.

### 1.1 Speed

The table gives these numbers, 30 frames of optcarrot and fib(32). The
counts are the user instructions of `perf stat`.

| Measurement | No records | Ivar records | Ivar and call records |
|---|---|---|---|
| optcarrot, instructions | 58.62G | 53.36G | 51.76G |
| optcarrot, fps | 8.8 | 10.1 | 10.7 |
| fib(32), instructions | 16.23G | 16.22G | 15.15G |

CRuby 4.0.9 runs optcarrot at 41 to 56 fps, and fib(32) in 0.22 s.
Fluxpinel takes about 1.0 s for fib(32).

## 2. The table

Each prism node gets one record (`FxSide`):

- A prism node has a `node_id`. `PM_NODE_INIT` gives each node the next
  number (`++parser->node_id`).
- `pm_parser_init` sets `parser.node_id` to 0. Thus `fx_parse_in`
  (`src/load.c`) writes the next number of a global count after
  `pm_parser_init` and before `pm_parse`. After the parse, it reads
  `parser.node_id` back as the next number of the count. Thus the numbers
  of two parses never overlap.
- After the parse, `fx_parse_in` makes sure that `root->node_id` is in
  the expected range. The write to `parser.node_id` depends on an internal
  struct of prism, and this check finds a break of that dependency after
  an update of prism.
- Only `fx_parse_in` makes a tree that the walker evaluates.
  `fx_session_state` (`src/console.c`) also parses, but only to see if the
  input is complete. It discards that tree, so the table does not see it.

The table is a list of chunks (`L.side`). Each chunk holds 64K records:

- `fx_side(n)` selects the chunk with `id >> 16`, and the record in the
  chunk with `id & 0xffff`.
- The table grows by more chunks after each parse. A chunk never moves, so
  an `FxSide*` stays valid across `eval` and `require`.
- A `node_id` out of the table is a bug. `fx_side(n)` and the check after
  the parse call `fx_side_broken`, which writes a message and ends the
  process (`_exit(2)`), as the other fatal checks of the walker do.

A record is a union of small structs, one for each node kind, and has 24
bytes. Each node gets a record, also a node that needs none (a literal or
`statements`). optcarrot has 4300 lines, so the table costs a few MB. Only
the case of the walker for a kind reads or writes the field of that kind,
so the case makes sure of the kind.

## 3. The records

### 3.1 Call nodes (`FxCallCache`)

- **The shape of the arguments.** The record keeps `fx_args_dynamic` and
  `fx_has_forwarding` of the arguments. The first run of the node sets
  them, from the syntax. `FX_ARGS_DYN` takes the shape from the record.
  The other callers of `FX_ARGS` (super and yield) still call
  `fx_args_dynamic`.
- **The method.** The record keeps the class id of the last receiver, the
  generation (`fx_methods_gen`) and the `FxMethod*` (`fx_call_cached`):
  - The cache serves only an object of the program, as the receiver or as
    self. For such an object, `fx_call_self` and `fx_send_v` also look up
    the method first, so the cache changes no order.
  - A call with a receiver other than `self.` checks the visibility of the
    method at each call. A denied method takes the general path.
  - When the class has no such method, the call takes the general path.
    The global cache of `fx_find_method` serves that path.

### 3.2 Instance variables (`FxIvarCache`)

- A node of `@x` keeps the class id and the slot of its last access. This
  covers the read, the write, the op-writes and the target of a multiple
  assignment.
- An accessor (`attr_reader`, `attr_writer`) has no node, so its
  `FxMethod` keeps the same cache.
- A hit compares only the class id in the box of `self` (`FX_IC_HIT`). The
  cache keeps only the id of an object of the program, which is no native
  object, and a slot never moves.
- A write through a hit still refuses a frozen object.
- A miss takes the general path (`fx_ivar_get`, `fx_ivar_set`), and then
  keeps the slot.

### 3.3 Fields that the records do not keep

A measurement or the code showed that these fields do not pay:

- **The slot of a local variable.** `fx_local` does a linear search of the
  names of the frame, but the frames are small. A slot in the record
  costs more than the search: 0.7% more instructions on optcarrot, and
  1.2% more on fib(32).
- **The Symbol of the name of a call.** `fx_sym` answers it from one
  table already.
- **A mark for the fast path (`fx_fast_call`).** Its test at run time
  (`c->receiver && !blk && !kw`) costs about the same as a mark, and `blk`
  is a value of the run.

## 4. Measures on hold

The call records give about 5% on optcarrot, below the stop condition of
section 6. Thus these measures stay on hold.

- **Evaluation functions (measure 5 of design.md).** A function pointer in
  the record would replace the evaluation of one node, such as `a + b` of
  two Integers. The cost of maintenance is high:
  - Two dispatches live together: the switch of `fx_eval` and the
    function pointer. A change to a node kind needs an understanding of
    both.
  - A specialized evaluation is a second implementation of the semantics
    in `fx_eval`. A change of the semantics then needs two edits.
  - A change of form (two Integers, then Float, then general) is a state
    machine. It adds more points to keep correct: the interaction with
    `fx_reopened_builtins`, and oracle coverage of both paths.

  If this measure goes ahead, it has no change of form. The node keeps
  only a mark of the last result of `fx_fast_call`.
- **Jumps in the result (measure 4 of design.md).** It changes the return
  type of `fx_eval`, so it affects the whole walker.

## 5. Invariants

- **GC.** A record holds only an `FxMethod*` (from malloc), class ids,
  slot numbers, a generation and flags. It holds no GC object, so it needs
  no root. If a record must hold a GC object, it needs marks.
- **The life of `FxMethod`.** `fx_method_new` (`src/object.c`) allocates
  with calloc, and no code frees the result. A node cache can keep an old
  `FxMethod*`:
  - The generation check rejects it.
  - The pointer never points to freed memory.

  Thus the caches need no root and no management of lifetimes.
- **Invalidation.** A definition, an `include` or an `extend` increments
  the generation `fx_methods_gen`, and thus makes every call cache old. A
  change of visibility writes the method in place, and the call checks the
  visibility each time. An ivar cache never becomes old, because a slot
  never moves.
- **Threads.** The walker runs on one OS thread (`SPINEL_WORKERS=1`). Thus
  the caches need no lock.
- **Repeated `eval`.** Each parse adds numbers and records. `L.sources`
  keeps each parsed source too, so the table grows at about the same
  rate as the sources.
- **The host build and the console.** They use the same walker. The console
  parses each line, and each parse continues the global count. Thus the
  numbers do not overlap.
- **The numbers of prism.** `PM_NODE_INIT` only counts the nodes. Another
  start value does not change the parse. The count has 32 bits, so an
  overflow is very unlikely.

## 6. Priority and stop conditions

The main purposes of Fluxpinel are the dynamic layer of spinel and the
oracle. Speed must not damage them or the ease of maintenance.

These conditions stop the work, so that the optimization does not go on
by itself:

- If a measure gives less than 10%, do not start the next one. The time of
  optcarrot, 30 frames, decides. The count of user instructions
  (`perf stat`) helps against the noise of the times.
- When optcarrot runs at more than 20 fps, keep the measures of section 4
  on hold.
