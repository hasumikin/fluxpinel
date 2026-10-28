# Forwardable and SingleForwardable (lib/forwardable.rb).

p require("forwardable")

class A
  extend Forwardable
  def_delegators :@a, :size, :first, :__send__
  p def_delegator(:@a, :last, :tail)
  p def_delegators(:@a, :pop)
  def_delegator "@a.first", :to_s, :fts
  def_delegator :items, :min
  def_delegator :@a, :map
  def items = @a
  delegate [:max, :sum] => :@a, :length => :items
  def initialize = @a = [1, 2]
end

a = A.new
p a.size, a.tail, a.fts, a.min, a.max, a.sum, a.length
p a.respond_to?(:first), A.public_method_defined?(:tail), A.instance_method(:tail).arity
p a.pop, a.size
p a.map { |x| x * 10 }

module R
  extend Forwardable
  extend SingleForwardable
  def self.core = [3, 4]
  def_single_delegators :core, :size, :last
  def_delegator :core, :first, :head
  single_delegate [:sum] => :core
  def_instance_delegators self, :head
end
p R.size, R.last, R.head, R.sum
class UsesR
  include R
end
p UsesR.new.head

p Forwardable.instance_methods(false).sort, SingleForwardable.instance_methods(false).sort
