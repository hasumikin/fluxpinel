# String#scrub and scrub! with a block: the block answers the replacement
# of each invalid byte sequence.
s = "ab\xFFcd\xE3\x81ef"
p s.scrub { |c| c.bytes.map { |b| "\\x#{b.to_s(16).upcase}" }.join }
p s.scrub { "?" }
p s.scrub("!")
p s.scrub { |c| c.bytes.size.to_s * 3 }
t = s.dup
p t.scrub! { |c| "<#{c.bytes.size}>" }, t
p "ok".scrub { "x" }
p "".scrub { "x" }
begin
  s.scrub { "\xFF" }
rescue ArgumentError => e
  p e.class
end
