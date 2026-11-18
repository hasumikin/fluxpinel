# The module functions of Kernel that end the program or take a signal,
# called on Kernel as the receiver: exit, exit!, abort and trap.

begin
  Kernel.exit 3
rescue SystemExit => e
  p [e.class, e.status, e.success?]
end

begin
  ::Kernel.exit
rescue SystemExit => e
  p [e.status, e.success?]
end

begin
  Kernel.exit(false)
rescue SystemExit => e
  p e.status
end

begin
  Kernel.abort "to stderr"
rescue SystemExit => e
  p [e.status, e.message]
end

begin
  Kernel.exit 1, 2
rescue ArgumentError => e
  p e.class
end

prev = Kernel.trap("INT") { |no| puts "got #{no}" }
p prev
got = []
Kernel.trap("USR2") { |no| got << no }
Process.kill("USR2", Process.pid)
sleep 0.05
p got.size
h = Kernel.trap("INT", "DEFAULT")
p h.class, h.lambda?
p Kernel.trap(:INT, prev)
p ::Kernel.trap("INT", nil).class
begin
  Kernel.trap("NOPE") { }
rescue ArgumentError => e
  puts e.message
end

# the module functions that take the block of the call
pr = ::Kernel.proc { |x| x }
p pr.call(3), pr.lambda?, Kernel.lambda { |x| x * 2 }.call(4), Kernel.lambda { }.lambda?
n = 0
Kernel.loop { n += 1; break if n == 3 }
p n, Kernel.catch(:t) { Kernel.throw :t, 9 }, Kernel.catch { |tag| throw tag, 7 }
def early = Kernel.proc { |v| return v }.call(5)
p early

# a signal with no handler and no block
begin
  Kernel.trap("USR1")
rescue ArgumentError => e
  puts e.message
end

# exit! ends the program at once, with no at_exit hook and no flush of
# what $stdout holds
at_exit { puts "hook" }
puts "end"
$stdout.flush
Kernel.exit! 4
