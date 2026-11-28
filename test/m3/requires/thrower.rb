$thrower_runs = ($thrower_runs || 0) + 1
throw :out, :thrown if $thrower_runs == 1
THROWER = $thrower_runs
