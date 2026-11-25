# The module Warning (lib/fluxpinel/warning.rb): the categories, their
# flags, and Warning.warn (minitest/autorun sets Warning[:deprecated]).
p defined?(Warning), Warning.class
p Warning.categories
p Warning[:deprecated], Warning[:experimental], Warning[:performance]
p((Warning[:deprecated] = true), Warning[:deprecated])
p((Warning[:performance] = 1), Warning[:performance])
p((Warning[:experimental] = nil), Warning[:experimental])
begin
  Warning[:nope]
rescue ArgumentError => e
  puts e.message
end
begin
  Warning[:nope] = true
rescue ArgumentError => e
  puts e.message
end
begin
  Warning["deprecated"]
rescue TypeError => e
  puts e.class
end
p Warning.warn("to stderr\n")
begin
  Warning.warn(:sym)
rescue TypeError => e
  puts e.class
end
begin
  Warning.warn("x\n", category: :nope)
rescue ArgumentError => e
  puts e.message
end
p Warning.respond_to?(:warn), Warning.instance_methods.include?(:warn)
