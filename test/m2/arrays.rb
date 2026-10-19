a = [3, 1, 2]
p a[0], a[-1], a[5]
a[1] = "one"
p a
a << 4
a.push(5)
p a, a.length, a.size
p a.first, a.last
p a.reverse
p a.include?(4)
p [1, 2, 3].sum

sieve = []
i = 0
while i <= 30
  sieve << true
  i += 1
end
primes = []
n = 2
while n <= 30
  if sieve[n]
    primes << n
    m = n * n
    while m <= 30
      sieve[m] = false
      m += n
    end
  end
  n += 1
end
p primes
