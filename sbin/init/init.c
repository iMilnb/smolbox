/*	$NetBSD: init.c,v 1.109 2021/10/11 20:23:25 jmcneill Exp $	*/

/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Donn Seeley at Berkeley Software Design, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <ttyent.h>
#include <err.h>
#include <util.h>
#include <unistd.h>

#include "pathnames.h"

#define	GETTY_SPACING		5	/* N secs minimum getty spacing */
#define	GETTY_SLEEP		30	/* sleep N secs after spacing problem */
#define	GETTY_NSPACE		5	/* N retries before spacing problem */
#define	STALL_TIMEOUT		30	/* wait N secs after warning */
#define	DEATH_WATCH		10	/* wait N secs for procs to die */

typedef long (*state_func_t)(void);
typedef state_func_t (*state_t)(void);

static void handle(sig_t, ...);
static void delset(sigset_t *, ...);

static void stall(const char *, ...) __attribute__((__format__(__printf__, 1, 2)));
static void warning(const char *, ...) __attribute__((__format__(__printf__, 1, 2)));
static void emergency(const char *, ...) __attribute__((__format__(__printf__, 1, 2)));
__attribute__((__noreturn__)) static void disaster(int);
static void badsys(int);

static state_func_t single_user(void);
static state_func_t runcom(void);
static state_func_t read_ttys(void);
static state_func_t multi_user(void);
static state_func_t clean_ttys(void);
static state_func_t catatonia(void);
static state_func_t death(void);

static void transition(state_t);
static void setctty(const char *);

typedef struct init_session {
	pid_t	se_process;		/* controlling process */
	struct timeval	se_started;	/* used to avoid thrashing */
	int	se_nspace;		/* spacing count */
	int	se_flags;		/* status of session */
#define	SE_SHUTDOWN	0x1		/* session won't be restarted */
#define	SE_PRESENT	0x2		/* session is in /etc/ttys */
	char	*se_device;		/* filename of port */
	char	*se_getty;		/* what to run on that port */
	char	**se_getty_argv;	/* pre-parsed argument array */
	struct	init_session *se_next;
} session_t;

static session_t *sessions;

static void collect_child(pid_t, int);
static void transition_handler(int);
static void alrm_handler(int);
static int do_setttyent(void);
static char **construct_argv(char *);
static pid_t start_getty(session_t *);
static void free_session(session_t *);
static session_t *new_session(session_t *, struct ttyent *);
static int setupargv(session_t *, struct ttyent *);
static session_t *find_session(pid_t);

static state_t requested_transition = read_ttys;
static int clang;

static enum { AUTOBOOT, FASTBOOT } runcom_mode = AUTOBOOT;

/*
 * The mother of all processes.
 */
int
main_init(int argc, char **argv)
{
	struct sigaction sa;
	sigset_t mask;
	int c;

	/* Dispose of random users. */
	if (getuid() != 0) {
		errno = EPERM;
		err(1, NULL);
	}

	/* System V users like to reexec init. */
	if (getpid() != 1)
		errx(1, "already running");

	/*
	 * Create an initial session.
	 */
	if (setsid() < 0)
		warn("initial setsid() failed");

	/*
	 * Establish an initial user so that programs running
	 * single user do not freak out and die (like passwd).
	 */
	if (setlogin("root") < 0)
		warn("setlogin() failed");

	/*
	 * Note that this does NOT open a file...
	 */
	openlog("init", LOG_CONS, LOG_AUTH);

	/*
	 * Parse command-line flags.
	 */
	while ((c = getopt(argc, argv, "sf")) != -1)
		switch (c) {
		case 's':
			requested_transition = single_user;
			break;
		case 'f':
			runcom_mode = FASTBOOT;
			break;
		default:
			warning("unrecognized flag `%c'", c);
			break;
		}

	if (optind != argc)
		warning("ignoring excess arguments");

	/*
	 * We catch or block signals rather than ignore them,
	 * so that they get reset on exec.
	 */
	handle(badsys, SIGSYS, 0);
	handle(disaster, SIGABRT, SIGFPE, SIGILL, SIGSEGV,
	    SIGBUS, SIGXCPU, SIGXFSZ, 0);
	handle(transition_handler, SIGHUP, SIGTERM, SIGTSTP, 0);
	handle(alrm_handler, SIGALRM, 0);
	(void)sigfillset(&mask);
	delset(&mask, SIGABRT, SIGFPE, SIGILL, SIGSEGV, SIGBUS, SIGSYS,
	    SIGXCPU, SIGXFSZ, SIGHUP, SIGTERM, SIGTSTP, SIGALRM, 0);
	(void)sigprocmask(SIG_SETMASK, &mask, NULL);
	(void)sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = SIG_IGN;
	(void)sigaction(SIGTTIN, &sa, NULL);
	(void)sigaction(SIGTTOU, &sa, NULL);

	/*
	 * Paranoia - close std file descriptors.
	 */
	(void)close(0);
	(void)close(1);
	(void)close(2);

	/*
	 * Start the state machine.
	 */
	transition(requested_transition);

	/*
	 * Should never reach here.
	 */
	return 1;
}

