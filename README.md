# Fluxpinel

Fluxpinel is an interpreter for Ruby that runs on the runtime of
[spinel](https://github.com/matz/spinel), the Ruby AOT compiler.

The name comes from the flux method, which grows spinel crystals in a melt.
Fluxpinel adds the flux of a dynamic language to a spinel program.

Fluxpinel has two purposes:

- A dynamic layer for an AOT binary. The binary loads Ruby scripts at run
  time, and a console connects to it while it runs. The interpreted code
  uses live objects in the same heap as the compiled code.
- A reference oracle for spinel. Fluxpinel runs a program with no type inference
  and no optimization, but with the same runtime as the AOT output.

Fluxpinel does not modify spinel on its own. It links against `libspinel_rt`
and pins one spinel commit.

## Build

Fluxpinel needs a built spinel checkout (`make deps && make` in spinel) and Ruby
for the build tools.

```sh
make SPINEL=/path/to/spinel
build/fluxpinel program.rb
make test
```

`make test` runs each program in `test/m2` under CRuby and Fluxpinel and
compares stdout and the exit status.

```sh
make host KERNEL=app.rb KERNEL_ENTRIES=App.add,App.greet
make test-host
```

`make host` builds `build/host/fluxpinel`: Fluxpinel and an AOT program (the
kernel) in one binary. `make test-host` runs the tests on that build, with
`test/kernel/app.rb` as the kernel.

See [docs/design.md](docs/design.md) for the design.
