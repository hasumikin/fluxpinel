# The methods of String that spinel emits from codegen code (string.c),
# and the Enumerators of String and Array with no block.

def t(label)
  r = yield
  puts "#{label}: #{r.inspect}"
rescue => e
  puts "#{label}: #{e.class} #{e.message}"
end

s = "Hello, World"
t("dup") { [s.dup, s.dup.equal?(s), s.clone] }
t("[]") { [s[1], s[1..3], s["World"], s["xyz"], s[/W\w+/], s[1, 3], s.slice(2), s.slice(2, 2)] }
t("upcase!") { x = +"abc"; [x.upcase!, x, "ABC".dup.upcase!] }
t("bang") { x = +" a\n"; [x.strip!, x.chomp!, x.reverse!, x] }
t("more bang") { x = +"aabbc"; [x.squeeze!, x.tr!("a", "z"), x.delete!("c"), x.succ!, x] }
t("to_i") { ["42abc".to_i, "ff".to_i(16)] }
t("<<") { x = +"a"; x << "b" << 99 }
t("concat") { x = +"a"; [x.concat("b", "c"), x.concat(x)] }
t("prepend") { x = +"c"; x.prepend("a", "b") }
t("unpack") { ["\x01\x02".unpack1("C"), "\x01\x02".unpack("C*")] }
t("index") { [s.index("o"), s.index("o", 5), s.rindex("o"), s.index("z"), s.byteindex("o")] }
t("bytes") { x = +"abc"; [s.getbyte(0), x.setbyte(0, 65), x] }
t("[]=") { x = +"abcd"; x[0] = "Z"; x[1..2] = "-"; x[0, 1] = "Q"; x["d"] = "D"; x }
t("-@") { [-"abc", (-"abc").frozen?] }
t("replace") { x = +"abc"; [x.replace("q"), x] }
t("insert") { x = +"abc"; x.insert(1, "_") }
t("slice!") { x = +"abcde"; [x.slice!(1), x.slice!(1..2), x.slice!("e"), x] }
t("compare") { ["b".between?("a", "c"), "z".clamp("a", "c")] }
t("bytesplice") { x = +"abc"; x.bytesplice(0, 1, "Z") }
t("partition") { [s.partition(", "), s.rpartition("o")] }
t("lines") { ["a\nb\n".lines, "a,b".lines(",")] }
t("set ops") { [s.count("lo"), s.count("lo", "o"), "hello".delete("l", "lo"), "aabb".squeeze("a")] }
t("clear") { x = +"abc"; [x.clear, x] }
t("split") { ["a,b,c".split(",", 2), "a b".split(" ")] }
t("with") { [s.start_with?("x", "He"), s.end_with?("x", "ld"), "abc".chomp("c")] }
t("ascii") { ["abc".upcase(:ascii), "ABC".downcase(:ascii)] }
t("pad") { ["a".center(5, "*"), "a".ljust(3, "."), "a".rjust(3, ".")] }
t("encoding") { ["abc".force_encoding("BINARY").encoding, "abc".encode("UTF-8")] }
t("each") { [s.each_char.to_a.size, "a\nb".each_line.to_a, "ab".each_byte.to_a] }
t("object_id") { s.object_id == s.object_id }
t("freeze") do
  x = +"abc"
  x.freeze
  [x.frozen?, (x << "d" rescue $!.class), (x.upcase! rescue $!.class)]
end
t("array each") { [[1, 2].each.to_a, [5, 6].each_with_index.to_a] }
p "abc".each_char, [1, 2].each, [1, 2].each_with_index
