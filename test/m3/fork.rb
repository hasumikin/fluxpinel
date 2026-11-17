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

# the at_exit hooks of the parent run in the child
at_exit { puts "at_exit in #{Process.pid == $main_pid ? "the parent" : "a child"}" }
$main_pid = Process.pid
pid = fork { }
Process.wait(pid)
p $?.exitstatus
