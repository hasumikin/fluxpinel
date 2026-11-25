# super in a method of a module that a class and its superclass both
# extend: the module is on the path of the singleton methods one time, so
# the super of its method is not the method itself (the new of
# Concurrent::Synchronization::SafeInitialization).
module Counted
  def new(*args, &block)
    super(*args, &block)
  ensure
    $count = $count.to_i + 1
  end
end

class A
  extend Counted
  def initialize(x)
    @x = x
  end
  attr_reader :x
end
p A.new(1).x, $count

class B < A
  extend Counted
end
p B.new(2).x, $count

class C < B
  extend Counted
  def self.new(*args)
    obj = super
    $via_c = true
    obj
  end
end
p C.new(3).x, $count, $via_c

# a method of another name, with a class method under the two extends
module Tagged
  def make(x) = "tagged " + super
end
class P
  def self.make(x) = "p#{x}"
end
class Q < P
  extend Tagged
end
class R < Q
  extend Tagged
end
p Q.make(1), R.make(2)

# StringScanner#<< and concat (lib/fluxpinel/strscan.rb): the String of
# the scanner grows, it is the same object, and the position stays
require "strscan"
text = +"<a>"
scanner = StringScanner.new(text)
p scanner.scan(/</), scanner.pos
p((scanner << "xyz").equal?(scanner), scanner.string, scanner.rest, scanner.pos, text, scanner.string.equal?(text))
p scanner.scan(/a>x/), scanner.rest
scanner.concat("!")
p scanner.rest, scanner.eos?, scanner.scan(/yz!/), scanner.eos?
