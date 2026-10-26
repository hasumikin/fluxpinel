# Integer#[], #~ and #object_id, and Array#uniq! with a block

p 5[0], 5[1], 5[2], 5[64], -1[100], 5[-1]
p 0b1011[0..2], 0b1011[1, 2], 0b1011[2..]
x = 0x8f
p x[7], x[2.9]
p ~5, ~-1, ~0
p 1.object_id, 0.object_id, -1.object_id, nil.object_id, true.object_id, false.object_id
s = "a"
p s.object_id == s.object_id, s.object_id == "a".object_id
a = [1, 2, 3, 4]
p a.uniq! { |v| v % 2 }, a
b = [1, 2]
p b.uniq! { |v| v }, b
o1 = Object.new
c = [o1, o1, Object.new]
c.uniq! { |e| e.object_id }
p c.size
