# The message of an arity error. With required keywords, CRuby adds all of
# them to the message, given or not.

def t(label)
  r = yield
  puts "#{label}: #{r.inspect}"
rescue => e
  puts "#{label}: #{e.class} #{e.message}"
end

def req(a, k:) = [a, k]
def opt(a, k: 1) = [a, k]
def many(a, b = 2, k:, j:, m: 3) = [a, b, k, j, m]
def rest(a, *r, k:) = [a, r, k]

t("one keyword") { req }
t("too many") { req(1, 2, k: 3) }
t("keyword given") { req(k: 1) }
t("optional keyword") { opt }
t("keywords") { many }
t("optional positional") { many(1, 2, 3, k: 1, j: 2) }
t("rest") { rest(k: 1) }
t("lambda") { ->(x, k:) { x }.call }
t("define_method") do
  Object.define_method(:dm) { |x, k:, j: 0| x }
  dm
end
t("missing keyword") { req(1) }
t("ok") { many(1, k: 2, j: 3) }
