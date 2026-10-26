# String#scan with a block searches the subject as it is at each turn, and
# a block that changes its length raises

s = +"abc"
acc = []
s.scan(/./) { |m| acc << m; s.upcase! if acc.size == 1 }
p acc, s

t = +"ab"
u = t
acc = []
begin
  t.scan(/./) { |m| acc << m; u << "z" }
rescue RuntimeError => e
  p e.message
end
p acc, t

v = +"abab"
begin
  v.scan(/(a)(b)/) { |x, y| v << "q" }
rescue => e
  p e.class, e.message, $~[0]
end

# $~ is the last match after the scan, even when the block matches on its
# own; a scan with no match leaves nil
w = +"a1b2"
w.scan(/[a-z]\d/) { "zz" =~ /z/ }
p $~[0]
w.scan(/q/) { }
p $~
p "k1k2".scan(/k(\d)/) { }
