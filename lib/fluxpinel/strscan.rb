# strscan.rb -- the constants StringScanner::Version and StringScanner::Id,
# and StringScanner#<< (concat).
#
# spinel's strscan package does not have them. Fluxpinel adds them when the
# package loads (src/load.c). A library reads Version to select code for an
# old strscan (rexml: `StringScanner::Version < "1.0.0"`). The value is the
# version of the strscan of CRuby 4.0, whose methods the package follows.
class StringScanner
  Version = "3.1.8"
  Id = "$Id$"

  # Appends str to the String of the scanner, which stays the same object,
  # and keeps the position. The scanner of the runtime holds the bytes of
  # its String, so the String goes in again after the append. That clears
  # the last match (matched, pre_match), which CRuby keeps.
  def <<(str)
    at = pos
    source = string
    source << str
    self.string = source
    self.pos = at
    self
  end
  alias concat <<
end
