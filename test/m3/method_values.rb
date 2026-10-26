# Method values: obj.method(:name)

class Foo
  def initialize(n) = @n = n
  def add(x, y = 1) = @n + x + y
  def each_n(&b) = [@n, @n * 2].each(&b)
  def self.make(n) = new(n)
  attr_reader :n

  private

  def secret = :hidden
end

class Bar < Foo
end

f = Foo.new(10)
m = f.method(:add)
p m.call(1), m.(1, 2), m[3], m.arity, m.name, m.owner, m.receiver.n
p f.method(:n).call, f.method(:n).arity
p f.method(:secret).call
p Foo.method(:make).call(5).n, Foo.method(:make).arity
p [1, 2].map(&f.method(:add))
pr = m.to_proc
p pr.call(5), pr.lambda?, pr.arity

# builtin methods
a = [1, 2]
push = a.method(:push)
push.call(3)
p a, push.receiver.equal?(a)
set = a.method(:[]=)
set[0, 9]
p a
p 3.method(:+).call(4), "ab".method(:upcase).call
p [1, 2, 3].map(&2.method(:*))

# a method of the main object
def twice(x) = x * 2
p method(:twice).call(4), [1, 2].map(&method(:twice))

# equal Methods are one Hash key
h = { f.method(:add) => 1 }
h[f.method(:add)] = 2
p h.size, f.method(:add) == f.method(:add), f.method(:add) == Foo.new(1).method(:add)

# case/when calls ===
even = 2.method(:==)
case 2
when even then p :two
end

# inspect
p Bar.new(1).method(:add).inspect.sub(/ \S+:\d+>/, ">")
p Foo.new(1).method(:secret).inspect.sub(/ \S+:\d+>/, ">")
p Foo.method(:make).inspect.sub(/ \S+:\d+>/, ">")
p m.source_location[1]

# a missing name, and public_method of a private one
begin
  f.method(:nope)
rescue NameError => e
  p e.message
end
begin
  f.public_method(:secret)
rescue NameError => e
  p e.class
end
p f.public_method(:add).call(0)
