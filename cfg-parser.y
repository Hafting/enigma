%{
/*
	cfg-parser.y
	Grammar describing enigma-like encryption machines
	© 2015 Helge Hafting, licenced under the GPL
*/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "enigma.h"

extern int yylineno;

/*  Prototyper */
int yylex(machine *m);

/* Temporary variables: */
wheelslot tmpslot;
int tmp_ints = 0;
int tmp_int[MAX_INT_SET];
bool *tmp_slotlimit;

/* Helper functions */


/* Read the machine alphabet into the data structure */
/* Validate, avoid duplicate letters 
   Return 0 on success, or position of first duplicate */
void read_alphabet(machine *m, const wchar_t *a) {
	m->alphabet = a;
	int l, p;
  for (; *a; ++a) {
		l = lookup(*a, m->alphabet);
		p = a-m->alphabet; 
		if (l != p) {
			yyerror(m, "the alphabet has a duplicate in positions %i and %i.\n", l+1, p+1);
			return;
		}
	}
	m->alphabet_len = p + 1;
	return;
}


/* Defaults for a new slot */
void zero_slot(wheelslot *s) {
	s->rot = 0;
  s->step = 1;
	s->fast = false;
	s->type = T_WHEEL;
	s->movement = false;
	s->affect_slots = 0;
	s->affect_slot = NULL;
	s->ringstellung = 0;
	s->w = NULL;
	s->pin_offset = 0;
}

/* Create & initialize wheelslots */
void set_wheelslots(machine *m, int slots) {
	m->wheelslots = slots;
	m->slot = malloc(slots * sizeof(wheelslot));
	for (int i = m->wheelslots; i--;) zero_slot(&m->slot[i]);
	zero_slot(&tmpslot);
	/* By default, any wheel can be put in any slot: */
	tmp_slotlimit = malloc(slots * sizeof(bool));
	for (int i = 0; i < slots; ++i) tmp_slotlimit[i] = true;
}


/* Store temporary slot into machine description */
void set_slot(machine *m, int slotnr) {
	if (slotnr && slotnr <= m->wheelslots) {
		if (tmpslot.type != T_WHEEL) { /* rewirable slot/plugboard gets its own special "wheel" */
			tmpslot.w = malloc(sizeof(wheel));
			tmpslot.w->name = tmpslot.step == 0 ? L"plugboard" : L"rewirable";
			tmpslot.w->reflector = false;
			tmpslot.w->next_in_set = tmpslot.w; /* single-item circular list */
			/* if this is a rotating slot with notches actions, give the wheel a single notch.
         The notch is always at the first position, use ringstellung to change that */
			if (tmpslot.step && tmpslot.affect_slots) {
				tmpslot.w->notch = calloc(m->alphabet_len, sizeof(bool));
				tmpslot.w->notch[0] = true; /* this default notch goes in the first position */
			} else {
				tmpslot.w->notch = NULL;
			}
			/* Set up an identity mapping, until the user rewires/replugs this item */
			tmpslot.w->encode = malloc(sizeof(int) * m->alphabet_len);
			tmpslot.w->decode = malloc(sizeof(int) * m->alphabet_len);
			identity_map(m, tmpslot.w);
		}
		memcpy(&m->slot[slotnr-1], &tmpslot, sizeof(wheelslot));
	} else yyerror(m, "invalid slot number '%i', this machine has slots 1-%i\n", slotnr, m->wheelslots);
	zero_slot(&tmpslot);
}


/* Sanity check before reading wheel descriptions */
void wheel_precheck(machine *m) {
	if (!m->alphabet) yyerror(m, "please specify the machine alhpabet before the code wheels.\n");
	if (!m->wheelslots) yyerror(m, "wheels in a machine with 0 slots for them?\n");
}


/* Set up tmp_slotlimit from tmp_int */
void set_restrictions(machine *m) {
	bool warned = false;
	/* Drop previously allowed slots */
	for (int i = 0; i < m->wheelslots; ++i) tmp_slotlimit[i] = false;
	/* Allow the specified slots */
	while (tmp_ints) {
		--tmp_ints; 
		if (tmp_int[tmp_ints] < 1 || tmp_int[tmp_ints] > m->wheelslots) {
			if (!warned) yyerror(m, "slot number %i not in the 1-%i range\n", tmp_int[tmp_ints], m->wheelslots);
			warned = true; /* Avoid long series of error messages */
		} else {
			tmp_slotlimit[tmp_int[tmp_ints]-1] = true;
		}
	}
}


/* 
	Make a new wheel, link it in. Most of the initialization happens later
*/
void new_wheel(machine *m) {
	wheel *w = malloc(sizeof(wheel));
	w->next_in_set = m->wheel_list;
	m->wheel_list = w;

	w->name = 0;

  /*Fill encode/decode with invalid indices, so the error checking
	  during parisng will work right. */
  int mapsize = sizeof(int)*m->alphabet_len;
	w->encode = malloc(mapsize);
	memset(w->encode, 255, mapsize);
	w->decode = malloc(mapsize);
	memset(w->decode, 255, mapsize);

	w->notch = NULL;
}


/*	Name a newly created wheel. 
		Returns: true when all is ok, false if the name was taken 
		Also make the next wheel to be filled out.
*/
void name_wheel(machine *m, wchar_t *name, bool reflector) {
	wheel *w = wheel_lookup(m, name);
  if (w) {
		yyerror(m, "cannot have two wheels both named %s\n", name);
		return;
	}
	w = m->wheel_list;
	w->name = name;
	w->reflector = reflector;
	int slen = wcslen(name);
	if (slen > m->longest_wheelname) m->longest_wheelname = slen;
	int arrsiz = sizeof(bool) * m->wheelslots;
	w->allow_slot = malloc(arrsiz);
	memcpy(w->allow_slot, tmp_slotlimit, arrsiz);

	new_wheel(m);
}


/* Set up wiring for the last wheel created
   a character string specifies how the alphabet gets scrambled */
void wheel_wiring(machine *m, wchar_t *wr) {
	wheel *w = m->wheel_list;
	int i;
	for (i = 0; wr[i]; ++i) {
		int nr = lookup(wr[i], m->alphabet);
		if (nr == -1) {
		yyerror(m, "attempt to wire letter «%lc» which is not in the machine alphabet\n", wr[i]);
			return;
		}
		/* now set up the connection */
    w->encode[i] = nr;
		if (w->decode[nr] > -1) {
			yyerror(m, "wheel maps several letters to «%lc»\n", m->alphabet[nr]);
			return;
		}
		w->decode[nr] = i;
	}
	if (i < m->alphabet_len) yyerror(m, "wheel don't map all of the machine alphabet\n");
	free(wr);
}

/* Set up wheel wiring based on a set of integers instead of a character string 
   Useful because some machines are documented using number mappings instead of symbols
   The first letter in the machine alphabet is "1", the second is "2" and so on.
*/
void wheel_wiring_ints(machine *m) {
	wheel *w = m->wheel_list;
	/* Some sanity checks first: */
	if (tmp_ints < m->alphabet_len) {
		yyerror(m, "wheel don't map all of the machine alphabet\n");
		tmp_ints = 0;
		return;
	}

	if (tmp_ints > m->alphabet_len) {
		yyerror(m, "attempt to map more letters than the machine alphabet have?\n");
		tmp_ints = 0;
		return;
	}

	do {
		--tmp_ints;
		int nr = tmp_int[tmp_ints] - 1;
		if (nr < 0 || nr >= m->alphabet_len) {
			yyerror(m, "attempt to wire letter numbered %i, which is out of the 1-%i range.\n", nr+1, m->alphabet_len);
			continue;
		}
		w->encode[tmp_ints] = nr;
		if (w->decode[nr] > -1) {
			yyerror(m, "wheel maps several positions to position %i\n", nr+1);
			continue;
		} 
		w->decode[nr] = tmp_ints;
	} while(tmp_ints);
}

/* Like wheel_wiring, but separate mappings for encipher & decipher */
void wiring_encipher_decipher(machine *m, wchar_t *ench, wchar_t *dech) {
	int i;
	wheel *w = m->wheel_list;
	for (i = 0; ench[i] && dech[i]; ++i) {
		int e = lookup(ench[i], m->alphabet), d = lookup(dech[i], m->alphabet);
		if (e == -1 || d == -1) yyerror(m, "attempt to wire letters «%lc» and «%lc», one of which isn't in the machine alphabet\n", ench[i], dech[i]);
		w->encode[i] = e;
		w->decode[i] = d;
	}
	if (i < m->alphabet_len) yyerror(m, "wheel don't map all of the machine alphabet\n");
	free(ench);
	free(dech);
}

/* Parse code-wheel notches or advance-blocking pins 
   notches and blocking pins are not the same things, but the parsing is
   identical as they both fith in the same datastructure.
	 Notches/pins indicated by a string, noting the characters where such features are located.
*/
void wheel_notches(machine *m, wchar_t *ws) {
	wheel *w = m->wheel_list;
	if (w->notch) {
		yyerror(m, "Wheel cannot have a second set of notches/pins.\n");
		return;
	}
	w->notch = calloc(m->alphabet_len, sizeof(bool));

  for (wchar_t *n = ws; *n; n++) {
		int l = lookup(*n, m->alphabet);
		if (l == -1) {
			yyerror(m, "notch/pin at character '%lc' which is not in the machine alphabet?", *n);
			return;
		}
		w->notch[l] = true;
	}
	free (ws);	
}

/* Code-wheel notches/advance-blocking pins, specified using a list of integer positions */
void wheel_notches_ints(machine *m) {
	/* Sanity checks */
	if (tmp_ints > m->alphabet_len) {
		yyerror(m, "More notches/pins than rotational positions?");
		tmp_ints = 0;
		return;
	}
	wheel *w = m->wheel_list;
	if (w->notch) {
		yyerror(m, "Wheel cannot have a second set of notches/pins.\n");
		return;
	}
	w->notch = calloc(m->alphabet_len, sizeof(bool));

	do {
		--tmp_ints;
		int l = tmp_int[tmp_ints] - 1;
		if (l < 0 || l >= m->alphabet_len) {
			yyerror(m, "notch/pin number (%i) outside the 1-%i range.\n", l+1, m->alphabet_len);
			continue;
		}
		w->notch[l] = true;
	} while(tmp_ints);
}

/* Read an int belonging to a set of ints, put in tmp storage */
/* Users of the set must zero out tmp_ints afterwards */
void collect_an_int(machine *m, int x) {
	if (tmp_ints >= MAX_INT_SET) {
		yyerror(m, "more than %i integers on a line, program limitation.\n", MAX_INT_SET);
		return;
	}
	tmp_int[tmp_ints++] = x; 
}

/* Read an integer range (such as 5-12) into a set of ints, put in tmp storage */
/* Users of the set must zero out tmp_ints afterwards */
void collect_int_range(machine *m, int from, int to) {
	if (from > to) {
		yyerror(m, "invalid range, from %i up to %i?\n", from, to);
		return;
	}
	if (tmp_ints + to - from >= MAX_INT_SET) {
		yyerror(m, "too big range, program limitation\n");
		return;
	}
	for (int i = from; i <= to; ++i) tmp_int[tmp_ints++] = i;
}

/* Register which slot(s) to spin when a notch comes up, or which to block when a pin appear */
/* The info is already collected in the temporary integer set,
   put it in the temporary slot structure 
	 Also reset the tmp_ints counter to 0 */
void slot_affect_slots(machine *m) {
	/* Is this an additional set of ints? */
	if (tmpslot.affect_slot) {
		tmpslot.affect_slot = realloc(tmpslot.affect_slot, sizeof(int) * (tmpslot.affect_slots + tmp_ints));
	} else tmpslot.affect_slot = malloc(sizeof(int) * tmp_ints);
	do {
		if (tmp_int[--tmp_ints] > m->wheelslots) {
			yyerror(m, "attempt to affect slot %i in a machine with only %i slots.\n", tmp_int[tmp_ints], m->wheelslots);
		}  
		tmpslot.affect_slot[tmpslot.affect_slots++] = tmp_int[tmp_ints]-1;
	} while (tmp_ints);
}

/* Get & validate a slot's pin/notch offset */
void read_pin_offset(machine *m, int off) {
	if (off >= m->alphabet_len) {
		yyerror(m, "pin offset of %i shouldn't exceed the alphabet length (%i)\n", off, m->alphabet_len);
		return;
	}
	tmpslot.pin_offset = off;
}

%}

