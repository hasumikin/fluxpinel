# raises the first time it loads, and defines ONCE_BROKEN the second
$once_broken = ($once_broken || 0) + 1
raise "once_broken: not yet" if $once_broken == 1
ONCE_BROKEN = $once_broken
