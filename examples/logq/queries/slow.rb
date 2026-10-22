# A query of logq. Change it and run logq again: the binary stays the same.

report "slow 5xx" do
  where_status(500..599).slower_than(1.0).top(5, by: :duration)
end

report "POST by path" do
  where_verb("POST").count_by_path
end

report "API errors" do
  where_path(%r{\A/api/}).where_status(400..).size
end
