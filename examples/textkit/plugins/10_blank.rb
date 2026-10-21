# Removes blank lines and the comment lines ("# ...").

class Blank
  def name = "blank"
  def filter(lines) = lines.reject { |line| line.strip.empty? || line.start_with?("#") }
end

Blank.new
