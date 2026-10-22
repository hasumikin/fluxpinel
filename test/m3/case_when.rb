# case / when: === of each value, a splat of values, a case with no
# subject, and the value of the case.

def kind(x)
  case x
  when 0 then "zero"
  when 1, 2, 3 then "small"
  when 4..9 then "digit"
  when Float then "float"
  when String, Symbol then "text #{x}"
  when nil then "nil"
  when ->(v) { v.respond_to?(:each) } then "list"
  else "other"
  end
end

p [0, 2, 7, 1.5, "s", :sym, nil, [1], { a: 1 }, 100].map { |x| kind(x) }

# a Regexp sets the match globals, also for a Symbol subject
def finder(name)
  case name
  when /\Afind_by_(\w+)\z/ then [:find, $1.split("_and_")]
  when /\Agroup_by_(\w+)\z/ then [:group, $~[1]]
  end
end

p finder(:find_by_name_and_age), finder("group_by_path"), finder(:other)

# a splat of values
SMALL = [1, 2, 3]
def splat(x)
  case x
  when *SMALL then :small
  when 10, *[20, 30] then :round
  else :no
  end
end

p [2, 20, 10, 5].map { |x| splat(x) }

# no subject: the first true value
def sign(n)
  case
  when n < 0 then -1
  when n.zero? then 0
  else 1
  end
end

p [-5, 0, 3].map { |n| sign(n) }

# === of a class of the program
class Even
  def self.===(v) = v.is_a?(Integer) && v.even?
end

p [1, 2].map { |v| case v when Even then :even else :odd end }

# the subject runs once; a when with no body answers nil; no match and
# no else answers nil
$n = 0
def subject = ($n += 1)
p(case subject when 1 then end, case 5 when 6 then :x end, $n)

# return and break from a when
def first_big(list)
  list.each do |v|
    case v
    when 100.. then return v
    end
  end
  nil
end

p first_big([1, 150, 200]), first_big([1])
p([3, 4, 5].each { |v| case v when 4 then break v * 10 end })
