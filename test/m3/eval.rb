# Kernel#eval of a String with no binding: the code sees the locals of the
# call, keeps its own new locals, and runs with the self of the call.

x = 10
p eval("x + 1")
eval("x = 20")
p x
p eval("y = 5; y * 2")
begin
  y
rescue NameError => e
  p e.class
end

def twice(n) = eval("n * 2")
p twice(21)

total = 0
[1, 2, 3].each { |i| eval("total += i * 10") }
p total

class Box
  def initialize(v) = @v = v
  def peek = eval("@v")
  def me = eval("self.class")
end
p Box.new(7).peek, Box.new(7).me

eval("def made_by_eval = :made")
p made_by_eval

inner = eval("->(k) { k + x }")
p inner.call(1)

p eval("[1, 2, 3].sum { |v| v * x }")

begin
  eval("1 +")
rescue SyntaxError => e
  p e.class
end

def yielder = eval("yield 3")
p yielder { |v| v * 3 }

z = 1
p eval("eval('z + 1')")
