# The query DSL of logq. Fluxpinel runs this file at each start, so a
# change here needs no build.
#
# A query is a chain on a Query, which holds a selection (an Array of row
# numbers). Each step calls one entry of the kernel, and the kernel loops
# over the rows. A step with no method of its own, such as where_status
# or count_by_path, goes to method_missing, which defines the method on
# its first call.

class Query
  def initialize(sel) = @sel = sel

  def size = @sel.size
  def slower_than(sec) = Query.new(Logq.slower(@sel, sec))
  def where(&blk) = Query.new(Logq.where_block(@sel, blk))
  def top(n, by:) = Logq.top(@sel, n, by.to_s)

  def method_missing(name, *args)
    case name
    when /\Awhere_(\w+)\z/
      field = $1
      self.class.define_method(name) { |cond| Query.new(Logq.where(@sel, field, cond)) }
    when /\Acount_by_(\w+)\z/
      field = $1
      self.class.define_method(name) { Logq.count_by(@sel, field) }
    else
      return super
    end
    send(name, *args)
  end

  def respond_to_missing?(name, include_all = false) = name.match?(/\A(where|count_by)_\w+\z/) || super
end

class Reports
  def initialize = @list = []
  def add(title, blk) = @list << [title, blk]

  # the title and the result of each report; a Query answers its size
  def run
    @list.map do |title, blk|
      result = Query.new(Logq.all).instance_eval(&blk)
      [title, result.is_a?(Query) ? "#{result.size} rows" : result]
    end
  end
end

REPORTS = Reports.new

def report(title, &blk) = REPORTS.add(title, blk)

REPORTS
