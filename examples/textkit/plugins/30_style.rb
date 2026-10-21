# Applies a String method to each line, by a name that a rule gives: a
# line with a question takes :upcase, any other line :capitalize.

class Style
  RULES = [[/\?|why|how/, :upcase], [//, :capitalize]].freeze

  def name = "style"

  def filter(lines)
    lines.map { |line| line.send(RULES.find { |re, _| line.match?(re) }.last) }
  end
end

Style.new
