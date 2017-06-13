/*
 * There used to be a note that said this:
 *
 * The author - Don Woods - apologises for the style of the code; it
 * is a result of running the original Fortran IV source through a
 * home-brew Fortran-to-C converter.)
 *
 * Now that the code has been restructured into something much closer
 * to idiomatic C, the following is more appropriate:
 *
 * ESR apologizes for the remaing gotos (now confined to two functions
 * in this file - there used to be hundreds of them, *everywhere*),
 * and the offensive globals.  Applying the Structured Program Theorem
 * can be hard.
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <getopt.h>
#include <signal.h>
#include <time.h>
#include "advent.h"
#include "database.h"
#include "linenoise/linenoise.h"
#include "newdb.h"

struct game_t game;

long LNLENG, LNPOSN, PARMS[MAXPARMS+1];
char rawbuf[LINESIZE], INLINE[LINESIZE+1];

long AMBER, AXE, BACK, BATTER, BEAR, BIRD, BLOOD,
		BOTTLE, CAGE, CAVE, CAVITY, CHAIN, CHASM, CHEST,
		CLAM, COINS, DOOR, DPRSSN, DRAGON, DWARF, EGGS,
		EMRALD, ENTER, ENTRNC, FIND, FISSUR, FOOD,
		GRATE, HINT, INVENT, JADE, KEYS,
		KNIFE, LAMP, LOCK, LOOK, MAGZIN,
		MESSAG, MIRROR, NUGGET, NUL, OGRE, OIL, OYSTER,
		PEARL, PILLOW, PLANT, PLANT2, PYRAM, RESER, ROD, ROD2,
		RUBY, RUG, SAPPH, SAY, SIGN, SNAKE,
    		STEPS, STREAM, THROW, TRIDNT, TROLL, TROLL2,
		URN, VASE, VEND, VOLCAN, WATER;
long WD1, WD1X, WD2, WD2X;

FILE  *logfp;
bool oldstyle = false;
bool editline = true;
bool prompt = true;
lcg_state lcgstate;

extern void initialise();
extern void score(long);
extern int action(FILE *, long, long, long);

void sig_handler(int signo)
{
    if (signo == SIGINT)
	if (logfp != NULL)
	    fflush(logfp);
    exit(0);
}

/*
 * MAIN PROGRAM
 *
 *  Adventure (rev 2: 20 treasures)
 *
 *  History: Original idea & 5-treasure version (adventures) by Willie Crowther
 *           15-treasure version (adventure) by Don Woods, April-June 1977
 *           20-treasure version (rev 2) by Don Woods, August 1978
 *		Errata fixed: 78/12/25
 *	     Revived 2017 as Open Advebture.
 */

static bool do_command(FILE *);

int main(int argc, char *argv[])
{
    int ch;
	
/*  Options. */

    while ((ch = getopt(argc, argv, "l:os")) != EOF) {
	switch (ch) {
	case 'l':
	    logfp = fopen(optarg, "w");
	    if (logfp == NULL)
		fprintf(stderr,
			"advent: can't open logfile %s for write\n",
			optarg);
	    signal(SIGINT, sig_handler);
	    break;
	case 'o':
	    oldstyle = true;
	    editline = prompt = false;
	    break;
	case 's':
	    editline = false;
	    break;
	}
    }

    linenoiseHistorySetMaxLen(350);

    /* Logical variables:
     *
     *  game.closed says whether we're all the way closed
     *  game.closng says whether it's closing time yet
     *  game.clshnt says whether he's read the clue in the endgame
     *  game.lmwarn says whether he's been warned about lamp going dim
     *  game.novice says whether he asked for instructions at start-up
     *  game.panic says whether he's found out he's trapped in the cave
     *  game.wzdark says whether the loc he's leaving was dark */

    /* Initialize our LCG PRNG with parameters tested against
     * Knuth vol. 2. by the original authors */
    lcgstate.a = 1093;
    lcgstate.c = 221587;
    lcgstate.m = 1048576;
    srand(time(NULL));
    long seedval = (long)rand();
    set_seed(seedval);

    /*  Initialize game variables */
    if (!game.setup)
	initialise();

    /*  Unlike earlier versions, adventure is no longer restartable.  (This
     *  lets us get away with modifying things such as OBJSND(BIRD) without
     *  having to be able to undo the changes later.)  If a "used" copy is
     *  rerun, we come here and tell the player to run a fresh copy. */
    if (game.setup <= 0) {
	RSPEAK(201);
	exit(0);
    }

    /*  Start-up, dwarf stuff */
    game.setup= -1;
    game.zzword=RNDVOC(3,0);
    game.novice=YES(stdin, 65,1,0);
    game.newloc=1;
    game.loc=1;
    game.limit=330;
    if (game.novice)game.limit=1000;

    if (logfp)
	fprintf(logfp, "seed %ld\n", seedval);

    /* interpret commands ubtil EOF or interrupt */
    for (;;) {
	if (!do_command(stdin))
	    break;
    }
    /* show score and exit */
    score(1);
}

