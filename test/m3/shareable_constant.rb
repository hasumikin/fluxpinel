# shareable_constant_value: literal
# The magic comment wraps each constant write (the time package has it);
# the value is written as it is.
module M
  VERSION = "0.4.2"
  LIST = [1, 2]
  A = B = 3
end
p M::VERSION, M::LIST, M::A, M::B, M::VERSION.frozen?
