# textkit

textkit is a small text tool. It shows the dynamic layer of Fluxpinel
(docs/design.md, section 5.1):

- Spinel compiles the tool (`app.rb`) to C. The tool reads the input,
  splits the lines into words and counts the words.
- Fluxpinel runs the plugins (`plugins/*.rb`) at run time. Each plugin adds a
  filter. You add or change a plugin, and you do not compile again.

The two sides call each other, and a value crosses as it is:

- The tool loads each plugin with `Fluxpinel.load` and calls its filter with
  `Fluxpinel.call` (`kernel/fluxpinel.rb`).
- A plugin calls the AOT code of the tool: `20_stopwords.rb` splits a line
  with `Textkit.words`.

## Build and run

Run these commands in the top directory of Fluxpinel:

```sh
make host HOST=build/textkit KERNEL=examples/textkit/app.rb KERNEL_ENTRIES=Textkit.main,Textkit.words
build/textkit/fluxpinel examples/textkit/run.rb examples/textkit/input.txt examples/textkit/plugins
```

The output:

```
blank: 4 lines
stopwords: 4 lines
style: 4 lines
--
Gem crystal earth
Jeweler looks at gem through loupe
WHY CRYSTAL SO CLEAR
Light room moves crystal gem
--
crystal: 3
gem: 3
at: 1
```

`KERNEL_ENTRIES` names the methods of the tool that Fluxpinel calls: the start
(`Textkit.main`) and the methods that a plugin calls.

## Add a plugin

Put a file in `plugins/`. Its last statement gives an object with `name`
and `filter(lines)`. For example, `plugins/40_number.rb`:

```ruby
class Number
  def name = "number"
  def filter(lines) = lines.each_with_index.map { |line, i| "#{i + 1}. #{line}" }
end

Number.new
```

Run `build/textkit/fluxpinel` again. The binary stays the same.

## Run under CRuby

The same files run under CRuby. `kernel/fluxpinel_cruby.rb` gives the
functions of `kernel/fluxpinel.rb`:

```sh
ruby -r ./kernel/fluxpinel_cruby.rb -r ./examples/textkit/app.rb examples/textkit/run.rb \
  examples/textkit/input.txt examples/textkit/plugins
```

The output is the same as the output of the Fluxpinel build.
