enigma: Makefile enigma.c enigma.h cfg-parser.c cfg-parser.h cfg-lexer.c 
	gcc -march=native -O2 -o enigma -std=gnu11 enigma.c cfg-parser.c cfg-lexer.c -lncurses

curs-test: Makefile curs-test.c
	gcc -std=gnu11 -O2 -o curs-test curs-test.c -lncurses

cfg-parser.c: cfg-parser.y
	bison --defines --output=cfg-parser.c cfg-parser.y

cfg-lexer.c: cfg-lexer.l
	flex --outfile=cfg-lexer.c --yylineno cfg-lexer.l

clean:
	rm -f enigma cfg-lexer.c cfg-parser.c cfg-parser.h

