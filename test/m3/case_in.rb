# case / in, `v in pat` and `v => pat`: value, Array, find and Hash
# patterns, bindings, alternatives, pins, guards, deconstruct and
# deconstruct_keys of a class, and the messages of a failed match.

def kind(v)
  case v
  in Integer | Float => n if n > 100 then "big #{n}"
  in Integer then "int"
  in String => s unless s.empty? then "text #{s}"
  in "" then "empty"
  in [] then "empty list"
  in [x] then "one #{x}"
  in [Integer => a, *rest] then "list from #{a}, #{rest.size} more"
  in {name: String => name, age: Integer => age} then "#{name} (#{age})"
  in {type: :point, **rest} then "point #{rest}"
  in nil then "nil"
  else "other"
  end
end

p [5, 500, 1.5, "abc", "", [], [7], [1, 2, 3], ["a", "b"], { name: "Ann", age: 30 },
   { type: :point, x: 1, y: 2 }, nil, :sym].map { |v| kind(v) }

# a find pattern binds the elements before and after the run
case [1, 2, 3, 42, "x", 5]
in [*pre, Integer => n, String => s, *post]
  p [pre, n, s, post]
end

# a Range, a Regexp, a pin of a local and of an expression
lim = 10
p(case 7; in 1..5 then :low; in 6.. then :high; end)
p(case "hello"; in /ell/ then :re; end)
p(case 10; in ^lim then :pinned; end)
p(case 4; in ^(lim - 6) then :expr; end)

# a Hash pattern: the shorthand binds the key, **nil wants no more keys
cfg = { host: "h", port: 80 }
case cfg
in { host:, port: Integer => port }
  p [host, port]
end
p((cfg in { host: String, **nil }))
p((cfg in { host: String }))
p(({} in {}))
p(({ a: 1 } in {}))

# a class with deconstruct and deconstruct_keys
class Point
  attr_reader :x, :y
  def initialize(x, y) = (@x = x; @y = y)
  def deconstruct = [x, y]
  def deconstruct_keys(keys) = { x: x, y: y }
end

case Point.new(1, 2)
in Point(x, y) then p [:pos, x, y]
end
case Point.new(3, 4)
in Point(x:, y:) then p [:key, x, y]
end
p((Point.new(0, 0) in Point(0, 0)))
p((Point.new(0, 1) in [Integer, Integer]))

# Struct
S = Struct.new(:a, :b)
case S.new(1, [2, 3])
in [a, [b, *c]] then p [a, b, c]
end
case S.new(1, 2)
in { a: 1, b: } then p b
end
p((S.new(1, 2) in {}), (S.new(1, 2) in { **nil }))

# `v => pat` binds, `v in pat` answers true or false
{ user: { name: "Bob", roles: [:admin, :dev] } } => { user: { name:, roles: [*, :admin, *] } }
p name
p((5 in String), (5 in Integer))

# the messages of a failed match
def t
  yield
  puts "matched"
rescue NoMatchingPatternError => e
  puts "#{e.class}: #{e.message}"
end

t { [1, 2] => [Integer, String] }
t { [1] => [Integer, *, String] }
t { 5 => String | Float }
t { "x" => [a] }
t { 5 => { a: } }
t { [1] => [*, String, *] }
t { { a: 1 } => { b: } }
t { { a: 1, b: 2 } => { a: Integer, **nil } }
t { { a: 1 } => {} }
t { case 5; in Integer if false then 1; end }
t { case 5; in String then 1; in Float then 2; end }
t { case { a: 1 }; in { b: } then 1; in { c: } then 2; end }

# a long inspect survives the allocation of the message
big = (1..2000).to_a
begin
  big => [Integer]
rescue NoMatchingPatternError => e
  puts e.message[-50..]
end
