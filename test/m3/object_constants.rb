# Object.constants lists the builtin classes and modules, and the
# constants of the program.
c = Object.constants
p c.include?(:Integer), c.include?(:String), c.include?(:Comparable), c.include?(:Kernel), c.include?(:Encoding)
p c.include?(:LoadError), c.include?(:Mutex), c.include?(:Process), c.include?(:Math)
p Object.constants(false).include?(:Array), c.include?(:Status), c.include?(:DomainError)
module Shop
  Price = 1
end
p Object.constants.include?(:Shop), Object.constants.include?(:Price), Shop.constants
p c.uniq.size == c.size, c.all?(Symbol)
