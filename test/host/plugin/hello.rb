# A plugin of test/host/scripts.rb: it calls an entry of the kernel, and
# the file answers an instance.

class Hello
  def greet(who) = "hello, #{who} (#{FxKernel.add(40, 2)})"
end

Hello.new
