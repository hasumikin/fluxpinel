# OpenSSL::KDF.pbkdf2_hmac, whose loop Fluxpinel has in C
# (lib/fluxpinel/openssl.rb): the vectors of RFC 6070 (SHA1) and the known
# ones of SHA256, a key longer and shorter than a block of the hash, a
# password and a salt with a NUL byte, the encoding of the answer, the
# errors, and OpenSSL::PKCS5.pbkdf2_hmac, which calls it.
require "openssl"

def hex(s) = s.unpack1("H*")

def kdf(pass, salt, iterations, length, hash)
  OpenSSL::KDF.pbkdf2_hmac(pass, salt: salt, iterations: iterations, length: length, hash: hash)
end

puts hex(kdf("password", "salt", 1, 20, "SHA1"))
puts hex(kdf("password", "salt", 2, 20, "SHA1"))
puts hex(kdf("password", "salt", 4096, 20, "SHA1"))
puts hex(kdf("passwordPASSWORDpassword", "saltSALTsaltSALTsaltSALTsaltSALTsalt", 4096, 25, "SHA1"))
puts hex(kdf("pass\0word", "sa\0lt", 4096, 16, "SHA1"))
puts hex(kdf("password", "salt", 1, 32, "SHA256"))
puts hex(kdf("password", "salt", 4096, 32, "SHA256"))
puts hex(kdf("passwordPASSWORDpassword", "saltSALTsaltSALTsaltSALTsaltSALTsalt", 4096, 40, "SHA256"))
puts hex(kdf("pass\0word", "sa\0lt", 4096, 16, "SHA256"))
# more than one block, less than one block, and a key longer than the
# block of the HMAC (64 bytes)
puts hex(kdf("p", "s", 3, 100, "SHA256"))
puts hex(kdf("p", "s", 3, 5, "sha256"))
puts hex(kdf("k" * 100, "s" * 100, 10, 33, "SHA256"))
puts hex(kdf("", "", 2, 32, "SHA256"))
puts hex(kdf("password", "salt", 3, 21, OpenSSL::Digest.new("SHA1")))

key = kdf("password", "salt", 2, 32, "SHA256")
p key.encoding, key.bytesize, key.frozen?
puts hex(OpenSSL::PKCS5.pbkdf2_hmac("password", "salt", 4096, 32, "SHA256"))

begin
  kdf("password", "salt", 0, 32, "SHA256")
rescue => e
  p e.class
end
