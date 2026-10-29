# trap and Signal.trap: the handler that a call installs and the previous
# one that it answers, and one signal that the program sends itself.

prev = trap("INT") { |no| puts "got #{no}" }
p prev
got = []
trap("USR2") { |no| got << no }
p Process.kill("USR2", Process.pid)
sleep 0.05
p got.size == 1, got.first.class
p Process.kill(0, Process.pid), Process.respond_to?(:kill)
h = trap("INT", "DEFAULT")
p h.class, h.lambda?
p trap(:INT, prev)
p trap("INT", nil).class
p Signal.trap("TERM", "IGNORE")
p Signal.trap("TERM") { }
p Signal.trap("TERM", "SYSTEM_DEFAULT").class
p trap("USR1", proc { |n| n }).class
p trap("USR1", "DEFAULT").class

begin
  trap("KILL") { }
rescue ArgumentError, Errno::EINVAL => e
  puts "refused"
end
begin
  trap("NOPE") { }
rescue ArgumentError => e
  puts e.message
end
begin
  trap
rescue ArgumentError => e
  puts e.message
end
