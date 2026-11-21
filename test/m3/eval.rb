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

# An eval with no file names its source "(eval at FILE:LINE)", the file
# and the line of the eval call, as CRuby does: in __FILE__, in a
# backtrace and in the message of a SyntaxError. Binding#eval names the
# place of the binding, and instance_eval of a String its call. Such an
# eval has no directory: __dir__ is nil and require_relative has no base
# path; an eval with a file has that file's.
def where(src) = eval(src)
p eval("__FILE__"), eval("__FILE__", binding), where("__FILE__")
p eval("__FILE__", nil, "f.rb"), eval("__FILE__", nil, "f.rb", 7), eval("__LINE__"), eval("\n\n__LINE__")
p 1.instance_eval("__FILE__"), Object.new.instance_eval("__FILE__")
b = binding
def other(b) = b.eval("__FILE__")
p other(b), b.eval("__FILE__")
begin
  eval("1 +")
rescue SyntaxError => e
  puts e.message[/\A[^:]*:\d+/]
end
begin
  where("1 +")
rescue SyntaxError => e
  puts e.message[/\A[^:]*:\d+/]
end
def raiser = eval("raise 'in eval'")
begin
  raiser
rescue => e
  puts e.backtrace.first
end
p eval("caller(0, 1).first")
p eval("__dir__"), eval("__dir__", nil, "x/y.rb")
begin
  eval("require_relative 'nowhere'")
rescue LoadError => e
  p e.message
end
# a method an eval defined evals in turn: its name is the eval's source,
# and a call of it beside an eval of this file keeps both names right
eval(%q(def from_eval = eval("__FILE__")))
p from_eval, eval("__FILE__")
2000.times { from_eval; eval("1") }
p from_eval, eval("__FILE__")
