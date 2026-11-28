# A thread that wakes while the main thread runs a loop with no wait in it
# gets its turn at the loop's next poll, as a thread does under CRuby's GVL
# when the holder reaches its next check. The walker lock is given up for a
# turn when a thread waits for it; the host build has several workers, and
# nothing else makes main let go.
t0 = Time.now
$woke = nil
Thread.new { sleep 0.2; $woke = Time.now - t0 }
x = 0
x += 1 while $woke.nil? && Time.now - t0 < 3
p $woke ? $woke < 2 : :never

# two threads computing, and main computing: each gets turns
counts = [0, 0]
threads = 2.times.map { |i| Thread.new { t = Time.now; counts[i] += 1 while Time.now - t < 0.5 } }
t = Time.now
y = 0
y += 1 while Time.now - t < 0.5
threads.each(&:join)
p counts.all?(&:positive?), y.positive?
