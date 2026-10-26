x3dfix: x3dfix.c
	clang -O2 -Wall -o x3dfix x3dfix.c -framework IOKit -framework CoreFoundation

clean:
	rm -f x3dfix
