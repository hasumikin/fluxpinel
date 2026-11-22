# IO.popen: a child with a pipe to its stdout (mode "r") or from its stdin
# ("w"). The IO knows the pid of the child; close waits for the child and
# sets $?; the block form closes and answers the block's value. The
# command is an argv Array or a command line, with the environment and
# the options of spawn (rubish reads a helper's output with
# IO.popen(env, argv, err: [:child, :out], &:read)).
require "tmpdir"
p IO.popen(["echo", "x"], &:read)
p IO.popen({ "FX_POPEN" => "from env" }, ["sh", "-c", "echo $FX_POPEN"], err: [:child, :out], &:read)
io = IO.popen(["sh", "-c", "echo a; echo b >&2; exit 3"], err: [:child, :out])
p io.pid.class, io.pid > 0, io.read.lines.sort, io.closed?
pid = io.pid
p io.close, io.closed?, $?.exitstatus, $?.pid == pid
p IO.popen("echo shell", "r").read
p IO.popen("exit 5") { |o| o.read }, $?.exitstatus
dir = Dir.mktmpdir("fx_popen")
f = File.join(dir, "o.txt")
IO.popen(["tr", "a-z", "A-Z"], "w", out: f) { |w| w.puts "hello" }
p File.read(f), $?.success?
p IO.popen(["cat"], in: File::NULL, &:read)
p IO.popen(["sh", "-c", "exit 0"], &:read), $?.exitstatus
begin
  IO.popen(["nonexistent_cmd_fx_xyz"], &:read)
rescue Errno::ENOENT => e
  p e.class
end
p $stdout.pid
File.delete(f)
Dir.rmdir(dir)
