# Kernel#fork and Process.fork: with a block, the child runs the block and
# exits 0, or with the status of its exit; an exception in the child ends
# the child with status 1 and never reaches the parent's rescue. Without a
# block, the pid in the parent and nil in the child. What was written to
# $stdout before the fork is written once. A child that execs a program
# hands it blocking stdio, so a pipeline of two children works. The
# at_exit hooks of the parent run in the child too.
$stdout.sync = false
print "before-fork "
r, w = IO.pipe
pid = fork do
  r.close
  w.puts "from the child, ppid ok: #{Process.ppid != Process.pid}"
  w.close
end
w.close
Process.wait(pid)
puts ""
p $?.exitstatus
p $?.pid == pid
p r.read
r.close

# the status of an exit in the block
pid = fork { exit 3 }
Process.wait(pid)
p $?.exitstatus

# an exception in the child: status 1 (and its report on stderr), and the
# parent's rescue is not run
begin
  pid = fork do
    raise "in the child"
  end
  Process.wait(pid)
  p $?.exitstatus
rescue RuntimeError => e
  p [:parent_rescued, e.message]
end

# without a block
pid = fork
if pid.nil?
  exit 7
end
Process.wait(pid)
p $?.exitstatus

# Process.fork, with and without a block
pid = Process.fork { exit 5 }
Process.wait(pid)
p $?.exitstatus
pid = Process.fork
exit 6 if pid.nil?
Process.wait(pid)
p $?.exitstatus

# a pipeline of two children, each replaced by a program
r, w = IO.pipe
pid1 = fork do
  r.close
  $stdout.reopen(w)
  w.close
  exec("printf", "a\\nb\\n")
end
pid2 = fork do
  w.close
  $stdin.reopen(r)
  r.close
  exec("tr", "a-z", "A-Z")
end
r.close
w.close
Process.wait(pid1)
Process.wait(pid2)
p $?.exitstatus

# fork { } from a method of a class, a class method, a method of a
# module, a method of a Struct, and Kernel.fork { }: fork is a private
# method of Kernel, so any self forks
class Sh
  def run = fork { exit 5 }
  def self.run = fork { exit 6 }
end
module Jobs
  def self.run = fork { exit 7 }
end
Job = Struct.new(:n) do
  def run = fork { exit n }
end
[Sh.new.run, Sh.run, Jobs.run, Job.new(8).run, Kernel.fork { exit 9 }].each do |pid|
  Process.wait(pid)
  print $?.exitstatus, " "
end
puts ""

# an exec that fails leaves the standard streams as they were: a thread of
# the child that then reads a pipe on $stdin parks, and the others run
r, w = IO.pipe
pid = fork do
  $stdin.reopen(r)
  begin
    exec("/nonexistent/command")
  rescue SystemCallError => e
    puts "exec failed: #{e.class}"
  end
  Thread.new { $stdin.read(1) }
  Thread.pass
  sleep 0.3
  puts "the main thread of the child runs"
  $stdout.flush
  exit! 0
end
sleep 1.5
Process.kill("KILL", pid) rescue nil
Process.wait(pid)
r.close
w.close
p $?.signaled?

# a child that dies of a signal: the status says so, as a shell reports a
# killed job; only an Interrupt is reported, as CRuby reports it, and the
# at_exit hooks and the buffered output come first
def child_stderr
  r, w = IO.pipe
  pid = fork do
    r.close
    $stderr.reopen(w)
    yield
  end
  w.close
  err = r.read
  r.close
  Process.wait(pid)
  [$?.signaled?, $?.termsig, $?.exitstatus, err]
end
r, w = IO.pipe
pid = fork { r.close; $stderr.reopen(w); sleep 5 }
w.close
sleep 0.3
Process.kill("TERM", pid)
Process.wait(pid)
p [$?.signaled?, $?.termsig, $?.exitstatus, r.read]
r.close
p child_stderr { raise SignalException, "SIGTERM" }
p child_stderr { raise SignalException, "HUP" }
p child_stderr { raise Interrupt }.then { |sig, no, st, err| [sig, no, st, err.include?("Interrupt")] }
p child_stderr { at_exit { puts "the hook runs before the signal"; $stdout.flush }; raise SignalException, "SIGTERM" }
p child_stderr { puts "unflushed"; raise SignalException, "SIGTERM" }

