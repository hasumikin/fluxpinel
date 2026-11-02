# Reflection on the classes of the kernel: the constants that name them,
# method_defined?, and the receiver of a NoMethodError on an object of the
# kernel. Only make test-host runs this file, and CRuby requires the kernel
# for its answer.

p Object.constants.include?(:FxKernel), Object.const_defined?(:FxKernel)
p FxKernel.constants.include?(:Counter), FxKernel.constants.include?(:Shape)
p FxKernel.constants(false).include?(:Counter)
p FxKernel::Counter.constants, FxKernel::Counter.const_defined?(:Nope)
p FxKernel::Counter.method_defined?(:nope), FxKernel::Counter.method_defined?(:to_s)
p FxKernel::Counter.instance_methods(false).include?(:nope)
c = FxKernel.make("c")
begin
  c.nope(1)
rescue NoMethodError => e
  p e.receiver.equal?(c), e.name, e.args
end
p FxKernel::Counter.superclass, FxKernel::Counter.ancestors.include?(Kernel)
