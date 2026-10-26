# The Errno module, its classes, their numbers and the message of a
# SystemCallError

p Errno::ENOENT, Errno::ENOENT.ancestors.first(3), Errno.class
begin
  File.read("/fluxpinel_no_such_file")
rescue Errno::ENOENT => e
  p :caught, e.class == Errno::ENOENT, e.errno == Errno::ENOENT::Errno
end
p Errno::EACCES::Errno
begin
  Errno::NOPE
rescue NameError => e
  p e.message
end
p [Errno::EEXIST, Errno::EISDIR].map(&:name)

begin
  raise Errno::ENOENT, "x"
rescue => e
  p e.message
end
begin
  raise SystemCallError, "y"
rescue => e
  p e.message, e.class
end
p Errno::ENOENT.new.message, Errno::ENOENT.new("z").errno
p RuntimeError.new("plain").message
