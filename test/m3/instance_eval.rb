# instance_eval, instance_exec, class_eval and their kin with a block:
# self, the def target, the constants of the block, and a DSL builder.

class Recipe
  attr_reader :steps

  def initialize(name) = (@name = name; @steps = [])
  def step(text) = @steps << text
  def self.define(name, &blk) = new(name).tap { |r| r.instance_eval(&blk) }
end

r = Recipe.define("tea") do
  step "boil"
  step "steep #{@name}"
  3.times { |i| step "stir #{i}" }
end
p r.steps

# the block takes the receiver; instance_exec passes its arguments
p r.instance_eval { |o| o.equal?(self) }
p r.instance_exec(2, 3) { |a, b| step(a * b); steps.size }
p 5.instance_eval { self + 1 }, "ab".instance_exec("!") { |s| self + s }

o = Object.new
o.instance_eval { @secret = 42 }
p o.instance_variable_get(:@secret)

# a def in instance_eval of a class is a class method; define_method and
# attr_* act on the class as the receiver
class Box; end
Box.instance_eval do
  def build = new
  define_method(:size) { 3 }
  attr_accessor :label
end
p Box.build.class, Box.new.size, Box.respond_to?(:size)

# class_eval and class_exec define instance methods
Box.class_eval do
  def open = :open
  alias_method :unlock, :open
  private def hidden = :hidden
end
p Box.new.open, Box.new.unlock, Box.private_method_defined?(:hidden)
p Box.class_exec(7) { |n| define_method(:seven) { n } }, Box.new.seven
p Box.module_eval { self }

# the constants of the block are those where it is written
X = :top
module Outer
  X = :outer
  p Box.instance_eval { X }, Box.class_eval { X }
  C = Class.new { def x = X }
  p C.new.x
end

# a block in class << self defines class methods too
class Box
  class << self
    %i[small large].each { |n| define_method(n) { n } }
  end
end
p Box.small, Box.large

# break leaves the call; without a block, the errors of CRuby
p([1, 2].map { |v| Box.instance_eval { break v * 10 } })
begin
  o.instance_eval
rescue ArgumentError => e
  p e.message
end
begin
  o.instance_exec
rescue LocalJumpError => e
  p e.message
end
