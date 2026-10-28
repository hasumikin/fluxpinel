# The reflection of constants: const_set, const_defined?, constants,
# remove_const and deprecate_constant.

module Outer
  A = 1
  module Inner
    B = 2
  end
end

class Base
  BASE = :base
end

class Derived < Base
  include Outer
  D = 4
end

p Outer.const_set(:C, 3), Outer::C, Outer.const_get(:C)
p Outer.const_defined?(:A), Outer.const_defined?(:Nope), Outer.const_defined?(:String), Outer.const_defined?(:String, false)
p Derived.const_defined?(:BASE), Derived.const_defined?(:BASE, false), Derived.const_defined?(:A), Derived.const_defined?(:A, false)
p Outer.constants.sort, Outer.constants(false).sort, Outer::Inner.constants
p Derived.constants.sort, Derived.constants(false)
p Base.constants, Object.const_defined?(:Outer), Object.const_defined?(:Derived, false)

k = Class.new
Outer.const_set(:Named, k)
p k.name, Outer::Named.equal?(k)

p Outer.send(:remove_const, :C), Outer.const_defined?(:C), Outer.constants.sort
module Outer
  p remove_const(:A)
end
p Outer.const_defined?(:A)

p Outer.deprecate_constant(:Inner).equal?(Outer), Outer::Inner::B
p Object.deprecate_constant(:Base).equal?(Object), Base::BASE

begin
  Outer.const_set(:lower, 1)
rescue NameError => e
  puts e.message
end
begin
  Outer.const_defined?(:lower)
rescue NameError => e
  puts e.message
end
begin
  Outer.send(:remove_const, :Nope)
rescue NameError => e
  puts e.message
end
begin
  Outer.deprecate_constant(:Nope)
rescue NameError => e
  puts e.message
end
