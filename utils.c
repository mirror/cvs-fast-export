#include <stdlib.h>
#include "cvs.h"

#if defined(__APPLE__)
#include <mach/mach_time.h>

int clock_gettime(clockid_t clock_id, struct timespec *tp)
{
    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);
    uint64_t time = mach_absolute_time();
    tp->tv_nsec = ((double)time * (double)timebase.numer)/((double)timebase.denom);
    tp->tv_sec = ((double)time * (double)timebase.numer)/((double)timebase.denom * NSEC_PER_SEC);
    return EXIT_SUCCESS;
}
#endif

unsigned int warncount;

#if _POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600
void* xmemalign(size_t align, size_t size, char const *legend)
{
    void *ret;
    int err;

    err = posix_memalign(&ret, align, size);
    if (err)
	fatal_error("posix_memalign(%zd, %zd) failed in %s: %s",
			   align, size, legend, strerror(err));
    return ret;
}
#endif

void* xmalloc(size_t size, char const *legend)
{
    void *ret = malloc(size);
#ifndef __COVERITY__
    if (!ret && !size)
	ret = malloc(1);
#endif /* __COVERITY__ */
    if (!ret)
	fatal_system_error("Out of memory, malloc(%zd) failed in %s",
			   size, legend);
    return ret;
}

void* xcalloc(size_t nmemb, size_t size, char const *legend)
{
    void *ret = calloc(nmemb, size);
    if (!ret)
	fatal_system_error("Out of memory, calloc(%zd, %zd) failed in %s",
			   nmemb, size, legend);
    return ret;
}

void* xrealloc(void *ptr, size_t size, char const *legend)
{
    void *ret = realloc(ptr, size);
#ifndef __COVERITY__
    if (!ret && !size)
	ret = realloc(ptr, 1);
#endif /* __COVERITY__ */
    if (!ret)
	fatal_system_error("Out of memory, realloc(%zd) failed in %s",
			   size, legend);
    return ret;
}

char *
cvstime2rfc3339(const cvstime_t date)
/* RFC3339 time representation (not thread-safe!) */
{
    static char timestr[23];
    time_t udate = RCS_EPOCH + date;
    struct tm	*tm = localtime(&udate);

    (void)strftime(timestr, sizeof(timestr), "%Y-%m-%dT%H:%M:%SZ", tm);
    return timestr;
}

/*
 * Print progress messages.
 *
 * Call progress_begin() at the start of some activity that may take a
 * long time.  Call progress_step() zero or more times during that
 * activity.  Call progress_end() at the end of the activity.
 *
 * Global 'progress' flag enables or disables all this.
 *
 * Note: These functions are not thread-safe!
 */

static char *progress_msg = "";
static int progress_counter = 0;
static va_list _unused_va_list;
static struct timespec start;  
static int progress_max = NO_MAX;
static bool progress_in_progress;

static void _progress_print(bool /*newline*/, const char * /*format*/, va_list)
	_printflike(2, 0);

void
progress_begin(const char *msg, const int max)
{
    static char timestr[128];
    time_t now = time(NULL);
    struct tm	*tm = localtime(&now);

    if (!progress)
	return;
    progress_max = max;
    progress_counter = 0;
    progress_in_progress = true;

    (void)strftime(timestr, sizeof(timestr), "%Y-%m-%dT%H:%M:%SZ: ", tm);
    strncat(timestr, msg, sizeof(timestr)-1);
    progress_msg = timestr;

    _progress_print(false, "", _unused_va_list);
    clock_gettime(CLOCK_REALTIME, &start);
}

void
progress_step(void)
{
    if (!progress)
	return;
    progress_in_progress = true;
    progress_counter++;
    _progress_print(false, "", _unused_va_list);
}

void
progress_jump(const int count)
{
    if (!progress)
	return;
    progress_in_progress = true;
    progress_counter = count;
    _progress_print(false, "", _unused_va_list);
}

void
progress_end(const char *format, ...)
{
    va_list args;

    if (!progress)
	return;
    progress_in_progress = false;
    progress_max = progress_counter; /* message will say "100%" or "done" */
    va_start(args, format);
    _progress_print(true, format, args);
    progress_max = NO_MAX;
    va_end(args);
}

static void
_progress_print(bool newline, const char *format, va_list args)
{
    if (!progress)
	return;

    /*
     * If a non-empty format was given, use the format and args.
     * Otherwise, try to print as much information as possible,
     * such as: <message>: <count> of <max> (<percent>)
     * or:      <message>: <count>
     * or:      <message>: done
     * or just: <message>
     */
    if (format && *format) {
	fprintf(STATUS, "\r%s", progress_msg);
	vfprintf(STATUS, format, args);
    } else if (progress_max > 0) {
	fprintf(STATUS, "\r%s%d of %d(%d%%)   ", progress_msg,
		progress_counter, progress_max,
		(progress_counter * 100 / progress_max));
    } else if (progress_counter > 0) {
	fprintf(STATUS, "\r%s%d", progress_msg, progress_counter);
    } else if (progress_counter == progress_max) {
	/* they should both be zero at this point, but it still means "done" */
	fprintf(STATUS, "\r%sdone ", progress_msg);
    } else {
	fprintf(STATUS, "\r%s", progress_msg);
    }
    if (newline)
    {
	struct timespec end;

	clock_gettime(CLOCK_REALTIME, &end);
	fprintf(STATUS, " (%.3fsec)", seconds_diff(&end, &start));
	fprintf(STATUS, "\n");
    }
    fflush(STATUS);
}

static void progress_interrupt(void)
{
    if (progress && progress_in_progress) {
	fputc('\n', stderr);
	progress_in_progress = false;
    }
#ifdef __UNUSED__
    if (progress_max != NO_MAX) {
	progress_max = NO_MAX;
    }
#endif /* __UNUSED__ */
}

void fatal_system_error(char const *format,...)
{
    va_list args;
    int errno_save = errno;

    progress_interrupt();
    fprintf(stderr, "cvs-fast-export fatal: ");
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs(": ", stderr);
    errno = errno_save;
    perror(NULL);
    exit(1);
}

void fatal_error(char const *format,...)
{
    va_list args;

    progress_interrupt();
    fprintf(stderr, "cvs-fast-export fatal: ");
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
    exit(1);
}

void announce(char const *format,...)
{
    va_list args;

    progress_interrupt();
    fprintf(stderr, "cvs-fast-export: ");
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

void warn(char const *format,...)
{
    va_list args;

    if (LOGFILE == stderr)
	progress_interrupt();
    fprintf(LOGFILE, "cvs-fast-export: ");
    va_start(args, format);
    vfprintf(LOGFILE, format, args);
    va_end(args);

    warncount++;
}

void debugmsg(char const *format,...)
{
    va_list args;

    progress_interrupt();
    va_start(args, format);
    vfprintf(LOGFILE, format, args);
    va_end(args);
}

// end
