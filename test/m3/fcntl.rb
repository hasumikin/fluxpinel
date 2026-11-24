# require "fcntl": the Fcntl module, the commands and flags of fcntl(2) with
# the values of the platform (rubish clears O_NONBLOCK on the ends of a pipe
# with io.fcntl(Fcntl::F_SETFL, io.fcntl(Fcntl::F_GETFL) & ~Fcntl::O_NONBLOCK)).
p defined?(Fcntl)
p require("fcntl"), require("fcntl")
p Fcntl.class, Fcntl.frozen?
p [Fcntl::F_DUPFD, Fcntl::F_GETFD, Fcntl::F_SETFD, Fcntl::F_GETFL, Fcntl::F_SETFL, Fcntl::FD_CLOEXEC, Fcntl::O_ACCMODE]
p Fcntl::O_NONBLOCK == File::NONBLOCK, Fcntl::O_CREAT == File::CREAT, Fcntl::O_RDWR == File::RDWR, Fcntl::O_NDELAY == Fcntl::O_NONBLOCK
p [Fcntl::O_RDONLY, Fcntl::O_WRONLY, Fcntl::O_EXCL == File::EXCL, Fcntl::O_TRUNC == File::TRUNC, Fcntl::O_APPEND == File::APPEND]
p Fcntl::F_GETLK.class, Fcntl::F_SETLK.class, Fcntl::F_SETLKW.class, Fcntl::F_RDLCK.class, Fcntl::F_WRLCK.class, Fcntl::F_UNLCK.class, Fcntl::O_NOCTTY.class
r, w = IO.pipe
p r.fcntl(Fcntl::F_GETFD) & Fcntl::FD_CLOEXEC
p r.fcntl(Fcntl::F_GETFL) & Fcntl::O_NONBLOCK
r.fcntl(Fcntl::F_SETFL, r.fcntl(Fcntl::F_GETFL) | Fcntl::O_NONBLOCK)
p r.fcntl(Fcntl::F_GETFL) & Fcntl::O_NONBLOCK == Fcntl::O_NONBLOCK
r.fcntl(Fcntl::F_SETFL, r.fcntl(Fcntl::F_GETFL) & ~Fcntl::O_NONBLOCK)
p r.fcntl(Fcntl::F_GETFL) & Fcntl::O_NONBLOCK
begin
  Fcntl::NOPE
rescue NameError => e
  puts e.message
end
