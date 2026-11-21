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

# a bang form sets the encoding in both directions: a BINARY receiver of
# ASCII bytes becomes UTF-8 when a replacement is
s2 = "a-b-c".b
s2.gsub!(/-/) { "日" }
p s2.encoding, s2 == "a日b日c"
s3 = "a-b".b
s3.sub!(/-/) { "x" }
p s3.encoding

# the pieces of the receiver fold too: a receiver with its own high bytes
# and a replacement of the other encoding's are incompatible
def bad; yield; rescue Encoding::CompatibilityError => e; puts e.message; end
bad { "a-b-日".gsub(/-/) { 157.chr } }
bad { "a-b-\xff".b.gsub(/-/) { "日" } }
bad { "日-a".sub(/-/) { 157.chr } }
bad { "a-日".gsub("-", "-" => 157.chr) }
p "日-a".gsub(/-/) { "x".b }.encoding, "\xff-a".b.gsub(/-/) { "x" }.encoding
