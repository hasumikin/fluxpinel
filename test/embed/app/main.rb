# A file of a program in the binary (EMBED_EXTRA=app=test/embed/app): it
# reads the other files of the directory by the three calls.
require_relative "sub/helper"
require "/@fluxpinel/app/test/inner"
load "/@fluxpinel/app/sub/../sub/again.rb"

module EmbedApp
  def self.report
    [Helper.name_of(__FILE__), Inner::VALUE, AGAIN_COUNT]
  end
end
