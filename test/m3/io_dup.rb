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
