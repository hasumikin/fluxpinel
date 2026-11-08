/* socket.c -- the socket classes of the runtime: TCPServer, TCPSocket,
   UNIXServer, UNIXSocket, UDPSocket and Socket, and Addrinfo.

   A socket is an sp_File whose mode labels the kind (sp_io_fdopen_sock),
   so the IO methods of global.c read and write it as they do a file. This
   file adds what codegen writes into the typed program by hand (the
   constructors in codegen_call.c, the methods of a socket in
   codegen_call_io.c, the constants in codegen_call_class.c): the same
   runtime calls, from a boxed receiver. A wait of the runtime (accept)
   gives the walker lock up for its time, as a read does. */

#define FX_IS(s) (!strcmp(name, (s)))

/* The kind of sp_sock_for_fd for a class name: 0 for Socket. */
static int fx_sock_kind(const char *cls) {
  if (!strcmp(cls, "TCPServer")) return 1;
  if (!strcmp(cls, "TCPSocket")) return 2;
  if (!strcmp(cls, "UDPSocket")) return 3;
  if (!strcmp(cls, "UNIXServer")) return 4;
  if (!strcmp(cls, "UNIXSocket")) return 5;
  return 0;
}

static int fx_is_sock_class(const char *cls) {
  return fx_sock_kind(cls) || !strcmp(cls, "Socket") || !strcmp(cls, "BasicSocket") || !strcmp(cls, "IPSocket");
}

/* A packed sockaddr that the runtime answers: a binary String, as CRuby's */
static sp_RbVal fx_sockaddr_box(const char *sa) { return sp_box_str_new(sp_str_as_binary(sa)); }

/* A packed sockaddr argument (Socket.sockaddr_in, Addrinfo#to_sockaddr): a
   byte String, which may hold NUL */
static const char *fx_sockaddr_arg(sp_RbVal v, sp_int *len) {
  const char *sa = sp_poly_arg_str_chk(v);
  *len = (sp_int)sp_str_byte_len(sa);
  return sa;
}

/* Addrinfo.tcp(host, port), .udp(host, port), .ip(host) and .unix(path):
   an sp_Addrinfo of the runtime, as codegen_call_module.c makes them */
static int fx_addrinfo_class(const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  sp_Addrinfo *a;
  if ((FX_IS("tcp") || FX_IS("udp")) && argc == 2) {
    const char *host = sp_poly_arg_str_chk(argv[0]);
    SP_GC_ROOT_STR(host);
    a = sp_addrinfo_new(host, sp_poly_arg_int_chk(argv[1]), sp_sock_const(FX_IS("udp") ? "SOCK_DGRAM" : "SOCK_STREAM"), 0);
  } else if (FX_IS("ip") && argc == 1) {
    a = sp_addrinfo_new(sp_poly_arg_str_chk(argv[0]), 0, 0, 0);
  } else if (FX_IS("unix") && argc == 1) {
    a = sp_addrinfo_new(sp_poly_arg_str_chk(argv[0]), 0, sp_sock_const("SOCK_STREAM"), 1);
  } else {
    return 0;
  }
  *out = sp_box_addrinfo(a);
  return 1;
}

/* The class methods: new and open of each class, for_fd, and the class
   methods of Socket. */
