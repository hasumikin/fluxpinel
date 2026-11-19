# What the did_you_mean gem needs of an exception and of the walker:
# full_message asks a detailed_message of the program, a NameError of a
# bare name keeps the locals of its scope, the body of `class << obj`
# reads the constants of the scope where it is written, and
# String#each_codepoint takes a block.

class E1 < StandardError
  def detailed_message(highlight: false, **) = "detailed(#{highlight}) " + super
end
begin
  raise E1, "boom"
rescue => e
end
puts e.full_message(highlight: false).lines.first, e.full_message(highlight: true).lines.first.inspect
puts e.detailed_message, e.message

# a module ahead of the class
module Tag
  def detailed_message(**) = super + " [tag]"
end
class E2 < RuntimeError
  prepend Tag
end
begin
  raise E2, "x"
rescue => e
end
puts e.full_message(highlight: false).lines.first
begin
  raise "plain"
rescue => e
end
puts e.full_message(highlight: false).lines.first

# NameError#local_variables
def scope(first_arg)
  inner = 1
  [1].each { |blk_var| return no_such_name }
rescue NameError => e
  e.local_variables
end
p scope(0)

# class << obj in a module
module Outer
  Inner = 1
  class << (Checker = Object.new)
    def get = Inner
    Own = 2
    def own = Own
  end
  o = Object.new
  class << o
    def get = Inner
  end
  p Checker.get, Checker.own, o.get
end

# each_codepoint with a block
acc = []
p "aあ".each_codepoint { |c| acc << c }, acc, "".each_codepoint { |c| acc << c }
