# IO.for_fd(fd, mode, autoclose: false), IO.new and IO.open with the keyword:
# the IO leaves the descriptor open when it closes and when the collector
# takes it, as CRuby's does (rubish writes its xtrace through
# IO.for_fd(fd, "w", autoclose: false) and drops the IO; the file behind the
# descriptor goes on). IO#autoclose? reads the flag and IO#autoclose= sets it.
require "tmpdir"
dir = Dir.mktmpdir("fx_autoclose")
path = File.join(dir, "t.txt")
f = File.open(path, "w")
io = IO.for_fd(f.fileno, "w", autoclose: false)
p io.autoclose?, io.fileno == f.fileno
io.puts "via for_fd"
io.flush
io = nil
GC.start
10.times { "x" * 1000 }
GC.start
f.puts "still open"
f.close
p File.read(path)
# a close of the IO leaves the descriptor to its owner
g = File.open(path, "a")
io = IO.for_fd(g.fileno, "a", autoclose: false)
io.close
g.puts "after the close"
g.close
p File.read(path).lines.size
# the flag read and set
r, w = IO.pipe
a = IO.for_fd(w.fileno, "w")
p a.autoclose?, (a.autoclose = false), a.autoclose?, (a.autoclose = nil), a.autoclose?, (a.autoclose = 1), a.autoclose?
a.autoclose = false
a.close
w.puts "w still writes"
w.close
p r.read
r.close
b = IO.new($stdout.fileno, "w", autoclose: false)
p b.autoclose?
c = IO.open($stdout.fileno, "w", autoclose: false) { |x| x.autoclose? }
p c
r2, w2 = IO.pipe
d = IO.for_fd(w2.fileno, "w", autoclose: true)
p d.autoclose?
d.autoclose = false
d.close
w2.close
r2.close
begin
  d.autoclose?
rescue IOError => e
  puts e.message
end
begin
  d.autoclose = false
rescue IOError => e
  puts e.message
end
begin
  b.send(:autoclose=, 1, 2)
rescue ArgumentError => e
  puts e.message
end
File.delete(path)
Dir.rmdir(dir)
puts "end"