%param {machine *m}

%union {
	int i;
	wchar_t *ws;
}
%token <i> INTEGER
%token <ws> WSTRING
%token <ws> NAME
%token ALPHABET MACHINE WHEELSLOTS WHEEL WIRING SLOT SLOTS FAST REVERSE REWIRABLE PLUGBOARD REFLECTOR NONROTATING NOTCHES PUSH FOR PINS OFFSET BLOCKS STEPPING ENCIPHER DECIPHER

%%
/*
	Spec for the entire code machine. 
  Must have an alphabet, usually some code wheels,
  often a system for turning some of the wheels.
  Wheels may turn (or not) based on wheel properties, and/or
  wheel position in the machine.
*/

machine:     
	MACHINE WSTRING machine_details slot_setup wheelset {
		m->name = $2;
	}
	;

machine_details:
	machine_detail
	| machine_details machine_detail
	;

machine_detail:
	machine_alphabet
	| machine_wheelslots
	| STEPPING NOTCHES { m->steptype = T_NOTCH_ENABLING; } 
	| STEPPING PINS    { m->steptype = T_PIN_BLOCKING; }
	;

/* The input-output alphabet for this machine */
machine_alphabet: 
	ALPHABET WSTRING { 
			read_alphabet(m,  $2);			
	}
	;