# a number for the signal, and a name that is no signal: the child ends as
# with any other exception, and never runs the code of the parent
p child_stderr { raise SignalException, 15 }
main = Process.pid
r, w = IO.pipe
pid = begin
  fork { r.close; $stderr.reopen(w); raise SignalException, "bogus" }
rescue ArgumentError => e
  puts "rescued #{e.class} in #{Process.pid == main ? "the parent" : "THE CHILD"}"
  nil
end
w.close
err = r.read
r.close
Process.wait(pid) if pid
puts "after the fork in #{Process.pid == main ? "the parent" : "THE CHILD"}"
p [$?.signaled?, $?.exitstatus, err.empty?]

# the at_exit hooks of the parent run in the child
at_exit { puts "at_exit in #{Process.pid == $main_pid ? "the parent" : "a child"}" }
$main_pid = Process.pid
pid = fork { }
Process.wait(pid)
p $?.exitstatus

# exec with the options of spawn: a leading Hash is the environment of the
# program, a trailing one its redirections and the other options, as
# CRuby's exec takes them
require "tmpdir"
dir = Dir.mktmpdir("fx_exec")
out = File.join(dir, "out.txt")
pid = fork { exec("printf", "to a file\\n", out: out) }
Process.wait(pid)
p $?.exitstatus, File.read(out)
pid = fork { exec({ "FX_EXEC" => "from env" }, "sh", "-c", "printf '%s\\n' \"$FX_EXEC\"", out: out) }
Process.wait(pid)
p File.read(out)
pid = fork { exec("sh", "-c", "printf err\\\\n >&2", err: [:child, :out], out: out) }
Process.wait(pid)
p File.read(out)
r, w = IO.pipe
pid = fork { r.close; exec("printf", "piped", out: w) }
w.close
p r.read
r.close
Process.wait(pid)
pid = fork { exec("pwd", out: out, chdir: dir) }
Process.wait(pid)
p File.read(out).chomp == File.realpath(dir)
# a descriptor named by its number or by an IO on it, as CRuby takes it,
# for exec and for spawn
r, w = IO.pipe
pid = fork { r.close; exec("printf", "by number", 1 => w) }
w.close
p r.read
r.close
Process.wait(pid)
r, w = IO.pipe
pid = spawn("printf", "by IO", $stdout => w)
w.close
p r.read
r.close
Process.wait(pid)
# a group of the standard descriptors, named by number or by Symbol
r, w = IO.pipe
pid = fork { r.close; exec("sh", "-c", "printf out; printf err >&2", [1, 2] => w) }
w.close
p r.read
r.close
Process.wait(pid)
r, w = IO.pipe
pid = fork { r.close; exec("sh", "-c", "printf out; printf err >&2", [:out, :err] => w) }
w.close
p r.read
r.close
Process.wait(pid)
# a descriptor past 2, to an IO or to a number: the command finds it open
r, w = IO.pipe
pid = fork { r.close; exec("sh", "-c", "printf three >&3", 3 => w) }
w.close
p r.read
r.close
Process.wait(pid)
p $?.exitstatus
r, w = IO.pipe
pid = fork { r.close; exec("sh", "-c", "printf five >&5", 5 => w.fileno) }
w.close
p r.read
r.close
Process.wait(pid)
# a standard stream and a descriptor past 2 in one call: 3 is the stdout
# of before the call
r, w = IO.pipe
r2, w2 = IO.pipe
pid = fork do
  r.close
  r2.close
  $stdout.reopen(w2)
  exec("sh", "-c", "printf new; printf old >&3", 1 => w, 3 => $stdout)
end
w.close
w2.close
p r.read, r2.read
r.close
r2.close
Process.wait(pid)
# two of them that read each other, as they were before the call
ra, wa = IO.pipe
rb, wb = IO.pipe
pid = fork do
  ra.close
  rb.close
  a = wa.fileno
  b = wb.fileno
  # (a shell takes one digit for a descriptor: with a larger one, the child
  # writes the same bytes itself)
  exec("sh", "-c", "printf to_a >&#{a}; printf to_b >&#{b}", a => wb, b => wa) if a < 10 && b < 10
  wa.write("to_b")
  wb.write("to_a")
  exit!(0)
