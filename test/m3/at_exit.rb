# at_exit: the hooks run in the reverse order of their registration when
# the program ends, a hook can register another, and a hook that exits
# decides the exit status

x = 1
at_exit { puts "registered first, runs last #{x}" }
p(at_exit { puts "second" }.class)
def register = at_exit { puts "from a method" }
register
at_exit do
  at_exit { puts "registered in a hook" }
  puts "outer"
end
at_exit do
  puts "exits with 3"
  exit 3
end
x = 2
puts "main ends"
