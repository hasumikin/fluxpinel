# engine_flags.rb -- print the C flags of the engine constants that
# spinel gives a program: RUBY_VERSION, RUBY_RELEASE_DATE, RUBY_REVISION
# and RUBY_COPYRIGHT. The values come from the spinel source, so that they
# follow the pin.
#
# Usage: ruby tools/engine_flags.rb SPINEL

spinel = ARGV[0] or abort "usage: ruby tools/engine_flags.rb SPINEL"
analyze = File.read(File.join(spinel, "src/analyze.h"))
codegen = File.read(File.join(spinel, "src/codegen_expr.c"))

def find(src, re, what)
  src[re, 1] or abort "engine_flags.rb: no #{what} in the spinel source"
end

values = {
  "FX_RUBY_VERSION" => find(analyze, /^#define SP_RUBY_VERSION "([^"]+)"/, "SP_RUBY_VERSION"),
  "FX_RUBY_RELEASE_DATE" => find(codegen, /sp_str_ruby_release_date", "\\"([^\\]+)/, "RUBY_RELEASE_DATE"),
  "FX_RUBY_REVISION" => find(codegen, /sp_str_ruby_revision", "\\"([^\\]+)/, "RUBY_REVISION"),
  "FX_RUBY_COPYRIGHT" => find(codegen, /sp_str_ruby_copyright", "\\"([^\\]+)/, "RUBY_COPYRIGHT"),
}
puts values.map { |k, v| "-D#{k}='\"#{v}\"'" }.join(" ")