static int fx_sock_class(const FxClass *k, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                         sp_RbVal *out) {
  if (k->cls_id >= 0 || blk) return 0;
  if (!strcmp(k->name, "Addrinfo")) return fx_addrinfo_class(name, argc, argv, out);
  if (!fx_is_sock_class(k->name)) return 0;
  const char *cn = k->name;
  int is_new = FX_IS("new") || FX_IS("open");
  sp_File *f = NULL;
  /* IPSocket.getaddress(host), on IPSocket and the classes below it: the
     first address of the name; the lookup can wait */
  if (FX_IS("getaddress") && argc == 1 && (!strcmp(cn, "IPSocket") || !strcmp(cn, "TCPSocket") || !strcmp(cn, "UDPSocket"))) {
    const char *host = sp_poly_arg_str_chk(argv[0]), *s;
    SP_GC_ROOT_STR(host);
    FX_UNLOCKED_ROOT(s = sp_sock_getaddress(host), SP_GC_ROOT_STR(s));
    *out = sp_box_str_new(s);
    return 1;
  }
  if (!strcmp(cn, "Socket")) {
    /* Socket.getaddrinfo(host, port[, family[, socktype]]): the family and
       the socket type as an Integer, a String or a Symbol, or nil for any */
    if (FX_IS("getaddrinfo") && argc >= 2 && argc <= 4) {
      const char *host = argv[0].tag == SP_TAG_NIL ? NULL : sp_poly_arg_str_chk(argv[0]);
      SP_GC_ROOT_STR(host);
      sp_int port = argv[1].tag == SP_TAG_NIL ? 0 : sp_poly_arg_int_chk(argv[1]);
      sp_int fam = argc > 2 && argv[2].tag != SP_TAG_NIL ? sp_sock_addrinfo_hint(argv[2], 1) : 0;
      sp_int st = argc > 3 && argv[3].tag != SP_TAG_NIL ? sp_sock_addrinfo_hint(argv[3], 0) : 0;
      sp_PolyArray *a;
      FX_UNLOCKED_ROOT(a = sp_sock_getaddrinfo(host, port, fam, st), SP_GC_ROOT(a));
      *out = sp_box_poly_array(a);
      return 1;
    }
    if ((FX_IS("sockaddr_in") || FX_IS("pack_sockaddr_in")) && argc == 2) {
      const char *host = sp_poly_arg_str_chk(argv[1]);
      SP_GC_ROOT_STR(host);
      sp_int port = sp_poly_arg_int_chk(argv[0]);
      const char *sa;
      /* the lookup of a host name can wait */
      FX_UNLOCKED_ROOT(sa = sp_sock_pack_sockaddr_in(port, host), SP_GC_ROOT_STR(sa));
      *out = fx_sockaddr_box(sa);
      return 1;
    }
    if ((FX_IS("sockaddr_un") || FX_IS("pack_sockaddr_un")) && argc == 1) {
      *out = fx_sockaddr_box(sp_sock_pack_sockaddr_un(sp_poly_arg_str_chk(argv[0])));
      return 1;
    }
    if (FX_IS("unpack_sockaddr_in") && argc == 1) {
      *out = sp_box_poly_array(sp_sock_unpack_sockaddr_in(sp_poly_arg_str_chk(argv[0])));
      return 1;
    }
    if (FX_IS("unpack_sockaddr_un") && argc == 1) {
      *out = sp_box_str_new(sp_sock_unpack_sockaddr_un(sp_poly_arg_str_chk(argv[0])));
      return 1;
    }
  }
  if (FX_IS("for_fd") && argc == 1) {
    f = sp_sock_for_fd(sp_poly_arg_int_chk(argv[0]), fx_sock_kind(cn));
  } else if (is_new && !strcmp(cn, "TCPServer") && argc >= 1 && argc <= 2) {
    /* TCPServer.new(port) and TCPServer.new(host, port); a nil host binds
       every address */
    const char *host = argc == 2 && argv[0].tag != SP_TAG_NIL ? sp_poly_arg_str_chk(argv[0]) : NULL;
    sp_int port = sp_poly_arg_int_chk(argv[argc - 1]);
    int fd;
    FX_UNLOCKED(fd = sp_net_listen_host(host, (int)port, 0));
    f = sp_io_fdopen_sock(fd, SPL("tcpserver"));
  } else if (is_new && !strcmp(cn, "TCPSocket") && argc == 2) {
    const char *host = sp_poly_arg_str_chk(argv[0]);
    sp_int port = sp_poly_arg_int_chk(argv[1]);
    int fd;
    FX_UNLOCKED(fd = sp_net_connect(host, (int)port));
    f = sp_io_fdopen_sock(fd, SPL("tcp"));
  } else if (is_new && !strcmp(cn, "UNIXServer") && argc == 1) {
    f = sp_sock_unix_server(sp_poly_arg_str_chk(argv[0]));
  } else if (is_new && !strcmp(cn, "UNIXSocket") && argc == 1) {
    FX_UNLOCKED_ROOT(f = sp_sock_unix_connect(sp_poly_arg_str_chk(argv[0])), SP_GC_ROOT(f));
  } else if (is_new && !strcmp(cn, "UDPSocket") && argc <= 1) {
    f = sp_sock_udp_new(argc == 1 ? sp_sock_family_arg(argv[0], 1) : 0);
  } else if (is_new && !strcmp(cn, "Socket") && argc >= 2 && argc <= 3) {
    f = sp_sock_new(sp_sock_family_arg(argv[0], 1), sp_sock_family_arg(argv[1], 0),
                    argc == 3 ? sp_poly_arg_int_chk(argv[2]) : 0);
  } else if (!strcmp(cn, "Socket") && FX_IS("tcp") && argc == 2) {
    const char *host = sp_poly_arg_str_chk(argv[0]);
    sp_int port = sp_poly_arg_int_chk(argv[1]);
    FX_UNLOCKED_ROOT(f = sp_sock_tcp_socket(host, port), SP_GC_ROOT(f));
  } else if (!strcmp(cn, "Socket") && FX_IS("unix") && argc == 1) {
    FX_UNLOCKED_ROOT(f = sp_sock_unix_socket(sp_poly_arg_str_chk(argv[0])), SP_GC_ROOT(f));
  } else if (!strcmp(cn, "Socket") && FX_IS("gethostname") && argc == 0) {
    *out = sp_box_str_new(sp_sock_gethostname());
    return 1;
  } else if (!strcmp(cn, "Socket") && FX_IS("tcp_server_sockets") && argc == 2) {
    const char *host = argv[0].tag == SP_TAG_NIL ? NULL : sp_poly_arg_str_chk(argv[0]);
    sp_int port = sp_poly_arg_int_chk(argv[1]);
    sp_PolyArray *a;
    FX_UNLOCKED_ROOT(a = sp_sock_tcp_server_sockets(host, port), SP_GC_ROOT(a));
    *out = sp_box_poly_array(a);
    return 1;
  } else {
    return 0;
  }
  *out = sp_box_obj(f, SP_BUILTIN_IO);
  return 1;
}

