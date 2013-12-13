#include "cvs.h"

void fatal_system_error(char const *s)
{
	perror(s);
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

void* xmalloc(size_t size)
{
        void *ret = malloc(size);
        if (!ret && !size)
                ret = malloc(1);
        if (!ret)
                fatal_system_error("Out of memory, malloc failed");
        return ret;
}

void* xrealloc(void *ptr, size_t size)
{
        void *ret = realloc(ptr, size);
        if (!ret && !size)
                ret = realloc(ptr, 1);
        if (!ret)
                fatal_system_error("Out of memory, realloc failed");
        return ret;
}

// end
