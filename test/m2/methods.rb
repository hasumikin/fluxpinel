def fib(n)
  return n if n < 2
  fib(n - 1) + fib(n - 2)
end

p fib(20)

def fact(n)
  r = 1
  while n > 1
    r *= n
    n -= 1
  end
  r
end

p fact(20)

def greet(name, times)
  out = ""
  i = 0
  while i < times
    out += "hi #{name}! "
    i += 1
  end
  out
end

puts greet("fluxpinel", 3)

def two
  return 1, 2
end

p two

def nothing
end

p nothing

def tak(x, y, z)
  if y < x
    tak(tak(x - 1, y, z), tak(y - 1, z, x), tak(z - 1, x, y))
  else
    z
  end
end

p tak(12, 6, 0)
