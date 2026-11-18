# IO#reopen with a path: the handle is the other file now, and keeps its
# mode when none is given, as CRuby's does.
require "tmpdir"
Dir.mktmpdir("fx_reopen") do |d|
  a = File.join(d, "a")
  b = File.join(d, "b")
  File.write(a, "aaa\n")
  File.write(b, "bbb\n")

  # the same object, the other path, and reads from the other file
  f = File.open(a)
  r = f.reopen(b)
  p r.equal?(f), f.path == b, f.read
  f.close

  # no mode: a "w" handle reopens for writing, so the other file is truncated
  w = File.open(a, "w")
  w.write("x")
  w.reopen(b)
  w.write("new")
  w.close
  p File.read(a), File.read(b)

  # a mode replaces the handle's
  f2 = File.open(a)
  f2.reopen(b, "a")
  f2.write("+")
  f2.close
  p File.read(b)

  # a closed handle gets a fresh stream
  c = File.open(a)
  c.close
  c.reopen(b, "r")
  p c.closed?, c.read
  c.close

  # a missing file, and an argument that is neither a path nor an IO
  p((File.open(a).reopen(File.join(d, "none")) rescue $!.class))
  p((File.open(a).reopen(123) rescue $!.class))

  # the IO form stays
  g = File.open(a)
  h = File.open(b)
  g.reopen(h)
  p g.read
  g.close
  h.close

  # the write end of a pipe becomes a file
  rd, wr = IO.pipe
  wr.reopen(b, "w")
  wr.puts "pipe-end"
  wr.close
  rd.close
  p File.read(b)

  # the standard streams of a child, as a shell redirects them
  out = File.join(d, "out")
  pid = fork do
    $stdout.reopen(out, "w")
    $stderr.reopen(File::NULL, "w")
    puts "child"
    warn "hidden"
    $stdout.flush
    exit!(0)
  end
  Process.wait(pid)
  p $?.exitstatus, File.read(out)
end
