# alias $NEW $OLD: the two names are one global variable, read and
# written through either; an alias of a special global reads the special.
alias $new $old
p $new, $old, defined?($new), defined?($old)
$old = 1
p $new, defined?($new)
$new = 2
p $old
$new += 1
p $old
$new ||= 9
p $old
alias $third $new
$third = 3
p $old, $new
p global_variables.include?(:$new), global_variables.include?(:$third)
$a, $b = 4, 5
alias $aa $a
p $aa

# the specials: the exception of a rescue, the last match of the frame,
# the status of the last child, the pid, the program, the streams
alias $err $!
begin
  raise "boom"
rescue
  p $err.message, $err.equal?($!)
end
p $err
alias $st $?
system("true")
p $st.success?
alias $pid $$
p $pid == Process.pid
alias $out $stdout
$out.puts "via alias"
alias $in $stdin
p $in.equal?($stdin)
alias $prog $0
p $prog == $0
alias $m $~
alias $whole $&
alias $before $`
alias $after $'
alias $last $+
"abc" =~ /b/
p $m[0], $whole, $before, $after, $last
def inner
  "xyz" =~ /(y)/
  [$m[0], $whole, $last]
end
p inner, $m[0], $whole
p defined?($whole)
"abc" =~ /z/
p $m, $whole, defined?($whole)
alias $sep $/
p $sep

# a read-only special stays read-only under its new name
begin
  alias $ro $$
  $ro = 5
rescue NameError => e
  p e.message
end
