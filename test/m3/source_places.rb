# The places of the source that reflection answers: Module.nesting,
# Module#const_source_location and the inspect of a Proc.

p Module.nesting
module A
  p Module.nesting
  class B
    p Module.nesting
    def m = Module.nesting
    def self.s = Module.nesting
  end
end
p A::B.new.m, A::B.s
p eval("::Module.nesting"), [1].map { Module.nesting }

class Foo
  BAR = 1
end
X = 2
module M
  Y = 3
  class Inner; end
end
p Object.const_source_location(:Foo).last, Foo.const_source_location(:BAR).last, Object.const_source_location("X").last
p M.const_source_location(:Y).last, M.const_source_location(:Inner).last
p Object.const_source_location(:String), Object.const_source_location(:NoSuch)
p Foo.const_source_location(:X).last, Foo.const_source_location(:X, false)
p File.basename(Object.const_source_location(:Foo).first)
# the first body of a class is its place
class Foo; end
p Object.const_source_location(:Foo).last
Z = Class.new
p Object.const_source_location(:Z).last

mask = ->(s) { s.gsub(/0x\h+/, "0xX").sub(/\S*source_places\.rb/, "FILE") }
pr = proc { 1 }
l = lambda { 2 }
puts mask.(pr.inspect), mask.(l.to_s)
p pr.inspect =~ /\A#<Proc:0x\h{16} /
def blk(&b) = b
puts mask.(blk { 3 }.inspect)