/*
 * Associate a function with a signal handler.
 */
static void
handle(sig_t handler, ...)
{
	int sig;
	struct sigaction sa;
	sigset_t mask_everything;
	va_list ap;

	va_start(ap, handler);

	sa.sa_handler = handler;
	(void)sigfillset(&mask_everything);

	while ((sig = va_arg(ap, int)) != 0) {
		sa.sa_mask = mask_everything;
		sa.sa_flags = sig == SIGCHLD ? SA_NOCLDSTOP : 0;
		(void)sigaction(sig, &sa, NULL);
	}
	va_end(ap);
}

/*
 * Delete a set of signals from a mask.
 */
static void
delset(sigset_t *maskp, ...)
{
	int sig;
	va_list ap;

	va_start(ap, maskp);

	while ((sig = va_arg(ap, int)) != 0)
		(void)sigdelset(maskp, sig);
	va_end(ap);
}

/*
 * Log a message and sleep for a while.
 */
static void
stall(const char *message, ...)
{
	va_list ap;

	va_start(ap, message);
	vsyslog(LOG_ALERT, message, ap);
	va_end(ap);
	closelog();
	(void)sleep(STALL_TIMEOUT);
}

/*
 * Like stall(), but doesn't sleep.
 */
static void
warning(const char *message, ...)
{
	va_list ap;

	va_start(ap, message);
	vsyslog(LOG_ALERT, message, ap);
	va_end(ap);
	closelog();
}

/*
 * Log an emergency message.
 */
static void
emergency(const char *message, ...)
{
	va_list ap;

	va_start(ap, message);
	vsyslog(LOG_EMERG, message, ap);
	va_end(ap);
	closelog();
}

/*
 * Catch an unexpected signal.
 */
static void
disaster(int sig)
{

	emergency("fatal signal: %s", strsignal(sig));
	(void)sleep(STALL_TIMEOUT);
	_exit(sig);		/* reboot */
}

/*
 * Catch a SIGSYS signal.
 */
static void
badsys(int sig)
{
	static int badcount = 0;

	if (badcount++ < 25)
		return;
	disaster(sig);
}

/*
 * Change states in the finite state machine.
 */
static void
transition(state_t s)
{

	if (s == NULL)
		return;
	for (;;) {
		s = (state_t)(*s)();
	}
}

/*
 * Start a session and allocate a controlling terminal.
 * Only called by children of init after forking.
 */
static void
setctty(const char *name)
{
	int fd;

	(void)revoke(name);
	if ((fd = open(name, O_RDWR)) == -1) {
		stall("can't open %s: %m", name);
		_exit(1);
	}
	if (login_tty(fd) == -1) {
		stall("can't get %s for controlling terminal: %m", name);
		_exit(2);
	}
}

/*
 * Bring the system up single user.
 */
