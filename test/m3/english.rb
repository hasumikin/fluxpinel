# require "English": the English names of the special globals.
require "English"
system("true")
p $CHILD_STATUS.success?, $PID == Process.pid, $PROCESS_ID == $$
begin
  raise ArgumentError, "bad"
rescue ArgumentError
  p $ERROR_INFO.message, $ERROR_INFO.class
end
"the quick fox" =~ /qu(i)ck/
p $MATCH, $PREMATCH, $POSTMATCH, $LAST_PAREN_MATCH, $LAST_MATCH_INFO[1]
p $INPUT_RECORD_SEPARATOR, $RS
p $OUTPUT_FIELD_SEPARATOR, $OUTPUT_RECORD_SEPARATOR
p require("English")
