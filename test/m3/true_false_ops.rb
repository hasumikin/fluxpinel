# The logic operators of true and false, which take any value, as CRuby's
# TrueClass#&, #|, #^ and FalseClass#&, #|, #^ do (rubish's arithmetic
# evaluator writes (a < 0) ^ (b < 0) for the sign of a quotient).
p true & true, true & false, true & nil, true & 1
p true | true, true | false, true | nil
p true ^ true, true ^ false, true ^ nil, true ^ 0
p false & true, false & nil, false & 1
p false | true, false | false, false | nil, false | 1
p false ^ true, false ^ false, false ^ nil, false ^ 0
p (-7 < 0) ^ (2 < 0), (7 < 0) ^ (2 < 0)
p true.send(:^, false), false.public_send(:|, 3)
begin
  true.&
rescue ArgumentError => e
  p e.message
end
begin
  false.^(1, 2)
rescue ArgumentError => e
  p e.message
end
