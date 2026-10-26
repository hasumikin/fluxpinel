# The lookup cache and the ivar slot cache see each change

class A
  def m = :a1
end
a = A.new
p a.m
class A
  def m = :a2
end
p a.m

# a module included after a call, and a method of the module
module Shout
  def n = :shout
end
class A
  def n = :plain
end
p a.n
class B < A
end
b = B.new
p b.n
class B
  include Shout
end
p b.n, a.n

# a miss, then the method
class C
  def method_missing(name, *) = :missing
end
c = C.new
p c.k
class C
  def k = :found
end
p c.k

# define_method after a call, and a method of a superclass
class D < A
end
d = D.new
p d.m
D.define_method(:m) { :d }
p d.m, a.m

# ivars of two classes with one name, and an ivar added later
class P
  def initialize = @v = 1
  def v = @v
  def w = @w
  def set_w = @w = 2
end
class Q
  def initialize
    @u = 0
    @v = 9
  end
  def v = @v
end
pp = P.new
qq = Q.new
p pp.v, qq.v, pp.w
pp.set_w
p pp.w, P.new.w
