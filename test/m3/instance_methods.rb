# instance_methods with the builtin methods: of a builtin class, of a
# class of the program with its ancestors, and of a module.
module Greet
  def greet = "hi"
end
class Base
  def base = 1
  private def hidden = 2
end
class Kid < Base
  include Greet
  def kid = 3
  protected def shielded = 4
end
p Kid.instance_methods(false).sort
p Kid.public_instance_methods(false).sort
p Kid.protected_instance_methods.sort
p (Kid.instance_methods - Object.instance_methods).sort
p Kid.private_instance_methods(false), Base.private_instance_methods(false)
p Kid.instance_methods.include?(:inspect), Kid.instance_methods.include?(:puts)
p Kid.private_instance_methods.include?(:puts)
p Greet.instance_methods, Comparable.instance_methods.sort
m = Array.instance_methods
p m.include?(:map), m.include?(:each_with_index), m.include?(:inspect), m.include?(:puts)
p Array.instance_methods(false).include?(:map), Array.instance_methods(false).include?(:inspect)
p String.instance_methods.include?(:upcase), Hash.instance_methods.include?(:each_pair)
p Kernel.private_instance_methods.include?(:puts), Kernel.instance_methods.include?(:puts)
class Array
  def twice = self * 2
end
p Array.instance_methods(false).include?(:twice), Array.instance_methods.include?(:twice)
p Array.instance_methods.uniq.size == Array.instance_methods.size
