# Regexp: literals, the match family, $~ and the String methods.

re = /(\d+)-(\d+)/
p re
p re.source
puts re.to_s
p(/ab/i.casefold?)
p(/a#{1 + 1}b/)

s = "tel 12-34 and 56-78"
p(s =~ re)
p $~
p [$1, $2, $&, $`, $']
p(s !~ /xyz/)
p(s.match?(/\d/))
p "abc".match?(/z/)

m = re.match(s)
p m[0], m[1], m[2], m.pre_match, m.post_match
p m.captures
p m.begin(0), m.end(1)
p m.to_a
p m.values_at(1, 2)
p m[1..2]

m = "John Smith".match(/(?<first>\w+) (?<last>\w+)/)
p m[:first], m["last"]
p m.names
p m.named_captures
p m.named_captures(symbolize_names: true)

if /(?<year>\d{4})-(?<mon>\d\d)/ =~ "date 2026-10"
  p [year, mon]
end
/(?<nothing>zzz)/ =~ "abc"
p nothing

p "a1b22c333".scan(/\d+/)
p "k1=v1;k2=v2".scan(/(\w+)=(\w+)/)
"x1y2".scan(/[a-z](\d)/) { |d| print d, " " }
puts

p "hello world".sub(/o/, "0")
p "hello world".gsub(/o/, "0")
p "hello world".gsub(/(l+)/) { $1.upcase }
p "hello world".gsub(/[eo]/, "e" => "3", "o" => "0")
p "a-b-c".gsub("-") { "+" }
p "john smith".gsub(/\b(\w)/) { |c| c.upcase }
p "2026-10-20".sub(/(\d+)-(\d+)-(\d+)/, '\3/\2/\1')

t = +"aaa"
p t.sub!(/a/, "b"), t
p t.gsub!(/z/, "y"), t
p t.gsub!(/a/) { "c" }, t

p "a, b,c ,d".split(/\s*,\s*/)
p "a1b2c3".split(/\d/, 2)
p "one two".index(/two/), "abcabc".rindex(/b/)
p "price: 120 yen"[/\d+/]
p "key=value"[/(\w+)=(\w+)/, 2]
p "aXbXc".partition(/X/), "aXbXc".rpartition(/X/)
p "start here".start_with?(/st/), "start".start_with?("x", /ar/)

u = +"remove 42 now"
p u.slice!(/\d+ /), u
u[/now/] = "later"
p u

p Regexp.escape("a.b*c")
p Regexp.union("a.b", /c+/)
p Regexp.new("h(.)llo") =~ "hallo"
p Regexp.new("abc", Regexp::IGNORECASE).match?("ABC")
p Regexp.last_match(1)

p %w[apple banana cherry].grep(/an/)
p %w[apple banana cherry].grep_v(/an/)
p [1, "a", :b, 2.0].grep(Numeric)
p %w[a1 b2].grep(/(\d)/) { $1 }

p(/a/ == /a/, /a/ == /b/)
p(/abc/ === "xabcx")

def inner
  "zz" =~ /(z)/
  $1
end
"outer" =~ /(out)/
p inner
p $1

case_like = ["x1", "y", "z22"].map { |w| w =~ /\d/ ? w.to_sym : w }
p case_like
