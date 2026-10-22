# Writes an access log of N lines (default 1000000) for logq to stdout.
# The same N gives the same log: a linear congruential generator picks
# each field.
#
#   ruby examples/logq/gen.rb 1000000 > access.log

n = (ARGV[0] || 1_000_000).to_i
seed = 42
rand = lambda do |k|
  seed = (seed * 1_103_515_245 + 12_345) % 2_147_483_648
  (seed >> 16) % k   # the low bits of the generator repeat in short cycles
end

paths = %w[/ /login /logout /api/users /api/orders /api/items /search /cart /static/app.js /static/app.css]
verbs = %w[GET GET GET POST GET PUT DELETE GET]
t0 = 1_792_000_000
n.times do |i|
  status = rand.(100) < 90 ? 200 : [301, 404, 404, 500, 502, 503][rand.(6)]
  duration = (rand.(1000) / 1000.0) * (status >= 500 ? 3 : 1)
  puts "#{Time.at(t0 + i).utc.strftime("%Y-%m-%dT%H:%M:%S")} #{verbs[rand.(verbs.size)]} " \
       "#{paths[rand.(paths.size)]} #{status} #{duration.round(3)}"
end
