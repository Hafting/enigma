/*
	enigma.c
	Enigma, generic open-source simulator for rotor-based encryption machines,
	such as the WWII enigma machines, or the Russian fialka 

	Can also do simpler stuff: 
		cæsar cipher:    a rotor machine with a single non-moving rotor/plugboard
		vigenere cipher: a rotor machine with a single fast rotor

	© 2015 Helge Hafting, licenced under the GPL
*/

/*
  Necessary define, or ncurses won't provide get_wch() with gcc -std=gnu11 
  or c99/c11 for that matter. Strangely, the #define is not necessary 
	without c99...
*/
#define _XOPEN_SOURCE_EXTENDED 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h> 
#include <wctype.h>
#include <wchar.h>
#include <ctype.h>

#include "enigma.h"
#include "cfg-parser.h"
extern FILE *yyin;

/* Give error message and abort immediately */
void feil(char *m) {
	wprintf(L"%s", m);
	exit(1);
}

/* Helper functions */

/* 
	Cuts off the first part of a wide string
  Zero out the rest, as other code needs that.                 
*/
void leftcut(wchar_t *ws, int cutaway, int keep) {
	wmemmove(ws, ws+cutaway, keep);
	wmemset(ws+keep, 0, cutaway);
}

/* Allocates a new wide string, fills it from a plain string
   returns the wide string, or 0 on failure. */
wchar_t *mbstowcsdup(const char *s) {
	int wlen = mbstowcs(0, s, 0);
	if (wlen < 0) return NULL;
	wchar_t *ws = malloc(sizeof(wchar_t) * (wlen + 1));
  if (!ws) return NULL;
  int r = mbstowcs(ws, s, wlen);
  if (r < 0) return NULL;
	ws[wlen] = 0;
  return ws;
}


/* Lookup a characters position in a wide string 
   A hash table lookup has lower complexity, but most 
   alphabets are so short that the linear lookup is faster than
   a good hash function anyway.  
   (Unless you make a rotor with the entire Chinese character set...)
*/
int lookup(const wchar_t wc, const wchar_t *ws) {
	const wchar_t *wx = ws;
	for(;;) {
		if (!*wx) return -1;
		if (*wx == wc) return wx - ws;
		++wx;
	} 
}

/* Functions used to build the machine description */

/* 
	populate the wheel slots with default wheels.
	if there are more slots than wheels, 
	use more of the same wheels
*/
void default_wheelorder(machine *m) {
	wheel *w = m->wheel_list;
	for (int i = m->wheelslots; i--;) {
		/* Skip any slots already equipped */
		if (m->slot[i].w) continue;
		/* Find a wheel that is valid for this slot */
		while (!w->allow_slot[i]) w = w->next_in_set;

		m->slot[i].w = w;
		w = w->next_in_set;
	}
	step_cleanup(m);
}


/* Make a 0-terminated linked list circular instead,
   for the benefit of the user interface */
void circularize(wheel *w0) {
	if (!w0) return;
	wheel *w;
	for (w = w0; w->next_in_set; w=w->next_in_set ) ;
	w->next_in_set = w0;
}

/* Open the machine description file & parse it */
machine *getdescr(char *filename) {
  FILE *f = fopen(filename, "r");
	if (!f) feil("file error\n");
  machine *m = calloc(1, sizeof(machine));

  //Parse the machine description
	yyin = f;
  yyparse(m);
  fclose(f);
	if (m->broken_description) {
		free(m);
		return NULL;
	}
  /* toss the dummy wheel */
	wheel *w = m->wheel_list;
	m->wheel_list = m->wheel_list->next_in_set;
	free(w);

	circularize(m->wheel_list);

	/* A machine with slots must have at least one code wheel */
	if (m->wheelslots && !m->wheel_list) feil("A machine with wheel slots cannot work with no code wheels.\n");

	/* set a default wheel order, so the machine is instantly useable */
	default_wheelorder(m);	

	return m;
}


/* Code wheel functions */

/* Set a code wheel to identity mapping. (Useful for clearing plugboards etc.) */
void identity_map(machine *m, wheel *w) {
	for (int i = m->alphabet_len; i--; ) w->encode[i] = w->decode[i] = i;
}


/* Lookup a wheel by name, or return 0 */
wheel *wheel_lookup(machine *m, wchar_t *name) {
	for (wheel *w = m->wheel_list; w; w = w->next_in_set) {
		if (!w->name) continue; /* A wheel not yet named */
		if (!(wcscmp(name, w->name))) return w;
	}
	return NULL;
}


