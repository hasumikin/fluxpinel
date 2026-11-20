# define_method(name, method): the body of a Method or an UnboundMethod
# runs on the receiver of the new method; the method must come from an
# ancestor of the class.
def helper(x) = "#{self.class}:#{x * 2}"
class K
  def own(y) = "own #{y} of #{self.class}"
  def block_body = [1, 2].map { |i| i + size }
  def size = 10
end
class L < K
  def size = 100
end
class M; end

p K.define_method(:h, method(:helper))
p K.new.h(2), K.new.method(:h).arity
L.define_method(:o2, K.instance_method(:own))
p L.new.o2(3)
L.define_method(:bb, K.instance_method(:block_body))
p L.new.bb
M.define_method(:h3, method(:helper))
p M.new.h3(4), M.new.respond_to?(:h3), M.instance_methods(false).sort
begin
  M.define_method(:bad, K.instance_method(:own))
rescue TypeError => e
  p e.message
end
k = K.new
def k.sing(z) = "sing #{z}"
begin
  M.define_method(:s, k.method(:sing))
rescue TypeError => e
  p e.message
end
p M.new.respond_to?(:s)

# a singleton class takes one too
o = Object.new
o.singleton_class.define_method(:h4, method(:helper))
p o.h4(5)