static state_func_t
single_user(void)
{
	pid_t pid, wpid;
	int status;
	struct sigaction sa, satstp, sahup;
	const char *argv[2];

	/*
	 * If the kernel is in secure mode, downgrade it to insecure mode.
	 */
	(void)sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = SIG_IGN;
	(void)sigaction(SIGTSTP, &sa, &satstp);
	(void)sigaction(SIGHUP, &sa, &sahup);
	if ((pid = fork()) == 0) {
		/*
		 * Start the single user session.
		 */
		if (access(_PATH_CONSOLE, F_OK) == 0)
			setctty(_PATH_CONSOLE);
		else
			setctty("/dev/tty");

		/*
		 * Unblock signals.
		 */
		sigset_t mask;

		(void)sigemptyset(&mask);
		(void)sigprocmask(SIG_SETMASK, &mask, NULL);

		/*
		 * Fire off a shell.
		 */
		argv[0] = "-sh";
		argv[1] = NULL;
		(void)setenv("PATH", _PATH_STDPATH, 1);
		(void)execv(_PATH_BSHELL, (char *const *)argv);
		emergency("can't exec `%s' for single user: %m", _PATH_BSHELL);
		(void)sleep(STALL_TIMEOUT);
		_exit(3);
	}

	if (pid == -1) {
		emergency("can't fork single-user shell: %m, trying again");
		while (waitpid(-1, NULL, WNOHANG) > 0)
			continue;
		(void)sigaction(SIGTSTP, &satstp, NULL);
		(void)sigaction(SIGHUP, &sahup, NULL);
		return (state_func_t)single_user;
	}

	requested_transition = 0;
	do {
		if ((wpid = waitpid(-1, &status, WUNTRACED)) != -1)
			collect_child(wpid, status);
		if (wpid == -1) {
			if (errno == EINTR)
				continue;
			warning("wait for single-user shell failed: %m; "
			    "restarting");
			return (state_func_t)single_user;
		}
		if (wpid == pid && WIFSTOPPED(status)) {
			warning("shell stopped, restarting");
			(void)kill(pid, SIGCONT);
			wpid = -1;
		}
	} while (wpid != pid && !requested_transition);

	if (requested_transition) {
		(void)sigaction(SIGTSTP, &satstp, NULL);
		(void)sigaction(SIGHUP, &sahup, NULL);
		return (state_func_t)requested_transition;
	}

	if (WIFSIGNALED(status)) {
		if (WTERMSIG(status) == SIGKILL) {
			/* executed /sbin/reboot; wait for the end quietly */
			sigset_t s;

			(void)sigfillset(&s);
			for (;;)
				(void)sigsuspend(&s);
		} else {
			warning("single user shell terminated (%x), restarting",
			    status);
			(void)sigaction(SIGTSTP, &satstp, NULL);
			(void)sigaction(SIGHUP, &sahup, NULL);
			return (state_func_t)single_user;
		}
	}

	runcom_mode = FASTBOOT;
	(void)sigaction(SIGTSTP, &satstp, NULL);
	(void)sigaction(SIGHUP, &sahup, NULL);
	return (state_func_t)runcom;
}

/*
 * Run the system startup script.
 */
static state_func_t
runcom(void)
{
	pid_t pid, wpid;
	int status;
	const char *argv[4];
	struct sigaction sa;

	switch ((pid = fork())) {
	case 0:
		(void)sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sa.sa_handler = SIG_IGN;
		(void)sigaction(SIGTSTP, &sa, NULL);
		(void)sigaction(SIGHUP, &sa, NULL);

		setctty(_PATH_CONSOLE);

		argv[0] = "sh";
		argv[1] = _PATH_RUNCOM;
		argv[2] = (runcom_mode == AUTOBOOT ? "autoboot" : NULL);
		argv[3] = NULL;

		(void)sigprocmask(SIG_SETMASK, &sa.sa_mask, NULL);

		(void)execv(_PATH_BSHELL, (char *const *)argv);
		stall("can't exec `%s' for `%s': %m", _PATH_BSHELL, _PATH_RUNCOM);
		_exit(5);	/* force single user mode */
		/*NOTREACHED*/
	case -1:
		emergency("can't fork for `%s' on `%s': %m", _PATH_BSHELL,
		    _PATH_RUNCOM);
		while (waitpid(-1, NULL, WNOHANG) > 0)
			continue;
		(void)sleep(STALL_TIMEOUT);
		return (state_func_t)single_user;
	default:
		break;
	}

	do {
		if ((wpid = waitpid(-1, &status, WUNTRACED)) != -1)
			collect_child(wpid, status);
		if (wpid == -1) {
			if (errno == EINTR)
				continue;
			warning("wait for `%s' on `%s' failed: %m; going to "
			    "single user mode", _PATH_BSHELL, _PATH_RUNCOM);
			return (state_func_t)single_user;
		}
		if (wpid == pid && WIFSTOPPED(status)) {
			warning("`%s' on `%s' stopped, restarting",
			    _PATH_BSHELL, _PATH_RUNCOM);
			(void)kill(pid, SIGCONT);
			wpid = -1;
		}
	} while (wpid != pid);

	if (WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM &&
	    requested_transition == catatonia) {
		sigset_t s;

		(void)sigfillset(&s);
		for (;;)
			(void)sigsuspend(&s);
	}

	if (!WIFEXITED(status)) {
		warning("`%s' on `%s' terminated abnormally, going to "
		    "single user mode", _PATH_BSHELL, _PATH_RUNCOM);
		return (state_func_t)single_user;
	}

	if (WEXITSTATUS(status))
		return (state_func_t)single_user;

	return (state_func_t)read_ttys;
}

/*
 * Walk the list of ttys and create sessions for each active line.
 */
