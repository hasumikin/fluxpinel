# The calls that fx_fast_call answers, after a program changes the builtin
# classes: a change for one class leaves the calls of the other classes

def all(a, h)
  [1 + 2, 7 - 2, 3 * 4, 9 / 2, 9 % 4, 1 < 2, 2 == 2, 2 != 3, 5[0], 1 << 3,
   1.5 + 1.0, 1.5 < 2.5, :a == :a, :a === :b, a[0], a.size, a.length, h[:k]]
end

a = [3, 4]
h = { k: 1 }
p all(a, h)

# a value with a singleton class answers its own methods
b = [1, 2]
def b.size = 99
class << b
  def [](i) = :single
end
g = { k: 2 }
def g.[](k) = :single
p b.size, b[0], b.length, g[:k], a.size, a[0], h[:k]

# a method of another name, and a method in another class
class String
  def zz = 1
end
class Integer
  def twice = self * 2
end
p all(a, h), 4.twice

# a module in a builtin class that gets the method later
module Late; end
class Symbol
  prepend Late
end
p :a == :a
module Late
  def ==(o) = :late
end
p :a == :a, 1 == 1, a[0]
Late.send(:remove_method, :==)
p :a == :a

# an included module does not hide the method of the class itself
module Under
  def +(o) = :under
  def size = :under
end
class Integer
  include Under
end
class Array
  include Under
end
p 1 + 2, a.size

class Float
  def <(o) = :float_lt
end
p 1.0 < 2.0, 1 < 2, 1.0 + 2.0

class Array
  def <<(o) = :push
end
p a << 5, 1 << 2, a[0], a.size

class Hash
  alias_method :fetch_it, :[]
  def [](k) = [:mine, fetch_it(k)]
end
p h[:k], a[0]

class Integer
  define_method(:*) { |o| :times }
  private def -(o) = 1
end
p 2 * 3, 4 / 2
begin
  p 3 - 1
rescue NoMethodError => e
  p e.class
end

class Integer
  undef_method :/
end
begin
  p 4 / 2
rescue NoMethodError => e
  p e.class
end
p 4.5 / 2.0
