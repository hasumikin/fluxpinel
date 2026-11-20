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

# a count of a Range and an index of an Integer
t { (1..5).first(o) }
t { (1..).first(o) }
t { ("a".."e").first(o) }
t { ("a".."e").last(o) }
t { (1..5).last(o) }
t { (1..5).take(o) }
t { (1..5).drop(o) }
t { (1..5).min(o) }
t { (1..5).max(o) }
t { (1..5).each_slice(o).to_a }
t { (1..5).each_cons(o).to_a }
t { 5[o] }
t { 5[o, 1] }
t { (1..5).first(Bad.new) }
t { (1..5).first(Object.new) }
t { 5[Bad.new] }
t { 5[Object.new] }

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
