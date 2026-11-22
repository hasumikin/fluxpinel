# min, max, minmax and first(n) of a Range of Symbols answer Symbols, as
# CRuby's do; those of a String Range answer Strings.
r = :a..:c
p r.min, r.max, r.minmax, r.first(2), r.first(0), r.first(9), r.last(2)
p (:a...:c).max, (:a...:c).min, (:a...:a).min, (:a...:a).max, (:c..:a).min, (:c..:a).max, (:c..:a).minmax
p (:y..:ab).to_a, (:y..:ab).max, (:y...:ab).max, (:y..:ab).min
p ("a".."c").min, ("a".."c").max, ("a".."c").first(2)
