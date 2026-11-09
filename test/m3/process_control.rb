# Process.spawn, Kernel#spawn, Kernel#system, the backtick and the waits.
r, w = IO.pipe
pid = Process.spawn("/bin/echo", "-n", "hello", out: w)
w.close
p r.read
got, st = Process.waitpid2(pid)
p got == pid, st.class, st.exited?, st.success?, st.exitstatus
p $?.exitstatus

# a command line: the shell for a shell character, words else
r, w = IO.pipe
pid = Process.spawn("echo one two | tr a-z A-Z", out: w)
w.close
Process.wait(pid)
p r.read, $?.success?
pid = spawn("sleep 0")
p Process.waitpid(pid) == pid, $?.exitstatus

# the environment, chdir: and [program, argv0]
r, w = IO.pipe
pid = Process.spawn({ "FX_SPAWN" => "child" }, "sh", "-c", "printf %s \"$FX_SPAWN\"; pwd", chdir: "/", out: w)
w.close
Process.wait(pid)
p r.read
r, w = IO.pipe
Process.wait(Process.spawn(["printf", "argv0"], "%s|", "a", out: w))
w.close
p r.read
args = ["printf", "%s-", "x", "y"]
r, w = IO.pipe
Process.wait(Process.spawn(*args, out: w))
w.close
p r.read

# a filename redirection, a group key and [:child, :out]
require "tmpdir"
path = File.join(Dir.tmpdir, "fx_process_control_#{Process.pid}")
Process.wait(Process.spawn("sh", "-c", "echo O; echo E 1>&2", [:out, :err] => path))
p File.read(path).lines.sort
Process.wait(Process.spawn("sh", "-c", "echo both 1>&2", out: path, err: [:child, :out]))
p File.read(path)
File.write(path, "from file\n")
r, w = IO.pipe
Process.wait(Process.spawn("cat", in: path, out: w))
w.close
p r.read
File.delete(path)

# the failures
begin
  Process.spawn("/no/such/command_fx")
rescue SystemCallError => e
  p e.class
end
p $?.exitstatus
begin
  Process.spawn("true", chdir: "/no/such/dir_fx")
rescue SystemCallError => e
  p e.class
end
begin
  Process.spawn(*[])
rescue ArgumentError => e
  p e.message
end
begin
  Process.spawn({ "A" => "x" })
rescue ArgumentError => e
  p e.message
end
begin
  Process.spawn(["printf"])
rescue ArgumentError => e
  p e.message
end
begin
  Process.spawn({ "A=B" => "x" }, "true")
rescue ArgumentError => e
  p e.class
end

# system
p system("true"), $?.exitstatus
p system("sh", "-c", "exit 3"), $?.exitstatus
p system("/no/such/command_fx"), $?.exitstatus
r, w = IO.pipe
p system("echo", "via system", out: w)
w.close
p r.read
p system({ "FX_SYS" => "env" }, "sh", "-c", "test \"$FX_SYS\" = env")
begin
  system("sh", "-c", "exit 4", exception: true)
rescue RuntimeError => e
  p e.message
end
begin
  system("/no/such/command_fx", exception: true)
rescue SystemCallError => e
  p e.class
end
p system("true", exception: true)
p Kernel.system("true")

# the backtick: the command, a method of the program, super and Kernel.`
p `echo hi`
n = 2
p `echo #{n + 1}`, $?.success?
begin
  `/no/such/command_fx`
rescue SystemCallError => e
  p e.class
end
class Shell
  def `(cmd) = "Shell ran #{cmd}"
  def run = `ls -l`
  def runs(k) = (1..k).map { |i| %x(job #{i}) }
end
class Wrapped
  def `(cmd)
    "wrapped " + super
  end
  def run = `echo inner`
end
class Logged
  def `(cmd) = "log: " + Kernel.`(cmd)
  def run = `echo logged`
end
class Frozen
  def `(cmd) = cmd.frozen?
  def plain = `x`
  def interp(v) = `x#{v}`
end
class Recorder
  def self.`(cmd) = "class-level #{cmd}"
  def self.run = `date`
end
class Blank < BasicObject
  def `(cmd) = "blank " + super
  def run = `echo blank`
end
class Bare < BasicObject
  def run = `echo bare`
end
p Shell.new.run, Shell.new.runs(2), Wrapped.new.run, Logged.new.run
p Frozen.new.plain, Frozen.new.interp(1), Recorder.run
p((Blank.new.run rescue $!.class), (Bare.new.run rescue $!.class))

# wait2 with no pid, detach, last_status and exec in a child
pid = Process.spawn("sh", "-c", "exit 5")
got, st = Process.wait2
p got == pid, st.exitstatus, Process.last_status.exitstatus
pid = Process.spawn("sh", "-c", "exit 3")
t = Process.detach(pid)
st = t.value
p t.is_a?(Thread), st.exitstatus, st.pid == pid
begin
  Process.wait2(-1)
rescue Errno::ECHILD => e
  p e.class
end