/* The methods of a socket handle, from fx_io_core. */
static int fx_sock_core(sp_File *f, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  int kw = argc > 0 && fx_is_kwargs(argv[argc - 1]);
  if (FX_IS("accept") && argc == 0) {
    sp_File *c;
    FX_UNLOCKED_ROOT(c = sp_sock_accept(f), SP_GC_ROOT(c));
    *out = sp_box_obj(c, SP_BUILTIN_IO);
    return 1;
  }
  /* accept_nonblock(exception: false) answers :wait_readable when no
     connection waits, as codegen emits it */
  if (FX_IS("accept_nonblock") && argc == kw) {
    int exc = kw ? fx_kw_exception(argv[0]) : 1;
    sp_File *c = sp_sock_accept_nb(f, exc);
    *out = c ? sp_box_obj(c, SP_BUILTIN_IO) : sp_box_sym(sp_sym_intern("wait_readable"));
    return 1;
  }
  if ((FX_IS("addr") || FX_IS("peeraddr")) && argc <= 1 && !kw) {
    /* the lookup of the host name can wait */
    int peer = FX_IS("peeraddr"), rl = argc == 1 ? sp_sock_rl_flag(argv[0]) : -1;
    sp_PolyArray *a;
    FX_UNLOCKED_ROOT(a = sp_sock_addr_rl(f, peer, rl), SP_GC_ROOT(a));
    *out = sp_box_poly_array(a);
    return 1;
  }
  if (FX_IS("listen") && argc == 1) {
    *out = sp_box_int(sp_sock_listen(f, sp_poly_arg_int_chk(argv[0])));
    return 1;
  }
  /* bind and connect take a host and a port, or the address packed
     (Socket.sockaddr_in, Addrinfo#to_sockaddr); a connect can wait */
  if ((FX_IS("bind") || FX_IS("connect")) && (argc == 1 || argc == 2) && !kw) {
    int is_bind = FX_IS("bind");
    sp_int n;
    if (argc == 2) {
      const char *host = sp_poly_arg_str_chk(argv[0]);
      SP_GC_ROOT_STR(host);
      sp_int port = sp_poly_arg_int_chk(argv[1]);
      if (is_bind) FX_UNLOCKED(n = sp_sock_bind(f, host, port));
      else FX_UNLOCKED(n = sp_sock_connect(f, host, port));
    } else {
      sp_int len;
      const char *sa = fx_sockaddr_arg(argv[0], &len);
      SP_GC_ROOT_STR(sa);
      if (is_bind) n = sp_sock_bind_sa(f, sa, len);
      else FX_UNLOCKED(n = sp_sock_connect_sa(f, sa, len));
    }
    *out = sp_box_int(n);
    return 1;
  }
  /* connect_nonblock(sockaddr[, exception: false]) answers :wait_writable
     while the connection is in progress, as codegen emits it */
  if (FX_IS("connect_nonblock") && argc == 1 + kw) {
    int exc = kw ? fx_kw_exception(argv[1]) : 1;
    sp_int len;
    const char *sa = fx_sockaddr_arg(argv[0], &len);
    SP_GC_ROOT_STR(sa);
    sp_oint n = sp_sock_connect_nb_sa(f, sa, len, exc);
    *out = n.nil ? sp_box_sym(sp_sym_intern("wait_writable")) : sp_box_int(n.v);
    return 1;
  }
  if (FX_IS("getsockopt") && argc == 2) {
    *out = sp_box_sockopt(sp_sock_getsockopt(f, sp_poly_arg_int_chk(argv[0]), sp_poly_arg_int_chk(argv[1])));
    return 1;
  }
  if ((FX_IS("local_address") || FX_IS("remote_address")) && argc == 0) {
    *out = sp_box_addrinfo(sp_sock_address(f, FX_IS("remote_address")));
    return 1;
  }
  if ((FX_IS("getsockname") || FX_IS("getpeername")) && argc == 0) {
    *out = fx_sockaddr_box(sp_sock_getname(f, FX_IS("getpeername")));
    return 1;
  }
  if (FX_IS("setsockopt") && argc == 3) {
    sp_int v = argv[2].tag == SP_TAG_INT ? argv[2].v.i : sp_sock_optval(argv[2]);
    *out = sp_box_int(sp_sock_setsockopt(f, sp_poly_arg_int_chk(argv[0]), sp_poly_arg_int_chk(argv[1]), v));
    return 1;
  }
  if (FX_IS("shutdown") && argc <= 1) {
    *out = sp_box_int(sp_sock_shutdown(f, argc == 1 ? sp_poly_arg_int_chk(argv[0]) : 2));
    return 1;
  }
  if (FX_IS("recv") && argc == 1) {
    const char *s;
    sp_int len = sp_poly_arg_int_chk(argv[0]);
    FX_UNLOCKED_ROOT(s = sp_sock_recv(f, len), SP_GC_ROOT_STR(s));
    *out = sp_box_str_new(s);
    return 1;
  }
  if (FX_IS("do_not_reverse_lookup") && argc == 0) {
    *out = sp_box_bool(sp_sock_dnrl(f));
    return 1;
  }
  if (FX_IS("do_not_reverse_lookup=") && argc == 1) {
    sp_sock_set_dnrl(f, sp_poly_truthy(argv[0]));
    *out = argv[0];
    return 1;
  }
  return 0;
}

