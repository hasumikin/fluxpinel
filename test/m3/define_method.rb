# define_method and define_singleton_method: a method whose body is a
# block or a Proc.

def t(label)
  r = yield
  puts "#{label}: #{r.inspect}"
rescue => e
  puts "#{label}: #{e.class} #{e.message}"
end

class Base
  def greet(name) = "base #{name}"
end

class Shape < Base
  UNIT = "cm"
  scale = 3

  r = define_method(:area) { |w, h| w * h * scale }
  puts "define_method: #{r.inspect}"
  define_method("unit") { UNIT }
  define_method(:opt) { |a, b = 2, *rest, k: 1| [a, b, rest, k] }
  define_method(:early) { |x| return :neg if x < 0; :pos }
  define_method(:greet) { |name| "shape, " + super(name) }
  define_method(:bare_super) { |name| super }
  define_method(:with_block) { |x, &b| b ? b.call(x) : :none }
  define_method(:me) { self }
  define_method(:loop_break) { [1, 2, 3].each { |i| break i * 10 if i == 2 } }

  %w[red green].each_with_index do |c, i|
    define_method("#{c}?") { i.even? }
  end

  double = ->(x) { x * 2 }
  define_method(:double, double)
  define_method(:plain, proc { |a, b| [a, b] })

  class << self
    define_method(:kind) { "shape class" }
  end
  define_singleton_method(:make) { |*a| new }
end

s = Shape.new
t("area") { s.area(2, 4) }
t("unit") { s.unit }
t("opt") { [s.opt(1), s.opt(1, 3, 4, 5, k: 9)] }
t("early") { [s.early(-1), s.early(1)] }
t("super") { s.greet("x") }
t("bare super") { s.bare_super("x") }
t("block") { [s.with_block(5) { |v| v + 1 }, s.with_block(5)] }
t("self") { s.me.equal?(s) }
t("break") { s.loop_break }
t("computed names") { [s.red?, s.green?] }
t("lambda body") { s.double(21) }
t("proc body arity") { s.plain(1, 2) }
t("strict arity") { s.area(1) }
t("strict proc arity") { s.plain(1) }
t("class << self") { Shape.kind }
t("define_singleton_method") { Shape.make.class }
t("respond_to?") { [s.respond_to?(:area), s.respond_to?(:blue?), Shape.respond_to?(:kind)] }
t("method_defined?") { [Shape.method_defined?(:area), Shape.method_defined?(:nope)] }
t("send") { Shape.send(:define_method, :sent) { :via_send } }
t("sent") { s.sent }
t("redefine") do
  Shape.define_method(:unit) { "mm" }
  s.unit
end
t("no block") { Shape.define_method(:x) }
t("not a proc") { Shape.define_method(:x, 1) }

module Greeter
  define_method(:hello) { "hello from #{self.class}" }
end

class Person
  include Greeter
end
t("module") { Person.new.hello }

counter = 0
Shape.define_method(:bump) { counter += 1 }
s.bump
s.bump
t("closure") { counter }
GC.start
t("after GC") { [s.area(1, 1), s.bump] }

top = 4
define_method(:top_twice) { top * 2 }
t("main") { [top_twice, Shape.new.respond_to?(:top_twice)] }