static bool fallback_handler(char *buf)
/* fallback handler for commands not handled by FORTRANish parser */
{
    long sv;
    if (sscanf(buf, "seed %ld", &sv) == 1) {
	set_seed(sv);
	printf("Seed set to %ld\n", sv);
	// autogenerated, so don't charge user time for it.
	--game.turns;
	// here we reconfigure any global game state that uses random numbers
	game.zzword=RNDVOC(3,0);
	return true;
    }
    return false;
}

/*  Check if this loc is eligible for any hints.  If been here
 *  long enough, branch to help section (on later page).  Hints
 *  all come back here eventually to finish the loop.  Ignore
 *  "HINTS" < 4 (special stuff, see database notes).
 */
static void checkhints(FILE *cmdin)
{
    if (COND[game.loc] >= game.conds) {
	for (int hint=1; hint<=HNTMAX; hint++) {
	    if (game.hinted[hint])
		continue;
	    if (!CNDBIT(game.loc,hint+10))
		game.hintlc[hint]= -1;
	    ++game.hintlc[hint];
	    /*  Come here if he's been long enough at required loc(s) for some
	     *  unused hint. */
	    if (game.hintlc[hint] >= HINTS[hint][1]) 
	    {
		int i;

		switch (hint-1)
		{
		case 0:
		    /* cave */
		    if (game.prop[GRATE] == 0 && !HERE(KEYS))
			break;
		    game.hintlc[hint]=0;
		    return;
		case 1:	/* bird */
		    if (game.place[BIRD] == game.loc && TOTING(ROD) && game.oldobj == BIRD)
			break;
		    return;
		case 2:	/* snake */
		    if (HERE(SNAKE) && !HERE(BIRD))
			break;
		    game.hintlc[hint]=0;
		    return;
		case 3:	/* maze */
		    if (game.atloc[game.loc] == 0 &&
			game.atloc[game.oldloc] == 0 &&
			game.atloc[game.oldlc2] == 0 &&
			game.holdng > 1)
			break;
		    game.hintlc[hint]=0;
		    return;
		case 4:	/* dark */
		    if (game.prop[EMRALD] != -1 && game.prop[PYRAM] == -1)
			break;
		    game.hintlc[hint]=0;
		    return;
		case 5:	/* witt */
		    break;
		case 6:	/* urn */
		    if (game.dflag == 0)
			break;
		    game.hintlc[hint]=0;
		    return;
		case 7:	/* woods */
		    if (game.atloc[game.loc] == 0 &&
			game.atloc[game.oldloc] == 0 &&
			game.atloc[game.oldlc2] == 0)
			break;
		    return;
		case 8:	/* ogre */
		    i=ATDWRF(game.loc);
		    if (i < 0) {
			game.hintlc[hint]=0;
			return;
		    }
		    if (HERE(OGRE) && i == 0)
			break;
		    return;
		case 9:	/* jade */
		    if (game.tally == 1 && game.prop[JADE] < 0)
			break;
		    game.hintlc[hint]=0;
		    return;
		default:
		    BUG(27);
		    break;
		}
    
		/* Fall through to hint display */
		game.hintlc[hint]=0;
		if (!YES(cmdin,HINTS[hint][3],0,54))
		    return;
		SETPRM(1,HINTS[hint][2],HINTS[hint][2]);
		RSPEAK(261);
		game.hinted[hint]=YES(cmdin,175,HINTS[hint][4],54);
		if (game.hinted[hint] && game.limit > 30)
		    game.limit=game.limit+30*HINTS[hint][2];
	    }
	}
    }
}

