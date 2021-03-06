// Copyright (c) Hercules Dev Team, licensed under GNU GPL.
// See the LICENSE file
// Portions Copyright (c) Athena Dev Teams

#include "../common/mmo.h"
#include "../common/showmsg.h"
#include "../common/malloc.h"
#include "../common/strlib.h"
#include "core.h"
#include "../common/console.h"

#ifndef MINICORE
	#include "../common/db.h"
	#include "../common/socket.h"
	#include "../common/timer.h"
	#include "../common/thread.h"
	#include "../common/mempool.h"
	#include "../common/sql.h"
	#include "../config/core.h"
	#include "../common/HPM.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#else
#include "../common/winapi.h" // Console close event handling
#endif

/// Called when a terminate signal is received.
void (*shutdown_callback)(void) = NULL;

int runflag = CORE_ST_RUN;
int arg_c = 0;
char **arg_v = NULL;

char *SERVER_NAME = NULL;

#ifndef MINICORE	// minimalist Core
// Added by Gabuzomeu
//
// This is an implementation of signal() using sigaction() for portability.
// (sigaction() is POSIX; signal() is not.)  Taken from Stevens' _Advanced
// Programming in the UNIX Environment_.
//
#ifdef WIN32	// windows don't have SIGPIPE
#define SIGPIPE SIGINT
#endif

#ifndef POSIX
#define compat_signal(signo, func) signal(signo, func)
#else
sigfunc *compat_signal(int signo, sigfunc *func) {
	struct sigaction sact, oact;

	sact.sa_handler = func;
	sigemptyset(&sact.sa_mask);
	sact.sa_flags = 0;
#ifdef SA_INTERRUPT
	sact.sa_flags |= SA_INTERRUPT;	/* SunOS */
#endif

	if (sigaction(signo, &sact, &oact) < 0)
		return (SIG_ERR);

	return (oact.sa_handler);
}
#endif

/*======================================
 *	CORE : Console events for Windows
 *--------------------------------------*/
#ifdef _WIN32
static BOOL WINAPI console_handler(DWORD c_event) {
    switch(c_event) {
		case CTRL_CLOSE_EVENT:
		case CTRL_LOGOFF_EVENT:
		case CTRL_SHUTDOWN_EVENT:
			if( shutdown_callback != NULL )
				shutdown_callback();
			else
				runflag = CORE_ST_STOP;// auto-shutdown
			break;
		default:
			return FALSE;
    }
    return TRUE;
}

static void cevents_init() {
	if (SetConsoleCtrlHandler(console_handler,TRUE)==FALSE)
		ShowWarning ("Unable to install the console handler!\n");
}
#endif

/*======================================
 *	CORE : Signal Sub Function
 *--------------------------------------*/
static void sig_proc(int sn) {
	static int is_called = 0;

	switch (sn) {
		case SIGINT:
		case SIGTERM:
			if (++is_called > 3)
				exit(EXIT_SUCCESS);
			if( shutdown_callback != NULL )
				shutdown_callback();
			else
				runflag = CORE_ST_STOP;// auto-shutdown
			break;
		case SIGSEGV:
		case SIGFPE:
			do_abort();
			// Pass the signal to the system's default handler
			compat_signal(sn, SIG_DFL);
			raise(sn);
			break;
	#ifndef _WIN32
		case SIGXFSZ:
			// ignore and allow it to set errno to EFBIG
			ShowWarning ("Max file size reached!\n");
			//run_flag = 0;	// should we quit?
			break;
		case SIGPIPE:
			//ShowInfo ("Broken pipe found... closing socket\n");	// set to eof in socket.c
			break;	// does nothing here
	#endif
	}
}

void signals_init (void) {
	compat_signal(SIGTERM, sig_proc);
	compat_signal(SIGINT, sig_proc);
#ifndef _DEBUG // need unhandled exceptions to debug on Windows
	compat_signal(SIGSEGV, sig_proc);
	compat_signal(SIGFPE, sig_proc);
#endif
#ifndef _WIN32
	compat_signal(SIGILL, SIG_DFL);
	compat_signal(SIGXFSZ, sig_proc);
	compat_signal(SIGPIPE, sig_proc);
	compat_signal(SIGBUS, SIG_DFL);
	compat_signal(SIGTRAP, SIG_DFL);
#endif
}
#endif

const char *versao () {
	static char vers[13]="";
		FILE *fp;
    if((fp=fopen("conf/import/versao.txt","r")) != NULL){
        fgets(vers, 12, fp);
	} else {
		strcpy(vers,"Desconhecida");
	}
	
	fclose(fp);

	return vers;
}
// Warning if executed as superuser (root)
void usercheck(void) {
#ifndef _WIN32
    if (geteuid() == 0) {
		ShowWarning ("Voc� est� rodando o Cronus com privil�gios root, isto n�o � necess�rio.\n");
    }
#endif
}
void core_defaults(void) {
#ifndef MINICORE
	hpm_defaults();
#endif
	console_defaults();
	strlib_defaults();
	malloc_defaults();
#ifndef MINICORE
	sql_defaults();
	timer_defaults();
	db_defaults();
#endif
}
/*======================================
 *	CORE : MAINROUTINE
 *--------------------------------------*/
int main (int argc, char **argv) {
	{// initialize program arguments
		char *p1 = SERVER_NAME = argv[0];
		char *p2 = p1;
		while ((p1 = strchr(p2, '/')) != NULL || (p1 = strchr(p2, '\\')) != NULL) {
			SERVER_NAME = ++p1;
			p2 = p1;
		}
		arg_c = argc;
		arg_v = argv;
	}
	core_defaults();
	
	iMalloc->init();// needed for Show* in display_title() [FlavioJS]
	
	console->display_title();
	
#ifdef MINICORE // minimalist Core
	usercheck();
	do_init(argc,argv);
	do_final();
#else// not MINICORE
	set_server_type();
	usercheck();

	Sql_Init();
	rathread_init();
	mempool_init();
	DB->init();
	signals_init();
	
#ifdef _WIN32
	cevents_init();
#endif

	iTimer->init();

	console->init();

#ifndef MINICORE
	HPM->init();
#endif
		
	socket_init();

	do_init(argc,argv);
	{// Main runtime cycle
		int next;
		while (runflag != CORE_ST_STOP) {
			next = iTimer->do_timer(iTimer->gettick_nocache());
			do_sockets(next);
		}
	}

	console->final();
	
	do_final();
#ifndef MINICORE
	HPM->final();
#endif
	iTimer->final();
	socket_final();
	DB->final();
	mempool_final();
	rathread_final();
#endif

	iMalloc->final();

	return 0;
}
