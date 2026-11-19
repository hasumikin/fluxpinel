# A backtrace names the builtin methods that run: the method of a block
# call (`Array#each`), the method that raises (`Hash#fetch`), and the
# function of Kernel (`Kernel#loop`), each by the class or module that
# owns it. A method of Enumerable has the each of its receiver above it.

def show(e) = e.backtrace.map { |l| l.sub(/^.*?:(\d+):in /, '\1 ') }

def t
  yield
  nil
rescue StandardError, UncaughtThrowError => e
  show(e)
end

p t { Integer("x") }
p t { Float("x") }
p t { [1].each { raise "a" } }
p t { [1].map { |x| [x].each { raise "b" } } }
p t { loop { raise "c" } }
p t { 3.times { raise "d" } }
p t { { a: 1 }.fetch(:b) }
p t { [3, 1].sort_by { raise "e" } }
p t { "x".then { raise "f" } }
p t { catch(:x) { raise "g" } }
p t { [1].first("x") }
p t { [1, 2].inject { |a, b| raise "i" } }
p t { 1.upto(2) { raise "k" } }
p t { Integer.sqrt(-1) }
p t { [1, 2].each_with_index { |a, i| raise "l" } }
p t { { a: 1 }.each { |k, v| raise "m" } }
p t { { a: 1 }.map { raise "n" } }
p t { (1..3).each { raise "o" } }
p t { "ab".each_char { raise "p" } }
p t { [1].each { [2].each { [3].each { raise "q" } } } }
p t { 1.clamp(3, 2) }
p t { [1].fetch(9) }
p t { "x".center }
p t { throw :nope }
p t { [1].tap { raise "r" } }
p t { [3, 1].sort { raise "s" } }
p t { [1].each_slice(1) { raise "u" } }

# a method of the program between two builtin methods
def m = yield
p t { m { [1].each { raise "v" } } }

class K
  def initialize = raise("w")
end
p t { K.new }
p t { nil.foo }
p t { send(:raise, "x") }

# caller sees the builtin method too
p t { [1].each { puts caller(0, 2).map { |l| l.sub(/^.*?:(\d+):in /, '\1 ') }.join(" | ") } }

# the trace of a raise that passes a loop keeps the frames of the block
def deep = loop { [1].each { raise "y" } }
p t { deep }
