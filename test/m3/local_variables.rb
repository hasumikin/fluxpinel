# Kernel#local_variables lists the locals of the scope, then those of each
# scope out of it. A bare name on a builtin value as self is a NameError
# when the value has no such method. A private method of Kernel is owned
# by Kernel on any receiver.

def t
  yield
rescue Exception => e
  "#{e.class}: #{e.message}"
end

a = 1
p local_variables.include?(:a), binding.local_variables == local_variables
def m(x, y = 2) = (z = 3; local_variables)
p m(1)
[1].each { |q| w = 1; p local_variables.first(2), local_variables.include?(:a) }
p t { local_variables(1) }

# self is a builtin value
b = 5.instance_eval { binding }
p eval("v = 1; local_variables.first", b), 5.instance_eval { local_variables.include?(:a) }
p t { eval("conf", b) }, t { eval("conf()", b) }
p t { 5.instance_eval { nosuch } }, t { nil.instance_eval { nosuch } }, t { "s".instance_eval { nosuch(1) } }
p 5.instance_eval { abs }, [3, 1].instance_eval { size }, "s".instance_eval { upcase }

# the owner of a private method of Kernel
p method(:puts).owner, method(:format).owner, 5.method(:puts).owner, "s".method(:require).owner
p Object.new.method(:puts).owner, [].method(:print).owner, 5.method(:+).owner, [].method(:push).owner
def puts2; end
p method(:puts2).owner

# Proc#source_location
pr = proc { 1 }
l = ->(x) do
  x
end
p pr.source_location.map { |v| v.is_a?(String) ? File.basename(v) : v }, l.source_location.last
p :upcase.to_proc.source_location, method(:puts).to_proc.source_location
def loc(&blk) = blk.source_location.last
p loc { 2 }, pr.respond_to?(:source_location)
