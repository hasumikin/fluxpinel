# The result of sub and gsub with a block or a Hash takes the encoding the
# receiver and the replacements share, as a concatenation does: an
# ASCII-only receiver with a BINARY replacement is BINARY, as CRuby has it.
bin = 157.chr
p bin.encoding
p "a-b".gsub(/-/) { bin }.encoding, "a-b".sub(/-/) { bin }.encoding
p "a-b".gsub("-") { bin }.encoding, "a-b".gsub(/-/, "-" => bin).encoding
s = "a-b-c".dup
s.gsub!(/-/) { bin }
p s.encoding, s.bytes
t = "a-b".dup
t.sub!("-") { bin }
p t.encoding, t.bytes, t == "a\x9Db".b
p "a-b".gsub(/-/) { "x" }.encoding, "日-b".gsub(/-/) { "x" }.encoding, "a-b".gsub(/-/) { "日" }.encoding
p "a-b".gsub(/-/) { bin } == "a\x9Db".b, "a-b".gsub(/-/) { "" }.encoding
p "abc".b.gsub(/b/) { "x" }.encoding, "abc".b.gsub(/b/) { "日" }.encoding

# rubish's key sequence: a Meta-Control key becomes one high byte
seq = '\\M-\\C-]'.dup
seq.gsub!(/\\M-\\C-([a-zA-Z@\[\]\\^_?])/) { |_| (0x80 | ($1.upcase.ord & 0x1F)).chr }
p seq.encoding, seq.bytes, seq == 157.chr
