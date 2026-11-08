# Addrinfo, Socket.getaddrinfo, the packed sockaddr forms, and the methods
# of a socket that take or answer them.
require "socket"

a = Addrinfo.tcp("127.0.0.1", 80)
p a.class, a.ip_address, a.ip_port, a.afamily == Socket::AF_INET, a.ipv4?, a.ipv6?, a.ip?, a.unix?
p a.socktype == Socket::SOCK_STREAM, a.inspect, a.to_s
b = Addrinfo.udp("::1", 53)
p b.ipv6?, b.afamily == Socket::AF_INET6, b.socktype == Socket::SOCK_DGRAM
u = Addrinfo.unix("/tmp/fluxpinel_ai.sock")
p u.unix?, u.ip?, u.afamily == Socket::AF_UNIX, u.unix_path
p [a][0].class, {k: b}[:k].ipv6?

p Socket.getaddrinfo("127.0.0.1", 80, :INET, :STREAM).first.first(4)
p Socket.getaddrinfo("127.0.0.1", 80, nil, Socket::SOCK_DGRAM).map { |r| r[5] }.uniq == [Socket::SOCK_DGRAM]
sa = Socket.sockaddr_in(8080, "127.0.0.1")
p sa == Socket.pack_sockaddr_in(8080, "127.0.0.1"), Socket.unpack_sockaddr_in(sa)
p Socket.unpack_sockaddr_un(Socket.pack_sockaddr_un("/tmp/fluxpinel_x.sock"))
p a.to_sockaddr == Socket.sockaddr_in(80, "127.0.0.1")

srv = Socket.new(Socket::AF_INET, Socket::SOCK_STREAM, 0)
p srv.bind(Socket.sockaddr_in(0, "127.0.0.1")), srv.listen(5)
la = srv.local_address
p la.class, la.ip_address, la.ip_port == Socket.unpack_sockaddr_in(srv.getsockname)[0]
port = la.ip_port
cl = Socket.new(Socket::AF_INET, Socket::SOCK_STREAM, 0)
p cl.connect(Addrinfo.tcp("127.0.0.1", port).to_sockaddr)
peer, = srv.accept
p peer.remote_address.ip_port == cl.local_address.ip_port, cl.remote_address.ip_port == port
o = srv.getsockopt(Socket::SOL_SOCKET, Socket::SO_REUSEADDR)
p o.class, o.level == Socket::SOL_SOCKET, o.optname == Socket::SO_REUSEADDR, o.int.class, o.bool.class
p o.inspect.start_with?("#<Socket::Option:")
[peer, cl, srv].each(&:close)

ud = UDPSocket.new
p ud.bind("127.0.0.1", 0), ud.local_address.ip_address
ud.close
