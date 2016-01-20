/*
  Necessary define, or ncurses won't provide get_wch() with gcc -std=gnu11 
  or c99 for that matter. Strangely, the #define is not necessary without c99...
*/
#define _XOPEN_SOURCE_EXTENDED

#include <wchar.h>
#include <ncurses.h>
#include <locale.h>

int main(int argc, char *argv[]) {

  int rc; 
	wint_t wch;

	setlocale(LC_ALL, "");
	initscr();
	start_color();
	int hascolor=has_colors(); //   ? color or A_STANDOUT A_NORMAL A_UNDERLINE
	int canchangecolor=can_change_color();// ? custom or std. colors.
  // rxvt-unicode-256color can change colors, plain xterm can't
	//correct TERM necessary to get F1 processing and such.
  cbreak(); /* stop buffering */
	noecho(); 
	keypad(stdscr, true);

  printw("has_color=%i, can change*=%i\n", hascolor, canchangecolor);
	printw("COLORS=%i COLOR_PAIRS=%i\n",COLORS,COLOR_PAIRS);

	if (hascolor) {
		int max = COLOR_PAIRS < 270 ? COLOR_PAIRS : 270; int i;
		for (i = 0; i < max; ++i) {
			init_pair(2*i, COLOR_WHITE, i&255);
			init_pair(2*i+1, COLOR_BLACK, i&255);
			attron(COLOR_PAIR(2*i));
			printw("%03i",i);
			attron(COLOR_PAIR(2*i+1));
			printw("%03i",i);
		}
		printw("\n");
		attrset(A_NORMAL);
		
	}
/*
weird effects (TERM=rxvt-unicode-256color):
color pairs 0-127: works as documented
color pairs >= 128 has inverse video for some reason
color pairs >= 256 has underlining too, for some reason
So, stick to color PAIRS 0-127 then.

Common cases to handle:
* no colors - make do without
* xterm     - work with the meager 8 colors provided. Even ANSI do better :-/
* better    - define colors & use properly!

*/

	attrset(A_NORMAL);
	attron(A_NORMAL);  	printw("A_NORMAL    ");attrset(A_NORMAL);
	attron(A_STANDOUT); printw("A_STANDOUT  ");attrset(A_NORMAL);
	attron(A_UNDERLINE);printw("A_UNDERLINE ");attrset(A_NORMAL);
	attron(A_REVERSE);	printw("A_REVERSE   ");attrset(A_NORMAL);
	attron(A_DIM);			printw("A_DIM       ");attrset(A_NORMAL);
	attron(A_BOLD);			printw("A_BOLD      ");attrset(A_NORMAL);
/*
	STANDOUT ofte samme som REVERSE. BOLD kan endre både font og farge
*/

	if (canchangecolor) {
		init_color(10, 250, 250, 250); //Metallgrå
		init_color(11, 400, 400, 400); //Lysere grå
		init_color(12, 800, 800, 800); //gråhvit?
		init_pair(22, COLOR_WHITE, 11);
		init_pair(20, COLOR_YELLOW, 10);
		init_pair(21, 12, 10);
		attron(COLOR_PAIR(21));	
		printw("\n G ");
		attron(A_UNDERLINE);printw("\n F ");attrset(A_NORMAL);
		attron(A_UNDERLINE | A_BOLD | COLOR_PAIR(22));	printw("\n E ");attrset(A_NORMAL);
		attron(COLOR_PAIR(21));	
		printw("\n D "); 
		printw("\n C ");
	}

  get_wch(&wch);
	clear();

  rc = KEY_CODE_YES; wch = KEY_RESIZE;
	int hjul=3;
	int topheight = 5+hjul;
	
	WINDOW *wtop = newwin(topheight, 2*hjul+5,0,0);
	WINDOW *wcode= newwin(LINES-topheight,COLS,topheight,0);
  do {	
	  //Lag noe
  	wprintw(wcode, "LINES:%i  COLS:%i\n", LINES, COLS);
		if (rc == OK) wprintw(wcode,"(%lc)",wch); 
	  //øvre vindu: høyde 5+antall hjul  bredde 2xantall hjul + lengste hjulnavn

  	//nedre vindu: full bredde. Høyde LINES-høyden for øvre.

		//int x = getch(); //KEY_DOWN KEY_UP KEY_RESIZE
//int get_wch(wint_t *wch); //ret. KEY_CODE_YES for function key, OK for wide char, or ERR. wide char / function key in "wch"
		wrefresh(wcode);
		rc = get_wch(&wch);
	} while (rc != KEY_CODE_YES || wch != KEY_F(1)); //Until F1...
	endwin();
}
