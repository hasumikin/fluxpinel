# The singleton class of a class or module: a definition through it is a
# class method, class_eval on it defines class methods, and it lists them.
module Util
  singleton_class.attr_reader :level
  singleton_class.class_eval do
    def shout(s) = s.upcase
  end
  @level = 3
end
p Util.singleton_class, Util.singleton_class.name, Util.singleton_class.singleton_class?, Util.singleton_class?
p Util.level, Util.shout("hi")
p Util.singleton_class.instance_methods(false).sort, Util.singleton_class.method_defined?(:shout)
p Util.singleton_class.method_defined?(:nothing), Util.singleton_class.superclass

class Base
  def self.make = :base
end
class Kid < Base; end
p Kid.singleton_class.superclass, Kid.singleton_class.superclass == Base.singleton_class
p Kid.singleton_class.equal?(Kid.singleton_class), Base.singleton_class.instance_methods(false)
Kid.singleton_class.define_method(:twice) { make.to_s * 2 }
p Kid.twice
Kid.singleton_class.alias_method(:thrice, :make)
p Kid.thrice
module Mixin
  def mixed = :mixed
end
Kid.singleton_class.include(Mixin)
p Kid.mixed
Kid.singleton_class.send(:private, :twice)
begin
  Kid.twice
rescue NoMethodError => e
  p e.class
end
p Object.singleton_class.superclass, BasicObject.singleton_class.superclass
p Class.new.singleton_class.superclass, String.singleton_class.inspect
o = Object.new
p o.singleton_class.singleton_class?, Kid.singleton_class?
