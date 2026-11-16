# A method_missing hook that waits and then calls super: the error is the
# one of the miss of its own thread (NameError for a bare name,
# NoMethodError for a call with a receiver), also when the hook of another
# thread runs in the wait.
class Waiter
  def method_missing(name, *args)
    sleep 0.0005
    super
  end
end

count = Hash.new(0)
threads = [0, 1].map do |i|
  Thread.new do
    30.times do
      w = Waiter.new
      begin
        i == 0 ? w.instance_eval { nosuch } : w.nosuch
      rescue NameError => e
        count[[i, e.class]] += 1
      end
    end
  end
end
threads.each(&:join)
p count.sort_by { |k, _| k.to_s }
