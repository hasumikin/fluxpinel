# The parts of an interpolation: adjacent literals, of which some
# interpolate, join into one String, and an embedded variable ("#@x")
# interpolates as "#{@x}" does.
x = 1
s = "a#{x}-" \
    "#{x + 1}c"
p s
p "p" "q#{x}", "#{x}" "y", "#{x}" "#{x}" "#{x}", "one " "two " "three"
p "a#{x}" 'b' "c#{x}"
def heading(n) = "#{n}. " \
                 "#{n * 2}"
p heading(3)

@a = "ivar"
$c = "gvar"
p "#@a #$c", "#@a", "x#$c" "y#@a"
p :"s#{x}", :"#@a"
class Holder
  @@b = "cvar"
  def self.show = "#@@b and #{@@b}"
end
p Holder.show
p "#{"inner #{x}"}", "#{"a" "b#{x}"}"
h = { "k#{x}" "v" => 1 }
p h
case "a1b"
when "a#{x}" "b" then p :matched
end

# tempfile names its files with adjacent literals
require "tempfile"
Tempfile.create("parts") do |f|
  f.write("in the file")
  f.flush
  p File.exist?(f.path), File.basename(f.path).start_with?("parts"), File.read(f.path)
end