/* An Addrinfo and a Socket::Option: inspect and to_s before the generic
   inspect of fx_core, and the level of an option, which the generated
   table leaves out */
static int fx_addrinfo_core(sp_RbVal r, const char *name, int argc, sp_RbVal *out) {
  if (r.tag != SP_TAG_OBJ || !r.v.p || argc != 0) return 0;
  if (r.cls_id == SP_BUILTIN_ADDRINFO && FX_IS("inspect")) {
    const char *s;
    FX_UNLOCKED_ROOT(s = sp_addrinfo_inspect((sp_Addrinfo *)r.v.p), SP_GC_ROOT_STR(s));
    *out = sp_box_str_new(s);
    return 1;
  }
  /* Addrinfo#to_sockaddr, and to_s, which is the same in CRuby: a binary
     String (the generated row answers it as text) */
  if (r.cls_id == SP_BUILTIN_ADDRINFO && (FX_IS("to_sockaddr") || FX_IS("to_s"))) {
    const char *s;
    FX_UNLOCKED_ROOT(s = sp_addrinfo_to_sockaddr((sp_Addrinfo *)r.v.p), SP_GC_ROOT_STR(s));
    *out = fx_sockaddr_box(s);
    return 1;
  }
  if (r.cls_id != SP_BUILTIN_SOCKOPT) return 0;
  sp_SockOpt *o = (sp_SockOpt *)r.v.p;
  if (FX_IS("inspect") || FX_IS("to_s")) { *out = sp_box_str_new(sp_sockopt_inspect(o)); return 1; }
  if (FX_IS("level")) { *out = sp_box_int(o->level); return 1; }
  return 0;
}