/* 
	Workaround for a stupid bug. (curses 5.9, linux 64 bit, march 2015) 
	refresh() and friends do not display ANYTHING until after the first getch()
	Very dumb, as I want to set up the screen and THEN wait for input.
	curses is apparently INCAPABLE of doing this. It HAS TO have a single getch()
	before I can display anything in windows. Incredibly annoying.
	I don't want to start the UI with black screen until the first keypress, 
	so the ugly solution is a single nodelay getch() before the main loop :-(

  Even worse - the bug re-appear when I resize rxvt. 
	(run rxvt locally, ssh to another host to run this software)
	Whenever I make the rxvt smaller, the display is black until the first getch
	Doesn't happen if I extend the rxvt. How do such odd bugs happen :-(
*/

void curses_bug_workaround() {
	nodelay(stdscr,true);getch();nodelay(stdscr,false);
}

/*
	ncurses-based user interface
	Shows typed text as well as encoded/decoded text
	Shows the code wheels and how they move	
*/

/* Draw the rotating part of one wheel */
void draw_wheel_rot(machine *m, ui_info *ui, int slotnum) {
	wheelslot *sl = &m->slot[slotnum];
	int x = slotnum*4 + 2;
	/* The center column with numbers */
	wattrset(ui->w_wheels, ui->attr_wheel_plain);
	int y0 = (ui->layout == 0) ? m->wheelslots + 5 : m->longest_wheelname + 5;
	for (int i = 1; i <=2; ++i) {
		mvwprintw(ui->w_wheels, y0-i, x, "%lc", m->alphabet[(sl->rot+i) % m->alphabet_len]);
		mvwprintw(ui->w_wheels, y0+i, x, "%lc", m->alphabet[(m->alphabet_len+sl->rot-i) % m->alphabet_len]);
	}
	/* The one number showing the current setting */
	wattrset(ui->w_wheels, ui->attr_wheel_activ);
	mvwprintw(ui->w_wheels, y0, x, "%lc", m->alphabet[sl->rot]);
	/* Wheel sides. Possibly selected. Notch/block-pins, ring setting mark */
	wattrset(ui->w_wheels, slotnum == ui->chosen_wheel ? ui->attr_wheelside_activ : ui->attr_wheelside_plain);
	int al = m->alphabet_len;
	if (sl->w->notch) for (int i = -2; i <= 2; ++i) {
		/* Normal wheel side, or notch mark */
		wchar_t l = sl->w->notch[(sl->rot-i+al) % al] ? L'-' : L' '; 
		/* Normal wheel side, or ring setting mark */
		wchar_t r = sl->ringstellung == (sl->rot - i + al) % al ? L'*' : L' '; 
		mvwprintw(ui->w_wheels, y0 + i, x-1, "%lc", l);
		mvwprintw(ui->w_wheels, y0 + i, x+1, "%lc", r);
	}
}

/* Check if any wheels reached a notch position, set 'movement' for indicated slot(s) 
   or check if a blocking pin came up, and clear 'movement' for indicated slot(s)
*/
void post_step(machine *m) {
	for (int i = m->wheelslots; i--; ) {
		wheelslot *s = &m->slot[i];
		if (!s->step  || !s->w->notch) continue; /* Meaningless for nonrotating slot/featureless wheel */
		if (s->w->notch[(s->rot + s->pin_offset) % m->alphabet_len]) for (int j = s->affect_slots; j--; ) {
			m->slot[s->affect_slot[j]].movement = (m->steptype == T_NOTCH_ENABLING);
		}
	}
}

/* Cleanup when the user has turned wheels manually */
void step_cleanup(machine *m) {
	bool init_movenext = (m->steptype == T_PIN_BLOCKING);
	for (int i = m->wheelslots; i--; ) m->slot[i].movement = init_movenext;
	post_step(m);
}

/* Step the machine / turn wheels. Update the UI if a window is provided. 
   Go through all slots:
	 Turn the wheel if the slot is  "fast" or has "movement",
   also, reset 'movement' according to stepping type */
void step(machine *m, ui_info *ui) {
	for (int i = m->wheelslots; i--; ) {
		wheelslot *s = &m->slot[i];
		if (!s->step) continue;
		if (s->fast || s->movement) { 
			s->rot = (s->rot + s->step) % m->alphabet_len;
			if (ui) draw_wheel_rot(m, ui, i);
		}
		s->movement = (m->steptype == T_PIN_BLOCKING);
	}
	post_step(m);
}


