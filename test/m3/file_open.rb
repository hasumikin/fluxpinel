# File.open, File.new, File.foreach, File.readlines and the reads of an
# open File

path = "/tmp/fluxpinel_file_open_#{$$}.txt"
File.open(path, "w") { |f| f.puts "a;b;c"; f.write "line2\n", "line3\n"; f << "end\n" }

f = File.open(path)
p f.class, f.gets, f.closed?
f.close
p f.closed?

# the block: its value, and the File closes after a raise and a break
p File.open(path) { |g| g.read.size }
held = nil
begin
  File.open(path) { |g| held = g; raise "boom" }
rescue => e
  p e.message, held.closed?
end
g = File.open(path) { |h| break h }
p g.closed?

# the mode and the permission, positional and as keywords
File.open(path, "a") { |h| h.puts "more" }
File.open(path, File::WRONLY | File::APPEND) { |h| h.puts "flags" }
p File.new(path, mode: "r").read.lines.size
File.open(path, "r", 0o644) { |h| p h.gets }
begin
  File.open("/tmp/fluxpinel_no_such_dir/x.txt")
rescue SystemCallError => e
  p e.class
end

# the reads with a length, a separator and a limit
File.open(path) do |h|
  p h.read(3), h.gets(";"), h.gets(chomp: true), h.gets(3), h.read(2)
  p h.readlines.size, h.read(4), h.read
end
File.open(path) { |h| h.each_line(";") { |l| p l } }
File.open(path) { |h| h.each_line(chomp: true) { |l| p l } }
File.open(path) { |h| p h.readlines(";").size }
File.open(path) { |h| n = 0; h.each_char { n += 1 }; p n }
File.open(path) { |h| n = 0; h.each_byte { |b| n += b }; p n }

File.foreach(path) { |l| p l }
File.foreach(path, chomp: true) { |l| p l }
File.foreach(path, 4) { |l| p l }
p IO.foreach(path) { |l| break l }
p File.readlines(path), File.readlines(path, chomp: true)

File.delete(path)
