# Fluxpinel

Fluxpinel adds a dynamic layer to a program that
[spinel](https://github.com/matz/spinel) compiles ahead of time.

Spinel compiles Ruby to C and gives a fast native binary. A compiled
program is closed: it cannot load new code while it runs. Fluxpinel is a
Ruby interpreter that lives in the same binary. It opens the program again.

Fluxpinel also runs a Ruby script on its own, as the `ruby` command does.
The runtime of spinel runs inside it.

## What you get

- **Plugins and scripts.** The binary loads a Ruby file at run time. You
  change a plugin, and you do not compile again.
- **Two-way calls.** Compiled code calls the script, and the script calls
  compiled code. A value crosses as it is, with no copy and no conversion.
- **One heap.** The script uses the live objects of the program. One
  garbage collector sees all of them.
- **The same semantics.** Both sides use one runtime. Where Fluxpinel calls
  the runtime function of a builtin method, the method gives the same
  result on both sides. Fluxpinel writes some builtin methods itself, and
  the oracle compares them (see below).
- **A standalone interpreter.** `fluxpinel script.rb` runs a script with
  no compiled program. A large program such as optcarrot runs (see
  below).

## An example

[examples/logq](examples/logq) is an access log analyzer. Spinel compiles
the tool, and a user writes the questions in Ruby. The binary reads them
at run time:

```sh
build/logq/logq access.log examples/logq/queries/errors.rb
```

```ruby
# queries/errors.rb: a user writes this file, and Fluxpinel runs it.
report "slow 5xx" do
  where_status(500..599).slower_than(1.0).top(5, by: :duration)
end

report "POST by path" do
  where_verb("POST").count_by_path
end
```

No class defines `where_status` or `count_by_path`. The DSL, also a file
that Fluxpinel runs, reads the name in `method_missing` and adds the method
with `define_method` on its first call:

```ruby
# dsl.rb: Fluxpinel runs this file at each start.
class Query
  def initialize(sel) = @sel = sel

  def method_missing(name, *args)
    case name
    when /\Awhere_(\w+)\z/
      field = $1
      self.class.define_method(name) { |cond| Query.new(Logq.where(@sel, field, cond)) }
    when /\Acount_by_(\w+)\z/
      field = $1
      self.class.define_method(name) { Logq.count_by(@sel, field) }
    else
      return super
    end
    send(name, *args)
  end
end
```

`Logq.where` and `Logq.count_by` are compiled methods. Each step of a
query calls one of them, and the loop over the rows runs in compiled code:

```ruby
# app.rb: spinel compiles this file.
module Logq
  #: (Array[Integer], String, untyped) -> Array[Integer]
  def self.where(sel, field, cond)
    out = []
    case field
    when "status" then sel.each { |i| out << i if cond === TABLE.statuses[i] }
    # ...
    end
    out
  end
end
```

On a log of one million lines, logq runs `errors.rb` in about 0.8 seconds.
The same files take about 2.2 seconds in CRuby 4.0.9, and about 2.1
seconds with YJIT or ZJIT. Spinel alone does not
call `method_missing`, and it refuses `define_method` with a name that the
program makes at run time.

## How it works

Spinel compiles the program (the kernel) to C. The build puts the kernel
and Fluxpinel into one C translation unit (TU):

```
+-------------------------- one TU, one binary --------------------------+
|                                                                        |
|  kernel.c (spinel output)         Fluxpinel (tree-walking interpreter) |
|    Logq.main  ---- Fluxpinel.load / .call / .eval ---->  query.rb      |
|    Logq.where <-------------- entries --------------  Logq.where(...)  |
|                                                                        |
|  spinel runtime: values, heap, GC, exceptions, builtin methods         |
+------------------------------------------------------------------------+
```

- The kernel declares the API of Fluxpinel in `kernel/fluxpinel.rb`.
- Spinel exports the entries that you name (`--ext-entry`). A script calls
  them as module methods.
- An exception crosses the boundary in both directions. A `rescue` on one
  side catches a raise on the other side.

See [docs/design.md](docs/design.md), section 3.6, for the details.

## What it can and cannot do

Fluxpinel can:

- load a script or a plugin, and call its methods (`Fluxpinel.load`,
  `Fluxpinel.call`)
- evaluate a String at the top level (`Fluxpinel.eval`)
- call the exported entries of the kernel, and use its objects and classes

Fluxpinel cannot yet:

- call a method of an instance of a compiled class: only the module
  methods that you name export
- see the locals of a compiled method (`eval` with a binding)
- take a call that the compiled code cannot resolve (dynamic dispatch)
- replace a method that compiled code calls directly
- run as fast as CRuby: the interpreter is for the glue of a program, not
  for its hot paths

The two gaps in the list need a change to spinel. See
[docs/design.md](docs/design.md), section 5.

## An oracle for spinel

Fluxpinel also checks spinel. It runs a program with no type inference and
no optimization, on the same runtime as the compiled output. The oracle runs
each test of spinel three ways (CRuby, Fluxpinel and AOT) and compares the
output:

| CRuby vs Fluxpinel | Fluxpinel vs AOT | Probable cause |
|---|---|---|
| same | same | no defect |
| same | differ | spinel analysis or codegen |
| differ | same | spinel runtime builtin |
| differ | differ | Fluxpinel, or more than one cause |

The oracle also keeps the promise of "the same semantics" above. See
[docs/design.md](docs/design.md), section 4.

## A standalone interpreter

Fluxpinel runs a Ruby script on its own, as the `ruby` command does. The
arguments after the script go to `ARGV`:

```sh
build/promote/fluxpinel script.rb arg1 arg2
build/promote/fluxpinel -r ./mylib.rb -e 'p MyLib.answer'
echo 'p 1 + 2' | build/promote/fluxpinel
build/promote/fluxpinel -v
```

- The runtime of spinel runs inside. A builtin method calls the runtime
  function of spinel where one exists.
- Use the promote build (`build/promote/fluxpinel`) for the semantics of
  CRuby. An Integer that overflows becomes a Bignum there. The default
  build (`build/fluxpinel`) raises a RangeError on an overflow.
- [optcarrot](https://github.com/mame/optcarrot), an NES emulator and a
  common benchmark of Ruby, runs. `make optcarrot` runs it under CRuby and
  Fluxpinel, and compares the checksums. On 180 frames, Fluxpinel runs at
  about 9 fps, and CRuby 4.0.9 at about 55 fps.

The command line takes a part of the options of `ruby`:

- `-e`, `-c`, `-I`, `-r`, `-v`, `-w`, `-W`, `-h` and `--version`
- `--console`, for a console on stdin

With no script and no `-e`, Fluxpinel reads the program from stdin. `-v`
prints the version of Fluxpinel (one integer in `VERSION`), the commit of
spinel and the version of Ruby that spinel gives.

The walker keeps a side table next to the nodes of prism, for the caches
of each call and each instance variable. See
[docs/side-table.md](docs/side-table.md).

## The name

The name joins "flux" and "spinel". "Flux" means a flow and a change.
Fluxpinel adds a flow to a compiled spinel program, so that the program
can change while it runs.

## Build and test

Fluxpinel needs git, a C compiler, libffi and Ruby for the build tools.
`make deps` fetches spinel at the commit in `SPINEL_COMMIT` into
`deps/spinel`, and builds it there.

```sh
make deps
make
build/fluxpinel program.rb
make test
make optcarrot
```

To use a spinel checkout of your own, set `SPINEL` on the command line or
in `config.mk`. Git does not track `config.mk`.

```make
SPINEL = $(HOME)/src/spinel
```

The checkout must be at the commit in `SPINEL_COMMIT`, or `make` stops.
Pass `SPINEL_PIN=0` to use another commit, for example to run the oracle
on it.

`make test` runs each program in `test/m2` and `test/m3` under CRuby and
Fluxpinel, and compares stdout and the exit status. It also runs each
input in `test/console` through the console of both, and the cases of the
command line in `test/cli/cases.rb`.

`build/fluxpinel --console` reads Ruby from stdin, one line at a time, as
irb does. A host program gives the same console to a socket with
`Fluxpinel.session` and `Fluxpinel.feed`.

```sh
make host KERNEL=app.rb KERNEL_ENTRIES=App.add,App.greet
make test-host
make oracle
```

- `make host` builds `build/host/fluxpinel`: Fluxpinel and a kernel in one
  binary. `HOST` sets the directory and `HOST_NAME` the name of the
  binary. With `KERNEL_MAIN=App.main`, the binary takes no script and
  calls `App.main` with `ARGV`. See [examples/calc](examples/calc), the
  smallest one, and [examples/logq](examples/logq).
  `KERNEL_FLAGS` passes more flags to spinel for the kernel, such as the
  flags that `spin flags` prints for a spin project. `HOST_LIBS` adds
  libraries to the link, such as `-lsqlite3`.
- `make test-host` runs the tests on that build, with `test/kernel/app.rb`
  as the kernel.
- `make oracle` runs the oracle on the tests of spinel and writes
  `build/oracle/report.md`.

For the checks during development and the update of the pin, see
[docs/development.md](docs/development.md). For the console in a Rails app
that Roundhouse compiles, see [docs/roundhouse.md](docs/roundhouse.md).

## Relation to spinel

Fluxpinel is an independent project. It does not modify spinel. It links
against the runtime of spinel (`libspinel_rt`) and pins one spinel commit
in `SPINEL_COMMIT`. A change of the pin is a commit of its own.

## License

Fluxpinel is released under the MIT License. See [LICENSE](LICENSE).
