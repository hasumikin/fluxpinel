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
# an options Hash at the end of the command Array and an environment at its
# head, as CRuby reads them (rubish: IO.popen([exe, "-c", script, err:
# %i[child out]], &:read)); the options given as keywords join those of the
# Array, and a stream or option named by both is an ArgumentError
p IO.popen(["sh", "-c", "echo out; echo err >&2", err: [:child, :out]], &:read).lines.sort
p IO.popen([{ "FX_POPEN" => "1" }, "sh", "-c", "echo $FX_POPEN; echo e >&2", { err: File::NULL }], &:read)
p IO.popen(["sh", "-c", "pwd", chdir: "/"], &:read)
p IO.popen(["sh", "-c", "pwd; echo e >&2", chdir: "/"], err: File::NULL, &:read)
p IO.popen({ "FX_POPEN" => "2" }, [{ "FX_POPEN" => "1", "FX_POPEN_B" => "5" }, "sh", "-c", "echo $FX_POPEN $FX_POPEN_B"], &:read)
p IO.popen([["sh", "shx"], "-c", "echo $0", err: File::NULL], &:read)
[-> { IO.popen(["sh", "-c", "echo", in: File::NULL], in: File::NULL, &:read) },
 -> { IO.popen(["sh", "-c", "pwd", chdir: "/"], chdir: "/tmp", &:read) },
 -> { IO.popen(["sh", "-c", "echo", err: File::NULL], 2 => File::NULL, &:read) },
 -> { IO.popen(["sh", "-c", "echo w", 1 => :err], &:read) },
 -> { IO.popen([{ "FX_POPEN" => "1" }], &:read) }].each do |l|
  begin
    l.call
  rescue ArgumentError => e
    p e.message
  end
end
p $stdout.pid
File.delete(f)
Dir.rmdir(dir)