/* encipher() & decipher() 

Ordinary wheels/mappings uses the encipher mapping from rigth to left, and the
decipher mapping from left to right.

Reflectors use either the encipher or decipher mapping, depending on whether the
machine is enciphering or deciphering. For a simple wire-based reflector,
these mappings are identical.

encipher:
* start at the keyboard/input (far right)
* proceed through the enchiper mapping of all the slots, from right to left
* if no reflector, use the result obtained so far.
* if the last wheel is a reflector, go back through decipher stages from left to right,
* starting with the wheel next to the reflector.

decipher:
* if there is a reflector, start at the far right side. 
* proceed through the encipher mappings of all slots, right to left,
* but not the reflector's mapping.
* then proceed with the rest:
* if no reflector, start here:
* proceed through the decipher mappings from left to right, starting with the reflector.
*/
wchar_t encipher(machine *m, wchar_t c, ui_info *ui) {
	int l = lookup(c, m->alphabet);
	if (l == -1) return c;
	step(m, ui);
	int al = m->alphabet_len;
  /*  Process all the wheels ... */
	for (int i = m->wheelslots; i--;) {
		wheelslot *sl = &m->slot[i];
		l = (sl->w->encode[(l+sl->rot+al-sl->ringstellung) % al] + al - sl->rot + sl->ringstellung) % al;

	}
	/* ... reflect if we have a reflector ... */
	if (m->slot[0].w->reflector) for (int i = 1; i < m->wheelslots; ++i) {
		wheelslot *sl = &m->slot[i];
		l = (sl->w->decode[(l+sl->rot+al-sl->ringstellung) % al] + al - sl->rot + sl->ringstellung) % al;		
	}
	return m->alphabet[l];
}

wchar_t decipher(machine *m, wchar_t c, ui_info *ui) {
	int l = lookup(c, m->alphabet);
	if (l == -1) return c;
	step(m, ui);
	int al = m->alphabet_len;
  /* Is the machine eqipeed with a reflector? */
  if (m->slot[0].w->reflector) for (int i = m->wheelslots; --i;) {
		wheelslot *sl = &m->slot[i];
		l = (sl->w->encode[(l+sl->rot+al-sl->ringstellung) % al] + al - sl->rot + sl->ringstellung) % al;		
	}
	for (int i = 0; i < m->wheelslots; ++i) {
		wheelslot *sl = &m->slot[i];
		l = (sl->w->decode[(l+sl->rot+al-sl->ringstellung) % al] + al - sl->rot + sl->ringstellung) % al;
	} 
	return m->alphabet[l];
}

/* Draw wheel number i */
void draw_wheel(machine *m, ui_info *ui, int i) {
	bool highlight = ui->chosen_wheel == i;
	wattrset(ui->w_wheels, ui->attr_wheel_activ);
	int base_y = (ui->layout == 0) ? m->wheelslots : m->longest_wheelname;
	mvwprintw(ui->w_wheels, base_y + 5, i*4, "     ");
	if (!highlight) wattrset(ui->w_wheels, ui->attr_wheel_plain);
	/* Wheel background, inactive parts */
	for (int j = 2; j <= 6; ++j) mvwprintw(ui->w_wheels, base_y + j + 1, i*4+1, "   ");
  if (m->slot[i].step) {
		/* Wheel turning knobs */
		wattrset(ui->w_wheels, highlight ? ui->attr_btnh : ui->attr_btn );
		mvwprintw(ui->w_wheels, base_y + 8, i*4+1, " v ");
		mvwprintw(ui->w_wheels, base_y + 2, i*4+1, " ^ ");
		/* Wheel text */
		draw_wheel_rot(m, ui, i);
		/* Ring settings */
		wattrset(ui->w_wheels, highlight ? ui->attr_lblh : ui->attr_lbl);
		mvwprintw(ui->w_wheels, base_y + 9, i*4+1, " %lc ", m->alphabet[m->slot[i].ringstellung]);
	}
  /* Wheel name */ 
	wattrset(ui->w_wheels, highlight ? ui->attr_lblh : ui->attr_lbl);
  if (ui->layout == 0) {
		mvwprintw(ui->w_wheels, i+1, i*3+m->wheelslots+3, "%*ls", -m->longest_wheelname, m->slot[i].w->name);
		for (int j = 1; j < 1+m->wheelslots-i; ++j) mvwprintw(ui->w_wheels, i+j+1, 3+i*3+m->wheelslots-j, "%c", '/');
	} else {

		int nlen = m->slot[i].w->name_len;
		for (int j = 0; j < nlen; ++j) mvwprintw(ui->w_wheels, 1+j+m->longest_wheelname-nlen, 2+i*4, "%lc", m->slot[i].w->name[j]);	
		mvwprintw(ui->w_wheels, m->longest_wheelname+1, 2+i*4, "|");

	}
}

