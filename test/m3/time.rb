# The class methods of Time and the methods of a Time, in UTC and fixed
# offsets, so that the output does not depend on the local zone

t = Time.at(0).utc
p t, t.year, t.month, t.hour, t.to_i, t.utc?, t.zone
u = Time.utc(2020, 3, 4, 5, 6, 7)
p u, u.to_s, u.wday, u.yday, u.strftime("%Y-%m-%d %H:%M:%S"), u.to_a
p Time.utc(2020, "mar", 4), Time.gm(2000), Time.utc(2020, 1, 2, 3, 4, 5.5).nsec
p Time.utc(2020, 1, 2, 3, 4, 5, 123456).usec, Time.utc(7, 6, 5, 4, 3, 2020, nil, nil, false, nil)
p Time.at(1, 500, :millisecond).nsec, Time.at(1.5).to_f, Time.at(1, 7).usec
p Time.at(100, in: "+09:00").hour, Time.at(100, in: "+09:00").utc_offset
p Time.new(2020, 1, 2, 3, 4, 5, "+09:00"), Time.new(2020, 1, 2, in: "Z"), Time.new(2020, 1, 2, 3, 4, 5, 3600)
p Time.now.class, Time.now(in: "+01:00").utc_offset, Time.new.is_a?(Time)

# arithmetic and comparison
p u + 1, u - 1.5, u - Time.utc(2020, 3, 4)
p u == Time.utc(2020, 3, 4, 5, 6, 7), u.eql?(u + 0), u < u + 1, u <=> u - 1, u <=> 1
p u.between?(u - 1, u + 1), u.clamp(u + 1, u + 2) == u + 1, [u + 2, u, u + 1].max == u + 2
p u.hash == Time.utc(2020, 3, 4, 5, 6, 7).hash, Time.at(u) == u, Time.at(u).equal?(u), u.is_a?(Comparable)

# the zone: utc and localtime change the receiver, the get* forms do not
p u.getlocal("+02:00"), u.round, u.to_r, u.iso8601, u.deconstruct_keys([:year, :hour])
l = Time.utc(2020, 1, 1)
p l.localtime("+03:00"), l, l.utc, l

[-> { Time.at("x") }, -> { u + u }, -> { u < 1 }, -> { u.strftime(7) }].each do |f|
  f.call
rescue TypeError, ArgumentError => e
  p e.class, e.message
end
