# remote_irb.rb -- a client of --irb-port in the same process: the main
# program connects to the listener, types as a telnet client would, and
# prints the answers of IRB. With a Reline that cannot run on a socket the
# listener is not there, and the program prints "skip".
require "socket"

port = Fluxpinel::RemoteIRB.port
if port.nil?
  puts "skip"
  exit
end

s = TCPSocket.new("127.0.0.1", port)
seen = "".b   # the telnet negotiation has bytes that are not UTF-8
deadline = Time.now + 10
# the screen as ASCII: without the bytes of the telnet negotiation and the
# escape sequences (colors, cursor moves)
plain = -> { seen.bytes.select { |b| b < 128 }.pack("C*").gsub(/\e\[[0-9;?]*[A-Za-z]/, "") }

# Reads until the screen holds text, and answers the cursor position query
# of Reline as a terminal does.
wait_for = lambda do |text|
  until plain.call.include?(text)
    raise "timeout: #{text.inspect} not in #{seen.inspect}" if Time.now > deadline
    next unless s.wait_readable(0.1)
    chunk = s.readpartial(4096)
    seen << chunk
    s.write("\e[1;1R") if chunk.include?("\e[6n")
  end
end

wait_for.call("irb(main):001")
s.write("1 + 1\r")
wait_for.call("=> 2\r\n")
s.write("'ab' + 'c'\r")
wait_for.call("=> \"abc\"\r\n")
s.write("exit\r")
begin
  s.read
rescue SystemCallError
  # the server closed first
end
puts plain.call.scan(/=> .*?\r\n/).map(&:chomp)
puts "bye"
