# An eval with no file names its source "(eval at FILE:LINE)", the file
# and the line of the eval call, as CRuby does: in __FILE__, in a
# backtrace and in the message of a SyntaxError. Binding#eval names the
# place of the binding, and instance_eval of a String its call.
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
