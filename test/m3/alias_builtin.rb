# alias and alias_method of a builtin method: the alias calls the builtin
# one, and keeps its visibility.

class Conf
  def initialize
    @mode = :quiet
  end
  alias __inspect__ inspect
  def inspect = "conf(#{@mode})"
  alias to_s inspect
  alias_method :same?, :==
  alias thing class
end

c = Conf.new
p c.inspect, c.to_s, "#{c}"
p c.__inspect__.sub(/0x\h+/, "0x"), c.__inspect__ =~ /\A#<Conf:0x/
p c.same?(c), c.same?(Conf.new), c.thing
p c.respond_to?(:__inspect__), Conf.instance_method(:__inspect__).owner
p Conf.public_method_defined?(:__inspect__), Conf.private_method_defined?(:__inspect__)

# a private method of Kernel stays private under the alias
module Loader
  alias ruby_require require
  alias ruby_puts puts
  def say = ruby_puts("said")
end
class Conf
  include Loader
end
c.say
p Loader.private_instance_methods(false).sort
begin
  c.ruby_puts("x")
rescue NoMethodError => e
  puts e.message[/private method 'ruby_puts' called/]
end

# the alias of a builtin method of a builtin class, and of a Struct member
class String
  alias size_alias size
end
p "four".size_alias
S = Struct.new(:a) do
  alias first_member a
  alias list to_a
end
p S.new(1).first_member, S.new(2).list

# a name that no ancestor has
begin
  class Conf
    alias nope no_such_method
  end
rescue NameError => e
  puts e.message
end
