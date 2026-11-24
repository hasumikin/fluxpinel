# PTY.open: a pseudo-terminal's master, an IO in sync, and its slave, a File
# on its /dev path; with a block the two are yielded and closed after it.
# PTY.spawn with a block yields the reader, the writer and the pid, leaves
# the two open and the child to the caller, and answers nil (rubish's tests
# feed a shell's stdin through PTY.open and run the shell under PTY.spawn's
# block).
require "pty"
master, slave = PTY.open
p [master.class, slave.class, slave.path.start_with?("/dev/"), master.tty?, slave.tty?, master.sync]
p [master.closed?, slave.closed?, master.close_on_exec?]
master.write("hi\n")
p slave.gets
slave.write("back\n")
p master.gets
master.close
slave.close
# two terminals open at once: each slave keeps the path of its own
m1, s1 = PTY.open
m2, s2 = PTY.open
p s1.path != s2.path, s1.path == File.realpath(s1.path), s2.path == File.realpath(s2.path)
[m1, s1, m2, s2].each(&:close)
x = PTY.open { |m, s| m.write("yo\n"); [s.gets, m, s] }
p x[0], x[1].closed?, x[2].closed?
v = PTY.spawn("echo spawned") do |reader, writer, pid|
  p reader.gets
  $pid = pid
  $reader = reader
  $writer = writer
  :block
end
p v, $reader.closed?, $writer.closed?
$reader.close
$writer.close
# the child of the block form is the caller's to wait for, ended or not
sleep 0.5
begin
  Process.wait($pid)
  p :waited
rescue Errno::ECHILD
  p :reaped
end
PTY.spawn("sleep 0.2") { |_r, _w, pid| $pid = pid }
sleep 0.7
begin
  Process.wait($pid)
  p :waited
rescue Errno::ECHILD
  p :reaped
end
PTY.spawn("true") { |r, _w, pid| r.read rescue nil; Process.wait(pid); $st = $?.exitstatus }
p $st
reader, writer, pid = PTY.spawn("echo plain")
p reader.gets, reader.closed?
reader.close
writer.close
Process.wait(pid)
begin
  PTY.open(1)
rescue ArgumentError => e
  puts e.message
end
