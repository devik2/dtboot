#include <stdint.h>
#include <string.h>

#ifndef TEST
#include "xprintf.h"
#define printf xprintf
#else
#include <stdio.h>
#endif
#include "fdt.h"

#define LFL_TOP 1
#define LFL_SKIP 2
static int scan_node(struct fdt_scan_data *dat,uint32_t *node,int pos,int flags)
{
	uint32_t *p = node;
	if (be_cpu(*p) != OF_DT_BEGIN_NODE) {
		printf ("bad node %x\n",node);
		return -1;
	}
	p++;
	const char *name = (void*)p;
	int noff = flags & LFL_TOP ? 0 : strlen(name);
	int skip = flags & LFL_SKIP;
	if (noff && !skip) {
		dat->path[pos] = '/';
		strcpy(dat->path+pos+1,name);
		noff++;
	}
	if (flags & LFL_TOP) dat->depth = 0;
	if (dat->begin && !skip && noff) {
		int bgs = dat->begin(dat,node);
		if (dat->verbose) printf (" bgs=%d path=%s\n",bgs,dat->path);
		if (bgs < 0) {
			return bgs;
		}
		if (bgs) skip = 1;
	}
	if (dat->verbose) printf ("node %s\n",dat->path);
	p += align4(strlen(name)+1)/4;
	for (;;) {
		uint32_t tp = be_cpu(*p);
		if (tp == OF_DT_PROP) {
			int sz = be_cpu(p[1]);
			if (!skip && dat->prop_found) {
				int off = be_cpu(p[2]);
				off += be_cpu(dat->hdr->off_dt_strings);
				const char *pname = (char*)dat->hdr + off;
				int rv = dat->prop_found(dat,pname,
						(char*)(p+3),sz,node);
				if (rv < 0) return rv;
			}
			p += align4(sz)/4+3;
		}
		else if (tp == OF_DT_BEGIN_NODE) {
			dat->depth++;
			int sub = scan_node(dat,p,pos+noff,flags & LFL_SKIP);
			dat->depth--;
			if (sub < 0) return sub;
			p += sub;
		} 
		else if (tp == OF_DT_END_NODE) 
			return p-node+1;
		else if (tp == OF_DT_END) 
			return p-node+1;
		else break;
	}
	return -2;
}

int scan_fdt(struct fdt_scan_data *dat,uint32_t *ptr)
{
	if (be_cpu(dat->hdr->magic) != OF_DT_HEADER) {
		printf("bad FDT magic\n");
		return -1;
	}
	uint32_t *p = (uint32_t*)dat->hdr + be_cpu(dat->hdr->off_dt_struct)/4;
	if (ptr) p = ptr;
	dat->path[0] = 0;
	return scan_node(dat,p,0,LFL_TOP);
}

const char *fdt_get_node_name(struct fdt_scan_data *dat,uint32_t *p)
{
	if (be_cpu(*p) != OF_DT_BEGIN_NODE) return NULL;
	return (void*)(p+1);
}

static int fdt_skip(struct fdt_scan_data *dat,uint32_t *node)
{
	return dat->path[0];
}

static int fdt_section(struct fdt_scan_data *dat,uint32_t *node)
{
	const char *s = (const char*)dat->arg2[0];
	//xprintf("%s\n",dat->path);
	if (!strcmp(dat->path,s)) {
		dat->arg2[2] = (uint32_t)node;
		return -1;
	}
	return 0;
}

static int fdt_find(struct fdt_scan_data *dat,const char *nm,
		const char *val,int sz,uint32_t *node)
{
	const char *s = (const char*)dat->arg2[0];
	if (dat->verbose) xprintf(" '%s' '%s' '%s' %d\n",dat->path,nm,s,dat->arg2[1]);
	//if (!strcmp(nm,"method")) asm("bkpt");
	if (strcmp(nm,s+dat->arg2[1])) return 0;
	if (dat->path[dat->arg2[1]?dat->arg2[1]-1:0]) return 0; // bad path length
	if (dat->arg2[1] && strncmp(dat->path,s,dat->arg2[1]-1)) return 0;
	dat->arg2[2] = (uint32_t)val;
	dat->arg2[3] = sz;
	return -1;
}

uint32_t *lookup_fdt_section(struct boot_param_header *hdr,
		const char *path,uint32_t *root)
{
	struct fdt_scan_data fsc;
	memset(&fsc,0,sizeof(fsc));
	fsc.hdr = hdr;
	fsc.begin = fdt_section;
	fsc.arg2[0] = (uint32_t)path;
	int rv = scan_fdt(&fsc,root);
	if (rv != -1) return 0;
	return (uint32_t*)fsc.arg2[2];
}

int lookup_fdt(struct boot_param_header *hdr,const char *path,
		uint32_t **ptr,uint32_t *root)
{
	struct fdt_scan_data fsc;
	memset(&fsc,0,sizeof(fsc));
	fsc.verbose = 0;
	fsc.hdr = hdr;
	fsc.prop_found = fdt_find;
	fsc.arg2[0] = (uint32_t)path;
	char *p = strrchr(path,'/');
	fsc.arg2[1] = p ? p-path+1 : 0;
	if (!p) fsc.begin = fdt_skip; // no '/', don't descend
	int rv = scan_fdt(&fsc,root);
	if (rv >= 0) return -1;
	if (ptr) *ptr = (uint32_t*)fsc.arg2[2];
	return fsc.arg2[3];
}

static int fdt_find_ph(struct fdt_scan_data *dat,const char *nm,
		const char *val,int sz,uint32_t *node)
{
	if (strcmp(nm,"phandle")) return 0;
	uint32_t ph = be_cpu(*(uint32_t*)val);
	if (dat->verbose) xprintf("PH '%s' %x %x\n",dat->path,ph,dat->arg2[0]);
	if (ph != dat->arg2[0]) return 0;
	dat->arg2[2] = (uint32_t)node;
	return -1;
}

uint32_t *lookup_fdt_by_phandle(struct boot_param_header *hdr,uint32_t phandle,
		uint32_t *root)
{
	struct fdt_scan_data fsc;
	memset(&fsc,0,sizeof(fsc));
	fsc.verbose = 0;
	fsc.hdr = hdr;
	fsc.prop_found = fdt_find_ph;
	fsc.arg2[0] = phandle;
	int rv = scan_fdt(&fsc,root);
	if (rv >= 0) return NULL;
	return (uint32_t*)fsc.arg2[2];
}

// loads BE converted integer data to tgt, checks min & max size
// returns integer cnt (0 if missing) or negative error
int fetch_fdt_ints(struct boot_param_header *fdt,uint32_t *root,
		const char *path, int mincnt,int maxcnt,uint32_t *tgt)
{
	uint32_t *arg;
	int i,sz = lookup_fdt(fdt,path,&arg,root);
	if (sz<0) return 0;
	if (!mincnt && !sz) {
		tgt[0] = 1; // special handling for bool props
		return 0;
	}
	if (sz & 3) goto bad;
	sz /= 4;
	if (sz<mincnt || sz>maxcnt) {
bad:
		xprintf("prop %s has bad data\n",path);
		return -22;
	}
	for (i=0;i<sz;i++) tgt[i] = be_cpu(arg[i]);
	return sz;
}

int clamp_int_warn(int x,int min,int max)
{
	int nx = x;
	if (x<min) nx = min;
	if (x>max) nx = max;
	if (nx != x) xprintf("bad value %d, clamped to %d\n");
	return nx;
}
