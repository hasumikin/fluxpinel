# The methods of the classes of the kernel (test/kernel/app.rb), called by
# name through the table of --ext-class: an instance method, an attr
# reader, a method that an ancestor defines, a singleton method, and the
# errors of a wrong call. Only make test-host runs this file, and CRuby
# requires the kernel for its answer.

c = FxKernel.make("c")
p c.bump(2), c.bump(3), c.n, c.name
p FxKernel::Counter.instance_methods(false).sort
p FxKernel::Counter.method_defined?(:bump), c.respond_to?(:bump), c.respond_to?(:nope)
s = FxKernel.shape("s", 4)
p s.grow, s.instance_variable_get(:@grown)
# the arity and the kind of an argument are checked at the boundary, as a
# builtin checks them (CRuby raises the TypeError in the body, with other
# words)
begin
  c.bump
rescue ArgumentError => e
  p e.message
end
begin
  c.bump("x")
rescue TypeError => e
  p e.class
end
# a singleton method of the module beside its entries
p FxKernel.respond_to?(:make), FxKernel.singleton_methods.include?(:make)
p c.method(:bump).arity, c.public_send(:bump, 1)
