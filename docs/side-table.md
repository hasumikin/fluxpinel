# Side table of nodes

This document gives a design for measure 1 of [design.md](design.md),
section 3.3.8 (a tree of the walker). The design puts a side table next to
the prism nodes, and does not make a new tree.

## 1. Background

### 1.1 Fluxpinel as a standalone interpreter

Fluxpinel adds a dynamic layer to an AOT spinel program. It is also an
oracle for spinel (design.md, section 1).

Fluxpinel also runs as a standalone Ruby interpreter:

- It takes a `.rb` script, as the `ruby` command does.
- The spinel runtime runs inside it. Fluxpinel uses the builtin methods of
  spinel where it can.
- The target program is mame/optcarrot, an NES emulator. optcarrot is a
  common benchmark for Ruby implementations.

`make optcarrot` runs optcarrot from the branch `experiment/spinel`. The
checksum agrees with CRuby.

### 1.2 Speed

The next target is the speed of the walker. Section 3.3.8 of design.md sets
the target: the speed of the CRuby interpreter with no JIT.

| Measurement | Fluxpinel | CRuby |
|---|---|---|
| fib(32) | 1.06 s | 0.22 s |
| optcarrot, 30 frames | 3.88 s | - |
| optcarrot, fps | about 7 fps | 41 to 56 fps |
| logq, `per_row_block` | about 2.28 s | - |

The small measures of section 3.3.8 are in place:

- A fast path for the commonest builtin calls (`fx_fast_call`)
- One intern for each name (`fx_sym`, `fx_name_sym`)
- A global method cache and a global cache of ivar slots
- Frame roots with one push (measure 8)
- Accessors with no frame (measure 7)

The walker is still about 5 times slower than CRuby. This is the profile
of fib(32):

| Function | Share |
|---|---|
| `fx_call` | 30% |
| `fx_eval` | 17% |
| `fx_invoke_b` | 11% |
| `fx_args` | 9% |
| `fx_bind_list` | 5% |
| `fx_fast_call` | 4.8% |
| `fx_local` | 4% |
| `fx_args_dynamic` | 2.5% |

Most of the cost is work that each call does again:

- The call turns its name into a Symbol.
- The call examines the shape of its arguments, and collects them on the
  general path.
- The call finds its method in the global cache.
- A local variable read does a linear search of the names of the frame.

For one node, these answers are almost always the same. A node that keeps
its answers removes the repeated work. This is the reason for measure 1.
Measures 3, 5 and 6 also need a field on each node.

### 1.3 Why not a new tree

Section 3.3.8 makes measure 1 a translation of the prism AST into nodes of
Fluxpinel. The reason is that the walker cannot add a field to a prism
node.

A new tree needs a large rewrite:

- `walk.c` names a prism node struct (`pm_*_node_t *`) in 204 places.
- `fx_eval` has 169 cases.
- `FxMethod.def`, `FxBlock.node`, `defined?`, the prelude and the console
  keep prism nodes too.

During such a rewrite, it is difficult to keep the agreement of the oracle
on its 5370 tests. (`tools/oracle_agree.txt` has 5372 lines. Two of them
are comments.)

## 2. Design

Each prism node gets one record (`FxSide`) in a side table. The prism tree
stays as it is.

- A prism node has a `node_id`. `PM_NODE_INIT` gives each node the next
  number (`++parser->node_id`), from 1.
- Before `pm_parse`, Fluxpinel sets `parser.node_id` to the next number of
  a global count. Thus the numbers of two parses never overlap.
- Only `fx_parse_in` (`src/load.c`) makes a tree that the walker evaluates.
  `fx_session_state` (`src/console.c`) also parses, but only to see if the
  input is complete. It discards that tree, so the table does not see it.
- `fx_side(n)` finds the record of a node from `n->node_id` (section 2.2).

A field on each node gives what measure 1 needs:

- The Symbol of a name
- The caches
- A function pointer for the evaluation (only if S4 goes ahead)

### 2.1 Reasons

- The work moves one node kind at a time. Each stage gets a check of the
  oracle and of the speed.
- The structures that keep prism nodes (`FxMethod.def` and others) stay as
  they are.
- A record has about 48 bytes. optcarrot has 4300 lines, so a record for
  each node costs a few MB.

### 2.2 The shape of the table

The table is a list of chunks. Each chunk holds 64K records.

- `fx_side(n)` selects the chunk with `id >> 16`, and the record in the
  chunk with `id & 0xffff`.
- The table grows by one more chunk. A chunk never moves, so an `FxSide*`
  stays valid across `eval` and `require`.
- The cost is one read of the small array of chunk pointers. Next to the
  cost of `fx_call`, it is small.

Each node gets a record, so a node that needs no record (a literal or
`statements`) also uses 48 bytes. At this size, that is not a problem. If
the memory becomes a problem, change the shape then.

A record is a union of small structs, one for each node kind. A caller
that reads a field of a kind makes sure of the kind of the node.

## 3. Stages

Each stage is one commit. Each stage gets these checks:

- `make regress`
- fib(32)
- logq, `per_row_block`
- optcarrot, 30 frames

### S1 The base

- Number the nodes with one global count:
  - `pm_parser_init` sets `parser.node_id` to 0. Thus `fx_parse_in` writes
    the next global number after `pm_parser_init` and before `pm_parse`.
  - After the parse, `fx_parse_in` reads `s->parser.node_id` back. That
    value becomes the next global number. The table grows to that number.
  - After the parse, `fx_parse_in` makes sure that `root->node_id` is in
    the expected range. The write to `parser.node_id` depends on an
    internal struct of prism. This check finds a break of that dependency
    after an update of prism.