static bool dwarfmove(void)
/* Dwarves move.  Return true if player survives, false if he dies. */
{
    int kk, stick, attack;
    long TK[21];

	/*  Dwarf stuff.  See earlier comments for description of
     *  variables.  Remember sixth dwarf is pirate and is thus
     *  very different except for motion rules. */

    /*  First off, don't let the dwarves follow him into a pit or
     *  a wall.  Activate the whole mess the first time he gets as
     *  far as the hall of mists (loc 15).  If game.newloc is
     *  forbidden to pirate (in particular, if it's beyond the
     *  troll bridge), bypass dwarf stuff.  That way pirate can't
     *  steal return toll, and dwarves can't meet the bear.  Also
     *  means dwarves won't follow him into dead end in maze, but
     *  c'est la vie.  They'll wait for him outside the dead
     *  end. */
    if (game.loc == 0 || FORCED(game.loc) || CNDBIT(game.newloc,3))
	return true;

    /* Dwarf activity level ratchets up */
    if (game.dflag == 0) {
	if (INDEEP(game.loc))
	    game.dflag=1;
	return true;
    }

    /*  When we encounter the first dwarf, we kill 0, 1, or 2 of
     *  the 5 dwarves.  If any of the survivors is at loc,
     *  replace him with the alternate. */
    if (game.dflag == 1) {
	if (!INDEEP(game.loc) || (PCT(95) && (!CNDBIT(game.loc,4) || PCT(85))))
	    return true;
	game.dflag=2;
	for (int i=1; i<=2; i++) {
	    int j=1+randrange(NDWARVES-1);
	    if (PCT(50))
		game.dloc[j]=0;
	}
	for (int i=1; i<=NDWARVES-1; i++) {
	    if (game.dloc[i] == game.loc)
		game.dloc[i]=DALTLC;
	    game.odloc[i]=game.dloc[i];
	}
	RSPEAK(3);
	DROP(AXE,game.loc);
	return true;
    }

    /*  Things are in full swing.  Move each dwarf at random,
     *  except if he's seen us he sticks with us.  Dwarves stay
     *  deep inside.  If wandering at random, they don't back up
     *  unless there's no alternative.  If they don't have to
     *  move, they attack.  And, of course, dead dwarves don't do
     *  much of anything. */
    game.dtotal=0;
    attack=0;
    stick=0;
    for (int i=1; i<=NDWARVES; i++) {
	int k;
	if (game.dloc[i] == 0)
	    continue;
	/*  Fill TK array with all the places this dwarf might go. */
	int j=1;
	kk=KEY[game.dloc[i]];
	if (kk != 0)
	    do {
		game.newloc=MOD(labs(TRAVEL[kk])/1000,1000);
		/* Have we avoided a dwarf enciounter? */
		bool avoided = (game.newloc > 300 ||
				!INDEEP(game.newloc) ||
				game.newloc == game.odloc[i] ||
				(j > 1 && game.newloc == TK[j-1]) ||
				j >= 20 ||
				game.newloc == game.dloc[i] ||
				FORCED(game.newloc) ||
				(i == PIRATE && CNDBIT(game.newloc,3)) ||
				labs(TRAVEL[kk])/1000000 == 100);
		if (!avoided) {
		    TK[j++] = game.newloc;
		}
		++kk;
	    } while
		(TRAVEL[kk-1] >= 0);
	TK[j]=game.odloc[i];
	if (j >= 2)
	    --j;
	j=1+randrange(j);
	game.odloc[i]=game.dloc[i];
	game.dloc[i]=TK[j];
	game.dseen[i]=(game.dseen[i] && INDEEP(game.loc)) || (game.dloc[i] == game.loc || game.odloc[i] == game.loc);
	if (!game.dseen[i]) continue;
	game.dloc[i]=game.loc;
	if (i == PIRATE) {
	    /*  The pirate's spotted him.  He leaves him alone once we've
	     *  found chest.  K counts if a treasure is here.  If not, and
	     *  tally=1 for an unseen chest, let the pirate be spotted.
	     *  Note that game.place(CHEST)=0 might mean that he's thrown
	     *  it to the troll, but in that case he's seen the chest
	     *  (game.prop=0). */
	    if (game.loc == game.chloc || game.prop[CHEST] >= 0)
		continue;
	    k=0;
	    for (int j=MINTRS; j<=MAXTRS; j++) {
		/*  Pirate won't take pyramid from plover room or dark
		 *  room (too easy!). */
		if (j == PYRAM && (game.loc == PLAC[PYRAM] || game.loc == PLAC[EMRALD])) {
		    if (HERE(j))
			k=1;
		    continue;
		}
		if (TOTING(j)) {
		    if (game.place[CHEST] == 0) {
			/*  Install chest only once, to insure it is
			 *  the last treasure in the list. */
			MOVE(CHEST,game.chloc);
			MOVE(MESSAG,game.chloc2);
		    }
		    RSPEAK(128);
		    for (int j=MINTRS; j<=MAXTRS; j++) {
			if (!(j == PYRAM && (game.loc == PLAC[PYRAM] || game.loc == PLAC[EMRALD]))) {
			    if (AT(j) && game.fixed[j] == 0)
				CARRY(j,game.loc);
			    if (TOTING(j))
				DROP(j,game.chloc);
			}
		    }
		    game.dloc[PIRATE]=game.chloc;
		    game.odloc[PIRATE]=game.chloc;
		    game.dseen[PIRATE]=false;
		    goto jumpout;
		}
		if (HERE(j))
		    k=1;
	    }
	    /* Force chest placement before player finds last treasure */
	    if (game.tally == 1 && k == 0 && game.place[CHEST] == 0 && HERE(LAMP) && game.prop[LAMP] == 1) {
		RSPEAK(186);
		MOVE(CHEST,game.chloc);
		MOVE(MESSAG,game.chloc2);
		game.dloc[PIRATE]=game.chloc;
		game.odloc[PIRATE]=game.chloc;
		game.dseen[PIRATE]=false;
		continue;
	    }
	    if (game.odloc[PIRATE] != game.dloc[PIRATE] && PCT(20))
		RSPEAK(127);
	    continue;
	}

	/* This threatening little dwarf is in the room with him! */
	++game.dtotal;
	if (game.odloc[i] == game.dloc[i]) {
	    ++attack;
	    if (game.knfloc >= 0)
		game.knfloc=game.loc;
	    if (randrange(1000) < 95*(game.dflag-2))
		++stick;
	}
    jumpout:;
    }

    /*  Now we know what's happening.  Let's tell the poor sucker about it.
     *  Note that various of the "knife" messages must have specific relative
     *  positions in the RSPEAK database. */
    if (game.dtotal == 0)
	return true;
    SETPRM(1,game.dtotal,0);
    RSPEAK(4+1/game.dtotal);
    if (attack == 0)
	return true;
    if (game.dflag == 2)game.dflag=3;
    SETPRM(1,attack,0);
    int k=6;
    if (attack > 1)k=250;
    RSPEAK(k);
    SETPRM(1,stick,0);
    RSPEAK(k+1+2/(1+stick));
    if (stick == 0)
	return true;
    game.oldlc2=game.loc;
    return false;
}

