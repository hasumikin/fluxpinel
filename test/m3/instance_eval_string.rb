# instance_eval of a String: the code runs with the receiver as self, reads
# its instance variables, sees the locals of the caller as a block would,
# and a def in it is a singleton method of the receiver (a class method
# when the receiver is a class). The file and line name the code.
class C
  def initialize
    @v = 7
  end
end

c = C.new
p c.instance_eval("@v + 1")
x = 10
p c.instance_eval("@v + x")
p c.instance_eval("self.class")
p c.instance_eval("@v = 9; @v")
p c.instance_variable_get(:@v)

# a def is a singleton method of the receiver
c.instance_eval("def hello = 'hi from ' + @v.to_s")
p c.hello
p C.new.respond_to?(:hello)
p c.singleton_methods

# on a class: self is the class, and a def is a class method
p C.instance_eval("self")
C.instance_eval("def make = new")
p C.make.class
p C.new.respond_to?(:make)

# class_eval of a String sees the caller's locals too, and a def in it is
# an instance method
p C.class_eval("x + 1")
C.class_eval("def twice = @v * 2")
p c.twice

# a local the code makes stays in the code
c.instance_eval("y = 1")
p defined?(y)

# the caller's local can be changed, as from a block
z = 1
c.instance_eval("z += @v")
p z

# an exception carries through
begin
  c.instance_eval("raise ArgumentError, 'from the string'")
rescue ArgumentError => e
  p e.message
end

# the file and the line of the code
p c.instance_eval("__FILE__", "fake.rb", 30)
p c.instance_eval("__LINE__", "fake.rb", 30)
p c.instance_eval("__LINE__")

# a builtin receiver
p "abc".instance_eval("size * 2")
p 5.instance_eval("self + x")

# the block form keeps working beside it
p c.instance_eval { @v + 100 }
