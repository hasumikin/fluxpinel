# require_relative, require of a path, __FILE__, __dir__ and __LINE__.

p require_relative("requires/shapes")
p require_relative("requires/shapes.rb")
p require_relative("requires/deep/util")
p Square.new(3).area, Util.double(21), Util.where
p SHAPES_LOADED
begin
  local_here
rescue NameError => e
  puts e.class
end

def load_in_method
  require_relative "requires/early_return"
end
p load_in_method
p load_in_method

begin
  require_relative "requires/missing"
rescue LoadError => e
  puts e.message.sub(__dir__, "<dir>")
end
begin
  require "./no_such_file_here"
rescue LoadError => e
  puts e.message
end
begin
  require_relative "requires/broken"
rescue ArgumentError => e
  puts "raised: #{e.message}"
end

p __FILE__.end_with?("requires.rb"), __LINE__
p __dir__.start_with?("/"), __dir__.end_with?("/m3")