/*  "You're dead, Jim."
 *
 *  If the current loc is zero, it means the clown got himself killed.
 *  We'll allow this maxdie times.  MAXDIE is automatically set based
 *  on the number of snide messages available.  Each death results in
 *  a message (81, 83, etc.)  which offers reincarnation; if accepted,
 *  this results in message 82, 84, etc.  The last time, if he wants
 *  another chance, he gets a snide remark as we exit.  When
 *  reincarnated, all objects being carried get dropped at game.oldlc2
 *  (presumably the last place prior to being killed) without change
 *  of props.  the loop runs backwards to assure that the bird is
 *  dropped before the cage.  (this kluge could be changed once we're
 *  sure all references to bird and cage are done by keywords.)  The
 *  lamp is a special case (it wouldn't do to leave it in the cave).
 *  It is turned off and left outside the building (only if he was
 *  carrying it, of course).  He himself is left inside the building
 *  (and heaven help him if he tries to xyzzy back into the cave
 *  without the lamp!).  game.oldloc is zapped so he can't just
 *  "retreat". */

static void croak(FILE *cmdin)
/*  Okay, he's dead.  Let's get on with it. */
{
    if (game.closng) {
	/*  He died during closing time.  No resurrection.  Tally up a
	 *  death and exit. */
	RSPEAK(131);
	++game.numdie;
	score(0);
    } else {
	++game.numdie;
	if (!YES(cmdin,79+game.numdie*2,80+game.numdie*2,54))
	    score(0);
	if (game.numdie == MAXDIE)
	    score(0);
	game.place[WATER]=0;
	game.place[OIL]=0;
	if (TOTING(LAMP))
	    game.prop[LAMP]=0;
	for (int j=1; j<=NOBJECTS; j++) {
	    int i=NOBJECTS + 1 - j;
	    if (TOTING(i)) {
		int k=game.oldlc2;
		if (i == LAMP)
		    k=1;
		DROP(i,k);
	    }
	}
	game.loc=3;
	game.oldloc=game.loc;
    }
}

/*  Given the current location in "game.loc", and a motion verb number in
 *  "K", put the new location in "game.newloc".  The current loc is saved
 *  in "game.oldloc" in case he wants to retreat.  The current
 *  game.oldloc is saved in game.oldlc2, in case he dies.  (if he
 *  does, game.newloc will be limbo, and game.oldloc will be what killed
 *  him, so we need game.oldlc2, which is the last place he was
 *  safe.) */