/* Draw the set of wheels (entire window) */
void draw_wheels(machine *m, ui_info *ui) {
	for (int i = 0; i < m->wheelslots; ++i) draw_wheel(m, ui, i);
	/* Machine name centered on top */
	wattrset(ui->w_wheels, ui->attr_wheel_activ);
	mvwprintw(ui->w_wheels, 0, (getmaxx(ui->w_wheels)-wcslen(m->name))/2, "%ls", m->name);
	/* Ring setting heading */
	wattrset(ui->w_wheels, A_NORMAL);
	mvwprintw(ui->w_wheels, 9+m->wheelslots, 4*m->wheelslots, "ring setting");
}


void highlight_wheel(machine *m, ui_info *ui, int wheelnr) {
	int old = ui->chosen_wheel;
	ui->chosen_wheel = (wheelnr < m->wheelslots && wheelnr >= 0 )? wheelnr : -1;
	if (old == ui->chosen_wheel) return; /* no change */
	if (old >= 0) draw_wheel(m, ui, old);
	if (ui->chosen_wheel >= 0) draw_wheel(m, ui, ui->chosen_wheel);
	wnoutrefresh(ui->w_wheels);
}


void highlight_left(machine *m, ui_info *ui) {
	int n = ui->chosen_wheel > 0 ? ui->chosen_wheel - 1 : m->wheelslots - 1;
	highlight_wheel(m, ui, n);
}


void highlight_right(machine *m, ui_info *ui) {
	int n = ui->chosen_wheel < m->wheelslots - 1 ? ui->chosen_wheel + 1 : 0;
	highlight_wheel(m, ui, n);
}


/* Manual turning of the chosen code wheel */
void wheel_turn(machine *m, ui_info *ui, int step) {
	if (ui->chosen_wheel < 0) return;
	if (!m->slot[ui->chosen_wheel].step) return;
	int *i = &m->slot[ui->chosen_wheel].rot;
	*i = (*i + m->alphabet_len + step) % m->alphabet_len;
	step_cleanup(m);
	draw_wheel(m, ui, ui->chosen_wheel);
	wnoutrefresh(ui->w_wheels);
}


/* Manual change of ring setting */
void ringstellung(machine *m, ui_info *ui, int step) {
	if (ui->chosen_wheel < 0) return;
	if (!m->slot[ui->chosen_wheel].step) return;
	int *i = &m->slot[ui->chosen_wheel].ringstellung;
	*i = (*i + m->alphabet_len + step) % m->alphabet_len;
	step_cleanup(m);
	draw_wheel(m, ui, ui->chosen_wheel);
	wnoutrefresh(ui->w_wheels);
}


