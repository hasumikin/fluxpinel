# The socket classes: TCPServer and TCPSocket on the loopback, UNIXServer
# and UNIXSocket on a path, the addresses, the options and the constants
# of Socket. The server side runs on a thread, as the tests of spinel do.

require "socket"
require "tmpdir"

srv = TCPServer.new("127.0.0.1", 0)
port = srv.addr[1]
p srv.class, srv.addr[0], srv.addr[3], port.positive?
p srv.respond_to?(:accept), srv.respond_to?(:nope)

server = Thread.new do
  c = srv.accept
  line = c.gets
  c.puts "echo #{line.chomp}"
  peer = c.peeraddr
  c.close
  [c.class, peer[0], peer[3]]
end

s = TCPSocket.new("127.0.0.1", port)
p s.class, s.peeraddr[1] == port, s.addr[3]
s.puts "hello"
p s.gets
p s.gets
p server.value
s.close
p s.closed?
srv.close

# TCPServer.new(port) binds every address; accept_nonblock with no client
srv2 = TCPServer.new(0)
p srv2.addr[1].positive?
p srv2.accept_nonblock(exception: false)
begin
  srv2.accept_nonblock
rescue IO::WaitReadable => e
  p e.class.ancestors.include?(IO::WaitReadable)
end
srv2.close

# setsockopt and shutdown
srv3 = TCPServer.new("127.0.0.1", 0)
p srv3.setsockopt(Socket::SOL_SOCKET, Socket::SO_REUSEADDR, true)
c3 = TCPSocket.new("127.0.0.1", srv3.addr[1])
a3 = srv3.accept
p c3.shutdown
p a3.read
a3.close
c3.close
srv3.close

# UNIXServer and UNIXSocket
Dir.mktmpdir do |d|
  path = File.join(d, "s.sock")
  us = UNIXServer.new(path)
  p us.class
  t = Thread.new { c = us.accept; c.write(c.read(3).upcase); c.close }
  uc = UNIXSocket.new(path)
  p uc.class
  uc.write("abc")
  p uc.read(3)
  t.join
  uc.close
  us.close
end

# the constants of Socket are the system's
p Socket::AF_INET, Socket::SOCK_STREAM.class, Socket::SOL_SOCKET.class
p Socket::AF_INET == Socket::PF_INET
p Socket.gethostname.class

# a refused connection
begin
  TCPSocket.new("127.0.0.1", 1)
rescue SystemCallError => e
  p e.class
end
