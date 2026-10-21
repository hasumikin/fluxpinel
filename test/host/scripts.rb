# AOT code of the kernel (test/kernel/app.rb) that calls Fluxpinel through
# kernel/fluxpinel.rb: it loads plugins, calls their methods and evaluates
# strings. Only make test-host runs this file.

dir = File.join(__dir__, "plugin")
p FxKernel.greet_all([File.join(dir, "hello.rb"), File.join(dir, "bye.rb")], "fluxpinel")
p FxKernel.script("1 + 2"), FxKernel.script("[Hello, Bye].map(&:name)")
p FxKernel.rescue_script("raise ArgumentError, 'from a script'")
begin
  FxKernel.script("raise IndexError, 'through AOT'")
rescue IndexError => e
  p e.class, e.message
end
p FxKernel.script("FxKernel.script('6 * 7')")
# a local of a plugin stays in the plugin
begin
  n
rescue NameError => e
  p e.class
end
