# Fiddle::Importer: a module that extends it binds C functions of a library
# with extern, and calls them by name. The fiddle package of spinel keeps
# the functions it binds in a table, so the call goes through that table.
require "fiddle/import"

module LibC
  extend Fiddle::Importer
  dlload Fiddle::Handle::DEFAULT
  extern "int abs(int)"
  extern "double fabs(double)"
  extern "size_t strlen(const char *)"
  extern "int toupper(int)"
end

p LibC.abs(-5)
p LibC.fabs(-2.5)
p LibC.strlen("hello, world")
p LibC.toupper(97).chr

# an unbound name is still a NoMethodError
begin
  LibC.no_such_function(1)
rescue NoMethodError => e
  p e.name
end

# a second module keeps its own table
module LibC2
  extend Fiddle::Importer
  dlload Fiddle::Handle::DEFAULT
  extern "long labs(long)"
end
p LibC2.labs(-1234567890123)
begin
  LibC2.abs(-1)
rescue NoMethodError => e
  p e.name
end
