# IO#close_on_exec= sets or clears the FD_CLOEXEC flag of the descriptor, as
# close_on_exec? reads it: a descriptor cleared stays open across an exec
# (rubish clears the shell's end of a process substitution's pipe, so the
# command reads /dev/fd/N). The value is taken for its truth and answered.
r, w = IO.pipe
p r.close_on_exec?
p (r.close_on_exec = false)
p r.close_on_exec?, r.fcntl(1) & 1
p (r.close_on_exec = nil), r.close_on_exec?
p (r.close_on_exec = 0), r.close_on_exec?
p (r.close_on_exec = true), r.close_on_exec?
f = File.open(__FILE__)
p f.close_on_exec?
f.close_on_exec = false
p f.close_on_exec?
f.close
p $stdout.close_on_exec?
r.close
begin
  r.close_on_exec = true
rescue IOError => e
  puts e.message
end
begin
  w.send(:close_on_exec=, 1, 2)
rescue ArgumentError => e
  puts e.message
end
p w.respond_to?(:close_on_exec=), w.respond_to?(:close_on_exec?)
w.close
# the descriptor reaches the command of an exec once the flag is cleared,
# and is closed for it while the flag stands (by its /dev/fd path: a shell
# keeps descriptors of its own from 10 up, which a redirection to the
# number could meet)
r, w = IO.pipe
pid = fork { r.close; w.close_on_exec = false; exec("sh", "-c", "echo via > /dev/fd/#{w.fileno}") }
w.close
p r.read
Process.wait(pid)
p $?.exitstatus
r.close
r, w = IO.pipe
pid = fork { r.close; exec("sh", "-c", "test -e /dev/fd/#{w.fileno}") }
w.close
p r.read
Process.wait(pid)
p $?.success?
