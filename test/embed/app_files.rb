# The files of a program in the binary (make test-embed builds with
# EMBED_EXTRA=app=test/embed/app): a load and a require of a path under
# /@fluxpinel/app read the table, a require_relative in such a file reads
# its neighbor, a directory named test is in, and a path with ".." is the
# file it names. No disk has these paths, so CRuby has no answer: the
# answer is app_files.rb.expected.
p load("/@fluxpinel/app/main.rb")
p EmbedApp.report
p require("/@fluxpinel/app/sub/helper"), require("/@fluxpinel/app/sub/../sub/helper.rb")
p load("/@fluxpinel/app/./sub/again.rb"), AGAIN_COUNT
p $LOADED_FEATURES.grep(/@fluxpinel\/app/).sort
begin
  require "/@fluxpinel/app/absent"
rescue LoadError => e
  puts e.message
end
begin
  load "/@fluxpinel/app/absent.rb"
rescue LoadError => e
  puts e.message
end
# a library of the binary beside the files of the program
require "json"
puts JSON.generate({ "ok" => true })
