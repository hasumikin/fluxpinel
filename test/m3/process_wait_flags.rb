# The waits of Process with the flags of waitpid(2), over the runtime:
# WNOHANG answers nil for a child still running and the pid once it has
# ended; WUNTRACED answers a stopped child, whose status says stopped? and
# stopsig; $? follows the child a flagged wait reaps. The process group
# calls of a shell's job control: setpgid, getpgid and setsid in a child.
pid = Process.spawn("sleep", "0.3")
p Process.wait2(pid, Process::WNOHANG)
p Process.waitpid(pid, Process::WNOHANG)
p Process.wait(pid, Process::WNOHANG)
p Process.waitpid2(pid, Process::WNOHANG)
r, st = Process.wait2(pid, 0)
p r == pid
p st.class
p st.exitstatus
p $?.pid == pid

pid = Process.spawn("/usr/bin/false")
sleep 0.2
p Process.wait(pid, Process::WNOHANG) == pid
p $?.exitstatus

pid = Process.spawn("sleep", "5")
sleep 0.2
Process.kill("STOP", pid)
r, st = Process.wait2(pid, Process::WUNTRACED)
p r == pid
p st.stopped?
p st.stopsig == Signal.list["STOP"]
p st.exited?
p st.exitstatus
p $?.stopped?
Process.kill("CONT", pid)
Process.kill("TERM", pid)
_, st = Process.wait2(pid)
p st.stopped?
p st.stopsig
p st.termsig == Signal.list["TERM"]

p Process::WNOHANG.is_a?(Integer)
p Process::WNOHANG != Process::WUNTRACED

# a child in a process group of its own, and one in a session of its own
pid = Process.spawn("sleep", "0.3", pgroup: true)
p Process.getpgid(pid) == pid
p Process.getpgid(0) == Process.getpgrp
Process.wait(pid)
p Process.setpgid(Process.pid, Process.getpgrp)
