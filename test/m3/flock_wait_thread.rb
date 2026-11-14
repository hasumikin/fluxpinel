# A thread that waits in flock for a lock of the main thread does not stop
# the main thread: the main thread wakes, unlocks, and the thread gets the
# lock.
path = "/tmp/fx_flock_wait_#{Process.pid}.lock"
File.write(path, "")
a = File.open(path, "r")
p a.flock(File::LOCK_EX)
arrived = Queue.new
t = Thread.new do
  b = File.open(path, "r")
  arrived << 1
  r = b.flock(File::LOCK_EX)
  b.close
  r
end
arrived.pop
sleep 0.1
puts "awake"
p a.flock(File::LOCK_UN)
p t.value
# a shared lock that waits for no one, and a lock that does not wait
c = File.open(path, "r")
p a.flock(File::LOCK_SH), c.flock(File::LOCK_SH)
p c.flock(File::LOCK_EX | File::LOCK_NB)
a.close
c.close
File.delete(path)