static state_func_t
read_ttys(void)
{
	session_t *sp, *snext;
	struct ttyent *typ;

	/*
	 * Destroy any previous session state.
	 */
	for (sp = sessions; sp; sp = snext) {
		snext = sp->se_next;
		free_session(sp);
	}
	sessions = NULL;

	(void)do_setttyent();

	/*
	 * Allocate a session entry for each active port.
	 */
	while ((typ = getttyent()) != NULL)
		if ((snext = new_session(sp, typ)) != NULL)
			sp = snext;
	(void)endttyent();

	return (state_func_t)multi_user;
}

/*
 * Construct an argument vector from a command line.
 */
static char **
construct_argv(char *command)
{
	int argc = 0;
	char **argv;
	static const char separators[] = " \t";

	argv = malloc(((strlen(command) + 1) / 2 + 1) * sizeof(char *));
	if (argv == NULL)
		return NULL;

	if ((argv[argc++] = strtok(command, separators)) == NULL) {
		free(argv);
		return NULL;
	}
	while ((argv[argc++] = strtok(NULL, separators)) != NULL)
		continue;
	return argv;
}

/*
 * Calculate getty argv vector.
 */
static int
setupargv(session_t *sp, struct ttyent *typ)
{

	if (sp->se_getty) {
		free(sp->se_getty);
		free(sp->se_getty_argv);
	}
	if (asprintf(&sp->se_getty, "%s %s", typ->ty_getty, typ->ty_name) < 0)
		return 0;
	sp->se_getty_argv = construct_argv(sp->se_getty);
	if (sp->se_getty_argv == NULL) {
		warning("can't parse getty for port `%s'", sp->se_device);
		free(sp->se_getty);
		sp->se_getty = NULL;
		return 0;
	}
	return 1;
}

/*
 * Allocate a new session descriptor.
 */
static session_t *
new_session(session_t *sprev, struct ttyent *typ)
{
	session_t *sp;

	if ((typ->ty_status & TTY_ON) == 0 || typ->ty_name == NULL ||
	    typ->ty_getty == NULL)
		return NULL;

	sp = malloc(sizeof(session_t));
	if (sp == NULL)
		return NULL;
	(void)memset(sp, 0, sizeof(*sp));

	sp->se_flags = SE_PRESENT;
	(void)asprintf(&sp->se_device, "/dev/%s", typ->ty_name);
	if (!sp->se_device) {
		free(sp);
		return NULL;
	}

	if (setupargv(sp, typ) == 0) {
		free_session(sp);
		return NULL;
	}

	sp->se_next = NULL;
	if (sprev == NULL)
		sessions = sp;
	else
		sprev->se_next = sp;

	return sp;
}

/*
 * Deallocate a session descriptor.
 */
static void
free_session(session_t *sp)
{

	free(sp->se_device);
	if (sp->se_getty) {
		free(sp->se_getty);
		free(sp->se_getty_argv);
	}
	free(sp);
}

/*
 * Look up a login session by pid.
 */
static session_t *
find_session(pid_t pid)
{
	session_t *sp;

	for (sp = sessions; sp; sp = sp->se_next)
		if (sp->se_process == pid)
			return sp;
	return NULL;
}

/*
 * Start a login session running.
 */
static pid_t
start_getty(session_t *sp)
{
	pid_t pid;
	sigset_t mask;
	time_t current_time;
	int do_sleep = 0;

	current_time = time(NULL);
	if (current_time >= sp->se_started.tv_sec &&
	    current_time - sp->se_started.tv_sec < GETTY_SPACING) {
		if (++sp->se_nspace > GETTY_NSPACE) {
			sp->se_nspace = 0;
			do_sleep = 1;
		}
	} else {
		sp->se_nspace = 0;
	}

	/*
	 * fork(), not vfork() -- we can't afford to block.
	 */
	if ((pid = fork()) == -1) {
		emergency("can't fork for getty on port `%s': %m",
		    sp->se_device);
		return -1;
	}

	if (pid)
		return pid;

	if (do_sleep) {
		warning("getty repeating too quickly on port `%s', sleeping",
		    sp->se_device);
		(void)sleep(GETTY_SLEEP);
	}

	(void)sigemptyset(&mask);
	(void)sigprocmask(SIG_SETMASK, &mask, (sigset_t *)0);

	(void)execv(sp->se_getty_argv[0], sp->se_getty_argv);
	stall("can't exec getty `%s' for port `%s': %m",
	    sp->se_getty_argv[0], sp->se_device);
	_exit(8);
	/*NOTREACHED*/
}

/*
 * Take the system multiuser.
 */
