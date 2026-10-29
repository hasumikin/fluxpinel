# trap and Signal.trap: the handler that a call installs and the previous
# one that it answers. No signal is delivered here.

prev = trap("INT") { |no| puts "got #{no}" }
p prev
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