/* change the highlighted code wheel (or plugboard) */
void next_wheel(machine *m, ui_info *ui) {
	if (ui->chosen_wheel < 0) return;
	wheelslot *sl = &m->slot[ui->chosen_wheel];
	if (sl->type == T_WHEEL) {
		/* Change an ordinary wheel or reflector, to another allowed wheel */
		do {
			sl->w = sl->w->next_in_set;
		} while(!sl->w->allow_slot[ui->chosen_wheel]);
		draw_wheel(m, ui, ui->chosen_wheel);
	} else {
		/* change plugboard- / crossbar-settings, or rewire a wheel */
		wint_t s[m->alphabet_len * 2];
		echo();
		char *err = ""; /* Short msg if they screw up */
		for (bool done = false; !done; ) {
			wclear(ui->w_pop);
			if (sl->type == T_PAIRSWAP) {
				wprintw(ui->w_pop, "%sGive plugboard swaps in the format AX CF ...\nor just enter for the identity mapping\n", err);
				mvwgetn_wstr(ui->w_pop, 2, 0, s, (m->alphabet_len / 2) * 3);
				identity_map(m, sl->w);
				wchar_t *l1=s, *l2;
				done = true; /* optimistic */
				do { /* Each iteration parses one stecker pair */
					while (*l1 == L' ') ++l1;
					if (*l1) { /*  if we didn't hit \0 */
						l2 = l1 + 1;
						int i1 = lookup(*l1, m->alphabet);
						int i2 = lookup(*l2, m->alphabet);
						if (i1 == -1 || i2 == -1) {
							*l1 = 0;
							err = "Letter not in machine alphabet. ";
							done = false;
						} else {
							/* Got a pair, and it is valid! Set up the encoding & decoding */						
							sl->w->encode[i1] = i2;
							sl->w->encode[i2] = i1;
							sl->w->decode[i1] = i2;
							sl->w->decode[i2] = i1;
							l1 = l2 + 1;
						}
					}
				} while (*l1);
			} else {
				wprintw(ui->w_pop, "%sType the new mapping like XYZABCDE...\nor just enter for the identity mapping\n", err);
				mvwgetn_wstr(ui->w_pop, 2, 0, s, m->alphabet_len);
				identity_map(m, sl->w);
				done = true; /* Unless we get a bad string */
				int i = 0;
				wchar_t *l = s;
				while (*l && i < m->alphabet_len && done) {
					int c = lookup(*l, m->alphabet);
					if (c == -1) {
						err = "Letter not in machine alphabet. ";
						done = false;
					} else {
						sl->w->encode[i] = c;
						sl->w->decode[c] = i;
					}
					++i;
					++l;
				}
				/* More sanity checking */
				if (done) {
					if (*l) {
						err = "Too many characters. ";
						done = false;
					} else if (i && i < m->alphabet_len) {
						err = "Too few characters. ";
						done = false;
					}
				}

			}
		}
		noecho();
		redrawwin(ui->w_code);
		wnoutrefresh(ui->w_code);	

	}
	wnoutrefresh(ui->w_wheels);
	step_cleanup(m);
}

