# The helpers of the tests of a host build of a Roundhouse application
# (test.rb): free ports, the wait for the server, the pages, the console and
# the IRB of --irb-port. A test sets $failed through check, and exits with 1
# when it is set.
require "socket"
require "net/http"
require "tmpdir"

$failed = false

def free_port
  server = TCPServer.new("127.0.0.1", 0)
  server.addr[1]
ensure
  server&.close
end

def check(what, ok)
  puts "#{ok ? "ok" : "NG"} #{what}"
  $failed = true unless ok
end

# Waits until port accepts. Stops when the server exits first.
def wait_port(port, pid, log)
  deadline = Time.now + 60
  loop do
    return TCPSocket.new("127.0.0.1", port).close
  rescue SystemCallError
    abort "test: the server exited\n" + File.read(log).lines.last(20).join if Process.waitpid(pid, Process::WNOHANG)
    abort "test: no server on port #{port} after 60 seconds" if Time.now > deadline
    sleep 0.2
  end
end

def get(port, path)
  Net::HTTP.start("127.0.0.1", port, read_timeout: 10) { |h| h.get(path) }
end

# The text the console writes before its next prompt.
def reply(sock)
  buf = +""
  until buf.end_with?("fx> ")
    abort "test: no prompt from the console, got #{buf.inspect}" unless sock.wait_readable(10)
    buf << sock.readpartial(4096)
  end
  buf.delete_suffix("fx> ")
end

def eval_line(sock, line)
  sock.write(line + "\n")
  reply(sock)
end

# Reads the screen of an IRB of --irb-port until it has text, as ASCII with
# no escape sequence, and answers the cursor position query of Reline as a
# terminal does. Answers the screen, or false after 20 seconds and at the
# end of the connection.
def irb_screen(sock, text)
  seen = "".b
  deadline = Time.now + 20
  loop do
    plain = seen.bytes.select { |b| b < 128 }.pack("C*").gsub(/\e\[[0-9;?]*[A-Za-z]/, "")
    return plain if plain.include?(text)
    return false if Time.now > deadline
    next unless sock.wait_readable(0.1)
    chunk = sock.readpartial(4096)
    seen << chunk
    sock.write("\e[1;1R") if chunk.include?("\e[6n")
  end
rescue EOFError, SystemCallError
  false
end

# Types a line at the prompt of the IRB, and waits for its answer.
def irb_line(sock, line, answer)
  return false unless irb_screen(sock, "irb(main):001")
  sock.write(line + "\r")
  irb_screen(sock, answer)
end

# Whether the server closes the connection within 10 seconds.
def irb_closed?(sock)
  deadline = Time.now + 10
  loop do
    return false if Time.now > deadline
    next unless sock.wait_readable(0.1)
    sock.readpartial(4096)
  end
rescue EOFError, SystemCallError
  sock.close
  true
end

# Ends the server: TERM, then KILL after 3 seconds.
def stop(pid)
  Process.kill(:TERM, pid)
  unless 30.times.any? { sleep 0.1; Process.waitpid(pid, Process::WNOHANG) }
    Process.kill(:KILL, pid)
    Process.wait(pid)
  end
rescue Errno::ESRCH, Errno::ECHILD
  # the server has exited, and the test has reaped it
end
