# ffi_callback: a Ruby method as a C function pointer.
# .expected is the output of this program compiled by spinel.

module L
  ffi_source <<~C
    #include <stdlib.h>
  C
  ffi_callback :cmp, [:ptr, :ptr], :int
  ffi_func :qsort, [:int_array, :size_t, :size_t, :cmp], :void
  ffi_read_i64 :val, 0
end

$calls = 0
def cmp(a, b)
  $calls += 1
  L.val(a) <=> L.val(b)
end

nums = [5, 3, 9, 1, 7]
L.qsort(nums, nums.size, 8, method(:cmp))
puts "sorted by a method, #{$calls > 0} comparisons"

def rcmp(a, b)
  $calls += 1
  L.val(b) <=> L.val(a)
end
$calls = 0
L.qsort(nums, nums.size, 8, method(:rcmp))
puts "sorted again, #{$calls > 0} comparisons"
