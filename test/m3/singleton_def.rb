# def Const.m and def expr.m where the receiver is a class or a module:
# a singleton method of it, as def self.m in its body.

TOP = :top

class A; end
module M; end
module Outer
  class Inner; end
end

def A.make = :made
def M.helper(x) = x * 2
inner = Outer::Inner
def inner.tag = [:inner, TOP]
def Outer::Inner(x) = [:conv, x]
k = A
def k.alt(*a, &b) = [:alt, a, b&.call]

p A.make, M.helper(3), Outer::Inner.tag, Outer::Inner(1), A.alt(1, 2) { :blk }
p A.respond_to?(:make), A.respond_to?(:alt), M.respond_to?(:helper), A.new.respond_to?(:make)

class B < A; end
p B.make, B.alt

def A.make = :again
p A.make, B.make

class C
  def self.one = 1
  def C.two = 2
  class << self
    def three = 3
  end
end
p C.one, C.two, C.three, C.singleton_methods.sort

# the body sees the constants of the place of the def
module Place
  NAME = "place"
  def A.where = NAME
end
p A.where