static bool playermove(FILE *cmdin, token_t verb, int motion)
{
    int LL, K2, KK=KEY[game.loc];
    game.newloc=game.loc;
    if (KK == 0)
	BUG(26);
    if (motion == NUL)
	return true;
    else if (motion == BACK) {
	/*  Handle "go back".  Look for verb which goes from game.loc to
	 *  game.oldloc, or to game.oldlc2 If game.oldloc has forced-motion.
	 *  K2 saves entry -> forced loc -> previous loc. */
	motion=game.oldloc;
	if (FORCED(motion))
	    motion=game.oldlc2;
	game.oldlc2=game.oldloc;
	game.oldloc=game.loc;
	K2=0;
	if (motion == game.loc)K2=91;
	if (CNDBIT(game.loc,4))K2=274;
	if (K2 == 0) {
	    for (;;) {
		LL=MOD((labs(TRAVEL[KK])/1000),1000);
		if (LL != motion) {
		    if (LL <= 300) {
			if (FORCED(LL) && MOD((labs(TRAVEL[KEY[LL]])/1000),1000) == motion)
			    K2=KK;
		    }
		    if (TRAVEL[KK] >= 0) {
			++KK;
			continue;
		    }
		    KK=K2;
		    if (KK == 0) {
			RSPEAK(140);
			return true;
		    }
		}

		motion=MOD(labs(TRAVEL[KK]),1000);
		KK=KEY[game.loc];
		break; /* fall through to ordinary travel */
	    }
	} else {
	    RSPEAK(K2);
	    return true;
	}
    }
    else if (motion == LOOK) {
	/*  Look.  Can't give more detail.  Pretend it wasn't dark
	 *  (though it may "now" be dark) so he won't fall into a
	 *  pit while staring into the gloom. */
	if (game.detail < 3)RSPEAK(15);
	++game.detail;
	game.wzdark=false;
	game.abbrev[game.loc]=0;
	return true;
    }
    else if (motion == CAVE) {
	/*  Cave.  Different messages depending on whether above ground. */
	RSPEAK((OUTSID(game.loc) && game.loc != 8) ? 57 : 58);
	return true;
    }
    else {
	/* none of the specials */
	game.oldlc2=game.oldloc;
	game.oldloc=game.loc;
    }

    /* ordinary travel */
    for (;;) {
	LL=labs(TRAVEL[KK]);
	if (MOD(LL,1000) == 1 || MOD(LL,1000) == motion)
	    break;
	if (TRAVEL[KK] < 0) {
	    /*  Non-applicable motion.  Various messages depending on
	     *  word given. */
	    int spk=12;
	    if (motion >= 43 && motion <= 50)spk=52;
	    if (motion == 29 || motion == 30)spk=52;
	    if (motion == 7 || motion == 36 || motion == 37)spk=10;
	    if (motion == 11 || motion == 19)spk=11;
	    if (verb == FIND || verb == INVENT)spk=59;
	    if (motion == 62 || motion == 65)spk=42;
	    if (motion == 17)spk=80;
	    RSPEAK(spk);
	    return true;
	}
	++KK;
    }
    LL=LL/1000;

    L12:
    for (;;) {
	game.newloc=LL/1000;
	motion=MOD(game.newloc,100);
	if (game.newloc <= 300) {
	    if (game.newloc <= 100) {
		if (game.newloc == 0 || PCT(game.newloc))
		    break;
		/* else fall through */
	    } if (TOTING(motion) || (game.newloc > 200 && AT(motion)))
		  break;
	    /* else fall through */
	}
	else if (game.prop[motion] != game.newloc/100-3)
	    break;
	do {
	    if (TRAVEL[KK] < 0)BUG(25);
	    ++KK;
	    game.newloc=labs(TRAVEL[KK])/1000;
	} while
	    (game.newloc == LL);
	LL=game.newloc;
    }

    game.newloc=MOD(LL,1000);
    if (game.newloc <= 300)
	return true;
    if (game.newloc <= 500) {
	game.newloc=game.newloc-300;
	switch (game.newloc)
	{
	case 1:
	    /*  Travel 301.  Plover-alcove passage.  Can carry only
	     *  emerald.  Note: travel table must include "useless"
	     *  entries going through passage, which can never be used for
	     *  actual motion, but can be spotted by "go back". */
	    game.newloc=99+100-game.loc;
	    if (game.holdng == 0 || (game.holdng == 1 && TOTING(EMRALD)))
		return true;
	    game.newloc=game.loc;
	    RSPEAK(117);
	    return true;
	case 2:
	    /*  Travel 302.  Plover transport.  Drop the emerald (only use
	     *  special travel if toting it), so he's forced to use the
	     *  plover-passage to get it out.  Having dropped it, go back and
	     *  pretend he wasn't carrying it after all. */
	    DROP(EMRALD,game.loc);
	    do {
		if (TRAVEL[KK] < 0)BUG(25);
		++KK;
		game.newloc=labs(TRAVEL[KK])/1000;
	    } while
		(game.newloc == LL);
	    goto L12;
	case 3:
	    /*  Travel 303.  Troll bridge.  Must be done only as special
	     *  motion so that dwarves won't wander across and encounter
	     *  the bear.  (They won't follow the player there because
	     *  that region is forbidden to the pirate.)  If
	     *  game.prop(TROLL)=1, he's crossed since paying, so step out
	     *  and block him.  (standard travel entries check for
	     *  game.prop(TROLL)=0.)  Special stuff for bear. */
	    if (game.prop[TROLL] == 1) {
		PSPEAK(TROLL,1);
		game.prop[TROLL]=0;
		MOVE(TROLL2,0);
		MOVE(TROLL2+NOBJECTS,0);
		MOVE(TROLL,PLAC[TROLL]);
		MOVE(TROLL+NOBJECTS,FIXD[TROLL]);
		JUGGLE(CHASM);
		game.newloc=game.loc;
		return true;
	    } else {
		game.newloc=PLAC[TROLL]+FIXD[TROLL]-game.loc;
		if (game.prop[TROLL] == 0)game.prop[TROLL]=1;
		if (!TOTING(BEAR)) return true;
		RSPEAK(162);
		game.prop[CHASM]=1;
		game.prop[TROLL]=2;
		DROP(BEAR,game.newloc);
		game.fixed[BEAR]= -1;
		game.prop[BEAR]=3;
		game.oldlc2=game.newloc;
		croak(cmdin);
		return false;
	    }
	}
	BUG(20);
    }
    RSPEAK(game.newloc-500);
    game.newloc=game.loc;
    return true;
}

