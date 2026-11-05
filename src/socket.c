/* socket.c -- the socket classes of the runtime: TCPServer, TCPSocket,
   UNIXServer, UNIXSocket, UDPSocket and Socket.

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

/* The class methods: new and open of each class, for_fd, and the class
   methods of Socket. */
static int fx_sock_class(const FxClass *k, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                         sp_RbVal *out) {
  if (k->cls_id >= 0 || blk || !fx_is_sock_class(k->name)) return 0;
  const char *cn = k->name;
  int is_new = FX_IS("new") || FX_IS("open");
  sp_File *f = NULL;
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
