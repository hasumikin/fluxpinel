# methods, public_methods, protected_methods, private_methods and
# global_variables: the names of the program, the singleton class, the
# builtin classes and the globals. The builtin names come from a table of
# CRuby's public methods, so the tests ask for inclusion and for the
# difference against a plain object, not for the whole list.

module Greet
  def greet = "hi"
  private def whisper = "psst"
end

class Base
  def base_pub = 1
  protected def base_prot = 2
  private def base_priv = 3
end

class A < Base
  include Greet
  attr_accessor :x
  def pub = 1
  protected def prot = 2
  private def priv = 3
  def self.cm = 4
end

a = A.new
plain = Object.new
p (a.methods - plain.methods).sort
p (a.public_methods - plain.public_methods).sort
p (a.protected_methods - plain.protected_methods).sort
p (a.private_methods - plain.private_methods).sort
p a.methods.include?(:prot), a.public_methods.include?(:prot), a.methods.include?(:priv)
p a.methods.include?(:inspect), a.methods.include?(:respond_to?), a.methods.include?(:instance_variables)
p a.private_methods.include?(:puts), a.private_methods.include?(:initialize), a.private_methods.include?(:pub)
p a.methods(false), a.public_methods(false).sort, a.private_methods(false), a.protected_methods(false)

def a.sing = 5
p a.methods(false), a.public_methods(false).sort, a.methods.include?(:sing), a.singleton_methods

# a class value: its singleton methods, then those of Class and Module
p A.methods(false), A.methods.include?(:cm), A.methods.include?(:new), A.methods.include?(:instance_methods)
p A.methods.include?(:attr_accessor), A.private_methods.include?(:puts), A.public_methods(false).sort
p Greet.methods.include?(:instance_method), Greet.methods.include?(:new)

# builtin values: the table of CRuby, and a method of a reopening
class String
  def shout = upcase
end
p "s".methods.include?(:upcase), "s".methods.include?(:shout), "s".methods.include?(:each), "s".methods(false)
p 1.methods.include?(:+), 1.methods.include?(:times), 1.methods.include?(:upcase), nil.methods.include?(:to_a)
p [].methods.include?(:each_slice), :sym.methods.include?(:to_proc), 1.private_methods.include?(:puts)
p Integer.methods.include?(:sqrt), File.methods.include?(:exist?)
p plain.methods.include?(:methods), plain.methods.size > 40, plain.private_methods.size > 40

# global_variables: the specials and the globals the program assigned
$my_global = 1
g = global_variables
p g.include?(:$stdout), g.include?(:$LOAD_PATH), g.include?(:$my_global), g.include?(:$0), g.include?(:$nope)
p g.uniq.size == g.size, Kernel.global_variables.include?(:$my_global), ::Kernel.respond_to?(:global_variables, true)