static bool closecheck(void)
/*  Handle the closing of the cave.  The cave closes "clock1" turns
 *  after the last treasure has been located (including the pirate's
 *  chest, which may of course never show up).  Note that the
 *  treasures need not have been taken yet, just located.  Hence
 *  clock1 must be large enough to get out of the cave (it only ticks
 *  while inside the cave).  When it hits zero, we branch to 10000 to
 *  start closing the cave, and then sit back and wait for him to try
 *  to get out.  If he doesn't within clock2 turns, we close the cave;
 *  if he does try, we assume he panics, and give him a few additional
 *  turns to get frantic before we close.  When clock2 hits zero, we
 *  branch to 11000 to transport him into the final puzzle.  Note that
 *  the puzzle depends upon all sorts of random things.  For instance,
 *  there must be no water or oil, since there are beanstalks which we
 *  don't want to be able to water, since the code can't handle it.
 *  Also, we can have no keys, since there is a grate (having moved
 *  the fixed object!) there separating him from all the treasures.
 *  Most of these problems arise from the use of negative prop numbers
 *  to suppress the object descriptions until he's actually moved the
 *  objects. */
{
    if (game.tally == 0 && INDEEP(game.loc) && game.loc != 33)
	--game.clock1;

    /*  When the first warning comes, we lock the grate, destroy
     *  the bridge, kill all the dwarves (and the pirate), remove
     *  the troll and bear (unless dead), and set "closng" to
     *  true.  Leave the dragon; too much trouble to move it.
     *  from now until clock2 runs out, he cannot unlock the
     *  grate, move to any location outside the cave, or create
     *  the bridge.  Nor can he be resurrected if he dies.  Note
     *  that the snake is already gone, since he got to the
     *  treasure accessible only via the hall of the mountain
     *  king. Also, he's been in giant room (to get eggs), so we
     *  can refer to it.  Also also, he's gotten the pearl, so we
     *  know the bivalve is an oyster.  *And*, the dwarves must
     *  have been activated, since we've found chest. */
    if (game.clock1 == 0)
    {
	game.prop[GRATE]=0;
	game.prop[FISSUR]=0;
	for (int i=1; i<=NDWARVES; i++) {
	    game.dseen[i]=false;
	    game.dloc[i]=0;
	}
	MOVE(TROLL,0);
	MOVE(TROLL+NOBJECTS,0);
	MOVE(TROLL2,PLAC[TROLL]);
	MOVE(TROLL2+NOBJECTS,FIXD[TROLL]);
	JUGGLE(CHASM);
	if (game.prop[BEAR] != 3)DSTROY(BEAR);
	game.prop[CHAIN]=0;
	game.fixed[CHAIN]=0;
	game.prop[AXE]=0;
	game.fixed[AXE]=0;
	RSPEAK(129);
	game.clock1= -1;
	game.closng=true;
	return true;
    } else if (game.clock1 < 0)
	--game.clock2;
    if (game.clock2 == 0) {
	/*  Once he's panicked, and clock2 has run out, we come here
	 *  to set up the storage room.  The room has two locs,
	 *  hardwired as 115 (ne) and 116 (sw).  At the ne end, we
	 *  place empty bottles, a nursery of plants, a bed of
	 *  oysters, a pile of lamps, rods with stars, sleeping
	 *  dwarves, and him.  At the sw end we place grate over
	 *  treasures, snake pit, covey of caged birds, more rods, and
	 *  pillows.  A mirror stretches across one wall.  Many of the
	 *  objects come from known locations and/or states (e.g. the
	 *  snake is known to have been destroyed and needn't be
	 *  carried away from its old "place"), making the various
	 *  objects be handled differently.  We also drop all other
	 *  objects he might be carrying (lest he have some which
	 *  could cause trouble, such as the keys).  We describe the
	 *  flash of light and trundle back. */
	game.prop[BOTTLE]=PUT(BOTTLE,115,1);
	game.prop[PLANT]=PUT(PLANT,115,0);
	game.prop[OYSTER]=PUT(OYSTER,115,0);
	OBJTXT[OYSTER]=3;
	game.prop[LAMP]=PUT(LAMP,115,0);
	game.prop[ROD]=PUT(ROD,115,0);
	game.prop[DWARF]=PUT(DWARF,115,0);
	game.loc=115;
	game.oldloc=115;
	game.newloc=115;
	/*  Leave the grate with normal (non-negative) property.
	 *  Reuse sign. */
	PUT(GRATE,116,0);
	PUT(SIGN,116,0);
	++OBJTXT[SIGN];
	game.prop[SNAKE]=PUT(SNAKE,116,1);
	game.prop[BIRD]=PUT(BIRD,116,1);
	game.prop[CAGE]=PUT(CAGE,116,0);
	game.prop[ROD2]=PUT(ROD2,116,0);
	game.prop[PILLOW]=PUT(PILLOW,116,0);

	game.prop[MIRROR]=PUT(MIRROR,115,0);
	game.fixed[MIRROR]=116;

	for (int i=1; i<=NOBJECTS; i++) {
	    if (TOTING(i))
		DSTROY(i);
	}

	RSPEAK(132);
	game.closed=true;
	return true;
    }

    return false;
}

