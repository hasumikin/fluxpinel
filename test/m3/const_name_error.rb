# The NameError of a missing constant names its scope as the receiver, and
# Object.constants lists the constants when a module is ahead of Object.

class Outer
  class Innner; end
  def m = Nope
end

def r
  yield
rescue NameError => e
  [e.name, e.receiver]
end
p r { Strng }, r { Outer::Inner }, r { Outer.new.m }, r { ::Zzz }, r { Outer::Innner::Deep }
p r { Outer.const_get(:Qq) }, r { Object.const_get(:Qq) }

module Front
  FRONT_C = 1
end
class Object
  prepend Front
end
X1 = 1
cs = Object.constants
p cs.include?(:String), cs.include?(:X1), cs.include?(:FRONT_C), cs.size > 50, cs.uniq.size == cs.size
class K
  KC = 1
end
p K.constants, Class.new(K).constants, K.constants(false), Front.constants

# The message names the scope as CRuby writes it: the lexical class of a
# bare name, the class of a path or of const_get, the singleton class of a
# `class << x` body or of an instance_eval String on a class, and no scope
# when its real class is Object. A block keeps the scope of its caller.
def msg
  yield
  "no error"
rescue NameError => e
  e.message.sub(/0x\h+/, "0x")
end
module Mod
  class Inner; end
end
puts msg { Nope }
puts msg { class Outer; Nope; end }
puts msg { module Mod; module Deep; Nope; end; end }
puts msg { class Mod::Inner; def self.m = Nope; end; Mod::Inner.m }
puts msg { class Outer; [1].each { Nope }; end }
puts msg { class Outer; Nope += 1; end }
puts msg { Class.new { Nope } }
puts msg { Struct.new(:a) { Nope } }
puts msg { Class.new.class_eval("Nope") }
puts msg { Outer.class_eval { Nope } }
puts msg { Outer.class_eval("Nope") }
puts msg { Outer.new.instance_eval("Nope") }
puts msg { Outer.new.instance_exec { Nope } }
puts msg { "s".instance_eval("Nope") }
puts msg { Outer.instance_eval("Nope") }
puts msg { Outer.singleton_class.class_eval("Nope") }
puts msg { class << Outer; Nope; end }
puts msg { class << "s"; Nope; end }
puts msg { Object.new.instance_eval("Nope") }
puts msg { instance_eval("Nope") }
puts msg { Mod.module_eval("Nope") }
puts msg { Mod::Nope }
puts msg { Object::Nope }
puts msg { Outer.const_get(:Nope) }
puts msg { Object.const_get(:Nope) }
puts msg { Kernel.const_get(:Nope) }
puts msg { Outer.singleton_class.const_get(:Nope) }
