# raises the first time it loads, and defines Geo::Flaky the second
$flaky = ($flaky || 0) + 1
raise "flaky: not yet" if $flaky == 1
module Geo
  Flaky = :loaded
end
