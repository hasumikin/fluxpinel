# A method of String that takes a String takes an object with a to_str
# too, as CRuby's implicit conversion. An object with no to_str is a
# TypeError, and == does not convert.

class C
  def to_str = "conv"
end
class N; end
class Bad
  def to_str = 1
end

def t
  yield
rescue TypeError, ArgumentError, NoMethodError => e
  "#{e.class}: #{e.message}"
end

c = C.new
p t { "a" + c }, t { +"a" << c }, t { (+"a").concat(c) }, t { (+"a").concat(c, c) }, t { (+"a").prepend(c) }
p t { (+"a").replace(c) }, t { (+"abc").insert(1, c) }, t { "a" * 2 + c }, t { "a".eql?(c) }, t { "conv" == c }
p t { "xconvx".index(c) }, t { "xconvx".include?(c) }, t { "xconvx".sub(c, "-") }, t { "xconvx".delete(c) }, t { "xconvx".count(c) }
p t { "a-b".split(c) }, t { "xconvx".tr(c, "-") }, t { "x".center(7, c) }, t { "x".ljust(7, c) }, t { "xconvx".start_with?(c) }
p t { "xconvx".partition(c) }, t { "abc".casecmp(c) }, t { "abc" <=> c }, t { "abc".squeeze(c) }, t { "xconvx".gsub(c, c) }
p t { "a" + N.new }, t { +"a" << N.new }, t { (+"a").concat(N.new) }, t { "a" + nil }, t { "a" + 1 }, t { +"a" << 97 }
p t { "a" + Bad.new }

s = +"a"
s += c
p s, s.class

# the object is asked once, and before the method runs
class Counted
  attr_reader :n
  def initialize = @n = 0
  def to_str = (@n += 1; "k")
end
k = Counted.new
p "a" + k, k.n, "xkx".include?(k), k.n
