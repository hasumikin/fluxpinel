# An argument that is an object with a to_int converts where a builtin
# method takes an Integer. A to_int that answers no Integer raises the
# TypeError that names the method and the class of its answer.

class One
  def to_int = 1
end

class Bad
  def to_int = "x"
end

class NilInt
  def to_int = nil
end

class Boom
  def to_int = raise("boom")
end

def t
  p yield
rescue => e
  p [e.class, e.message]
end

o = One.new
t { "abc"[o] }
t { "abc".slice(o) }
t { "abc"[o, 1] }
t { "abc"[1, o] }
t { "abc".slice(o, o) }
t { [1, 2, 3][o] }
t { [1, 2, 3].first(o) }

t { "abc"[Bad.new] }
t { "abc"[Bad.new, 1] }
t { [1][Bad.new] }
t { [1][Bad.new] = 1 }
t { [1].first(Bad.new) }
t { [1, 2].rotate(Bad.new) }
t { Array.new(Bad.new) }
t { [1].first(NilInt.new) }
t { [1][Boom.new] }
t { "abc"[Object.new] }
