$autoload_thrower_runs = ($autoload_thrower_runs || 0) + 1
throw :out, :thrown if $autoload_thrower_runs == 1
module Geo
  class Thrower; end
end
