# __method__ in a method, in its blocks and at the top level; and super
# from a module that Kernel prepends, which reaches the Kernel function.
class A
  def who = __method__
  def in_block = [1].map { __method__ }.first
  def callee = __callee__
  define_method(:defined) { __method__ }
end
a = A.new
p a.who, a.in_block, a.callee, a.defined, __method__

module Loud
  def warn(*msgs, uplevel: nil, category: nil)
    super(*msgs.map(&:upcase))
  end
end
Kernel.prepend(Loud)
$stderr = $stdout
warn "shout"
$stderr = STDERR
