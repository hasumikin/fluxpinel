# AOT code of the kernel (test/kernel/app.rb) that calls Lupe through
# kernel/lupe.rb: it loads plugins, calls their methods and evaluates
# strings. Only make test-host runs this file.

dir = File.join(__dir__, "plugin")
p LupeKernel.greet_all([File.join(dir, "hello.rb"), File.join(dir, "bye.rb")], "lupe")
p LupeKernel.script("1 + 2"), LupeKernel.script("[Hello, Bye].map(&:name)")
p LupeKernel.rescue_script("raise ArgumentError, 'from a script'")
begin
  LupeKernel.script("raise IndexError, 'through AOT'")
rescue IndexError => e
  p e.class, e.message
end
p LupeKernel.script("LupeKernel.script('6 * 7')")
# a local of a plugin stays in the plugin
begin
  n
rescue NameError => e
  p e.class
end