void interactive(machine *m) {
	/* Set up the ncurses interface */
	ui_info ui;
	initscr();
	start_color();
	raw(); 
	noecho(); 
	keypad(stdscr, true);

	/* Figure out the level of color support, choose attributes accordingly */
	if (has_colors()) {
		if (can_change_color()) {
			/* Best case, programmable colors */
			init_color(CLR_WHITEGRAY, 800, 800, 800); /* dull text */
			init_color(CLR_DARKGRAY, 400, 400, 400);  /* highlighted metal */
			init_color(CLR_DARKESTGRAY, 250, 250, 250); /* darkest metal */
			init_color(CLR_BRIGHTRED, 1000, 400, 400); /* coded text */
			init_pair(CP_WHEEL_PLAIN, CLR_WHITEGRAY, CLR_DARKESTGRAY);
			init_pair(CP_WHEEL_ACTIV, COLOR_WHITE, CLR_DARKGRAY);
			init_pair(CP_PLAIN, COLOR_WHITE, COLOR_BLACK);
			init_pair(CP_CODED, CLR_BRIGHTRED, COLOR_BLACK);
			init_pair(CP_BTN, COLOR_RED, CLR_DARKESTGRAY);
			init_pair(CP_BTNH, COLOR_RED, CLR_DARKGRAY);	
			init_pair(CP_WHEELSIDE_PLAIN, COLOR_BLACK, CLR_DARKESTGRAY);
			init_pair(CP_WHEELSIDE_ACTIV, COLOR_BLACK, CLR_DARKGRAY);
		} else {
			/* Second best, 8 color ncurses */
			init_pair(CP_WHEEL_PLAIN, COLOR_YELLOW, COLOR_BLUE);
			init_pair(CP_WHEEL_ACTIV, COLOR_WHITE, COLOR_RED);
			init_pair(CP_PLAIN, COLOR_WHITE, COLOR_BLACK);
			init_pair(CP_CODED, COLOR_RED, COLOR_BLACK);
			init_pair(CP_BTN, COLOR_RED, COLOR_BLUE);
			init_pair(CP_BTNH, COLOR_BLUE, COLOR_RED);
			init_pair(CP_WHEELSIDE_PLAIN, COLOR_BLACK, COLOR_BLUE);
			init_pair(CP_WHEELSIDE_ACTIV, COLOR_BLACK, COLOR_RED);
		}
		ui.attr_plain = COLOR_PAIR(CP_PLAIN);
		ui.attr_coded = COLOR_PAIR(CP_CODED);
		ui.attr_wheel_plain = COLOR_PAIR(CP_WHEEL_PLAIN);
		ui.attr_wheel_activ = COLOR_PAIR(CP_WHEEL_ACTIV) | A_BOLD;
		ui.attr_btn = COLOR_PAIR(CP_BTN);
		ui.attr_btnh = COLOR_PAIR(CP_BTNH);
		ui.attr_wheelside_plain = COLOR_PAIR(CP_WHEELSIDE_PLAIN);
		ui.attr_wheelside_activ = COLOR_PAIR(CP_WHEELSIDE_ACTIV);
	} else {
		/* No color fallback */
		ui.attr_plain = A_NORMAL;
		ui.attr_coded = A_BOLD;
		ui.attr_wheel_plain = A_REVERSE;
		ui.attr_wheel_activ = A_REVERSE | A_BOLD | A_UNDERLINE;
		ui.attr_btn = A_REVERSE | A_BOLD;
		ui.attr_btnh = ui.attr_btn;
		ui.attr_wheelside_plain = ui.attr_wheel_plain;
		ui.attr_wheelside_activ = ui.attr_wheel_activ;
	}
	ui.attr_lbl = A_NORMAL;
	ui.attr_lblh = A_BOLD;
	ui.chosen_wheel = -1;

	/* Make the windows.
     Two ways to print slot headings, select the lowest height:
		 way0: (height depends on the number of slots)

        UKW-B
       /   VII
      /   /   VI
     /   /   /   steckerbrett
    /   /   /   /

   way1: (height depends on the longest wheelname)
 
    U       V   E
    K       I   T
    W   V   I   W
    |   |   |   |
   */
	int topheight0 = 10 + m->wheelslots;
	int topheight1 = 10 + m->longest_wheelname;
	ui.layout = topheight0 < topheight1 ? 0 : 1;
	int topheight = ui.layout == 0 ? topheight0 : topheight1;
	int longestname = strlen("ring settings");
	if (ui.layout == 0 && longestname < m->longest_wheelname) longestname = m->longest_wheelname;
	int topwidth = 4 * m->wheelslots + 1 + longestname;
	int namelen = wcslen(m->name);
	if (namelen > topwidth) topwidth = namelen;
	ui.w_wheels = newwin(topheight, topwidth, 0, COLS-topwidth);
	int botheight = ((LINES - topheight) / 3) * 3 - 1;
	int botwidth = COLS;
	ui.w_code = newwin(botheight, botwidth, topheight, 0);
	ui.w_pop = newwin(botheight, botwidth, topheight, 0);
	draw_wheels(m, &ui);

  bool enciphering = true;		/* enchiper or dechiper */

	wchar_t plaintext[MAXLINE+1]; /* typed/dechipered text */
	wchar_t ciphertxt[MAXLINE+1]; /* typed/enchipered text */
	memset(plaintext, 0, sizeof(wchar_t)*(MAXLINE+1));
	memset(ciphertxt, 0, sizeof(wchar_t)*(MAXLINE+1));

	int textpos = 0;
  int maxpos = COLS - 2 > MAXLINE ? MAXLINE : COLS - 2;  

	curses_bug_workaround();
	wmove(ui.w_code, 1, 1);

	/* main loop. The first event has to be the KEY_RESIZE, it creates the display! */
	int rc = KEY_CODE_YES;
	wint_t wch = KEY_RESIZE; 
	for (bool active = true; active; rc = active ? get_wch(&wch) : 0) {
		switch (rc) {
			case KEY_CODE_YES:		/* specials */
				switch (wch) {
					case KEY_F(1):
						highlight_wheel(m, &ui, 0);
						break;
					case KEY_F(2):
						highlight_wheel(m, &ui, 1);
						break;
					case KEY_F(3):
						highlight_wheel(m, &ui, 2);
						break;
					case KEY_F(4):
						highlight_wheel(m, &ui, 3);
						break;
					case KEY_F(5):
						highlight_wheel(m, &ui, 4);
						break;
					case KEY_F(6):
						highlight_wheel(m, &ui, 5);
						break;
					case KEY_F(7):
						highlight_wheel(m, &ui, 6);
						break;
					case KEY_F(8):
						highlight_wheel(m, &ui, 7);
						break;
					case KEY_F(9):
						highlight_wheel(m, &ui, 8);
						break;
					case KEY_LEFT:
						highlight_left(m, &ui);
						break;
					case KEY_RIGHT:
						highlight_right(m, &ui);
						break;
					case KEY_F(10):
						highlight_wheel(m, &ui, 9);
						break;
					case KEY_NPAGE:
						next_wheel(m, &ui);
						break;
					case KEY_UP:
						if (ui.chosen_wheel == -1) {		
							/* switch to encoding */
							enciphering = true;
							wmove(ui.w_code, 1, textpos+1);
							wnoutrefresh(ui.w_code);
						} else wheel_turn(m, &ui, -1);
						break;
					case KEY_DOWN:
						if (ui.chosen_wheel == -1) {		
							/* switch to decoding */
							enciphering = false;
							wmove(ui.w_code, 2, textpos+1);
							wnoutrefresh(ui.w_code);
						} else wheel_turn(m, &ui, 1);		
						break;
					case KEY_RESIZE:	/* user resized the xterm - redraw all! */
						curses_bug_workaround();//Remove, and top window blanks out
						/* Must repaint all, as downsizing may blank the terminal */
						
						/* Deal with the code wheel window */
						botheight = ((LINES - topheight) / 3) * 3 - 1;
						botwidth = COLS;
						wresize(ui.w_code, botheight, botwidth);
						wresize(ui.w_pop, botheight, botwidth);
						int oldx = getbegx(ui.w_wheels);
						int newx = COLS-topwidth;
						mvwin(ui.w_wheels, 0, COLS-topwidth); /* move the window */
						/* now clear out the exposed screen area */
						if (newx > oldx) {
							int xlen = newx-oldx;
							char *spc = malloc(xlen+1);
							memset(spc, ' ', xlen);
							spc[xlen] = 0;
							for (int i=0; i < topheight; ++i) mvprintw(i, oldx, spc);
							free(spc); 
						}
						wnoutrefresh(ui.w_wheels);
						curses_bug_workaround(); //Remove, and the cursor will misplaced when upsizing
						

						/* Now the code text window */
						maxpos = COLS - 2 > MAXLINE ? MAXLINE : COLS - 2;  
                  
						if (textpos > maxpos) {
							leftcut(plaintext, textpos - maxpos, maxpos+1);
							leftcut(ciphertxt, textpos - maxpos, maxpos+1);
							textpos = maxpos;
							wattrset(ui.w_code, ui.attr_plain);
							mvwprintw(ui.w_code, 1, 1, "%ls", plaintext);wclrtoeol(ui.w_code);
							wattrset(ui.w_code, ui.attr_coded);
							mvwprintw(ui.w_code, 2, 1, "%ls", ciphertxt);wclrtoeol(ui.w_code);
							if (enciphering) wmove(ui.w_code, 1, textpos+1);
						}

						wnoutrefresh(ui.w_code);
						break;
				}
				break;
			case OK:
				if (iscntrl(wch)) switch (wch) {
					/* ctrl tv change ring settings */
					case 20:
						ringstellung(m, &ui, -1);
						break;
					case 22:
						ringstellung(m, &ui, 1);
						break;
					/* quit on ctrl+c */	
					case 3:
						active = false;
						break;
					/* unselect wheel on enter */
						case '\n':
							highlight_wheel(m, &ui, -1);
							wnoutrefresh(ui.w_code);
							break;
				}							
				/* plain typing */
				else { 
					if (ui.chosen_wheel > -1) highlight_wheel(m, &ui, -1);
					/* Need a line break first? */
					if (textpos >= maxpos) {
						/* add scrolling later !!! for now, just clear */
						textpos = 0;
						memset(plaintext, 0, sizeof(wchar_t)*(MAXLINE+1));
						memset(ciphertxt, 0, sizeof(wchar_t)*(MAXLINE+1));
						if (enciphering) {
							wmove(ui.w_code, 2, textpos+1); wclrtoeol(ui.w_code);
							wmove(ui.w_code, 1, textpos+1); wclrtoeol(ui.w_code);
						} else {
							wmove(ui.w_code, 1, textpos+1); wclrtoeol(ui.w_code);
							wmove(ui.w_code, 2, textpos+1); wclrtoeol(ui.w_code);
						}
					}
					if (enciphering) {
						plaintext[textpos] = wch;
						ciphertxt[textpos] = encipher(m, wch, &ui);
						wattrset(ui.w_code, ui.attr_coded);
						mvwprintw(ui.w_code, 2, 1, "%ls", ciphertxt);
						wattrset(ui.w_code, ui.attr_plain);
						mvwprintw(ui.w_code, 1, 1, "%ls", plaintext);
					} else {
						ciphertxt[textpos] = wch;
						plaintext[textpos] = decipher(m, wch, &ui);
						wattrset(ui.w_code, ui.attr_plain);
						mvwprintw(ui.w_code, 1, 1, "%ls", plaintext);
						wattrset(ui.w_code, ui.attr_coded);
						mvwprintw(ui.w_code, 2, 1, "%ls", ciphertxt);
					}
					textpos++;
					wnoutrefresh(ui.w_wheels);
					wnoutrefresh(ui.w_code);
					break;
				}
		} 
		doupdate();
	}
	endwin();
}


