%{
#include "enigma.h"
#include "cfg-parser.h"
#define YY_DECL int yylex(machine *m)
/*
	cfg-parser.l
	Lexer for enigma.y
	© 2015 Helge Hafting, licenced under the GPL

 UTF8 parsing notes:
A UTF-8 sequence can only start with values 0xC0 or greater, so that's what I used for checking the start of an identifier. Within an identifier, you would also want to allow characters >= 0x80, which is the range of UTF-8 continuation bytes.

Max 4 byte i et unicode-tegn

Klassifisering av enkelttegn:
iswalnum() "is wide alphanumeric"
iswalpha() alphabetic
iswcntrl() control character
iswdigit() digit
iswgraph() printable but not some sort of space (uses ink...)
iswprint() som over, men også space
iswlower() "små bokstaver"
iswpunct() punktum, komma, og mye annet
iswspace() mellomrom av mange slag
iswupper() "STORE BOKSTAVER"
iswblank() test for standard space/tab



*/
%}

INT		[0-9]+
SPC		[ \t]+

%%
[ \t]*#.*\n 	{ /* Comment lines */ }
{SPC}					{ /* white space */ }
\n						{ /* linefeed, for linenumber tracking with yylineno */ }

\"[^"\n]*["\n]  |
\'[^'\n]*['\n]	{
							  /* Quoted strings, may have spaces but not linefeeds */
								if (yytext[yyleng-1] != yytext[0]) yyerror(m, "unterminated string.");
								yytext[yyleng-1] = 0;
								yylval.ws = mbstowcsdup(yytext + 1);
								if (!yylval.ws) yyerror(m, "invalid string, possibly invalid utf-8/unicode"); 

								return WSTRING; /* wide string without the quotes */
							}
«.*»					{
								/* UTF-8 guillemets uses two bytes, cut accordingly.
                   The string may contain further guillemets */
								yytext[yyleng - 2] = 0;
								yylval.ws = mbstowcsdup(yytext + 2);
								if (!yylval.ws) yyerror(m, "invalid string, possibly invalid utf-8/unicode");
								return WSTRING; /* wide string sans guillemets */
							}

{INT}					{ /* (positive) integers */
								yylval.i = atoi(yytext);
								return INTEGER;
							}

-							return *yytext; /* hyphen, for number ranges */

"ciphermachine"	return MACHINE;
"alphabet"			return ALPHABET;
"wheelslots"		return WHEELSLOTS;
"wheel"					return WHEEL;
"wiring"				return WIRING;
"encipher"			return ENCIPHER;
"decipher"			return DECIPHER;
"slots"					return SLOTS;
"slot"					return SLOT;
"fast"					return FAST;
"reverse"				return REVERSE;
"reflector"			return REFLECTOR;
"mapping"				return MAPPING;
"nonrotating"		return NONROTATING;
"notches"				return NOTCHES;
"notch"					return NOTCHES;
"push"					return PUSH;
"rewirable"			return REWIRABLE;
"plugboard"			return PLUGBOARD;
"for"   				return FOR;
"pins"					return PINS;
"pin"						return PINS;
"offset"				return OFFSET;
"blocks"				return BLOCKS;
"block"					return BLOCKS;
"stepping"			return STEPPING;

[^[:cntrl:][:space:]]+	{  /*	Names. Supposed to be printable characters only. 
															But flex don't know unicode too well, so anything
															except whitespace or ascii control characters  */
								yylval.ws = mbstowcsdup(yytext);
								if (!yylval.ws) yyerror(m, "invalid unicode/utf-8 in wheel name");
								return NAME;
							}


%%
int yywrap(void) {
	return 1;
}
