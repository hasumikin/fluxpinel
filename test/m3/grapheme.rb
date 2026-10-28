# \X and String#grapheme_clusters: combining marks, emoji sequences,
# flags, Hangul jamo and CR LF.

samples = [
  "éx", "\u{1F468}‍\u{1F469}‍\u{1F467}!", "\u{1F1EF}\u{1F1F5}\u{1F1FA}\u{1F1F8}", "a\r\nb",
  "각ᄀ", "\u{1F44D}\u{1F3FD}a", "\u{1F468}‍\u{1F4BB}", "é", "a‍b",
  "①️", "abc", "", "\u{1F1EF}\u{1F1F5}\u{1F1FA}", "x̀́y"
]
samples.each do |s|
  p s.scan(/\X/).map(&:size)
  p s.grapheme_clusters == s.scan(/\X/), s.each_grapheme_cluster.to_a == s.grapheme_clusters
end

# a quantifier binds the whole cluster
p "áé".scan(/\X+/).size, "áé" =~ /\A\X\X\z/
# inside a character class it is the letter
p "X".match?(/[\X]/), "a".match?(/[\X]/)
# an escaped backslash before an X is not the escape
p "\\X".match?(/\\X/), "a".match?(/\\X/)
p Regexp.new('\X').match("é")[0].size, /#{'\X'}/.match("é")[0].size

n = 0
"\u{1F1EF}\u{1F1F5}ab".each_grapheme_cluster { |g| n += g.size }
p n