/* Print Vigènere tables for the code wheels */
/* 
     Wheel "III"
     input code      input decode     
     ABCDE           ABCDE
w  A               A
h  B               B
e  C               C
e  D               D
l  E               E

k
e
y
 */
void print_tables(machine *m) {
	wchar_t *vertical_header = L"Wheel key";
	int vlen = wcslen(vertical_header);
	int al = m->alphabet_len;
	wheel *firstwheel = m->wheel_list;
	wheel *w = firstwheel;
	wprintf(L"Vigènere tables for the wheels of machine \"%ls\"\n\n", m->name);

	do {
		bool rotatable = false;

		//Is 'rotation' meaningful for this 'wheel'?
		for (int i = 0; i < m->wheelslots && !rotatable; ++i) rotatable = w->allow_slot[i] && (m->slot[i].type == T_WHEEL);
		
		if (rotatable) {
			/* Heading */
			wprintf(L"     %s \"%ls\" \n", w->reflector ? "Reflector" : "Wheel", w->name);
			wprintf(L"     %-35s     %-35s\n", "Input code", "Input decode");
			wprintf(L"     %-35.*ls     %-35.*ls\n", m->alphabet_len, m->alphabet, m->alphabet_len, m->alphabet);
			/* Vigènere square. Loop through all orientations for this wheel */
			for (int j = 0; j < m->alphabet_len; ++j) {
				/* Enchipher the entire alphabet similiar to the enchiper() function, 
					 but using only this single wheel. (Ringstellung does not apply)*/ 
				wprintf(L"%c  %lc ", (j >= vlen) ? ' ' : vertical_header[j], m->alphabet[j]);
				for (int k=0; k < m->alphabet_len; ++k) wprintf(L"%lc", m->alphabet[(w->encode[(k+j) % al]+al-j)% al]);
				wprintf(L"%*s   %lc ", 35-m->alphabet_len, "", m->alphabet[j]);
				for (int k=0; k < m->alphabet_len; ++k) wprintf(L"%lc", m->alphabet[(w->decode[(k+j) % al]+al-j) % al]);
				wprintf(L"\n"); 
			}
		}

		wprintf(L"\n");
		//Next wheel...
		w = w->next_in_set;
	} while (w != firstwheel);
}


