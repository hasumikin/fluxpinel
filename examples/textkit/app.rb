# textkit -- a small text tool. Spinel compiles this file as the kernel of
# the host build, and the filters come from plugins that Lupe runs.
#
# Textkit.main reads the input, loads each plugin, runs the filter of each
# one on the lines, and counts the words. The words and the counts are AOT
# code; a plugin can call Textkit.words too.

require_relative "../../kernel/lupe"

module Textkit
  # the words of a line: lower case, letters and apostrophes only
  def self.words(line) = line.downcase.scan(/[a-z']+/)

  # the most frequent words, as [word, count] pairs
  def self.top(lines, n)
    counts = Hash.new(0)
    lines.each { |line| words(line).each { |w| counts[w] += 1 } }
    counts.sort_by { |w, c| [-c, w] }.first(n)
  end

  def self.main(args)
    lines = File.readlines(args[0], chomp: true)
    Dir.glob(File.join(args[1], "*.rb")).sort.each do |path|
      plugin = Lupe.load(path)
      lines = Lupe.call(plugin, "filter", [lines])
      puts "#{Lupe.call(plugin, "name", [])}: #{lines.size} lines"
    end
    puts "--"
    lines.each { |line| puts line }
    puts "--"
    top(lines, 3).each { |w, c| puts "#{w}: #{c}" }
  end
end
