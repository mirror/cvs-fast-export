#include "cvs.h"

void fatal_system_error(char const *format,...)
{
	va_list args;

	fprintf(stderr, "cvs-fast-export fatal: ");
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputs(": ", stderr);
	perror(NULL);
	exit(1);
}

void fatal_error(char const *format,...)
{
	va_list args;

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

	fprintf(stderr, "cvs-fast-export: ");
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
}

void* xmalloc(size_t size, char const *legend)
{
        void *ret = malloc(size);
        if (!ret && !size)
                ret = malloc(1);
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
        if (!ret && !size)
                ret = realloc(ptr, 1);
        if (!ret)
	    fatal_system_error("Out of memory, realloc(%zd) failed in %s",
			       size, legend);
        return ret;
}

/*
 * Print progress messages.
 *
 * Call progress_begin() at the start of some activity that may take a
 * long time.  Call progress_step() zero or more times during that
 * activity.  Call progress_end() at the end of the activity.
 *
 * Global 'progress' flag enables or disables all this.
 */

static char *progress_msg = "";
static int progress_max = -1;
static int progress_counter = 0;
static va_list _unused_va_list;

static void _progress_print(bool /*newline*/, const char * /*format*/, va_list);

void
progress_begin(char *msg, int max)
{

    if (!progress)
	return;
    progress_msg = msg;
    progress_max = max;
    progress_counter = 0;
    _progress_print(false, NULL, _unused_va_list);
}

void
progress_step(void)
{

    if (!progress)
	return;
    progress_counter++;
    _progress_print(false, NULL, _unused_va_list);
}

void
progress_jump(int count)
{

    if (!progress)
	return;
    progress_counter = count;
    _progress_print(false, NULL, _unused_va_list);
}

void
progress_end(const char *format, ...)
{
    va_list args;

    if (!progress)
	return;
    progress_max = progress_counter; /* message will say "100%" or "done" */
    va_start(args, format);
    _progress_print(true, format, args);
    va_end(args);
}

static void
_progress_print(bool newline, const char *format, va_list args)
{

    if (!progress)
	return;

    /*
     * If a format and args were given, use them.
     * Otherwise, try to print as much information as possible,
     * such as: <message>: <count> of <max> (<percent>)
     * or:      <message>: <count>
     * or:      <message>: done
     * or just: <message>
     */
    if (format) {
	fprintf(STATUS, "\r%s: ", progress_msg);
	vfprintf(STATUS, format, args);
    } else if (progress_max > 0) {
	fprintf(STATUS, "\r%s: %d of %d (%d%%)", progress_msg,
		progress_counter, progress_max,
		(progress_counter * 100 / progress_max));
    } else if (progress_counter > 0) {
	fprintf(STATUS, "\r%s: %d", progress_msg, progress_counter);
    } else if (progress_counter == progress_max) {
	/* they should both be zero at this point, but it still means "done" */
	fprintf(STATUS, "\r%s: done", progress_msg);
    } else {
	fprintf(STATUS, "\r%s", progress_msg);
    }
    if (newline)
	fprintf(STATUS, "\n");
    fflush(STATUS);
}

// end