int main(int argc, char *argv[]) {
	/* setlocale(), so mbtowc() etc will work. 
     We may want to encode/decode non-ascii stuff. 
     Spies works with many scripts . . . */
	if (setlocale(LC_ALL, "") == NULL) feil("Bad locale, please configure your computer correctly. Install the locale package, and/or set the LANG environment variable.\n");
	fwide(stdout,1);

  bool print_wheel_tables = (argc == 3) && !strcmp(argv[2], "-t");

  if ((argc < 2) || (argc > 3) || (argc == 3 && !print_wheel_tables )) feil("enigma machine-description [-t]\n -t prints wheel tables\n");
  machine *m=getdescr(argv[1]);
  if (!m) feil("Unuseable machine description\n");
  
  if (print_wheel_tables) print_tables(m); 
  else interactive(m);
}


/*
Trenger:
* Konvertere frem og tilbake mellom char fra maskinalfabetet og 0..n-1
* kode (og dekode) etter at parsing er ferdig.
* lex/yacc-parsing for programopsjoner også? 

* velge mellom kode/dekode, interaktivt og med opsjoner
* fil-modus og interaktivt modus
* For interaktivt modus: vis kodet tekst med farge, understrek e.l.
  Slå opp ANSI-koder for slike effekter, sjekk helst TERM / curses.

  Rotasjoner, multihjul, ...

*/
