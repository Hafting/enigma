/*
  enigma.h
	© 2015 Helge Hafting, licenced under the GPL
*/

#include <stdbool.h>
#include <stdarg.h>
#include <ncurses.h>

/* Color pair numbers for UI */
#define CP_WHEEL_PLAIN 20
#define CP_WHEEL_ACTIV 21
#define CP_PLAIN 22
#define CP_CODED 23
#define CP_BTN 24
#define CP_BTNH 25
#define CP_WHEELSIDE_PLAIN 26
#define CP_WHEELSIDE_ACTIV 27

#define CLR_WHITEGRAY   20
#define CLR_DARKESTGRAY 21
#define CLR_DARKGRAY    22
#define CLR_BRIGHTRED		23

/* Longest screenline to bother with */
#define MAXLINE 200

/* Maximum set of integers (on a line in a config file) */
#define MAX_INT_SET 100

/* Description of a code wheel */
typedef struct _wheel {
	wchar_t *name;
	int name_len;
	bool reflector;
	struct _wheel *next_in_set;

	int *encode; /* Array, code mapping for this wheel   */
	int *decode; /* Array, inverse mapping for decoding */ 

	bool *notch; /* Array of notch positions.  */
	bool *allow_slot; /* Array of slots the wheel will fit into (indexed by slot number) */
} wheel;


typedef enum {
	T_WHEEL, 			/* slots take a normal wheel, which may be changed by the user */
	T_PAIRSWAP,		/* slot for user-settable pair-swapping, i.e. enigma plugboard */
	T_REWIRABLE		/* user-settable mapping. Rewirable rotating wheel, or fialka card reader  */
} slot_type;


/* Machine stepping mechanisms. Enigma machines uses the first one, Fialka uses the second. */
typedef enum {
	T_NONE = 0,       /* No stepping specified. Fast wheels move, that's all! */
	T_NOTCH_ENABLING,	/* Fast wheels always move, an active wheel notch will move other wheels */
	T_PIN_BLOCKING		/* Any wheel will move, unless prevented by an active blocking pin somewhere */
} step_type;

/* A wheel slot in the rotor machine.
	 1. A code wheel
	 2. Rotation of this wheel  
	 3. slot-specific wheel stepping. (fast wheel, nonrotating, ...)
*/
typedef struct {
	wheel *w;
	int rot;  /* Number of wheel steps from the A-position */
	int ringstellung; /*  # of steps the electric connections is offset from the outer notched ring */
	int step;	/* steplength: 1=forw alfabeth_len-1=backw, 0=nonrotating (ETW,UKW,steckerbrett)  */
	bool fast; /* is this a 'fast' slot?  (Rotates for every keypress) */
	slot_type type; 
	bool movement; /* scheduled to move on next keypress, due to notched wheel */
	int affect_slots; /* How many slot(s) to nudge when a notch happens, or block when a pin comes up */
	int *affect_slot; /* Which slot(s) to nudge when a wheel notch comes up, or block when a pin comes up */

	/* A machine wheel may have a notch/blocking pin visibly next to "A", for example. 
     Therefore, it may be documented as an A-pin or A-notch.
     But the notch/pin mechanism may be offset for mechanical reasons. When "A" shows in the display,
     perhaps the pin/slot at "P" is active.
  */
	int pin_offset;
} wheelslot;

/* Description of a code machine */
typedef struct {
	bool broken_description;
	const wchar_t *name;			/* Name of the machine*/
	const wchar_t *alphabet;	/* Alphabet for (de)coding */
	int alphabet_len;					// Alphabet length in unicode characters	
	step_type steptype;				/* What type of stepping mechanism is used */
	wheel *wheel_list;	      /* Set of wheels belonging to this machine */
  int wheelslots;			      /* Number of slots for wheels */
	wheelslot *slot;		      /* Array of wheel slots */

	/* Needed for UI */
	int longest_wheelname;

} machine;

/* UI stuff */
typedef struct {
	int attr_plain, attr_coded; /* plain & enciphered text */
	int attr_wheel_plain, attr_wheel_activ; /* wheel parts */
	int attr_wheelside_plain, attr_wheelside_activ; 
	int attr_btn, attr_btnh; /* wheel parts */
	int attr_lbl, attr_lblh; /* label text */
	int layout; /* 0 or 1, how to print slot headings. see interactive() */
	WINDOW *w_wheels;
	WINDOW *w_code;
	WINDOW *w_pop;
	int chosen_wheel;
} ui_info;


wchar_t *mbstowcsdup(const char *s);
int lookup(const wchar_t wc, const wchar_t *ws);
wheel *wheel_lookup(machine *m, wchar_t *name);
void identity_map(machine *m, wheel *w);

void step_cleanup(machine *m);

void yyerror(machine *m, const char *s, ...);

