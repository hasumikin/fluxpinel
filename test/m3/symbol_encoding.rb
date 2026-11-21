# Symbol#encoding is US-ASCII for a Symbol of ASCII bytes and UTF-8 for one
# with other bytes, as CRuby answers.
p :WORD.encoding, :"a b".encoding, :"日本".encoding, :"café".encoding
p "x".to_sym.encoding, "日".to_sym.encoding, :+.encoding, :"".encoding
p :a.encoding == Encoding::US_ASCII, :a.encoding.equal?(:b.encoding), :a.encoding.name
p :a.respond_to?(:encoding)

# test-unit's assert_equal compares the encodings of two values that have
# one before it compares the values
expected, actual = :WORD, "WORD".to_sym
p expected.encoding != actual.encoding
