def sign(n)
  if n > 0
    "positive"
  elsif n < 0
    "negative"
  else
    "zero"
  end
end

p sign(3), sign(-2), sign(0)

i = 0
sum = 0
while i < 10
  i += 1
  next if i % 3 == 0
  sum += i
  break if sum > 20
end
p i, sum

j = 0
j += 1 until j >= 4
p j

p(1 && 2)
p(nil && 2)
p(nil || "default")
p(false || nil)

unless 1 > 2
  puts "unless"
end

r = while false
end
p r
