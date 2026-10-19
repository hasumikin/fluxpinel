# Lupe

Lupe is an interpreter for Ruby that runs on the runtime of
[spinel](https://github.com/matz/spinel), the Ruby AOT compiler.

A "Lupe" is the magnifier that a jeweler uses to examine a gem.
Spinel is a gem. Lupe looks into spinel programs.

Lupe has two purposes:

- A REPL inside an AOT binary while the binary runs. The REPL reads and
  changes live objects in the same heap as the compiled code.
- A reference oracle for spinel. Lupe runs a program with no type inference
  and no optimization, but with the same runtime as the AOT output.

Lupe does not modify spinel. It links against `libspinel_rt` and pins one
spinel commit.

## Build

Lupe needs a built spinel checkout (`make deps && make` in spinel) and Ruby
for the build tools.

```sh
make SPINEL=/path/to/spinel
build/lupe program.rb
make test
```

`make test` runs each program in `test/m2` under CRuby and Lupe and
compares stdout and the exit status.

See [docs/design.md](docs/design.md) for the design.
