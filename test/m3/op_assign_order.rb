# x OP= v reads x before v runs: a method or a block in v that writes x
# does not change the result, for a local, an instance, a class or a
# global variable, an attribute and an element.

def bump_g = ($g = 100; 3)
$g = 10
$g += bump_g
p $g

class Box
  attr_accessor :n

  def initialize
    @n = 10
    @@c = 10
  end

  def bump_i = (@n = 100; 3)
  def bump_c = (@@c = 100; 3)

  def run
    @n += bump_i
    @@c *= bump_c
    [@n, @@c]
  end
end
p Box.new.run

x = 10
set_x = -> { x = 100; 3 }
x -= set_x.call
p x

b = Box.new
b.n += (b.n = 100; 3)
p b.n

a = [10]
a[0] += (a[0] = 100; 3)
p a
