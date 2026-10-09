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
#include <sys/sysctl.h>
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
#include <unistd.h>
#include <util.h>
#include <err.h>

#include "pathnames.h"

#define	STALL_TIMEOUT		30	/* wait N secs after warning */

typedef long (*state_func_t)(void);
typedef state_func_t (*state_t)(void);

static void handle(sig_t, ...);
static void delset(sigset_t *, ...);

static void vstall(const char *, va_list)
    __attribute__((__format__(__printf__, 1, 0)));
static void stall(const char *, ...)
    __attribute__((__format__(__printf__, 1, 2)));
static void warning(const char *, ...)
    __attribute__((__format__(__printf__, 1, 2)));
static void emergency(const char *, ...)
    __attribute__((__format__(__printf__, 1, 2)));
static void report_console_err(const char *, ...)
    __attribute__((__format__(__printf__, 1, 2)));
static void disaster(int) __attribute__((__noreturn__));
static void badsys(int);

static state_func_t single_user(void);
static state_func_t runcom(void);

static enum { AUTOBOOT, FASTBOOT } runcom_mode = AUTOBOOT;

static void transition(state_t);
static void setctty(const char *);

static void collect_child(pid_t, int);
static void transition_handler(int);
static int has_securelevel(void);
static int getsecuritylevel(void);
static void setsecuritylevel(int);
static int securelevel_present;

/*
 * Boot through runcom so /etc/rc is executed at startup, like the
 * full NetBSD init; single_user is only the fallback state.
 */
static state_t requested_transition = runcom;

/*
 * The mother of all processes.
 */
