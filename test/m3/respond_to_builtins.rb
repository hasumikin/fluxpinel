# respond_to? on builtin values, classes and the mixins of a class of the
# program answers as CRuby does

p "s".respond_to?(:upcase), "s".respond_to?(:push), "s".respond_to?(:between?)
p 1.respond_to?(:succ), 1.respond_to?(:upcase), 1.5.respond_to?(:floor)
p nil.respond_to?(:to_a), nil.respond_to?(:upcase), true.respond_to?(:&)
p [1].respond_to?(:tally), [1].respond_to?(:to_set), { a: 1 }.respond_to?(:transform_keys!)
p (1..2).respond_to?(:step), :sym.respond_to?(:to_proc)
p(/a/.respond_to?(:source), /a/.respond_to?(:=~), /a/.match("a").respond_to?(:captures))
p RuntimeError.new("x").respond_to?(:message), LoadError.new("x").respond_to?(:path)

# private and missing methods
p 1.respond_to?(:puts), 1.respond_to?(:puts, true), "s".respond_to?(:nope)

# classes and modules
p String.respond_to?(:new), Integer.respond_to?(:sqrt), Math.respond_to?(:sqrt)
p Comparable.respond_to?(:new), Array.respond_to?(:[]), Float.respond_to?(:nope)

# objects of the program and their mixins
class Plain; end
p Plain.new.respond_to?(:display), Plain.new.respond_to?(:then), Plain.new.respond_to?(:map)
class Bag
  include Enumerable
  def each = yield(1)
end
p Bag.new.respond_to?(:map), Bag.new.respond_to?(:each_slice), Bag.new.respond_to?(:between?)
class Ver
  include Comparable
  def <=>(o) = 0
end
p Ver.new.respond_to?(:between?), Ver.new.respond_to?(:clamp)
module Deep0; include Enumerable; end
module Deep1; include Deep0; end
class DeepBag
  include Deep1
  def each = yield(1)
end
p DeepBag.new.respond_to?(:sort_by)
class MyError < StandardError; end
p MyError.new.respond_to?(:full_message), MyError.respond_to?(:exception)
p Plain.respond_to?(:allocate), Deep0.respond_to?(:instance_methods)
