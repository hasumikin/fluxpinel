# Binding: Kernel#binding, Binding#eval, the local_variable methods,
# receiver, source_location, dup and TOPLEVEL_BINDING.

x = 1
b = binding
p Binding === b, b.class, b.inspect.sub(/0x\h+/, "0x")
p b.local_variables, b.local_variable_get(:x), b.local_variable_defined?(:x), b.local_variable_defined?(:nope)
p b.eval("x + 1"), eval("x * 10", b)

# a local that an eval makes stays in the binding, and a later eval sees it
b.eval("y = 2; z = 3")
p b.local_variables, b.local_variable_get(:y), b.eval("y + z + x")
eval("y += 10", b)
p b.local_variable_get(:y)

# a write through the binding reaches the scope
b.local_variable_set(:x, 5)
p x, b.eval("x = 7"), x

# local_variable_set of a new name: the eval sees it, the scope does not
b.local_variable_set(:w, 4)
p b.local_variables, b.local_variable_get(:w), b.eval("w + 1"), defined?(w)

# receiver and source_location
p b.receiver, b.source_location[1], b.eval("self"), TOPLEVEL_BINDING.source_location
p TOPLEVEL_BINDING.receiver, TOPLEVEL_BINDING.local_variable_defined?(:x), TOPLEVEL_BINDING.eval("x")

# the binding of a method and of a block
def m(a)
  k = a * 2
  binding
end
mb = m(2)
p mb.local_variables, mb.receiver, mb.eval("a + k"), mb.eval("k = 9; k")
class Box
  def initialize(v) = @v = v
  def scope = binding
end
bx = Box.new(3).scope
p bx.receiver.class, bx.eval("@v"), bx.eval("self.class"), bx.local_variables
blocks = [10, 20].map { |e| inner = e + 1; binding }
p blocks.map { |bb| bb.eval("[e, inner, x]") }, blocks[0].local_variables
pr = proc { |q| binding }
p pr.call(8).eval("q")

# dup: the copy shares the scope and keeps its own new locals
d = b.dup
p d.eval("x"), d.eval("fresh = 1"), d.local_variable_defined?(:fresh), b.local_variable_defined?(:fresh)
d.eval("x = 11")
p x

# eval with a file and a line
p b.eval("__FILE__", "f.rb", 7), b.eval("__LINE__", "f.rb", 7), eval("__LINE__", b, "g.rb", 3)

# errors
begin
  eval("1", 2)
rescue TypeError => e
  puts e.message
end
begin
  b.local_variable_get(:nope)
rescue NameError => e
  puts e.message.sub(/0x\h+/, "0x")
end
begin
  b.eval("1 +")
rescue SyntaxError
  puts "SyntaxError"
end
begin
  b.eval("raise ArgumentError, 'in eval'")
rescue ArgumentError => e
  puts e.message
end
p b.respond_to?(:eval), b.respond_to?(:local_variables), b.respond_to?(:nope)
p ::Kernel.binding.local_variable_get(:x)
