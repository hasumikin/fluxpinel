# openssl.rb -- OpenSSL::KDF.pbkdf2_hmac with its loop in C.
#
# The openssl package of spinel has PBKDF2 in Ruby over the HMAC of the
# runtime (packages/openssl/openssl/kdf.rb). AOT compiles the loop. Under
# the walker 100,000 iterations take seconds, and the walker runs no
# other thread in that time. Fluxpinel puts this method in its place when
# the package loads (src/load.c). The loop is fx_pbkdf2_hmac (src/ffi.c),
# over the same HMAC. The checks, the errors and the answer are those of
# the package. OpenSSL::PKCS5.pbkdf2_hmac calls this method.
module OpenSSL
  module KDF
    module Native
      native_func :pbkdf2_hmac, [:string, :string, :int, :int, :int], :string, "fx_pbkdf2_hmac"
    end

    def self.pbkdf2_hmac(pass, salt:, iterations:, length:, hash:)
      algo = hash.is_a?(Digest) ? hash.name : hash.to_s.upcase
      sha1 = case algo
             when "SHA256" then 0
             when "SHA1"   then 1
             else raise Digest::DigestError, "unsupported digest algorithm: #{hash}"
             end
      raise KDFError, "iterations must be positive" if iterations <= 0
      return "".b if length <= 0
      Native.pbkdf2_hmac(pass, salt, iterations, length, sha1).b
    end
  end
end
