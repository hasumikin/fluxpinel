# Class variables: a subclass and the class methods share them, a module
# that a class includes gives them, ||= and += write them, and the
# reflection methods read and write them.

class Counter
  @@count = 0
  @@names = []

  def self.count = @@count
  def self.reset = (@@count = 0)

  def initialize(name)
    @@count += 1
    @@names << name
  end

  def names = @@names

  class << self
    def last_name = @@names.last
  end
end

class SubCounter < Counter
  def initialize(name)
    super(name.upcase)
    @@count += 10
  end
end

Counter.new("a")
c = SubCounter.new("b")
p Counter.count, SubCounter.count, c.names, Counter.last_name

module Settings
  @@level = 3
  def level = @@level
  def level=(v)
    @@level = v
  end
end

class Service
  include Settings
  def bump = (@@level += 1)
end

s = Service.new
p s.level, s.bump, s.level
s.level = 7
p Service.new.level, Service.class_variable_get(:@@level), Settings.class_variable_get("@@level")

class Lazy
  def self.cache = (@@cache ||= {})
  def self.flag = (@@flag &&= :changed)
end
Lazy.cache[:a] = 1
p Lazy.cache, Lazy.class_variable_defined?(:@@cache), Lazy.class_variable_defined?(:@@other)

class Pair
  def self.set(a, b) = (@@a, @@b = a, b)
  def self.get = [@@a, @@b]
end
Pair.set(1, 2)
p Pair.get

p Counter.class_variables.sort, SubCounter.class_variables(false), Service.class_variables
Counter.class_variable_set(:@@count, 100)
p Counter.count, SubCounter.count
p Counter.remove_class_variable(:@@count)

begin
  @@top = 1
rescue RuntimeError => e
  p [e.class, e.message]
end

class Empty
  def self.read = @@nothing
end

[-> { Empty.read }, -> { Lazy.flag }, -> { Counter.class_variable_get(:count) },
 -> { Counter.remove_class_variable(:@@none) }].each do |f|
  begin
    p f.call
  rescue NameError => e
    p [e.class, e.message]
  end
end
