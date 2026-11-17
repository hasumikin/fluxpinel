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

# a send of the name at the call sees the locals of the caller too
p c.send(:instance_eval, "@v + x"), c.__send__(:instance_eval, "x"), c.public_send("instance_eval", "x * 3")
C.send(:class_eval, "def thrice = @v * 3")
p c.thrice, C.send(:class_eval, "x")
p [1, 2].map { |i| c.send(:instance_eval, "x + i") }

# a send that the program defines takes the call
class OwnSend
  def send(a, b) = [:own, a, b]
end
p OwnSend.new.send(:instance_eval, "1")

# the code sees the block of the method around the call
def with_block = Object.new.instance_eval("[__method__, block_given?]")
p with_block, (with_block { })
def yields = Object.new.instance_eval("yield 3")
p(yields { |v| v * 2 })

# nil, true and false: a def goes to their class
p nil.instance_eval("def from_nil = 1; from_nil"), nil.from_nil
p true.instance_eval("def from_true = 2; from_true"), false.respond_to?(:from_true)
begin
  5.instance_eval("def no = 1")
rescue TypeError => e
  puts e.message
end

# a String with a block, and too many arguments
begin
  c.instance_eval("1") { 2 }
rescue ArgumentError => e
  puts e.message
end
begin
  c.instance_eval("1", "f.rb", 1, 2)
rescue ArgumentError => e
  puts e.message
end
