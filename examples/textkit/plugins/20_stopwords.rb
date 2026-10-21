# Removes the stop words. Textkit.words is the AOT code of the tool.

class Stopwords
  WORDS = %w[a an and the of to is in it].freeze

  def name = "stopwords"
  def filter(lines) = lines.map { |line| Textkit.words(line).reject { |w| WORDS.include?(w) }.join(" ") }
end

Stopwords.new
