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

// end
