# A class method under protected: a class method of the same class or of
# a class under it calls it with a receiver; a call from outside raises.
# An instance method under protected goes on as before.
class L
  class << self
    def tag(t) = L.register(t, self)
    protected
    def register(name, k) = [name, k]
  end
end
class M < L; end
p M.tag("m"), L.tag("l")
p((L.register("x", 1) rescue $!.class))

class A
  def cmp(o) = o.secret
  def self.peek(o) = o.secret
  protected
  def secret = 1
end
class B < A; end
p A.new.cmp(B.new), B.new.cmp(A.new)
p((A.new.secret rescue $!.class))
# the class itself is no instance of A: its class method does not reach
# the protected instance method
p((A.peek(A.new) rescue $!.class))
