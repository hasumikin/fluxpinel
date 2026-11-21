# File and the socket classes descend from IO, as in CRuby: a File is an IO
# (rubish keeps a redirection target as an IO and asks is_a?(IO) and
# `case ... when IO` of it), and IO is Enumerable.
require "socket"
p File.superclass, File.ancestors.take(2), File.open("/dev/null").is_a?(IO), IO === File.open("/dev/null")
p IO.ancestors.include?(Enumerable), File.ancestors.include?(Enumerable), File.new("/dev/null").is_a?(Enumerable)
case File.open("/dev/null")
when IO then puts "a File is an IO"
end
%w[BasicSocket IPSocket TCPSocket TCPServer UDPSocket UNIXSocket UNIXServer Socket].each do |n|
  c = Object.const_get(n)
  puts "#{n} < #{c.superclass}, IO: #{c.ancestors.include?(IO)}"
end
a, b = UNIXSocket.pair
p a.is_a?(IO), a.is_a?(BasicSocket), b.is_a?(UNIXSocket), IO === a
a.close
b.close
p $stdout.is_a?(IO), $stdout.is_a?(File), File.open("/dev/null").is_a?(File)
