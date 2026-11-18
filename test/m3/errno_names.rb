# Errno has the same names on each system, as in CRuby. A name that the
# system gives no number is Errno::NOERROR, the class of the number 0.

p Errno::NOERROR, Errno::NOERROR::Errno, Errno::NOERROR.superclass

# EDEADLOCK is a name of Linux, EPROCLIM and EFTYPE are names of the BSDs:
# each one is a class with a number of its own, or NOERROR
[:EDEADLOCK, :EPROCLIM, :EFTYPE, :ENOENT].each do |n|
  k = Errno.const_get(n)
  p k.equal?(Errno::NOERROR) == (k::Errno == 0)
end
p (Errno::EDEADLOCK::Errno == Errno::EDEADLK::Errno) == (Errno::EDEADLOCK == Errno::EDEADLK)
p defined?(Errno::EPROCLIM), defined?(Errno::EDEADLOCK), Errno.const_defined?(:EFTYPE)

# a name that CRuby does not have
p((Errno::ENOSUCH rescue $!.class), defined?(Errno::ENOSUCH))

# Errno.constants lists the names, of this system or not
names = Errno.constants
p names.size > 100, names.first.class, [:ENOENT, :EDEADLOCK, :EPROCLIM, :NOERROR].all? { |n| names.include?(n) }
p names.include?(:ENOSUCH), names.uniq.size == names.size

# an exception of NOERROR
begin
  raise Errno::NOERROR
rescue Errno::NOERROR => e
  p e.class, e.errno, e.is_a?(SystemCallError)
end
