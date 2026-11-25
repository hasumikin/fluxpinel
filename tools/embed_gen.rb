# embed_gen.rb -- the Ruby files of the libraries, as a table of C.
#
# Usage: ruby tools/embed_gen.rb NAME=DIR... > embed.inc
#
# Each NAME=DIR is a directory of Ruby files and its name in the binary:
# DIR/a/b.rb goes in as /@fluxpinel/NAME/a/b.rb. A directory named test is
# not a part of a library and does not go in. The output is
#
#   static const struct { const char *path; const unsigned char *src; size_t len; } fx_embed_files[]
#
# in the order of strcmp of the paths, and its count fx_embed_count, which
# src/load.c includes under FX_EMBED (docs/design.md, the libraries in the
# binary).

ROOT = "/@fluxpinel"

abort "usage: ruby tools/embed_gen.rb NAME=DIR..." if ARGV.empty?

files = ARGV.flat_map do |arg|
  name, dir = arg.split("=", 2)
  abort "embed_gen: #{arg} is not NAME=DIR" if dir.nil? || name.empty?
  abort "embed_gen: no directory #{dir}" unless File.directory?(dir)
  Dir.glob("**/*.rb", base: dir).reject { |rel| rel.split("/").include?("test") }.map do |rel|
    ["#{ROOT}/#{name}/#{rel}", File.binread(File.join(dir, rel))]
  end
end
# the order of strcmp: by the bytes
files.sort_by! { |path, _| path.b }

files.each_with_index do |(_, src), i|
  puts "static const unsigned char fx_embed_d#{i}[] = {"
  # one NUL after the bytes, as the buffer of a file that is read has
  (src.bytes + [0]).each_slice(32) { |row| puts row.join(",") + "," }
  puts "};"
end
puts "static const struct { const char *path; const unsigned char *src; size_t len; } fx_embed_files[] = {"
files.each_with_index do |(path, src), i|
  puts "  { #{path.dump}, fx_embed_d#{i}, #{src.bytesize} },"
end
puts "};"
puts "static const int fx_embed_count = #{files.size};"
