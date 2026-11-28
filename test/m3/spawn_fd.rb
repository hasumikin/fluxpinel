# spawn and system with a redirection of a descriptor past 2: the child
# gets the IO, the number, the file or :close on that descriptor, beside
# the standard streams and the other options.
r, w = IO.pipe
pid = spawn("sh", "-c", "echo hi >&3; echo out", 3 => w, out: File::NULL)
w.close
p r.read
Process.wait(pid)
p $?.exitstatus, $?.pid == pid
r.close

r2, w2 = IO.pipe
w2.write("abc\n")
w2.close
r, w = IO.pipe
pid = spawn("sh", "-c", "read x <&4; echo got-$x >&5", 4 => r2, 5 => w)
w.close
p r.read
Process.wait(pid)
r.close
r2.close

# a number names the descriptor of this process
r, w = IO.pipe
pid = spawn("sh", "-c", "echo num >&7", 7 => w.fileno)
w.close
p r.read
Process.wait(pid)
r.close

# a file by its path and mode, with chdir and an environment
require "tmpdir"
Dir.mktmpdir do |d|
  path = File.join(d, "three.txt")
  pid = spawn({ "SPAWN_FD_VAR" => "v" }, "sh", "-c", "echo $SPAWN_FD_VAR $(pwd) >&3", 3 => [path, "w"], chdir: d)
  Process.wait(pid)
  p File.read(path) == "v #{File.realpath(d)}\n"
  pid = spawn("sh", "-c", "echo again >&3", 3 => [path, File::WRONLY | File::APPEND, 0o644])
  Process.wait(pid)
  p File.read(path).lines.last
  pid = spawn("sh", "-c", "cat <&3", 3 => path, out: File::NULL)
  Process.wait(pid)
  p $?.success?
end

# the file a standard stream goes to is opened on the lowest free
# descriptor, which the call may redirect too
lowest = IO.pipe.then { |a, b| n = a.fileno; a.close; b.close; n }
r, w = IO.pipe
Dir.mktmpdir do |d|
  path = File.join(d, "in.txt")
  File.write(path, "from file\n")
  pid = spawn("sh", "-c", "cat <&#{lowest}; echo tail >&2", lowest => path, out: w, err: File::NULL)
  w.close
  p r.read
  Process.wait(pid)
  p $?.exitstatus
end
r.close

# :close: the child has no descriptor 3
p system("sh", "-c", "echo x >&3", 3 => :close, err: File::NULL)
p $?.exitstatus == 0

# the parent keeps its descriptor
r, w = IO.pipe
system("sh", "-c", "echo one >&3", 3 => w)
w.write("two\n")
w.close
p r.read
r.close

# a program that cannot run raises, with no child left behind
r, w = IO.pipe
begin
  spawn("no_such_program_for_spawn_fd", 3 => w)
rescue SystemCallError => e
  p e.class, e.message
end
w.close
p r.read
r.close

# a thread other than the main one: the child of the runtime makes the
# descriptor, so the call needs no fork of the walker
t = Thread.new do
  r, w = IO.pipe
  pid = spawn("sh", "-c", "echo thread >&3", 3 => w)
  w.close
  s = r.read
  Process.wait(pid)
  r.close
  s
end
p t.value
