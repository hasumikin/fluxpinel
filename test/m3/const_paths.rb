# A constant of a path (A::B) looks in A and its ancestors, but not in
# Object unless A is Object

module Compat
  Compat::RUBY_ENGINE ||= "truffleruby"
  p RUBY_ENGINE
end

module M; end
class C; end
X = 1
[-> { M::X }, -> { C::X }, -> { M::String }, -> { Comparable::String }].each do |l|
  begin
    p l.call
  rescue NameError => e
    p e.message
  end
end
p Object::X, ::X, Thread::Queue, Process::CLOCK_MONOTONIC.class, Regexp::IGNORECASE

module N
  Y = 2
  class D; end
end
p N::Y, N::D, N::D.name

class Base
  Z = 3
end
class Sub < Base; end
p Sub::Z
