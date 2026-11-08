# OpenStruct: a table of members of the runtime; a member is read and
# written by its name, and the fixed methods read the table.
require "ostruct"
require "json"

o = OpenStruct.new(a: 1, "b" => {c: 2})
p o.class, o.a, o.b, o.zz, o.respond_to?(:a), o.respond_to?(:zz), o.respond_to?(:to_h)
p o.dig(:b, :c), o[:a], o["a"], (o[:x] = 5), o.x
o.y = 7
p o.y, o.to_h, o.inspect, o.to_s, o
p o.each_pair { |k, v| }, o.each_pair.to_a
p o.delete_field(:x)
begin; o.delete_field(:nope); rescue NameError => e; p e.class, e.message; end
p o.delete_field(:nope) { :dflt }

q = OpenStruct.new(a: 1, b: {c: 2}, y: 7)
p o == q, o != q, o.eql?(q), o.eql?(OpenStruct.new(a: 1.0, b: {c: 2}, y: 7)), o.hash.class, o.frozen?
d = o.dup
d.a = 9
p o.a, d.a, o.clone.a
o.freeze
p o.frozen?, o.dup.frozen?, o.clone.frozen?
begin; o.y = 1; rescue => e; p e.class, e.message; end
begin; o.nn = 1; rescue => e; p e.class, e.message; end
p OpenStruct.new(nil).to_h, OpenStruct.new.to_h, OpenStruct === o, [o, 1][0].a
p(case q when OpenStruct then "os" else "other" end)
puts q.to_h

j = JSON.parse('{"size": 9, "limits": {"last": 2}, "rows": [{"min": 1}]}', object_class: OpenStruct)
p j.class, j.size, j.limits.last, j.rows[0].min
p [OpenStruct.new(upcase: :member), "abc"].map(&:upcase)
