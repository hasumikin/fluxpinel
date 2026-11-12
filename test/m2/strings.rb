s = "Hello"
p s.upcase, s.downcase, s.reverse
p s.length
p s.center(11)
p s.center(11, "*"), s.ljust(7, "."), s.rjust(7, ".")
begin
  s.center("11")
rescue TypeError => e
  p e.message
end
begin
  [1, 2, 3].first("2")
rescue TypeError => e
  p e.message
end
p "aabbcc".count("a"), "aabbcc".count("ab", "b"), "aabbcc".delete("a"), "aabbcc".squeeze("a")
begin
  "aabbcc".delete(nil)
rescue TypeError => e
  p e.message
end
p s.include?("ell")
p s.start_with?("He")
p s.succ
p 255.to_s(16)
p 3.7.floor, 3.2.ceil
p 10.gcd(4)
p "a-b-c".split("-")
t = s + " world"
p t
p t.to_s
p 12.to_s + "!"
