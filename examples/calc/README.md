# calc

calc evaluates each argument as Ruby. It is the smallest use of the
dynamic layer of Fluxpinel:

- Spinel compiles the tool (`app.rb`) to C.
- Fluxpinel evaluates each argument with `Fluxpinel.eval`, at the top level.
  A method that one argument defines stays for the next arguments.

spinel alone refuses this program with `eval(src)` in place of
`Fluxpinel.eval(src)`: the String comes at run time.

## Build and run

Run these commands in the top directory of Fluxpinel:

```sh
make host HOST=build/calc HOST_NAME=calc KERNEL=examples/calc/app.rb KERNEL_MAIN=Calc.main \
  KERNEL_ENTRIES=Calc.main
build/calc/calc '1 + 2' '(1..10).sum' 'def sq(x) = x * x; sq(12)' '[1, 2, 3].map { |v| sq(v) }'
```

The output:

```
1 + 2 => 3
(1..10).sum => 55
def sq(x) = x * x; sq(12) => 144
[1, 2, 3].map { |v| sq(v) } => [1, 4, 9]
```

- `KERNEL_MAIN` makes the binary start at `Calc.main` with `ARGV`.
- `KERNEL_ENTRIES` names the start. A script of calc calls no entry.