static state_func_t
multi_user(void)
{
	pid_t pid;
	int status;
	session_t *sp;

	requested_transition = 0;

	for (sp = sessions; sp; sp = sp->se_next) {
		if (sp->se_process)
			continue;
		if ((pid = start_getty(sp)) == -1) {
			/* serious trouble */
			requested_transition = clean_ttys;
			break;
		}
		sp->se_process = pid;
		(void)gettimeofday(&sp->se_started, NULL);
	}

	while (!requested_transition)
		if ((pid = waitpid(-1, &status, 0)) != -1)
			collect_child(pid, status);

	return (state_func_t)requested_transition;
}

/*
 * Collect exit status for a child.
 */
static void
collect_child(pid_t pid, int status)
{
	session_t *sp;

	if (!sessions)
		return;

	if ((sp = find_session(pid)) == NULL)
		return;

	sp->se_process = 0;

	if (sp->se_flags & SE_SHUTDOWN)
		return;

	if ((pid = start_getty(sp)) == -1) {
		/* serious trouble */
		requested_transition = clean_ttys;
		return;
	}

	sp->se_process = pid;
	(void)gettimeofday(&sp->se_started, NULL);
}

/*
 * Catch a signal and request a state transition.
 */
static void
transition_handler(int sig)
{

	switch (sig) {
	case SIGHUP:
		requested_transition = clean_ttys;
		break;
	case SIGTERM:
		requested_transition = death;
		break;
	case SIGTSTP:
		requested_transition = catatonia;
		break;
	default:
		requested_transition = 0;
		break;
	}
}

/*
 * Re-read /etc/ttys and adjust sessions.
 */
static state_func_t
clean_ttys(void)
{
	session_t *sp;
	struct ttyent *typ;

	for (sp = sessions; sp; sp = sp->se_next)
		sp->se_flags &= ~SE_PRESENT;

	(void)do_setttyent();

	while ((typ = getttyent()) != NULL) {
		for (sp = sessions; sp; sp = sp->se_next) {
			if (strcmp(typ->ty_name, sp->se_device + 5) == 0)
				break;
		}

		if (sp) {
			sp->se_flags |= SE_PRESENT;
			if ((typ->ty_status & TTY_ON) == 0 ||
			    typ->ty_getty == NULL) {
				sp->se_flags |= SE_SHUTDOWN;
				if (sp->se_process != 0)
					(void)kill(sp->se_process, SIGHUP);
				continue;
			}
			sp->se_flags &= ~SE_SHUTDOWN;
			if (setupargv(sp, typ) == 0) {
				warning("can't parse getty for port `%s'",
				    sp->se_device);
				sp->se_flags |= SE_SHUTDOWN;
				if (sp->se_process != 0)
					(void)kill(sp->se_process, SIGHUP);
			} else {
				sp->se_nspace = 0;
			}
			continue;
		}

		(void)new_session(sp, typ);
	}

	(void)endttyent();

	for (sp = sessions; sp; sp = sp->se_next)
		if ((sp->se_flags & SE_PRESENT) == 0) {
			sp->se_flags |= SE_SHUTDOWN;
			if (sp->se_process != 0)
				(void)kill(sp->se_process, SIGHUP);
		}

	return (state_func_t)multi_user;
}

/*
 * Block further logins.
 */
static state_func_t
catatonia(void)
{
	session_t *sp;

	for (sp = sessions; sp; sp = sp->se_next)
		sp->se_flags |= SE_SHUTDOWN;

	return (state_func_t)multi_user;
}

/*
 * Note SIGALRM.
 */
static void
/*ARGSUSED*/
alrm_handler(int sig)
{

	clang = 1;
}

/*
 * Bring the system down to single user.
 */
static state_func_t
death(void)
{
	session_t *sp;
	int i, status;
	pid_t pid;
	static const int death_sigs[3] = { SIGHUP, SIGTERM, SIGKILL };

	for (sp = sessions; sp; sp = sp->se_next)
		sp->se_flags |= SE_SHUTDOWN;

	for (i = 0; i < 3; ++i) {
		if (kill(-1, death_sigs[i]) == -1 && errno == ESRCH)
			return (state_func_t)single_user;

		clang = 0;
		(void)alarm(DEATH_WATCH);
		do
			if ((pid = waitpid(-1, &status, 0)) != -1)
				collect_child(pid, status);
		while (clang == 0 && errno != ECHILD);

		if (errno == ECHILD)
			return (state_func_t)single_user;
	}

	warning("some processes would not die; ps axl advised");

	return (state_func_t)single_user;
}

/*
 * Open /etc/ttys for reading.
 */
static int
do_setttyent(void)
{

	(void)endttyent();
	return setttyent();
}
