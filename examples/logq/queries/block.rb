# The same question as the "API errors" report of slow.rb, with a block
# that runs once for each row. It works, but each row enters Fluxpinel.

report "API errors (block)" do
  where { |row| row["path"].start_with?("/api/") && row["status"] >= 400 }.size
end
