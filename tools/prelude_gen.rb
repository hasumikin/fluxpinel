# prelude_gen.rb -- turn src/prelude.rb into a C string literal.
#
# Usage: ruby tools/prelude_gen.rb src/prelude.rb > prelude.inc
#
# The output is the body of
#
#   static const char fx_prelude_src[] = ...;
#
# which src/prelude.c includes. Fluxpinel parses it when it first needs a
# method of it.

src = ARGV[0]
abort "usage: ruby tools/prelude_gen.rb PRELUDE.rb" if src.nil?

File.binread(src).each_line do |line|
  puts '"' + line.gsub(/[\\"]/) { "\\#{_1}" }.gsub("\n", "\\n") + '"'
end
puts '""' if File.size(src).zero?
