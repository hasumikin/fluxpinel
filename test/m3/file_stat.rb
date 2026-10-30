# File.stat, File.lstat, File#stat and the readers of a File::Stat.
s = File.stat(__FILE__)
p s.file?, s.directory?, s.symlink?, s.socket?, s.pipe?, s.chardev?, s.blockdev?
p s.size == File.size(__FILE__), s.size?.class, s.zero?
p s.mode.class, s.mode & 0o170000 == 0o100000, s.mode & 0o66 != 0
p s.uid.class, s.gid.class, s.nlink >= 1, s.ino.class, s.dev.class, s.blksize.class, s.blocks.class
p s.readable?, s.writable?, s.executable?, s.readable_real?, s.writable_real?, s.executable_real?
p s.owned?, s.setuid?, s.setgid?, s.sticky?
p s.mtime == File.mtime(__FILE__), s.atime.class, s.ctime.class
p s.ftype
d = File.stat(File.dirname(__FILE__))
p d.directory?, d.file?, d.ftype
File.open(__FILE__) { |f| p f.stat.size == s.size, f.stat.file? }
p File.lstat(__FILE__).file?
begin
  File.stat("/no/such/file")
rescue SystemCallError => e
  p e.class
end
p File.respond_to?(:stat), File.respond_to?(:lstat)
