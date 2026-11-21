# cmake -DIN=<file> -DOUT=<file.c> -P embed.cmake: what mcidclan's build makes with
# "xxd -i" and two seds (the runners may not have xxd): IN's bytes as
# embedded_kcall[] and its length as embedded_kcall_len
file(READ "${IN}" hex HEX)
string(LENGTH "${hex}" digits)
math(EXPR size "${digits} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
file(WRITE "${OUT}" "unsigned char embedded_kcall[] = {${bytes}};\nunsigned int embedded_kcall_len = ${size};\n")
