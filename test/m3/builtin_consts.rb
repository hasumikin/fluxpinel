# The constants of the builtin classes Float, Math, IO, File and Regexp

p Float::MAX, Float::MIN, Float::EPSILON, Float::INFINITY, -Float::INFINITY
p Float::NAN.nan?, Float::INFINITY.infinite?, 1.0 / 0 == Float::INFINITY
p Float::DIG, Float::MANT_DIG, Float::RADIX, Float::MAX_EXP, Float::MIN_EXP
p Float::MAX_10_EXP, Float::MIN_10_EXP
p Math::PI, Math::E, Math::PI * 2 > 6.28

p IO::SEEK_SET, IO::SEEK_CUR, IO::SEEK_END, File::SEEK_END
p File::SEPARATOR, File::PATH_SEPARATOR, File::ALT_SEPARATOR, File::NULL
p File::SEPARATOR.frozen?, File::NULL.frozen?
p File::RDONLY, File::WRONLY, File::RDWR, File::CREAT, File::EXCL, File::TRUNC
p File::APPEND, File::NONBLOCK, File::NOFOLLOW, File::NOCTTY, File::BINARY
p File::LOCK_SH, File::LOCK_EX, File::LOCK_UN, File::LOCK_NB
p File::FNM_NOESCAPE, File::FNM_PATHNAME, File::FNM_DOTMATCH, File::FNM_CASEFOLD
p File::FNM_EXTGLOB, File::FNM_SYSCASE
p Regexp::IGNORECASE, Regexp::EXTENDED, Regexp::MULTILINE
p Regexp::FIXEDENCODING, Regexp::NOENCODING

# a constant of a builtin class that does not exist
begin
  Float::NOPE
rescue NameError => e
  p e.message
end

# a class of the program can still define its own of the name
class Grid
  INFINITY = :mine
  def self.inf = INFINITY
end
p Grid.inf, Grid::INFINITY
