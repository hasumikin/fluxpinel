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
