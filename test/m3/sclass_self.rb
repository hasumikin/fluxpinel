# self in `class << self` is the singleton class: the value of the body, a
# receiver of send, and the class that attr_accessor and define_method
# write to.
module M
  mc = (class << self; self; end)
  p mc, mc == singleton_class, mc.equal?(M)
  mc.send :attr_accessor, :x
  self.x = 1
  p x
end

class C
  class << self
    attr_accessor :w
    def hi = 1
    private
    def sec = 2
    public
    alias_method :hey, :hi
    define_method(:d) { 3 }
    p self
  end
end
C.w = 5
p C.w, C.hi, C.hey, C.d, (C.sec rescue :private)

module G
  def g = 7
end
class C
  class << self
    include G
  end
end
p C.g

k = (class << C; self; end)
p k == C.singleton_class
k.send(:define_method, :z) { 4 }
p C.z

# Etc.nprocessors: a count of processors
require "etc"
p Etc.nprocessors.is_a?(Integer), Etc.nprocessors >= 1
