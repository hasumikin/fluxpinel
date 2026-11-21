require "tmpdir"
# IO#dup: a new IO on a copy of the descriptor, which writes and reads the
# same stream and closes on its own.
r, w = IO.pipe
w2 = w.dup
p w2.class, w2.fileno != w.fileno, w2.closed?
w2.puts "via dup"
w2.close
p w.closed?, w2.closed?
w.puts "via original"
w.close
p r.read
r2 = r.dup
p r2.read, r2.eof?

# the standard streams, and a File keeps its path
o = $stdout.dup
o.puts "stdout dup"
o.close
puts "still here"
p $stdin.dup.fileno > 2
f = File.open("/dev/null")
g = f.dup
p g.class, g.path, g.fileno != f.fileno
f.close
p g.closed?, g.read(1)
g.close

# a closed IO cannot be copied
begin
  f.dup
rescue IOError => e
  p e.class, e.message
end

# the copy takes the state of the original: the line number, sync, the
# encoding, close-on-exec, and what stdio has read already stays in order
File.write(d = File.join(Dir.tmpdir, "fx_io_dup_#{$$}.txt"), "one\ntwo\nthree\n")
f = File.open(d)
f.gets
g = f.dup
p g.lineno, g.gets, g.lineno, f.gets
p g.close_on_exec?, $stderr.dup.sync, $stdout.dup.sync
f.binmode
p f.dup.external_encoding
f.close
g.close
File.delete(d)

# a socket is copied as a socket
require "socket"
a, b = UNIXSocket.pair
c = a.dup
p c.class, c.fileno != a.fileno
b.write "hi"
p c.recv(2)
a.close
b.close
c.close

# clone(freeze: true) freezes the copy; a frozen IO cannot be copied at all,
# with dup or with clone, as in CRuby
h = File.open("/dev/null")
p h.clone.frozen?, h.clone(freeze: true).frozen?, h.clone(freeze: false).frozen?
p h.freeze.equal?(h), h.frozen?
[-> { h.dup }, -> { h.clone }, -> { h.clone(freeze: false) }].each do |c|
  c.call
rescue FrozenError => e
  p e.class
end
