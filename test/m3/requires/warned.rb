# A file with warnings of the parser (parser_warnings.rb): two by default,
# and one more under -w.
WARNED_HASH = { a: 1, a: 2 }
x = 1
if x = 2
  WARNED_BRANCH = true
end
def warned_method
  return 1
  2
end
