# String#encode with its options: the destination and the source
# encodings, and invalid:, undef: and replace: for a bad byte or an
# unmappable character.
s = "café"
p s.encode, s.encode.encoding, s.encode("UTF-8"), s.encode(Encoding::UTF_8).encoding
p s.encode("US-ASCII", invalid: :replace, undef: :replace)
p s.encode("US-ASCII", invalid: :replace, undef: :replace, replace: "?")
p s.encode("US-ASCII", undef: :replace, replace: "")
bad = "ab\xFFcd"
p bad.valid_encoding?, bad.encode("UTF-8", invalid: :replace), bad.encode("UTF-8", invalid: :replace, replace: "*")
p bad.encode("US-ASCII", invalid: :replace, undef: :replace)
p "abc".encode("ASCII-8BIT"), "abc".encode("ASCII-8BIT").encoding, "abc".encode("UTF-8", "ASCII-8BIT")
begin
  s.encode("US-ASCII")
rescue Encoding::UndefinedConversionError => e
  p e.class
end
begin
  bad.encode("US-ASCII")
rescue Encoding::InvalidByteSequenceError => e
  p e.class
end
begin
  s.encode("UTF-8", "US-ASCII", "x")
rescue ArgumentError => e
  p e.message
end
