# lazy of an object whose each has no end comes back at once. Fluxpinel
# has no lazy, so the call raises NoMethodError there; the call does not
# make an Array of the values of each.
class Inf
  include Enumerable
  def each
    i = 1
    loop { yield i; i += 1 }
  end
end

begin
  Inf.new.lazy
rescue NoMethodError
end
p Inf.new.first(2)
