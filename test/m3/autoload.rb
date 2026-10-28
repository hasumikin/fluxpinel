# autoload: the first read of a constant loads its file.

dir = File.join(__dir__, "autoload")
autoload :Shape, File.join(dir, "shape.rb")
p autoload?(:Shape).end_with?("shape.rb")
p defined?(Shape)
puts "before the read"
p Shape.new.sides
p autoload?(:Shape)
p defined?(Shape)

module Geo
  autoload :Point, File.join(__dir__, "autoload", "point.rb")
  autoload :Missing, File.join(__dir__, "autoload", "missing.rb")
  autoload :Empty, File.join(__dir__, "autoload", "empty.rb")
  def self.origin = Point.new(0, 0)
end
p Geo.autoload?(:Point).end_with?("point.rb")
p defined?(Geo::Point)
p Geo.origin.to_a
p Geo::Point.new(1, 2).to_a
p Geo.autoload?(:Point)

begin
  Geo::Missing
rescue LoadError => e
  puts "LoadError #{e.message.include?('autoload/missing')}"
end
# the autoload stays after a LoadError
p Geo.autoload?(:Missing).end_with?("missing.rb")
begin
  Geo::Empty
rescue NameError => e
  puts "#{e.class}: #{e.message}"
end

# a class statement loads the file first, then reopens the class
autoload :Counter, File.join(dir, "counter.rb")
class Counter
  def bump2 = bump + bump
end
p Counter.new.bump2

# a defined constant keeps its value
Already = 1
autoload :Already, File.join(dir, "shape.rb")
p autoload?(:Already), Already

begin
  autoload :Bad, 1
rescue TypeError => e
  puts e.message
end
begin
  autoload :Bad, ""
rescue ArgumentError => e
  puts e.message
end
begin
  autoload :bad, "x"
rescue NameError => e
  puts e.message
end
