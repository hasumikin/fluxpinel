# strscan.rb -- the constants StringScanner::Version and StringScanner::Id.
#
# spinel's strscan package does not have them. Fluxpinel adds them when the
# package loads (src/load.c). A library reads Version to select code for an
# old strscan (rexml: `StringScanner::Version < "1.0.0"`). The value is the
# version of the strscan of CRuby 4.0, whose methods the package follows.
class StringScanner
  Version = "3.1.8"
  Id = "$Id$"
end
