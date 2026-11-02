# NoMethodError#receiver and #args, and NameError#receiver, of a call the
# walker misses: on an object of the program, a builtin value, a class,
# and through send.

class A
  def hi = :hi
end

a = A.new
begin
  a.nope
rescue NoMethodError => e
  p e.receiver.equal?(a), e.name, e.args
end
begin
  a.nope(1, "two")
rescue NoMethodError => e
  p e.args
end
begin
  1.nope
rescue NoMethodError => e
  p e.receiver, e.args
end
begin
  A.nope(:x)
rescue NoMethodError => e
  p e.receiver, e.args
end
begin
  a.send(:nope, 1)
rescue NoMethodError => e
  p e.receiver.equal?(a), e.args
end
begin
  nope_local
rescue NameError => e
  p e.class, e.name, e.receiver.equal?(self)
end
begin
  a.instance_eval { hidden }
rescue NameError => e
  p e.class, e.receiver.equal?(a)
end
