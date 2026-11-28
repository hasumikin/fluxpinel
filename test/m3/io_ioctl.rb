# IO#ioctl: the request with an Integer, or with a String buffer the call
# writes into; the answer of ioctl(2), and the Errno of a request the
# descriptor does not take.
require "pty"
require "io/console"
darwin = RUBY_PLATFORM.include?("darwin")
tiocgwinsz = darwin ? 0x40087468 : 0x5413
tiocsctty = darwin ? 0x20007461 : 0x540E

m, s = PTY.open
m.winsize = [24, 80]
buf = "\0" * 8
p s.ioctl(tiocgwinsz, buf), buf.unpack("S2")
p buf.bytesize

r, w = IO.pipe
begin
  r.ioctl(tiocgwinsz, "\0" * 8)
rescue SystemCallError => e
  p e.class
end
r.close
w.close

# a child that makes the pty its controlling terminal: setsid, then TIOCSCTTY
# on the slave, which a Process.getpgrp of its own then leads
out_r, out_w = IO.pipe
pid = fork do
  out_r.close
  Process.setsid
  STDIN.reopen(s)
  out_w.puts STDIN.ioctl(tiocsctty, 0)
  out_w.puts Process.getsid == Process.pid
  out_w.close
  exit!(0)
end
out_w.close
puts out_r.read
Process.wait(pid)
s.close
m.close

begin
  s.ioctl(tiocgwinsz, buf)
rescue IOError => e
  puts e.message
end
begin
  STDIN.ioctl("x")
rescue TypeError => e
  puts e.message
end