int
main_init(int argc, char **argv)
{
	struct sigaction sa;
	sigset_t mask;

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
	 * We catch or block signals rather than ignore them,
	 * so that they get reset on exec.
	 */
	handle(badsys, SIGSYS, 0);
	handle(disaster, SIGABRT, SIGFPE, SIGILL, SIGSEGV,
	    SIGBUS, SIGXCPU, SIGXFSZ, 0);
	handle(transition_handler, SIGHUP, SIGTERM, SIGTSTP, 0);
	(void)sigfillset(&mask);
	delset(&mask, SIGABRT, SIGFPE, SIGILL, SIGSEGV, SIGBUS, SIGSYS,
	    SIGXCPU, SIGXFSZ, SIGHUP, SIGTERM, SIGTSTP, 0);
	(void)sigprocmask(SIG_SETMASK, &mask, NULL);
	(void)sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = SIG_IGN;
	(void)sigaction(SIGTTIN, &sa, NULL);
	(void)sigaction(SIGTTOU, &sa, NULL);

	/*
	 * Paranoia -- close inherited stdio.
	 */
	(void)close(0);
	(void)close(1);
	(void)close(2);

	/*
	 * Securelevel might not be supported by the kernel. Query for it.
	 */
	securelevel_present = has_securelevel();

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
 * Log a formatted message (va_list form) and sleep for a while.
 */
static void
vstall(const char *message, va_list ap)
{

	vsyslog(LOG_ALERT, message, ap);
	closelog();
	(void)sleep(STALL_TIMEOUT);
}

/*
 * Log a message and sleep for a while.
 */
static void
stall(const char *message, ...)
{
	va_list ap;

	va_start(ap, message);
	vstall(message, ap);
	va_end(ap);
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
 * Catch an unexpected signal.
 */
__attribute__((__noreturn__))
static void
disaster(int sig)
{

	emergency("fatal signal: %s", strsignal(sig));
	(void)sleep(STALL_TIMEOUT);
	_exit(sig);
}

/*
 * Check if securelevel is present.
 */
static int
has_securelevel(void)
{
#ifdef KERN_SECURELVL
	int name[2], curlevel;
	size_t len;

	name[0] = CTL_KERN;
	name[1] = KERN_SECURELVL;
	len = sizeof curlevel;
	if (sysctl(name, 2, &curlevel, &len, NULL, 0) == -1) {
		if (errno == ENOENT)
			return 0;
	}
	return 1;
#else
	return 0;
#endif
}

/*
 * Get the security level of the kernel.
 */
static int
getsecuritylevel(void)
{
#ifdef KERN_SECURELVL
	int name[2], curlevel;
	size_t len;

	if (!securelevel_present)
		return -1;

	name[0] = CTL_KERN;
	name[1] = KERN_SECURELVL;
	len = sizeof curlevel;
	if (sysctl(name, 2, &curlevel, &len, NULL, 0) == -1) {
		emergency("cannot get kernel security level: %m");
		return -1;
	}
	return curlevel;
#else
	return -1;
#endif
}

/*
 * Set the security level of the kernel.
 */
static void
setsecuritylevel(int newlevel)
{
#ifdef KERN_SECURELVL
	int name[2], curlevel;

	if (!securelevel_present)
		return;

	curlevel = getsecuritylevel();
	if (newlevel == curlevel)
		return;
	name[0] = CTL_KERN;
	name[1] = KERN_SECURELVL;
	if (sysctl(name, 2, NULL, NULL, &newlevel, sizeof newlevel) == -1) {
		emergency("cannot change kernel security level from"
		    " %d to %d: %m", curlevel, newlevel);
		return;
	}
#endif
}

/*
 * Change states in the finite state machine.
 */
static void
transition(state_t s)
{

	if (s == NULL)
		return;
	for (;;)
		s = (state_t)(*s)();
}

/*
 * Log a message to the console, then stall().
 *
 * Only used by init's children, which run before syslogd exists;
 * vsyslog() alone would be lost, so also write to the console.
 */
static void
report_console_err(const char *message, ...)
{
	va_list ap, ap2;
	FILE *f;
	int fd;

	va_start(ap, message);
	if ((fd = open(_PATH_CONSOLE, O_WRONLY)) != -1) {
		if ((f = fdopen(fd, "w")) != NULL) {
			va_copy(ap2, ap);
			(void)fprintf(f, "init: ");
			(void)vfprintf(f, message, ap2);
			va_end(ap2);
			(void)fputc('\n', f);
			(void)fflush(f);
			(void)fclose(f);
		} else
			(void)close(fd);
	}
	vstall(message, ap);
	va_end(ap);
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
		/*
		 * The constant console can be missing from /dev at early
		 * boot; fall back to the console proper before giving up.
		 */
		if (strcmp(name, _PATH_CONSOLE) != 0) {
			(void)revoke(_PATH_CONSOLE);
			if ((fd = open(_PATH_CONSOLE, O_RDWR)) != -1)
				goto gotctty;
		}
		report_console_err("can't open %s: %m", name);
		_exit(1);
	}
gotctty:
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
	int from_securitylevel;
	sigset_t mask;
	struct sigaction sa, satstp, sahup;
	const char *argv[2];

	/*
	 * If the kernel is in secure mode, downgrade it to insecure mode.
	 */
	from_securitylevel = getsecuritylevel();
	if (from_securitylevel > 0)
		setsecuritylevel(0);

	(void)sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = SIG_IGN;
	(void)sigaction(SIGTSTP, &sa, &satstp);
	(void)sigaction(SIGHUP, &sa, &sahup);
	if ((pid = fork()) == 0) {
		/*
		 * Start the single user session.
		 */
		if (access(_PATH_CONSTTY, F_OK) == 0)
			setctty(_PATH_CONSTTY);
		else
			setctty(_PATH_CONSOLE);

		/*
		 * Unblock signals.
		 */
		(void)sigemptyset(&mask);
		(void)sigprocmask(SIG_SETMASK, &mask, NULL);

		/*
		 * Fire off a shell.
		 */
		argv[0] = "-sh";
		argv[1] = NULL;
		(void)setenv("PATH", _PATH_STDPATH, 1);
		(void)execv(_PATH_BSHELL, __UNCONST(argv));
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

	/*
	 * Shell exited normally -- run /etc/rc, then loop back.
	 */
	(void)sigaction(SIGTSTP, &satstp, NULL);
	(void)sigaction(SIGHUP, &sahup, NULL);
	runcom_mode = FASTBOOT;
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
	struct sigaction sa, satstp, sahup;

	(void)sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = SIG_IGN;
	(void)sigaction(SIGTSTP, &sa, &satstp);
	(void)sigaction(SIGHUP, &sa, &sahup);

	switch ((pid = fork())) {
	case 0:
		setctty(_PATH_CONSOLE);

		argv[0] = "sh";
		argv[1] = _PATH_RUNCOM;
		argv[2] = (runcom_mode == AUTOBOOT ? "autoboot" : NULL);
		argv[3] = NULL;

		(void)sigprocmask(SIG_SETMASK, &sa.sa_mask, NULL);

		(void)execv(_PATH_BSHELL, __UNCONST(argv));
		stall("can't exec `%s' for `%s': %m", _PATH_BSHELL, _PATH_RUNCOM);
		_exit(5);
		/*NOTREACHED*/
	case -1:
		emergency("can't fork for `%s' on `%s': %m", _PATH_BSHELL,
		    _PATH_RUNCOM);
		while (waitpid(-1, NULL, WNOHANG) > 0)
			continue;
		(void)sigaction(SIGTSTP, &satstp, NULL);
		(void)sigaction(SIGHUP, &sahup, NULL);
		(void)sleep(STALL_TIMEOUT);
		return (state_func_t)single_user;
	default:
		break;
	}

	/*
	 * Wait for /etc/rc to complete.
	 */
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

	(void)sigaction(SIGTSTP, &satstp, NULL);
	(void)sigaction(SIGHUP, &sahup, NULL);

	if (!WIFEXITED(status)) {
		warning("`%s' on `%s' terminated abnormally, going to "
		    "single user mode", _PATH_BSHELL, _PATH_RUNCOM);
		return (state_func_t)single_user;
	}

	if (WEXITSTATUS(status))
		return (state_func_t)single_user;

	runcom_mode = AUTOBOOT;
	return (state_func_t)single_user;
}

/*
 * Collect exit status for a child.
 * In LETS_GET_SMALL mode, there are no sessions to manage.
 */
static void
collect_child(pid_t pid, int status)
{
	(void)pid;
	(void)status;
}

/*
 * Catch a signal and request a state transition.
 * In LETS_GET_SMALL mode, no transitions are supported.
 */
static void
transition_handler(int sig)
{
	(void)sig;
	requested_transition = 0;
}
