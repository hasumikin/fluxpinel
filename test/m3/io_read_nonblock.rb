# IO#read_nonblock: what the pipe holds, at most n bytes, without waiting.
# With nothing to read it raises IO::EAGAINWaitReadable, or answers
# :wait_readable with exception: false; at the end of the stream it raises
# EOFError, or answers nil. What the handle buffered already comes first.
r, w = IO.pipe
w.write "abc"
p r.read_nonblock(2)
p r.read_nonblock(10)
begin
  r.read_nonblock(1)
rescue IO::WaitReadable => e
  p e.class, e.is_a?(Errno::EAGAIN), e.message
end
p r.read_nonblock(1, exception: false)
w.write "de\nfg"
p r.gets
p r.read_nonblock(100)
w.write "hij"
buf = +"old"
p r.read_nonblock(10, buf), buf
w.close
begin
  r.read_nonblock(1)
rescue EOFError => e
  puts e.message
end
p r.read_nonblock(1, exception: false)
r.close
begin
  r.read_nonblock(1)
rescue IOError => e
  puts e.message
end

# a socket: the same answers
require "socket"
server = TCPServer.new("127.0.0.1", 0)
client = TCPSocket.new("127.0.0.1", server.addr[1])
peer = server.accept
begin
  client.read_nonblock(5)
rescue IO::WaitReadable => e
  p e.class
end
p client.read_nonblock(5, exception: false)
peer.write "hello world"
sleep 0.1
p client.read_nonblock(5)
p client.read_nonblock(100)
peer.close
sleep 0.1
begin
  client.read_nonblock(1)
rescue EOFError => e
  puts e.message
end
p client.read_nonblock(1, exception: false)
client.close
server.close
