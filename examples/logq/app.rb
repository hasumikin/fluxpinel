# logq -- an access log analyzer. Spinel compiles this file as the kernel
# of the host build. The queries come from Ruby files that Fluxpinel runs.
#
# The kernel keeps the log in columns, one Array for each field, and does
# each loop over the rows: the parse, the filters, the sort and the
# counts. A query holds a selection: an Array of row numbers. It calls the
# entries below, and each entry loops over the selection in AOT code.

require_relative "../../kernel/fluxpinel"

module Logq
  class Table
    attr_reader :verbs, :paths, :statuses, :durations

    def initialize
      @verbs = []
      @paths = []
      @statuses = []
      @durations = []
    end

    def size = @statuses.size

    # a line: "2026-10-22T10:00:00 GET /api/users 200 0.123"
    def add(line)
      f = line.split(" ")
      @verbs << f[1]
      @paths << f[2]
      @statuses << f[3].to_i
      @durations << f[4].to_f
    end

    def row(i) = { "verb" => @verbs[i], "path" => @paths[i], "status" => @statuses[i], "duration" => @durations[i] }
  end

  TABLE = Table.new

  def self.load(path)
    File.foreach(path) { |line| TABLE.add(line) }
    TABLE.size
  end

  # The entries take a selection that a query passes back: a comment of
  # inline RBS types each parameter that no AOT code calls with a value.

  # every row
  #: () -> Array[Integer]
  def self.all = (0...TABLE.size).to_a

  # the rows of sel whose field takes cond with ===: a value, a Range, a
  # Regexp or a class
  #: (Array[Integer], String, untyped) -> Array[Integer]
  def self.where(sel, field, cond)
    out = []
    case field
    when "verb" then sel.each { |i| out << i if cond === TABLE.verbs[i] }
    when "path" then sel.each { |i| out << i if cond === TABLE.paths[i] }
    when "status" then sel.each { |i| out << i if cond === TABLE.statuses[i] }
    when "duration" then sel.each { |i| out << i if cond === TABLE.durations[i] }
    else raise ArgumentError, "no field #{field}"
    end
    out
  end

  # the rows of sel whose duration is at least sec
  #: (Array[Integer], Float) -> Array[Integer]
  def self.slower(sel, sec) = sel.select { |i| TABLE.durations[i] >= sec }

  # the rows of sel for which the block of the query answers true: the
  # query runs once for each row
  #: (Array[Integer], untyped) -> Array[Integer]
  def self.where_block(sel, blk) = sel.select { |i| Fluxpinel.call(blk, "call", [TABLE.row(i)]) }

  # the n rows of sel with the largest value of a numeric field; the row
  # number breaks a tie, so that each Ruby gives the same rows
  #: (Array[Integer], Integer, String) -> Array[Hash[String, untyped]]
  def self.top(sel, n, field)
    col = field == "status" ? TABLE.statuses.map(&:to_f) : TABLE.durations
    sel.sort_by { |i| [-col[i], i] }.first(n).map { |i| TABLE.row(i) }
  end

  # the number of rows of sel for each value of field
  #: (Array[Integer], String) -> Hash[String, Integer]
  def self.count_by(sel, field)
    counts = Hash.new(0)
    case field
    when "verb" then sel.each { |i| counts[TABLE.verbs[i]] += 1 }
    when "path" then sel.each { |i| counts[TABLE.paths[i]] += 1 }
    when "status" then sel.each { |i| counts[TABLE.statuses[i].to_s] += 1 }
    else raise ArgumentError, "no field #{field}"
    end
    counts.sort_by { |k, c| [-c, k] }.first(10).to_h
  end

  def self.print_result(v)
    case v
    when Array then v.each { |e| puts "  #{e}" }
    when Hash then v.each { |k, c| puts "  #{k}: #{c}" }
    else puts "  #{v}"
    end
  end

  # logq LOG QUERY: load the log, then the DSL and the query, and print
  # each report
  def self.main(args)
    n = load(args[0])
    puts "#{n} rows"
    dsl = Fluxpinel.load(File.join(__dir__, "dsl.rb"))
    Fluxpinel.load(args[1])
    Fluxpinel.call(dsl, "run", []).each do |title, result|
      puts "== #{title}"
      print_result(result)
    end
  end
end
