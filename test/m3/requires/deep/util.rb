# A file in a subdirectory: require_relative resolves from here.
module Util
  def self.double(x) = x * 2

  def self.where = __dir__.end_with?("/deep")
end

require_relative "../shapes"
puts "util loaded at line #{__LINE__}"
