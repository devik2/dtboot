#include <string.h>
#include <ctype.h>
#include "xprintf.h"
#include "system.h"
#include "ca7.h"

void panic(const char *fmt,...)
{
	console_stop_buf();
	va_list arp;
	va_start(arp, fmt);
	if (fmt) xvfprintf(&xstream_out, fmt, arp);
	va_end(arp);
	asm("bkpt");
	for (;;);
}

void cpu_info()
{
	int i;
	uint32_t scr;
//	asm("mrc p15, 0, %0, c1, c1, 0" : "=r" (scr));
//	xprintf("SCR (sif,hce|sce,net,aw,fw|ea,fiq,irq,ns): %03X\n",scr);
	for (i=0;i<1;i++) {
		uint32_t sctlr,tmp;
		tmp = (scr&0xfffffffe)|i;
//		asm("mcr p15, 0, %0, c1, c1, 0" : : "r" (tmp));
		asm("mrc p15, 0, %0, c1, c0, 0" : "=r" (sctlr));
		xprintf("NS%d,sctlr: %08X\n",i,sctlr);
		asm("mrc p15, 0, %0, c1, c0, 1" : "=r" (tmp));
		xprintf("NS%d,actlr: %08X\n",i,tmp);
		asm("mrc p15, 0, %0, c12, c0, 0" : "=r" (tmp));
		xprintf("NS%d,VBAR: %08X\n",i,tmp);
		//asm("mrc p15, 0, %0, c12, c0, 1" : "=r" (tmp));
		//xprintf("NS%d,MVBAR: %08X\n",i,tmp);
	}
	//asm("mcr p15, 0, %0, c1, c1, 0" : : "r" (scr));
}

struct bit_names {
	uint8_t off,sz;
	uint8_t _t1,_t2;
	const char *name;
};

static const struct bit_names __bn_cpsr[] = {
	{ 0,5,0,0,"mode|||||||||||||||||user|fiq|irq|sup|||mon|abrt|||hyp|und||||sys|" }, 
	{ 5,1,0,0,"thumb" }, { 6,1,0,0,"fiqm" }, { 7,1,0,0,"irqm" }, { 8,1,0,0,"aabm" }, 
	{ 9,1,0,0,"bige" }, { 16,4,0,0,"ge" }, { 27,1,0,0,"sat" }, { 28,1,0,0,"v" }, 
	{ 29,1,0,0,"c" }, { 30,1,0,0,"z" }, { 31,1,0,0,"n" }, { 0,0,0,0,NULL }
};
const struct bit_names *_bn_cpsr = __bn_cpsr;
static const struct bit_names __bn_actlr[] = {
	{ 6,1,0,0,"smp" }, { 10,1,0,0,"dodmbs" }, { 11,1,0,0,"l2radis" }, 
	{ 12,1,0,0,"l1radis" }, { 13,2,0,0,"l1pctl" }, { 15,1,0,0,"ddvm" }, 
	{ 28,1,0,0,"ddi" }, { 0,0,0,0,NULL }
};
const struct bit_names *_bn_actlr = __bn_actlr;
static const struct bit_names __bn_sctlr[] = {
	{ 30,1,0,0,"te" }, { 29,1,0,0,"afe" }, { 28,1,0,0,"tre" }, { 27,1,0,0,"nmfi" }, 
	{ 25,1,0,0,"ee" }, { 24,1,0,0,"ve" }, { 22,1,0,0,"u" }, { 21,1,0,0,"fi" }, 
	{ 20,1,0,0,"uwxn" }, { 19,1,0,0,"wxn" }, { 17,1,0,0,"ha" }, { 14,1,0,0,"rr" }, 
	{ 13,1,0,0,"v" }, { 12,1,0,0,"i" }, { 11,1,0,0,"z" }, { 10,1,0,0,"sw" }, 
	{ 7,1,0,0,"b" }, { 5,1,0,0,"ben" }, { 2,1,0,0,"c" }, { 1,1,0,0,"a" }, 
	{ 0,1,0,0,"m" }, { 0,0,0,0,NULL }
};
const struct bit_names *_bn_sctlr = __bn_sctlr;
static const struct bit_names __bn_scr[] = {
	{ 9,1,0,0,"sif" }, { 8,1,0,0,"hce" }, { 7,1,0,0,"scd" }, { 6,1,0,0,"net" }, 
	{ 5,1,0,0,"aw" }, { 4,1,0,0,"fw" }, { 3,1,0,0,"ea" }, { 2,1,0,0,"fiq" }, 
	{ 1,1,0,0,"irq" }, { 0,1,0,0,"ns" }, { 0,0,0,0,NULL }
};
const struct bit_names *_bn_scr = __bn_scr;

static void strpicpy(char **dst,const char *src,int mode)
{
	while (*src && *src!='|') {
		*((*dst)++) = mode==-1?*src:mode?toupper(*src):tolower(*src);
		src++;
	}
	**dst = 0;
}

static void format_bitreg(char *buf,uint32_t r,const struct bit_names *bn)
{
	*buf = 0;
	for (;bn->sz;bn++) {
		int i,maps=0;
		char map[33],*q;
		const char *p = bn->name;
		for (i=0;i<33;i++) {
			q = strchr(p+1,'|');
			if (!q) break;
			p = q;
			map[i] = q+1-bn->name; maps = i+1;
		}
		int fld = (r >> bn->off) & ((1 << bn->sz)-1);
		q = buf + strlen(buf);
		if (fld < maps) {
			strpicpy(&q," ",-1); strpicpy(&q,bn->name,-1);
			strpicpy(&q,":",-1); strpicpy(&q,bn->name+map[fld],-1);
		} else if (bn->sz==1) {
			strpicpy(&q," ",-1); strpicpy(&q,bn->name,fld);
		} else {
			strpicpy(&q," ",-1); strpicpy(&q,bn->name,-1);
			xsnprintf(q,20,"%x",fld);
		}
	}
}

void dump_reg(const char *name,uint32_t r,const struct bit_names *bn)
{
	char buf[120];
	format_bitreg(buf,r,bn); xprintf("%s%s\n",name,buf);
}