/* The number of wheelslots, defaults to 0 */
machine_wheelslots:
	WHEELSLOTS INTEGER { set_wheelslots(m, $2); }
	;

/*  Setup of wheel slots 
		slot 1 is the leftmost slot, first in the wheel order.
		If the machine uses a reflector, it goes in slot 1.
		

	  Input goes into the highest numbered slot first. This is where an
    enigma machine has its fast wheel.
    Default for a slot: no automatic movement, 
    but the user may turn the wheel in the slot. Other options:
		- fast slot   - the wheel there will advance one step on every keypress
	  - reverse     - the wheel steps in the other direction 
		- nonrotating - neither user nor machine will turn this wheel. Useful for
                     fixed eintrittwalze mapping
                     fialka crossbar mapping
                     steckerboard mapping
                     fixed reflectors
		- notch push slotnumber(s) - what wheel(s) to move when a notch comes up.
		
		- more may be needed to simulate a Russian fialka.
*/

slot_setup:  /* empty */
	| slot_setup slot 
	;

slot:
	SLOT INTEGER slot_details { set_slot(m, $2); }

slot_details: /* empty */
	| slot_details slot_detail
	;

slot_detail:	
	FAST					{ tmpslot.fast = true; }
	| REVERSE			{ tmpslot.step = m->alphabet_len - tmpslot.step;}
	| NONROTATING	{ tmpslot.step = 0; }
	| NOTCHES PUSH integers { slot_affect_slots(m); }
	| PINS BLOCKS integers { slot_affect_slots(m); }
	| REWIRABLE { tmpslot.type = T_REWIRABLE; }
	|	PLUGBOARD { tmpslot.type = T_PAIRSWAP; }
	| PINS OFFSET INTEGER { read_pin_offset(m, $3); }
	| NOTCHES OFFSET INTEGER { read_pin_offset(m, $3); }
	;