/* The constants of Socket are the system's: the runtime's table answers
   each name it knows (sp_sock_const), and a name it does not know on this
   platform stays undefined, as on CRuby. */
static void fx_sock_init(void) {
  static const char *const names[] = {
    "AF_INET", "AF_INET6", "AF_UNIX", "AF_UNSPEC", "PF_INET", "PF_INET6", "PF_UNIX", "PF_UNSPEC",
    "SOCK_STREAM", "SOCK_DGRAM", "SOCK_RAW", "SOCK_SEQPACKET",
    "SOL_SOCKET", "IPPROTO_IP", "IPPROTO_TCP", "IPPROTO_UDP", "IPPROTO_IPV6", "IPV6_V6ONLY", "IP_TTL",
    "SO_REUSEADDR", "SO_REUSEPORT", "SO_KEEPALIVE", "SO_BROADCAST", "SO_RCVBUF", "SO_SNDBUF", "SO_ERROR",
    "SO_LINGER", "SO_TYPE", "SO_RCVTIMEO", "SO_SNDTIMEO", "SO_RCVLOWAT", "SO_SNDLOWAT", "SO_OOBINLINE",
    "SO_DONTROUTE", "SO_ACCEPTCONN",
    "TCP_NODELAY", "TCP_KEEPIDLE", "TCP_KEEPALIVE", "TCP_KEEPINTVL", "TCP_KEEPCNT", "TCP_MAXSEG",
    "MSG_PEEK", "MSG_WAITALL", "MSG_DONTROUTE", "MSG_OOB", "SOMAXCONN",
    "SHUT_RD", "SHUT_WR", "SHUT_RDWR", NULL };
  FxClass *sock = fx_builtin("Socket");
  for (int i = 0; names[i]; i++) {
    sp_int v = sp_sock_const(names[i]);
    if (v >= 0) fx_const_set(sock, sp_sym_intern(names[i]), sp_box_int(v));
  }
}

#undef FX_IS
