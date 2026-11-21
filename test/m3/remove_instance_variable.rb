# Object#remove_instance_variable: the variable goes, and its value is the
# answer; a name the object has no variable of is a NameError.
class Case
  def initialize
    @a = 1
    @b = 2
  end
  def drop(name) = remove_instance_variable(name)
  attr_reader :a, :b
end

o = Case.new
p o.remove_instance_variable(:@a), o.instance_variables, o.a, o.instance_variable_defined?(:@a)
begin
  o.remove_instance_variable(:@a)
rescue NameError => e
  p e.class, e.message, e.name
end
p o.drop("@b"), o.instance_variables, o.b
o.instance_variable_set(:@a, 3)
p o.instance_variables, o.a
p o.remove_instance_variable(:@a), o.instance_variables

# a second object of the class keeps its own variables; one that sets a
# later variable of the class first has only that one
other = Case.new
p other.instance_variables, other.a, other.b
bare = Case.allocate
bare.instance_variable_set(:@b, 9)
p bare.instance_variables, bare.instance_variable_defined?(:@a), bare.a, bare.b
p Marshal.load(Marshal.dump(bare)).instance_variables

# the main object, a frozen object, a bad name, a builtin value
@top = 1
p remove_instance_variable(:@top), instance_variables.include?(:@top)
o.instance_variable_set(:@c, 4)
o.freeze
begin
  o.remove_instance_variable(:@c)
rescue => e
  p e.class
end
begin
  Case.new.remove_instance_variable(:x)
rescue NameError => e
  p e.message
end
begin
  1.remove_instance_variable(:@a)
rescue => e
  p e.class, e.message
end

# test-unit's use: the variables a run added go, by the difference of the
# lists
before = other.instance_variables
other.instance_variable_set(:@run, 5)
other.instance_variable_set(:@more, 6)
(other.instance_variables - before).each { |n| other.remove_instance_variable(n) }
p other.instance_variables, other.a
