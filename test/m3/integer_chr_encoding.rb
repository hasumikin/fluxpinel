# Integer#chr with an Encoding, or with the name of one: the character
# of the codepoint in it.
p 65.chr(Encoding::UTF_8), 65.chr(Encoding::BINARY).encoding, 65.chr("US-ASCII")
p 233.chr(Encoding::BINARY).bytes, 233.chr(Encoding::BINARY).encoding, 233.chr(Encoding::ASCII_8BIT).encoding
p 233.chr(Encoding::UTF_8), 233.chr("UTF-8").bytes, 233.chr(Encoding::UTF_8).encoding
p 0x1F600.chr(Encoding::UTF_8), 0x1F600.chr(Encoding::UTF_8).bytesize, 0x3042.chr("utf-8")
p 0.chr(Encoding::UTF_8).bytes, 0x7F.chr(Encoding::US_ASCII).bytes
p 0xE0.chr(Encoding::BINARY) + "\x80\x80".b
[256, -1].each do |i|
  begin
    i.chr(Encoding::BINARY)
  rescue RangeError => e
    puts e.message
  end
end
[0x110000, -1, 0xD800].each do |i|
  begin
    i.chr(Encoding::UTF_8)
  rescue RangeError => e
    puts e.message
  end
end
begin
  128.chr(Encoding::US_ASCII)
rescue RangeError => e
  puts e.message
end
begin
  65.chr("no-such-encoding")
rescue ArgumentError => e
  puts e.message
end