static void lampcheck(void)
/* Check game limit and lamp timers */
{
    if (game.prop[LAMP] == 1)
	--game.limit;

    /*  Another way we can force an end to things is by having the
     *  lamp give out.  When it gets close, we come here to warn
     *  him.  First following ar, if the lamp and fresh batteries are
     *  here, in which case we replace the batteries and continue.
     *  Second is for other cases of lamp dying.  12400 is when it
     *  goes out.  Even then, he can explore outside for a while
     *  if desired. */
    if (game.limit<=30 && HERE(BATTER) && game.prop[BATTER]==0 && HERE(LAMP))
    {
	RSPEAK(188);
	game.prop[BATTER]=1;
	if (TOTING(BATTER))
	    DROP(BATTER,game.loc);
	game.limit=game.limit+2500;
	game.lmwarn=false;
    } else if (game.limit == 0) {
	game.limit= -1;
	game.prop[LAMP]=0;
	if (HERE(LAMP))
	    RSPEAK(184);
    } else if (game.limit <= 30) {
	if (!game.lmwarn && HERE(LAMP)) {
	    game.lmwarn=true;
	    int spk=187;
	    if (game.place[BATTER] == 0)spk=183;
	    if (game.prop[BATTER] == 1)spk=189;
	    RSPEAK(spk);
	}
    }
}

static void listobjects(void)
/*  Print out descriptions of objects at this location.  If
 *  not closing and property value is negative, tally off
 *  another treasure.  Rug is special case; once seen, its
 *  game.prop is 1 (dragon on it) till dragon is killed.
 *  Similarly for chain; game.prop is initially 1 (locked to
 *  bear).  These hacks are because game.prop=0 is needed to
 *  get full score. */
{
    if (!DARK(game.loc)) {
	long obj;
	++game.abbrev[game.loc];
	for (int i=game.atloc[game.loc]; i != 0; i=game.link[i]) {
	    obj=i;
	    if (obj > NOBJECTS)obj=obj-NOBJECTS;
	    if (obj == STEPS && TOTING(NUGGET))
		continue;
	    if (game.prop[obj] < 0) {
		if (game.closed)
		    continue;
		game.prop[obj]=0;
		if (obj == RUG || obj == CHAIN)
		    game.prop[obj]=1;
		--game.tally;
		/*  Note: There used to be a test here to see whether the
		 *  player had blown it so badly that he could never ever see
		 *  the remaining treasures, and if so the lamp was zapped to
		 *  35 turns.  But the tests were too simple-minded; things
		 *  like killing the bird before the snake was gone (can never
		 *  see jewelry), and doing it "right" was hopeless.  E.G.,
		 *  could cross troll bridge several times, using up all
		 *  available treasures, breaking vase, using coins to buy
		 *  batteries, etc., and eventually never be able to get
		 *  across again.  If bottle were left on far side, could then
		 *  never get eggs or trident, and the effects propagate.  So
		 *  the whole thing was flushed.  anyone who makes such a
		 *  gross blunder isn't likely to find everything else anyway
		 *  (so goes the rationalisation). */
	    }
	    int kk=game.prop[obj];
	    if (obj == STEPS && game.loc == game.fixed[STEPS])
		kk=1;
	    PSPEAK(obj,kk);
	}
    }
}