end
wa.close
wb.close
p ra.read, rb.read
ra.close
rb.close
Process.wait(pid)
# a descriptor that is not open
pid = fork do
  begin
    exec("true", 3 => 9999)
  rescue SystemCallError => e
    puts e.class
  end
end
Process.wait(pid)
# a descriptor past 2 to a file: [path, mode] or [path, flags, perm] opens
# it, a bare path opens it for reading, :close closes it
f3 = File.join(dir, "f3.txt")
pid = fork { exec("sh", "-c", "printf written >&3", 3 => [f3, "w"]) }
Process.wait(pid)
p File.read(f3)
pid = fork { exec("sh", "-c", "printf more >&3", 3 => [f3, "a"]) }
Process.wait(pid)
p File.read(f3)
r, w = IO.pipe
pid = fork { r.close; exec("sh", "-c", "read line <&3; echo $line >&4", 3 => [f3, "r"], 4 => w) }
w.close
p r.read
r.close
Process.wait(pid)
f5 = File.join(dir, "f5.txt")
pid = fork { exec("sh", "-c", "printf flags >&5", 5 => [f5, File::WRONLY | File::CREAT | File::TRUNC, 0600]) }
Process.wait(pid)
p File.read(f5), File.stat(f5).mode & 0777
r, w = IO.pipe
pid = fork { r.close; exec("sh", "-c", "cat <&3 >&4", 3 => f5, 4 => w) }
w.close
p r.read
r.close
Process.wait(pid)
r, w = IO.pipe
pid = fork do
  r.close
  held = File.open(f3)
  n = held.fileno
  exec("sh", "-c", "if [ -e /dev/fd/#{n} ]; then echo open; else echo closed; fi", n => :close, 1 => w)
end
w.close
p r.read
r.close
Process.wait(pid)
# the file is opened before the command: a path that cannot be opened and a
# mode File.open refuses raise here, and a permission that is no number is
# a TypeError
pid = fork do
  begin
    exec("true", 3 => [File.join(dir, "none", "x"), "r"])
  rescue SystemCallError => e
    puts e.class
  end
  begin
    exec("true", 3 => [f3, "z"])
  rescue ArgumentError => e
    puts e.message
  end
  begin
    exec("true", 3 => [f3, "w", "0644"])
  rescue TypeError => e
    puts e.message
  end
  # the error names the path that failed to open, after a redirection to an
  # IO and after one to a file that opened
  f6 = File.join(dir, "f6.txt")
  begin
    exec("true", 3 => $stdout, 4 => [File.join(dir, "none", "x"), "r"])
  rescue SystemCallError => e
    puts e.message.sub(dir, "DIR")
  end
  begin
    exec("true", 3 => [f6, "w"], 4 => [File.join(dir, "none", "x"), "r"])
  rescue SystemCallError => e
    puts e.message.sub(dir, "DIR")
  end
  p File.exist?(f6)
  # an array whose first element is no String is the TypeError of File.open
  begin
    exec("true", 3 => [3, "r"])
  rescue TypeError => e
    puts e.message
  end
  begin
    exec("true", 3 => [])
  rescue TypeError => e
    puts e.message
  end
  exit 3
end
Process.wait(pid)
p $?.exitstatus
pid = fork { exec({ "FX_EXEC" => "x" }, "sh", "-c", "exit 7") }
Process.wait(pid)
p $?.exitstatus
FileUtils.rm_rf(dir) if defined?(FileUtils)
Dir.exist?(dir) && (File.delete(out) rescue nil; Dir.rmdir(dir))

# the file a standard stream goes to is opened on the lowest free
# descriptor, which the call may redirect too
lowest = IO.pipe.then { |a, b| n = a.fileno; a.close; b.close; n }
r, w = IO.pipe
pid = fork { r.close; exec("sh", "-c", "cat <&#{lowest}", lowest => f5, out: w, err: File::NULL) }
w.close
p r.read
Process.wait(pid)
r.close
