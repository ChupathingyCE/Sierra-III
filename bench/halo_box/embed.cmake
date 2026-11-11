# cmake -DIN=<file> -DOUT=<file.c> -P embed.cmake: IN's bytes as
# embedded_boxk[] and its length as embedded_boxk_len (what "xxd -i" would
# make; the runners may not have xxd)
file(READ "${IN}" hex HEX)
string(LENGTH "${hex}" digits)
math(EXPR size "${digits} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
file(WRITE "${OUT}" "unsigned char embedded_boxk[] = {${bytes}};\nunsigned int embedded_boxk_len = ${size};\n")
