# logq

logq is an access log analyzer. It shows the dynamic layer of Fluxpinel
with a query DSL in plain Ruby:

- Spinel compiles the tool (`app.rb`) to C. The tool reads the log into
  columns, and does each loop over the rows.
- Fluxpinel runs the DSL (`dsl.rb`) and the query (`queries/*.rb`) at run
  time. You change a query or the DSL, and you do not compile again.

## Files

```
examples/logq/
├── app.rb          the kernel: spinel compiles it
├── dsl.rb          the DSL: Fluxpinel runs it at each start
├── gen.rb          writes a sample log
└── queries/
    ├── slow.rb     a query of a user
    └── block.rb    the same kind of query, with a block for each row
```

| File | Who writes it | When it runs | A change needs |
|---|---|---|---|
| `app.rb` | the author of logq | at the build | a build |
| `dsl.rb` | the author of logq | at each start | no build |
| `queries/*.rb` | a user of logq | at each start | no build |

## A query

```ruby
report "slow 5xx" do
  where_status(500..599).slower_than(1.0).top(5, by: :duration)
end

report "POST by path" do
  where_verb("POST").count_by_path
end
```

No class defines `where_status` or `count_by_path`. `method_missing` in
`dsl.rb` reads the name, and `define_method` adds the method on its first
call. `instance_eval` runs the block of a report on a `Query`.

Each step of a query calls one entry of the kernel, for example
`Logq.where(sel, "status", 500..599)`. The entry loops over the rows in
AOT code. Thus the work of Fluxpinel grows with the number of steps, not
with the number of rows.

A block for each row (`queries/block.rb`) also works. Then the kernel
calls Fluxpinel once for each row, and the query is slower.

## Build and run

Run these commands in the top directory of Fluxpinel:

```sh
make host HOST=build/logq HOST_NAME=logq KERNEL=examples/logq/app.rb KERNEL_MAIN=Logq.main \
  KERNEL_ENTRIES=Logq.main,Logq.all,Logq.where,Logq.slower,Logq.where_block,Logq.top,Logq.count_by
ruby examples/logq/gen.rb 1000000 > access.log
build/logq/logq access.log examples/logq/queries/slow.rb
```

- `KERNEL_MAIN` makes the binary start at `Logq.main` with `ARGV`.
- `KERNEL_ENTRIES` names the start and the methods that the DSL calls.
- The kernel types the parameters of the entries with inline RBS
  (`#: (Array[Integer], String, untyped) -> Array[Integer]`). A selection
  thus crosses as an Array of Integer, with no copy.

## Run under CRuby

The same files run under CRuby. `kernel/fluxpinel_cruby.rb` gives the
functions of `kernel/fluxpinel.rb`:

```sh
echo 'Logq.main(ARGV)' > run.rb
ruby -r ./kernel/fluxpinel_cruby.rb -r ./examples/logq/app.rb run.rb access.log examples/logq/queries/slow.rb
```

The output is the same as the output of the Fluxpinel build.
