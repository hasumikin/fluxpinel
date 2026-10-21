# Fluxpinel

Fluxpinel adds a dynamic layer to a program that
[spinel](https://github.com/matz/spinel) compiles ahead of time.

Spinel compiles Ruby to C and gives a fast native binary. A compiled
program is closed: it cannot load new code while it runs. Fluxpinel is a
Ruby interpreter that lives in the same binary. It opens the program again.

## What you get

- **Plugins and scripts.** The binary loads a Ruby file at run time. You
  change a plugin, and you do not compile again.
- **Two-way calls.** Compiled code calls the script, and the script calls
  compiled code. A value crosses as it is, with no copy and no conversion.
- **One heap.** The script uses the live objects of the program. One
  garbage collector sees all of them.
- **The same semantics.** Both sides use one runtime, so a method of a
  builtin class gives the same result on both sides.

## An example

The compiled side loads a plugin and calls its filter. The code is a short
form of [examples/textkit](examples/textkit):

```ruby
# app.rb: spinel compiles this file.
require_relative "../../kernel/fluxpinel"

module Textkit
  def self.words(line) = line.downcase.scan(/[a-z']+/)

  def self.main(args)
    lines = File.readlines(args[0], chomp: true)
    Dir.glob(File.join(args[1], "*.rb")).sort.each do |path|
      plugin = Fluxpinel.load(path)
      lines = Fluxpinel.call(plugin, "filter", [lines])
    end
    lines.each { |line| puts line }
  end
end
```

The plugin is plain Ruby. It calls the compiled method `Textkit.words`:

```ruby
# plugins/20_stopwords.rb: Fluxpinel runs this file.
class Stopwords
  WORDS = %w[a an and the of to is in it].freeze

  def name = "stopwords"
  def filter(lines) = lines.map { |line| Textkit.words(line).reject { |w| WORDS.include?(w) }.join(" ") }
end

Stopwords.new
```

## How it works

Spinel compiles the program (the kernel) to C. The build puts the kernel
and Fluxpinel into one C translation unit (TU):

```
+-------------------------- one TU, one binary --------------------------+
|                                                                        |
|  kernel.c (spinel output)         Fluxpinel (tree-walking interpreter) |
|    Textkit.main  --- Fluxpinel.load / .call / .eval --->  script.rb    |
|    Textkit.words <------------- entries -------------  Textkit.words   |
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

- see the locals of a compiled method (`eval` with a binding)
- take a call that the compiled code cannot resolve (dynamic dispatch)
- replace a method that compiled code calls directly
- run fast: the interpreter is for the glue of a program, not for its hot
  paths

A console that connects to a running binary is a planned step. The two
gaps in the list need a change to spinel. See
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

## The name

The name joins "flux" and "spinel". The flux method grows spinel crystals
in a melt. "Flux" also means a flow and a change. Fluxpinel lets a compiled
spinel program flow and change while it runs.

## Build and test

Fluxpinel needs a built spinel checkout (`make deps && make` in spinel) and
Ruby for the build tools.

```sh
make SPINEL=/path/to/spinel
build/fluxpinel program.rb
make test
```

`make test` runs each program in `test/m2` and `test/m3` under CRuby and
Fluxpinel, and compares stdout and the exit status.

```sh
make host KERNEL=app.rb KERNEL_ENTRIES=App.add,App.greet
make test-host
make oracle
```

- `make host` builds `build/host/fluxpinel`: Fluxpinel and a kernel in one
  binary.
- `make test-host` runs the tests on that build, with `test/kernel/app.rb`
  as the kernel.
- `make oracle` runs the oracle on the tests of spinel and writes
  `build/oracle/report.md`.

## Relation to spinel

Fluxpinel is an independent project. It does not modify spinel. It links
against the runtime of spinel (`libspinel_rt`) and pins one spinel commit.
