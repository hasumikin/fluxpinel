# c_box.rb -- the C expression that boxes a value of a C type of spinel
# into an sp_RbVal. tools/entry_gen.rb and tools/ivar_gen.rb share it; the
# helpers fx_entry_str, fx_entry_oint and fx_entry_hash_box are in the
# output of entry_gen.rb.

module CBox
  RET = { "sp_RbVal" => "%s", "sp_int" => "sp_box_int(%s)", "sp_float" => "sp_box_float(%s)",
          "sp_bool" => "sp_box_bool(%s)", "sp_sym" => "sp_box_sym(%s)", "const char *" => "fx_entry_str(%s)",
          "sp_oint" => "fx_entry_oint(%s)",
          "sp_PolyArray *" => "sp_box_poly_array(%s)", "sp_IntArray *" => "sp_box_int_array(%s)",
          "sp_FloatArray *" => "sp_box_float_array(%s)", "sp_StrArray *" => "sp_box_str_array(%s)" }.freeze

  # The class id of a Hash type: sp_PolyPolyHash * -> SP_BUILTIN_POLY_POLY_HASH,
  # or nil for any other type.
  def self.hash_id(type)
    return nil unless type =~ /\Asp_(\w+)Hash \*\z/
    "SP_BUILTIN_#{$1.gsub(/(?<!\A)([A-Z])/, '_\1').upcase}_HASH"
  end

  # The box of expr, a value of type, or nil when there is none. A pointer
  # of a known kind boxes a NULL pointer as nil.
  def self.box(type, expr)
    if RET.key?(type)
      boxed = format(RET[type], expr)
      type.end_with?("*") && type != "const char *" ? "(#{expr} ? #{boxed} : sp_box_nil())" : boxed
    elsif hash_id(type)
      "fx_entry_hash_box(#{expr}, #{hash_id(type)})"
    end
  end
end
