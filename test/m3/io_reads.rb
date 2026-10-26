# pread, pwrite, readpartial, sysread, syswrite and readline on an open
# File, File.readlines with a separator, IO.pipe and IO.copy_stream

path = "/tmp/fluxpinel_io_reads_#{$$}.txt"
copy = "/tmp/fluxpinel_io_reads_copy_#{$$}.txt"
File.write(path, "alpha;beta\ngamma\ndelta\nend\n")

File.open(path) do |f|
  p f.pread(5, 0), f.pread(4, 6), f.pos
  p f.readline(";"), f.readline(chomp: true), f.readline(2), f.readline
  p f.readpartial(100)
  [-> { f.readline }, -> { f.readpartial(4) }].each do |l|
    l.call
  rescue EOFError => e
    p e.class
  end
end
File.open(path) { |f| p f.sysread(5) }
File.open(path, "r+") { |f| p f.pwrite("ALPHA", 0) }
File.open(path, "a") { |f| p f.syswrite("epsilon\n") }
p File.read(path)

p File.readlines(path, ";"), File.readlines(path, chomp: true), File.readlines(path, 6).size

r, w = IO.pipe
w.write "through the pipe\n"
w.close
p r.read, r.class
r.close
p(IO.pipe { |a, b| b.puts "in a block"; b.close; a.gets })

p IO.copy_stream(path, copy), File.read(copy) == File.read(path)
File.open(path) { |src| File.open(copy, "w") { |dst| p IO.copy_stream(src, dst) } }
p File.size(copy)
require "stringio"
sio = StringIO.new
p IO.copy_stream(path, sio), sio.string.size
p IO.copy_stream(StringIO.new("from a StringIO"), copy), File.read(copy)
File.open(copy, "r+") { |f| p f.pwrite("a\x00b", 0) }
p File.read(copy)[0, 4]

File.delete(path)
File.delete(copy)
