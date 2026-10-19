# Many short-lived Strings and Arrays across nested calls. A missing root
# in the walker shows up here as a wrong value or a crash.
def label(i)
  "item-" + i.to_s + "-" + (i * 2).to_s
end

def build(n)
  out = []
  i = 0
  while i < n
    out << [label(i), i, label(i + 1)]
    i += 1
  end
  out
end

total = 0
keep = []
round = 0
while round < 30
  rows = build(2000)
  total += rows.length
  keep << rows[round * 7]
  round += 1
end
p total
p keep.first
p keep.last
p keep.length
