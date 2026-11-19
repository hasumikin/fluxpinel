# frozen_string_literal: true
# The comment at the top of the file freezes each plain String literal of
# the file. A String with an interpolation, String.new, +"" and dup are
# not frozen.

a = "lit"
p a.frozen?, "x#{a}".frozen?, String.new("q").frozen?, (+"u").frozen?, "lit".dup.frozen?
p (begin; a << "x"; rescue => e; e.class; end)
def m = "in method"
p m.frozen?, 'single'.frozen?, %w[a b].first.frozen?
h = <<~H
  heredoc
H
p h.frozen?, [a, "b"].map(&:frozen?), { "k" => "v" }.keys.first.frozen?
b = a.dup
b << "!"
p b, a
