# The class methods of File that change a file, the reads into a buffer,
# Socket.pair, ARGF, extend at the top level, and the encoding and flags
# of the lists of Dir.
require "tmpdir"
require "socket"
root = Dir.mktmpdir("fx_file_class")
path = File.join(root, "f.txt")
File.write(path, "hello world")

# umask, chmod, chown, utime, lutime, symlink, link, readlink, truncate, mkfifo
old = File.umask(0o022)
p File.umask == 0o022, File.umask(old) == 0o022
p File.chmod(0o600, path), format("%o", File.stat(path).mode & 0o777)
p File.chmod(0o644, path, path)
p File.chown(nil, nil, path)
t = Time.at(1_700_000_000, 500, :usec)
p File.utime(t, t, path), File.stat(path).mtime.to_i, File.stat(path).mtime.nsec
p File.utime(1_600_000_000, 1_600_000_000, path), File.stat(path).mtime.to_i
link = File.join(root, "l")
p File.symlink(path, link), File.symlink?(link), File.readlink(link) == path
p File.lutime(Time.at(1_500_000_000), Time.at(1_500_000_000), link)
p File.lstat(link).mtime.to_i, File.stat(link).mtime.to_i
hard = File.join(root, "h")
p File.link(path, hard), File.stat(hard).nlink
p File.truncate(path, 5), File.read(path)
fifo = File.join(root, "fifo")
p File.mkfifo(fifo), File.ftype(fifo)
p((File.symlink(path, link) rescue $!.class))
p((File.chmod(0o644, File.join(root, "none")) rescue $!.class))
p((File.readlink(path) rescue $!.class))
File.delete(link, hard, fifo)

# readpartial and sysread into a buffer; EOF empties the buffer
r, w = IO.pipe
w.write "abcdefgh"
w.close
buf = +"seed"
x = r.readpartial(3, buf)
p x, buf, x.equal?(buf)
p r.sysread(2, buf), buf
p r.readpartial(10, buf), buf
begin
  r.readpartial(4, buf)
rescue EOFError => e
  p e.message, buf
end
r.close
r, w = IO.pipe
w.write "xy"
w.close
p r.readpartial(1, nil)
begin
  r.readpartial(-1, +"b")
rescue ArgumentError => e
  p e.message
end
r.close
File.open(path) do |f|
  b = +""
  p f.readpartial(3, b), b
end

# Socket.pair and UNIXSocket.pair
a, b = Socket.pair(Socket::AF_UNIX, Socket::SOCK_STREAM, 0)
p a.class, b.class
b.write "x"
p a.recv(8)
c, d = Socket.pair(:UNIX, :STREAM)
d.write "yz"
p c.read(2)
e, f = UNIXSocket.pair
f.write "m"
p e.readpartial(8), e.class
g, h = UNIXSocket.pair(:DGRAM)
h.send("dg", 0)
p g.recv(8)
[a, b, c, d, e, f, g, h].each(&:close)

# ARGF over the files of ARGV, which it shifts
one = File.join(root, "one.txt")
two = File.join(root, "two.txt")
File.write(one, "a1\nb1\r\n")
File.write(two, "c2\nd2")
p ARGF.class, ARGF.to_s
ARGV.replace([one, two])
p ARGF.filename == one, ARGV.size
p ARGF.gets, ARGF.gets(chomp: true), ARGF.lineno
p ARGF.eof?, ARGF.gets, ARGF.filename == two, ARGF.eof?
p ARGF.read
p ARGF.gets
ARGV.replace([one])
p ARGF.readlines
ARGV.replace([two, one])
seen = []
ARGF.each_line { |l| seen << l.chomp }
p seen
File.delete(one, two)

# extend at the top level: main's singleton class, ahead of the includes
module First
  def tag = "extend"
end
module Second
  def tag = "include"
end
extend First
include Second
p tag
p self.singleton_class.include?(First)

# the encoding keyword of the lists of Dir, and the flags of glob
File.write(File.join(root, ".hidden"), "")
p Dir.children(root, encoding: "BINARY").map(&:encoding).uniq
p Dir.entries(root, encoding: Encoding::UTF_8).sort
names = []
Dir.each_child(root, encoding: "UTF-8") { |n| names << n }
p names.sort
p Dir.foreach(root, encoding: nil).to_a.sort
p Dir.glob("*", File::FNM_DOTMATCH, base: root).sort
p Dir.glob("*", 0, base: root).sort
p Dir.glob(File.join(root, "*"), File::FNM_DOTMATCH).map { |x| File.basename(x) }.sort
File.delete(File.join(root, ".hidden"), path)
Dir.rmdir(root)
