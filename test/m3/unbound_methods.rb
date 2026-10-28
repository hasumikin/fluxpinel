# UnboundMethod: Module#instance_method, bind, bind_call, parameters.

class Foo
  attr_reader :r
  attr_writer :w
  define_method(:dm) { |a, b = 1, *c, k:, **o, &blk| [a, b, c, k, o] }
  def initialize(n) = @n = n
  def m(a, (b, c), d = 1, *e, f, g:, h: 2, **i, &j) = [@n, a, b, c, d, e, f, g, h, i]
  def fwd(...) = m(...)
  def nk(**nil) = :nk
  def anon(*, **, &) = :anon
  def plain(x) = @n + x
  private def priv = :priv
end

class Bar < Foo
end

um = Foo.instance_method(:plain)
p um.class, um.name, um.owner, um.arity
p um.bind(Foo.new(10)).call(5), um.bind_call(Bar.new(1), 2)
p Foo.instance_method(:m).parameters
p Foo.instance_method(:fwd).parameters, Foo.instance_method(:nk).parameters, Foo.instance_method(:anon).parameters
p Foo.instance_method(:dm).parameters, Foo.instance_method(:dm).arity
p Foo.instance_method(:r).parameters, Foo.instance_method(:w=).parameters
p Foo.new(1).method(:m).parameters == Foo.instance_method(:m).parameters
p Foo.instance_method(:priv).name
p Bar.instance_method(:plain).owner, Bar.instance_method(:plain) == um
puts um.inspect.sub(/ \S+:\d+>/, ">")

# a Method and back
m = Foo.new(3).method(:plain)
p m.unbind.class, m.unbind == um, m.unbind.bind(Foo.new(4)).call(1)

# builtin methods
p Kernel.instance_method(:class).bind(1).call, Kernel.instance_method(:class).bind_call("x")
p String.instance_method(:upcase).bind_call("abc"), String.instance_method(:upcase).owner
p String.instance_method(:upcase).inspect
p Array.instance_method(:push).parameters
p Kernel.instance_method(:warn).class, Object.instance_method(:inspect).bind_call(Foo.new(1)).start_with?("#<Foo")
p Comparable.instance_method(:clamp).bind(3).call(1, 2)

begin
  Foo.instance_method(:nope)
rescue NameError => e
  puts e.message
end
begin
  Kernel.instance_method(:nope)
rescue NameError => e
  puts e.message
end
begin
  Foo.public_instance_method(:priv)
rescue NameError => e
  puts e.message
end
begin
  String.instance_method(:upcase).bind(1)
rescue TypeError => e
  puts e.message
end
begin
  um.bind(1)
rescue TypeError => e
  puts e.message
end
begin
  um.call
rescue NoMethodError => e
  puts e.class
end