integers: /* one or more positive integers (or integer ranges) */
	int_or_range
	| integers int_or_range
	;

int_or_range:
	one_int
	| int_range
	;

one_int: INTEGER {collect_an_int(m, $1); } ;

int_range: INTEGER '-' INTEGER {collect_int_range(m, $1, $3); } ;

/* The set of scrambler wheels */
wheelset:    {wheel_precheck(m);new_wheel(m);}
	wheel_or_restriction 
	| wheelset wheel_or_restriction
	;

wheel_or_restriction:
	wheel
	| restriction {set_restrictions(m); }
	;

/* Restriction (in what slots(s) can the wheel(s) following be used? */
restriction:  
	FOR SLOT one_int	
	| FOR SLOTS integers
	;


/* One code wheel */
wheel:
	WHEEL NAME wheel_spec 			{	name_wheel(m, $2, false); }
	| REFLECTOR NAME wheel_spec	{ name_wheel(m, $2, true); }
	;

wheel_spec: 
	wiring stepping 
	;

/* Connectors & wiring for a wheel */
wiring:
	WIRING WSTRING { wheel_wiring(m, $2); }
	| WIRING integers { wheel_wiring_ints(m); }
	| ENCIPHER WSTRING DECIPHER WSTRING { wiring_encipher_decipher(m, $2, $4); }
	| DECIPHER WSTRING ENCIPHER WSTRING { wiring_encipher_decipher(m, $4, $2); }
	;

/* wheel specific stepping features, such as 
   notches (done) or advance-blocking pins (not done yet!!!)*/
stepping: /* empty */
	| NOTCHES WSTRING { wheel_notches(m, $2); }
	| NOTCHES integers {wheel_notches_ints(m); }
	| PINS WSTRING { wheel_notches(m, $2); }
	| PINS integers { wheel_notches_ints(m); }
	;


%%

void yyerror(machine *m, const char *s, ...) {
	va_list ap;
	va_start(ap, s);
	m->broken_description = true;
  fprintf(stderr, "Line: %i, ", yylineno);
	vfprintf(stderr, s, ap);
	va_end(ap);
	return;
}