static bool do_command(FILE *cmdin)
/* Get and execute a command */ 
{
    long KQ, VERB, KK, V1, V2;
    long i, k, KMOD;
    static long igo = 0;
    static long obj = 0;
    enum speechpart part;

    /*  Can't leave cave once it's closing (except by main office). */
    if (OUTSID(game.newloc) && game.newloc != 0 && game.closng) {
	RSPEAK(130);
	game.newloc=game.loc;
	if (!game.panic)game.clock2=15;
	game.panic=true;
    }

    /*  See if a dwarf has seen him and has come from where he
     *  wants to go.  If so, the dwarf's blocking his way.  If
     *  coming from place forbidden to pirate (dwarves rooted in
     *  place) let him get out (and attacked). */
    if (game.newloc != game.loc && !FORCED(game.loc) && !CNDBIT(game.loc,3)) {
	for (i=1; i<=NDWARVES-1; i++) {
	    if (game.odloc[i] == game.newloc && game.dseen[i]) {
		game.newloc=game.loc;
		RSPEAK(2);
		break;
	    }
	}
    }
    game.loc=game.newloc;

    if (!dwarfmove())
	croak(cmdin);

    /*  Describe the current location and (maybe) get next command. */

    for (;;) {
	if (game.loc == 0)
	    croak(cmdin);
	char* msg = short_location_descriptions[game.loc];
	if (MOD(game.abbrev[game.loc],game.abbnum) == 0 || msg == 0)
	    msg=long_location_descriptions[game.loc];
	if (!FORCED(game.loc) && DARK(game.loc)) {
	    /*  The easiest way to get killed is to fall into a pit in
	     *  pitch darkness. */
	    if (game.wzdark && PCT(35)) {
		RSPEAK(23);
		game.oldlc2 = game.loc;
		croak(cmdin);
		continue;	/* back to top of main interpreter loop */
	    }
	    msg=arbitrary_messages[16];
	}
	if (TOTING(BEAR))RSPEAK(141);
	newspeak(msg);
	if (FORCED(game.loc)) {
	    if (playermove(cmdin, VERB, 1))
		return true;
	    else
		continue;	/* back to top of main interpreter loop */
	}
	if (game.loc == 33 && PCT(25) && !game.closng)RSPEAK(7);

	listobjects();

    L2012:
	VERB=0;
	game.oldobj=obj;
	obj=0;

    L2600:
	checkhints(cmdin);

	/*  If closing time, check for any objects being toted with
	 *  game.prop < 0 and set the prop to -1-game.prop.  This way
	 *  objects won't be described until they've been picked up
	 *  and put down separate from their respective piles.  Don't
	 *  tick game.clock1 unless well into cave (and not at Y2). */
	if (game.closed) {
	    if (game.prop[OYSTER] < 0 && TOTING(OYSTER))
		PSPEAK(OYSTER,1);
	    for (i=1; i<=NOBJECTS; i++) {
		if (TOTING(i) && game.prop[i] < 0)
		    game.prop[i] = -1-game.prop[i];
	    }
	}
	game.wzdark=DARK(game.loc);
	if (game.knfloc > 0 && game.knfloc != game.loc)
	    game.knfloc=0;

	/* This is where we get a new command from the user */
	if (!GETIN(cmdin, &WD1,&WD1X,&WD2,&WD2X))
	    return false;

	/*  Every input, check "game.foobar" flag.  If zero, nothing's
	 *  going on.  If pos, make neg.  If neg, he skipped a word,
	 *  so make it zero. */
    L2607:
	game.foobar=(game.foobar>0 ? -game.foobar : 0);
	++game.turns;
	if (game.turns == game.thresh) {
	    newspeak(turn_threshold_messages[game.trndex]);
	    game.trnluz=game.trnluz+TRNVAL[game.trndex]/100000;
	    ++game.trndex;
	    game.thresh = -1;
	    if (game.trndex <= TRNVLS)
		game.thresh=MOD(TRNVAL[game.trndex],100000)+1;
	}
	if (VERB == SAY && WD2 > 0)
	    VERB=0;
	if (VERB == SAY) {
	    part=transitive;
	    goto Laction;
	}
	if (closecheck())
	    if (game.closed)
		return true;
	    else
		goto L19999;

	lampcheck();

    L19999:
	k=43;
	if (LIQLOC(game.loc) == WATER)k=70;
	V1=VOCAB(WD1,-1);
	V2=VOCAB(WD2,-1);
	if (V1 == ENTER && (V2 == STREAM || V2 == 1000+WATER)) {
	    RSPEAK(k);
	    goto L2012;
	}
	if (V1 == ENTER && WD2 > 0) {
	    WD1=WD2;
	    WD1X=WD2X;
	    WD2=0;
	} else {
	    if (!((V1 != 1000+WATER && V1 != 1000+OIL) ||
		  (V2 != 1000+PLANT && V2 != 1000+DOOR))) {
		if (AT(V2-1000))
		    WD2=MAKEWD(16152118);
	    }
	    if (V1 == 1000+CAGE && V2 == 1000+BIRD && HERE(CAGE) && HERE(BIRD))
		WD1=MAKEWD(301200308);
	}
    L2620:
	if (WD1 == MAKEWD(23051920)) {
	    ++game.iwest;
	    if (game.iwest == 10)
		RSPEAK(17);
	}
	if (WD1 == MAKEWD( 715) && WD2 != 0) {
	    if (++igo == 10)
		RSPEAK(276);
	}
    L2630:
	i=VOCAB(WD1,-1);
	if (i == -1) {
	    /* Gee, I don't understand. */
	    if (fallback_handler(rawbuf))
		return true;
	    SETPRM(1,WD1,WD1X);
	    RSPEAK(254);
	    goto L2600;
	}
	KMOD=MOD(i,1000);
	KQ=i/1000+1;
	switch (KQ-1)
	{
	case 0:
	    if (playermove(cmdin, VERB, KMOD))
		return true;
	    else
		continue;	/* back to top of main interpreter loop */
	case 1: part=unknown; obj = KMOD; break;
	case 2: part=intransitive; VERB = KMOD; break;
	case 3: RSPEAK(KMOD); goto L2012;
	default: BUG(22);
	}

    Laction:
	switch (action(cmdin, part, VERB, obj)) {
	case GO_TERMINATE:
	    return true;
	case GO_MOVE: 
	    playermove(cmdin, VERB, NUL);
	    return true;
	case GO_TOP: continue;	/* back to top of main interpreter loop */
	case GO_CLEAROBJ: goto L2012;
	case GO_CHECKHINT: goto L2600;
	case GO_CHECKFOO: goto L2607;
	case GO_LOOKUP: goto L2630;
	case GO_WORD2:
	    /* Get second word for analysis. */
	    WD1=WD2;
	    WD1X=WD2X;
	    WD2=0;
	    goto L2620;
	case GO_UNKNOWN:
	    /*  Random intransitive verbs come here.  Clear obj just in case
	     *  (see attack()). */
	    SETPRM(1,WD1,WD1X);
	    RSPEAK(257);
	    obj=0;
	    goto L2600;
	case GO_DWARFWAKE:
	    /*  Oh dear, he's disturbed the dwarves. */
	    RSPEAK(136);
	    score(0);
	    return true;
	default:
	    BUG(99);
	}
    }
}

/* end */
