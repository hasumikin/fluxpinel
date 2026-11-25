# SIGINT and SIGTERM with no trap raise Interrupt and SignalException where
# the program is: in a sleep, in a loop that only computes, and in the main
# thread of a program with a thread. rescue and ensure run.
$stdout.sync = true

begin
  Process.kill("INT", Process.pid)
  sleep 1
  puts "not interrupted"
rescue Interrupt => e
  puts "rescued #{e.class} #{e.signo} #{e.message.inspect}"
end

begin
  Process.kill("TERM", Process.pid)
  sleep 1
rescue SignalException => e
  p [e.class, e.message, e.signo]
end

begin
  begin
    Process.kill("INT", Process.pid)
    sleep 1
  ensure
    puts "ensure ran"
  end
rescue Interrupt
  puts "after ensure"
end

i = 0
begin
  Process.kill("TERM", Process.pid)
  loop { i += 1 }
rescue SignalException => e
  puts "left a loop: #{e.signm}"
end

t = Thread.new { sleep 0.2; :done }
begin
  Process.kill("INT", Process.pid)
  sleep 1
  puts "not interrupted"
rescue Interrupt
  puts "interrupted beside a thread"
end
p t.value

# a signal of another process, while the main thread waits in accept beside
# a thread that sleeps: the program ends by the signal (CRuby raises in the
# main thread and ends by the signal; Fluxpinel ends by the default of the
# system after a moment)
require "socket"
child = fork do
  $stderr.reopen(File::NULL)
  server = TCPServer.new("127.0.0.1", 0)
  Thread.new { sleep 30 }
  server.accept
end
sleep 0.5
Process.kill("TERM", child)
deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + 5
status = nil
until status || Process.clock_gettime(Process::CLOCK_MONOTONIC) > deadline
  status = Process.wait2(child, Process::WNOHANG)&.last
  sleep 0.05 unless status
end
Process.kill("KILL", child) unless status
p status && [status.signaled?, status.termsig]

# the Proc of a trap runs in a loop that only computes
hits = 0
trap("USR1") { hits += 1 }
Process.kill("USR1", Process.pid)
n = 0
n += 1 while hits == 0 && n < 50_000_000
puts "trap ran: #{hits}"
