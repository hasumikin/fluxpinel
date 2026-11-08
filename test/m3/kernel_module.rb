# Kernel and Module: include at the top level, class_eval of a String,
# the default hooks that super reaches, attr, pp, Proc#curry, >> and <<,
# Class#subclasses, dup and clone of an object, BasicObject.new.
module Settings
  DEPTH = 7
  def depth_of = DEPTH
end
include Settings
def depth = DEPTH
p depth, depth_of, Object.include?(Settings)
class String
  class_eval "def shout = upcase + '!'"
end
class Sink
  class_eval <<~RUBY
    def safe
      [1].map do |x|
        raise "bad"
      rescue
        x + 6
      end
    end
  RUBY
  class_eval "def level = 1", "sink.rb", 10
end
p "hi".shout, Sink.new.safe, Sink.new.level, Sink.class_eval("self"), Sink.module_eval("level rescue :no")
class Base
  @subs = []
  def self.subs = @subs
  def self.inherited(sub)
    super
    Base.subs << sub
    puts "inherited #{sub} into #{self}"
  end
end
class A < Base
  puts "A's body"
end
p Base.subs
r = pp(1, 2); p r; r2 = pp(3); p r2; p pp
class K; attr :a, true; attr :b, :c; def initialize; @a = 1; @b = 2; @c = 3; end; end
k = K.new; k.a = 9; p k.a, k.b, k.c, K.instance_methods(false).sort
class K2; x = attr :z; p x; end
f = ->(a, b, c) { a + b + c }
p f.curry[1][2][3], f.curry[1, 2][3], f.curry.(1).(2, 3), f.curry.arity, f.curry.lambda?, f.curry.class
g = proc { |a, b| [a, b] }
p g.curry[1].class, g.curry(2)[1][2], g.curry.arity, g.curry[1, 2]
h = ->(*a) { a }; p h.curry(3)[1][2][3]
begin; f.curry(2); rescue ArgumentError => e; p e.message; end
dbl = ->(x) { x * 2 }; inc = ->(x) { x + 1 }
p (dbl >> inc).call(3), (dbl << inc).call(3), (dbl >> inc).lambda?, (1.method(:+) >> dbl).call(3), (dbl >> 1.method(:+)).call(3), 1.method(:+).curry[2]
begin; dbl >> 1; rescue TypeError => e; p e.message; end
class B0; end; class Kid1 < B0; end; class Kid2 < B0; end; class GK < Kid1; end
p B0.subclasses.map(&:name).sort, Kid1.subclasses, Kid2.subclasses, B0.subclasses.length
class P; attr_accessor :v; def initialize_copy(o); @copied = true; super; end; end
o = P.new; o.v = [1]; d = o.dup; p d.v.equal?(o.v), d.instance_variable_get(:@copied), d.class, o.clone.frozen?, o.freeze.clone.frozen?, o.dup.frozen?
class Q; def initialize; @x = 1; end; attr_reader :x; end
p Q.new.dup.x, Q.new.clone.x, Q.new.dup.class
p BasicObject.new.instance_eval { 42 }
class W; def self.inherited(k); super; p [:inh, k.name]; end; end; class W2 < W; end
p({b: 2, a: 1}.minmax, {}.minmax, [1, 2].each.to_h { |x| [x, x] }, [[1, 2]].each.to_h, %w[a b].each_with_index.to_h)
