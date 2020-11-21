/*------------------------------------------------------------------------*/
/* Universal string handler for user console interface  (C)ChaN, 2011     */
/*------------------------------------------------------------------------*/

#ifndef _STRFUNC
#define _STRFUNC

#define _USE_XFUNC_OUT	1	/* 1: Use output functions */
#define	_CR_CRLF		1	/* 1: Convert \n ==> \r\n in the output char */

#define _USE_XFUNC_IN	0	/* 1: Use input function */
#define	_LINE_ECHO		1	/* 1: Echo back input chars in xgets function */


#if _USE_XFUNC_OUT
#include <stdarg.h>
typedef struct _xfunc_out_t {
	char *ptr;	// when not NULL, then store output there
	union {
	int space;	// up to 'space' chars (used only when ptr!=0)
	void *xptr;
	};
	int len;	// incremented for each fputc, cleared at *printf start
	void (*putc)(struct _xfunc_out_t *f,unsigned char); // otherwise use this fn
} xstream_t;

extern xstream_t xstream_out;

void xputc (char c);
void xfputc (xstream_t *f,char c);
void xputs (const char* str);
void xfputs (xstream_t *f, const char* str);
int xprintf (const char* fmt, ...);
int xsnprintf (char* buff, int len, const char* fmt, ...);
int xvfprintf (xstream_t *f,const char* fmt, va_list arp);
int xfprintf (xstream_t *f, const char* fmt, ...);
void put_dump (const void* buff, unsigned long addr, int len, int width);
#define DW_CHAR		sizeof(char)
#define DW_SHORT	sizeof(short)
#define DW_LONG		sizeof(long)
#endif

#if _USE_XFUNC_IN
#define xdev_in(func) xfunc_in = (unsigned char(*)(void))(func)
extern unsigned char (*xfunc_in)(void);
int xgets (char* buff, int len);
int xfgets (unsigned char (*func)(void), char* buff, int len);
int xatoi (char** str, long* res);
#endif

#endif
