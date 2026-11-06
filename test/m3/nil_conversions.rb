# The conversions and the logic operators of nil (IRB's History asks
# IRB.conf[:SAVE_HISTORY].to_i of a nil setting).
p nil.to_i, nil.to_f, nil.to_a, nil.to_h, nil.to_s, nil.to_r
p nil & true, nil & nil, nil | 1, nil | nil, nil ^ nil, nil ^ 0
h = nil.to_h
h[:a] = 1
p h, nil.to_a.frozen?
