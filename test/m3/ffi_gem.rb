# The ffi gem (the ffi package of spinel): attach_function and
# attach_variable define methods on the module.

require "ffi"

module M
  extend FFI::Library
  ffi_lib "libc.so.6"
  attach_function :strlen, [:string], :size_t
  attach_function :my_abs, :abs, [:int], :int
  attach_variable :stdin, :pointer
end

p M.strlen("abc"), M.my_abs(-3), M.stdin.null?
p M.respond_to?(:strlen), M.respond_to?(:nope)
p M.strlen("hello"), M::strlen("hi")
begin
  M.nope
rescue NoMethodError => e
  puts e.message
end
