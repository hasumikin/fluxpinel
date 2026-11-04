# super evaluates its arguments before its block argument, as a call does:
# `&s` reads the s that an argument wrote, with a Symbol and with a Proc.

class E
  def m(a, &b) = b.call(a)
  def y = yield(255, 16)
end

class F < E
  def m(a)
    s = :upcase
    super((s = :downcase; a), &s)
  end

  def y
    s = :to_s
    super(&s)
  end
end

class G < E
  def m(a)
    s = proc { |x| x.upcase }
    super((s = proc { |x| x.downcase }; a), &s)
  end
end

p F.new.m("Hi")
p F.new.y
p G.new.m("Hi")
p E.new.m("Hi") { |x| x * 2 }
