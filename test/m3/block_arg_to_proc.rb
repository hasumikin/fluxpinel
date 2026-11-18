# A block argument that is no Proc and no Symbol: CRuby asks the value for
# its to_proc. A value with no to_proc is a TypeError, and so is a to_proc
# that answers no Proc.

def m(*a) = block_given? ? yield(*a) : :no_block

def refused
  yield
rescue TypeError => e
  e.message
end

p refused { m(1, &1) }, refused { m(1, &"s") }, refused { m(1, &[1]) }
p refused { m(1, &1.5) }, refused { m(1, &Object.new) }

class Doubler
  def to_proc = proc { |x| x * 2 }
end

class Lam
  def to_proc = ->(x) { x + 1 }
end

class Bad
  def to_proc = 5
end

p m(4, &Doubler.new), [1, 2].map(&Doubler.new), [1, 2].map(&Lam.new)
p refused { m(1, &Bad.new) }

# a singleton to_proc, and the forms that were there before
o = Object.new
def o.to_proc = proc { |x| [:single, x] }
p m(7, &o), m(3, &nil), m(3, &:to_s), m(3, &method(:p).unbind.bind(self)).class

# a to_proc that the program gives a builtin class
class Integer
  def to_proc = proc { |x| x + self }
end
p m(1, &10), [1, 2].map(&5)
