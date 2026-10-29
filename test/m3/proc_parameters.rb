# Proc#parameters: a proc lists a required parameter as :opt, a lambda as
# :req, and lambda: asks for the other list.

pr = proc { |a, b = 1, *r, c, k:, j: 2, **o, &blk| }
p pr.parameters
p pr.parameters(lambda: true)
l = ->(a, b = 1) {}
p l.parameters, l.parameters(lambda: false), l.parameters(lambda: nil)
p proc { |(a, b), c| }.parameters, proc { |a,| }.parameters
p proc { it }.parameters, proc { _1 + _2 }.parameters, proc {}.parameters
p proc { |*| }.parameters, proc { |**| }.parameters, proc { |&b| }.parameters
p :upcase.to_proc.parameters
def two(x, y = 0) = x + y
p method(:two).to_proc.parameters, method(:two).to_proc.lambda?
p pr.respond_to?(:parameters)

# what irb's inspector asks: the kind of the second parameter
inspector = proc { |v, output, colorize: true| }
p inspector.parameters[1]&.first
