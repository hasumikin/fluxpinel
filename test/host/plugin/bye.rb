# A plugin of test/host/scripts.rb, with a local and a block.

class Bye
  def initialize(n) = @n = n
  def greet(who) = (["bye"] * @n).map { |w| "#{w}, #{who}" }.join(" / ")
end

n = 2
Bye.new(n)
