# The FFI of spinel: ffi_func and its kin, with libc and SQLite.
# .expected is the output of this program compiled by spinel.

module LibC
  ffi_func :strlen, [:str], :size_t
  ffi_func :abs, [:int], :int
  ffi_func :labs, [:long], :long
  ffi_func :atof, [:str], :double
  ffi_func :toupper, [:int], :int
  ffi_func :malloc, [:size_t], :ptr
  ffi_func :free, [:ptr], :void
  ffi_func :memset, [:ptr, :int, :size_t], :ptr
  ffi_func :getenv, [:str], :str
  ffi_buffer :scratch, 16
  ffi_write_u32 :put_u32, 4
  ffi_read_u32 :get_u32, 4
  ffi_read_u8 :get_u8, 0
  ffi_read_i8 :get_i8, 1
  ffi_const :ANSWER, 42
end

p LibC.strlen("hello, world")
p LibC.abs(-7), LibC.labs(-1234567890123)
p LibC.atof("2.5")
p LibC.toupper(97).chr
p LibC::ANSWER
p LibC.getenv("LUPE_SURELY_UNSET_VARIABLE")

buf = LibC.scratch
p LibC.put_u32(buf, 305419896)
p LibC.get_u32(buf)
LibC.memset(buf, 255, 2)
p LibC.get_u8(buf), LibC.get_i8(buf)

m = LibC.malloc(32)
p m.nil?
LibC.free(m)

begin
  LibC.abs(nil)
rescue TypeError => e
  puts e.message
end

module SQL
  ffi_lib "sqlite3"
  ffi_const :OK, 0
  ffi_const :ROW, 100
  ffi_const :DONE, 101
  ffi_func :sqlite3_open, [:str, :ptr], :int
  ffi_func :sqlite3_close, [:ptr], :int
  ffi_func :sqlite3_exec, [:ptr, :str, :ptr, :ptr, :ptr], :int
  ffi_func :sqlite3_prepare_v2, [:ptr, :str, :int, :ptr, :ptr], :int
  ffi_func :sqlite3_step, [:ptr], :int, blocking: true
  ffi_func :sqlite3_column_int, [:ptr, :int], :int
  ffi_func :sqlite3_column_double, [:ptr, :int], :double
  ffi_func :sqlite3_column_text, [:ptr, :int], :str
  ffi_func :sqlite3_finalize, [:ptr], :int
  ffi_buffer :db_out, 8
  ffi_buffer :stmt_out, 8
  ffi_read_ptr :read_ptr, 0
end

p SQL.sqlite3_open(":memory:", SQL.db_out)
db = SQL.read_ptr(SQL.db_out)
p db.nil?
p SQL.sqlite3_exec(db, "create table t (n integer, x real, s text)", nil, nil, nil)
p SQL.sqlite3_exec(db, "insert into t values (1, 1.5, 'one'), (2, 2.5, 'two')", nil, nil, nil)
p SQL.sqlite3_prepare_v2(db, "select n, x, s from t order by n", -1, SQL.stmt_out, nil)
stmt = SQL.read_ptr(SQL.stmt_out)
rows = []
while SQL.sqlite3_step(stmt) == SQL::ROW
  rows << [SQL.sqlite3_column_int(stmt, 0), SQL.sqlite3_column_double(stmt, 1), SQL.sqlite3_column_text(stmt, 2)]
end
p rows
p SQL.sqlite3_finalize(stmt), SQL.sqlite3_close(db)
