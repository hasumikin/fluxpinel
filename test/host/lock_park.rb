# A wait of the runtime in a thread of the walker, while the main thread
# holds the walker lock when the wait ends: the thread parks again on its
# way back to the lock, and a collection in that park must keep the result
# of the wait (the line that gets read, the character that getc read). The
# main thread holds the lock for a while with allocations, so the GC
# stress leg of make test-host collects in that park.

r, w = IO.pipe
q = Queue.new
t = Thread.new { q << 1; s = r.gets; c = r.getc; [s, c] }
q.pop
w.puts "abc"
w.write "z"
x = Time.now
a = [1] while Time.now - x < 0.2
p t.value

# the same for a value that a Queue hands over
q2 = Queue.new
t2 = Thread.new { s = q2.pop; s.upcase }
q2 << "abc".dup
x = Time.now
a = [1] while Time.now - x < 0.2
p t2.value
