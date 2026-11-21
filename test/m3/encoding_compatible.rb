# Encoding.compatible?(a, b): the encoding a String of the two would have,
# or nil, as CRuby answers for Strings, Symbols, Regexps and Encodings.
E = Encoding
def show(a, b) = p [a, b, E.compatible?(a, b)]

# two Strings
show "a", "b"
show "a".b, "日"
show "日", "x".b
show "日".b, "日"
show "日", "日".b
show "", "日".b
show "日".b, ""
show "", ""
show "x".b, "y".b

# an Encoding or a Symbol or a Regexp beside a String
show E::UTF_8, "x"
show E::UTF_8, "x".b
show E::UTF_8, "日".b
show E::UTF_16LE, "x"
show "x", E::UTF_16LE
show :a, "b"
show "b", :a
show /a/, "日"
show "日", /a/
show :"日", "x".b

# two of neither
show E::UTF_8, E::US_ASCII
show E::US_ASCII, E::UTF_8
show E::UTF_8, E::BINARY
show E::BINARY, E::UTF_8
show E::UTF_8, E::UTF_8
show E::UTF_8, :a
show :a, :b
show :"日", :a

# a value with no encoding
show 1, 2
show E::UTF_8, 1
show "x", nil

# a NUL byte is a byte of the String, not its end
show "a\0日".b, "日"
show "日", "a\0\xff".b
show "a\0b".b, "日"

