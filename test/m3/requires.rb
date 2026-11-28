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
# a file that raised is not loaded: the next require runs it again, and
# $LOADED_FEATURES does not name it (once_broken.rb raises the first time)
begin
  require_relative "requires/broken"
rescue ArgumentError => e
  puts "raised again: #{e.message}"
end
p $LOADED_FEATURES.any? { |f| f.end_with?("requires/broken.rb") }
begin
  require_relative "requires/once_broken"
rescue RuntimeError => e
  puts e.message
end
p require_relative("requires/once_broken"), ONCE_BROKEN, require_relative("requires/once_broken")
# a throw out of the file leaves it not loaded too (thrower.rb throws the
# first time)
p catch(:out) { require_relative "requires/thrower"; :no_throw }
p require_relative("requires/thrower"), defined?(THROWER), THROWER

p __FILE__.end_with?("requires.rb"), __LINE__
p __dir__.start_with?("/"), __dir__.end_with?("/m3")

# $LOADED_FEATURES names each file that a require ran, once, by its
# absolute path; the main program is not in it
loaded = $LOADED_FEATURES.select { |f| f.start_with?(File.join(__dir__, "requires")) }
p loaded.map { |f| f.sub(__dir__, "<dir>") }.sort
p $LOADED_FEATURES.include?(File.expand_path(__FILE__)), $".equal?($LOADED_FEATURES)
