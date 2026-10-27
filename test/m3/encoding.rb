# The class Encoding, its constants and its methods

e = "a".encoding
p e, e.class, e.is_a?(Encoding), Encoding, Encoding.class
p Encoding::UTF_8, Encoding::BINARY, Encoding::US_ASCII, Encoding::ASCII_8BIT, Encoding::ASCII
p Encoding::Shift_JIS.name, Encoding::SHIFT_JIS.name, Encoding::SJIS.name, Encoding::EucJP.name,
  Encoding::EUCJP.name, Encoding::CP65001, Encoding::ISO_8859_1.name, Encoding::ANSI_X3_4_1968
p Encoding::UTF_8 == e, Encoding::UTF_8.equal?(e), Encoding::UTF_8.eql?(e), "a".b.encoding == Encoding::BINARY
p Encoding::UTF_8 == Encoding::BINARY, Encoding::UTF_8.equal?(Encoding.find("utf-8")), e.equal?("x")
p Encoding::UTF_8.name, Encoding::UTF_8.to_s, "#{Encoding::BINARY}", Encoding::UTF_8.name.frozen?
p Encoding.find("utf-8"), Encoding.find("binary"), Encoding.find(Encoding::UTF_8), Encoding.find("locale")
p Encoding.list.size, Encoding.list.first(3), Encoding.list.all?(Encoding)
p Encoding.default_external, Encoding.default_internal, Encoding::UNICODE_VERSION, Encoding::UNICODE_VERSION.frozen?
p Encoding::UTF_16.dummy?, Encoding::UTF_8.dummy?, Encoding::ISO_2022_JP.dummy?
p Encoding::UTF_16LE.ascii_compatible?, Encoding::UTF_16.ascii_compatible?, Encoding::UTF_8.ascii_compatible?,
  Encoding::EUC_JP.ascii_compatible?
p "x".force_encoding(Encoding::BINARY).encoding, "x".b.force_encoding(Encoding::UTF_8).encoding
s = "abc"
p s.force_encoding("BINARY").encoding, s.encoding, s.dup.force_encoding(Encoding::UTF_8).encoding
begin
  "lit".freeze.force_encoding("BINARY")
rescue FrozenError => x
  p x.class
end
p({ Encoding::UTF_8 => 1 }[e], [Encoding::BINARY].include?("a".b.encoding))
case e
when Encoding::BINARY then p :binary
when Encoding::UTF_8 then p :utf8
end

begin
  Encoding::NOPE
rescue NameError => x
  p x.message
end
begin
  Encoding.find("nope")
rescue ArgumentError => x
  p x.message
end
begin
  Encoding.new
rescue NoMethodError => x
  p x.message
end
begin
  Encoding.allocate
rescue TypeError => x
  p x.message
end
