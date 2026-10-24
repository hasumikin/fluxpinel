# defined?: the kind of each expression, or nil when a name in it is not
# defined, with no error and with no call of the method it asks about.

x = 1
@iv = 2
$gv = 3
p defined?(x), defined?(@iv), defined?(@none), defined?($gv), defined?($none), defined?($stdout)
p defined?(nil), defined?(true), defined?(false), defined?(self), defined?(()), defined?(1 + 1)
p defined?(x = 2), defined?(@iv += 1), defined?(x ||= 3), defined?([1, x]), defined?([1, Nope])
p defined?(String), defined?(Nope), defined?(::String), defined?(Comparable)

module Outer
  VALUE = 1
  class Inner; end
end
class Base
  K = :k
  def hello = :hi
  private def secret = :s
end
class Sub < Base
  def check = [defined?(hello), defined?(secret), defined?(self.secret), defined?(super), defined?(nope)]
  def hello = defined?(super)
end
p defined?(Outer::VALUE), defined?(Outer::Inner), defined?(Outer::Nope), defined?(Sub::K), defined?(Outer::String)
p defined?(Nope::X), defined?(Outer::Inner::VALUE)

s = Sub.new
p s.check, s.hello
p defined?(s.hello), defined?(s.secret), defined?(s.nope), defined?(s.hello.size), defined?(nope.size)
p defined?(puts), defined?(String.new), defined?(x.succ), defined?(x.nope)

def blocky = defined?(yield)
p blocky, blocky { }

"hello" =~ /e(l)/
p defined?($~), defined?($&), defined?($1), defined?($2)

class Counter
  @@n = 0
  def self.has = [defined?(@@n), defined?(@@m)]
end
p Counter.has

module Helpers
  p defined?(name), defined?(new), defined?(module_function), defined?(superclass)
end

class Blank < BasicObject
  def visible = 1
  def facts = [defined?(freeze), defined?(visible), defined?(equal?(1))]
end
p Blank.new.facts

# a receiver that raises answers nil
def boom = raise("x")
p defined?(boom.size), defined?(String.new.nope)
