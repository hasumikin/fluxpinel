# AOT code of the kernel (test/kernel/app.rb) that drives several console
# sessions from several threads at once: each session keeps its own locals
# while the others run, wait in the walker (sleep) and make threads of
# their own. Only make test-host runs this file.

puts FxKernel.sessions(4, ["k = K", "s = 0; 2000.times { |i| s += i }; s + k", "sleep 0.01; k * 10"])
# a thread of a session waits for the lock, which the session gives up at
# a sleep and at the end of each piece
puts FxKernel.sessions(3, ["t = Thread.new { sleep 0.005; K + 1 }; t.value", "Thread.new { K * 2 }.join; K"])
# a session that raises does not hold the lock
puts FxKernel.sessions(2, ["raise IndexError, 'in K'", "K + 100"])
puts :done
