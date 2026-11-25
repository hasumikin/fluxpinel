# embed_gen.rb -- the Ruby files of the libraries, as a table of C.
#
# Usage: ruby tools/embed_gen.rb NAME=DIR... [--extra NAME=DIR...] > embed.inc
#
# Each NAME=DIR is a directory of Ruby files and its name in the binary:
# DIR/a/b.rb goes in as /@fluxpinel/NAME/a/b.rb. Before --extra, the
# directories are those of the libraries: a directory named test is not a
# part of a library and does not go in. After --extra, they are the
# directories of a program (EMBED_EXTRA of the Makefile), and each .rb file
# goes in. The output is
#
#   static const struct { const char *path; const unsigned char *src; size_t len; } fx_embed_files[]
#
# in the order of strcmp of the paths, and its count fx_embed_count, which
# src/load.c includes under FX_EMBED (docs/design.md, the libraries in the
# binary).

ROOT = "/@fluxpinel"

abort "usage: ruby tools/embed_gen.rb NAME=DIR... [--extra NAME=DIR...]" if ARGV.empty?

extra = false
names = {}
files = ARGV.flat_map do |arg|
  if arg == "--extra"
    extra = true
    next []
  end
  name, dir = arg.split("=", 2)
  abort "embed_gen: #{arg} is not NAME=DIR" if dir.nil? || name.empty?
  abort "embed_gen: the name #{name} has a /" if name.include?("/")
  abort "embed_gen: the name #{name} is given two times" if names[name]
  abort "embed_gen: no directory #{dir}" unless File.directory?(dir)
  names[name] = true
  rels = Dir.glob("**/*.rb", base: dir)
  rels = rels.reject { |rel| rel.split("/").include?("test") } unless extra
  rels.map { |rel| ["#{ROOT}/#{name}/#{rel}", File.binread(File.join(dir, rel))] }
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
