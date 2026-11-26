# A redirection of spawn, system or exec to [:child, fd]: the descriptor of
# the child after its own redirections, in whichever order the options name
# them; [:child, 1] is [:child, :out]. A descriptor of the child that no
# option redirects is CRuby's ArgumentError, and so is a cycle, named as
# CRuby names it.
r, w = IO.pipe
system("echo one >&2", err: [:child, :out], out: w)
system("echo two", out: [:child, :err], err: w)
system("echo three >&2", err: [:child, 1], out: w)
pid = spawn("echo four >&2", err: [:child, :out], out: w)
Process.wait(pid)
pid = fork { exec("echo five >&2", err: [:child, :out], out: w) }
Process.wait(pid)
pid = fork { exec("echo six >&3", 3 => [:child, :out], out: w) }
Process.wait(pid)
pid = fork { exec("echo seven >&3", 3 => [:child, 4], 4 => w) }
Process.wait(pid)
w.close
p r.read
[{ err: [:child, :out] },
 { out: [:child, :err], err: [:child, :out] },
 { in: [:child, :err], err: [:child, :in] },
 { err: [:child, :out], out: [:child, :in], in: [:child, :err] }].each do |opts|
  begin
    system("true", **opts)
  rescue ArgumentError => e
    p e.message
  end
end
