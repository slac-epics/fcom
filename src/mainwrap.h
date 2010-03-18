#include <stdarg.h>
#define mainwrap(nm) \
                     \
extern int nm##_main(int, char**); \
                     \
int                  \
nm(char *arg0,...)   \
{                    \
int     argc = 0;    \
char    *argv[10];   \
va_list ap;          \
	argv[argc++] = #nm;\
	va_start(ap, arg0);\
	if ( arg0 ) {    \
		argv[argc++] = arg0;\
		while (    argc < sizeof(argv)/sizeof(argv[0]) \
			    && (argv[argc] = va_arg(ap, char*)) ) {\
			argc++;  \
		}            \
	}                \
	va_end(ap);      \
                     \
	return nm##_main(argc, argv);\
}
