# The ivars of an object of the kernel (test/kernel/app.rb), through the
# inspect table of spinel. Only make test-host runs this file.

s = FxKernel.shape("box", 4)
p s.instance_variables
p s.instance_variable_get(:@name), s.instance_variable_get("@w"), s.instance_variable_get(:@ratio)
p s.instance_variable_get(:@tags), s.instance_variable_get(:@sizes)
owner = s.instance_variable_get(:@owner)
p owner.class, owner.instance_variable_get(:@name), FxKernel.bump(owner, 3)
# an ivar the object has not set, and a name it has no ivar of
p s.instance_variable_get(:@grown), s.instance_variable_get(:@none)
FxKernel.grow(s)
p s.instance_variable_get(:@grown), s.instance_variable_get(:@late), s.instance_variables