- Add the `FxSide` table and `fx_side(n)` (section 2.2). A `node_id` out of
  the range of the table is a bug, so `fx_side(n)` stops on it.
- A failed check calls `fx_side_broken`. It writes a message and ends the
  process (`_exit(2)`), as the other fatal checks of the walker do.
- Do not change the behavior. Make sure that the speed does not drop.

### S2 Call nodes

`fx_call` takes 30% on fib, the largest share. The record of a call node
(`FxCallCache`) keeps these fields:

- The shape of the arguments: `fx_args_dynamic` and `fx_has_forwarding`
  of the arguments. The first evaluation of the node sets them, from the
  syntax. S2 adds no third way to collect the arguments: `FX_ARGS_DYN`
  takes the shape from the record, and the other callers of `FX_ARGS`
  still call `fx_args_dynamic`.
- A method cache: the class id of the last receiver, the generation and
  the `FxMethod*`. The cache serves only an object of the program, as the
  receiver or as self. For such an object, `fx_call_self` and `fx_send_v`
  also look up the method first, so the cache changes no order.
  - A call with a receiver other than `self.` still checks the
    visibility. A denied method takes the general path.
  - When the class has no such method, the call takes the general path.
  - The global cache of `fx_find_method` stays for a miss.

The record keeps no Symbol of the name: `fx_sym` already answers it from
one table. The record keeps no mark for the fast path (`fx_fast_call`)
either. Its run-time test (`c->receiver && !blk && !kw`) costs about the
same as a mark, and `blk` is a run-time value.

### S3 Ivars

S3 comes before S2: on optcarrot, the ivar functions take about 11%.

- A node of `@x` keeps the class id and the slot of its last access
  (`FxIvarCache`). This covers the read, the write, the op-writes and the
  target of a multiple assignment.
  - A hit compares only the class id in the box of `self`. The cache
    keeps only the id of an object of the program, which is not a native
    object, and a slot never moves.
  - A miss takes the general path (`fx_ivar_get`, `fx_ivar_set`), and
    then keeps the slot.
  - A write through a hit still refuses a frozen object.
- An accessor (`attr_reader`, `attr_writer`) has no node, so its
  `FxMethod` keeps the same cache.
- A local variable keeps no slot. `fx_local` does a linear search of the
  names of the frame, but the frames are small. A measurement showed that
  a slot in the record costs more than the search: 0.7% more instructions
  on optcarrot, and 1.2% more on fib.

### S4 Evaluation functions (on hold)

This stage includes measure 5 of section 3.3.8. Measure S2 and S3 first.
Then, if necessary, propose S4 separately.

S4 is on hold because of the cost of maintenance:

- Two dispatches live together: the switch of `fx_eval` and the function
  pointer. A change to a node kind needs an understanding of both.
- A specialized evaluation is a second implementation of the semantics in
  `fx_eval`. A change of the semantics then needs two edits.
- The change of form (two Integers, then Float, then general) is a state
  machine. It adds more points to keep correct: the interaction with
  `fx_reopened_builtins`, and oracle coverage of both paths.

If S4 goes ahead, it has no change of form. The node keeps only a mark of
the last result of `fx_fast_call`. This uses the fast path that exists, and
adds no new evaluation.

### S5 Jumps in the result (on hold)

This stage is measure 4 of section 3.3.8.

- It changes the return type of `fx_eval`, so it affects the whole walker.
- Measure the stages up to S3 first. Then, if necessary, propose S5
  separately.

## 4. Risks

- **Repeated `eval`.** Each parse adds numbers and records. But
  `L.sources` already keeps each parsed source. The growth is about the
  same as now, and it is not a new leak.
- **GC.** A record holds only `FxMethod*`, `FxClass*` (both from malloc)
  and slot numbers. It holds no GC object, so it needs no root. If a record
  must hold a GC object later, add the marks then.
- **The life of `FxMethod`.** `fx_method_new` (`src/object.c`) allocates
  with calloc, and no code frees the result. A node cache can keep an old
  `FxMethod*`:
  - The generation check rejects it.
  - The pointer never points to freed memory.

  Thus the caches need no root and no management of lifetimes.
- **Threads.** The walker runs on one OS thread (`SPINEL_WORKERS=1`). Thus
  the caches need no lock.
- **Invalidation.** The node caches use the generation `fx_methods_gen`. A
  definition, an `include`, an `extend` or a change of visibility makes
  every cache old at once.
- **Growth of the table.** The table is a list of chunks, so growth does
  not move a record (section 2.2). An `FxSide*` stays valid at all times.
- **The host build and the console.** They use the same walker. The console
  parses each line, and each parse continues the global count. Thus the
  numbers do not overlap.
- **The numbers of prism.** `PM_NODE_INIT` only counts the nodes. Another
  start value does not change the parse. The count has 32 bits, so an
  overflow is very unlikely.

## 5. Changes to design.md

The S1 commit changes design.md:

- Rewrite measure 1 of section 3.3.8 as the side table.
- Remove the statement that the walker cannot add a field to a prism node.
- In measures 3, 5 and 6, change "node" to "the record of a node".

## 6. Priority and stop conditions

The main purposes of Fluxpinel are the dynamic layer of spinel and the
oracle. Speed must not damage them or the ease of maintenance.

These conditions stop the work, so that the optimization does not go on
by itself:

- If a stage gives less than 10%, do not start the next stage. The time
  of optcarrot, 30 frames, decides. The count of user instructions
  (`perf stat`) helps against the noise of the times.
- When optcarrot runs at more than 20 fps, keep S4 and later on hold.

## 7. Decisions

- The side table replaces the new tree.
- The first commit holds S1 only. S1 alone gives no speed, but it shows on
  its own that the behavior does not change. S2 follows.
