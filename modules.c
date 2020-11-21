#include <string.h>
#include <stdlib.h>
#include "system.h"
#include "xprintf.h"
#include "coro.h"
#include "fdt.h"

//extern struct boot_param_header *fdt;	// global FDT

#define MAX_MOD_INST 32

// index of all mod inst subtrees in active FDT sorted
// by string after @ in node name
static uint32_t *idx[MAX_MOD_INST];
static uint8_t idx_sz;

static int fdt_cb_section(struct fdt_scan_data *dat,uint32_t *node)
{
	if (idx_sz >= MAX_MOD_INST-1) return -1; // idx is full
	if (dat->depth>1) return 1;
	idx[idx_sz++] = node;
	return 0;
}

static int qcmp(const void *a, const void *b,void *fdt)
{
	uint32_t *pa,*pb,dflt = 100;
	const char *sa = fdt_get_node_name(fdt,*(uint32_t**)a);
	const char *sb = fdt_get_node_name(fdt,*(uint32_t**)b);
	if (!sa || !sb) {
		xprintf("ERROR: fatal in modules sorting");
		return 0;
	}
	if (strchr(sa,'@')) sa = strchr(sa,'@')+1;
	if (strchr(sb,'@')) sb = strchr(sb,'@')+1;
	return strcasecmp(sa,sb);
}

static int index_modules(struct boot_param_header *fdt,uint32_t *root)
{
	idx_sz = 0;
	struct fdt_scan_data fsc;
	memset(&fsc,0,sizeof(fsc));
	fsc.hdr = fdt;
	fsc.begin = fdt_cb_section;
	fsc.verbose = 0;
	int rv = scan_fdt(&fsc,root);
	// NOTE: _GNU_SOURCE needed to get correct qsort_r
	qsort_r(idx,idx_sz,sizeof(uint32_t),qcmp,(void*)fdt);
	if (rv == -1) return -1; // too much sections
	return idx_sz;
}

extern uint32_t _MOD_TABLE_[];

static struct module_desc_t *lookup_mod(const char *name)
{
	struct module_desc_t *d = (void*)_MOD_TABLE_;
	for (;d->name;d++) {
		if (!strcasecmp(d->name,name)) return d;
	}
	return NULL;
}

void run_modules(struct boot_param_header *fdt,uint32_t *root)
{
	index_modules(fdt,root);
	int i;
	for (i=0;i<idx_sz;i++) {
		char *ptr;
		if (lookup_fdt(fdt,"compatible",(uint32_t**)&ptr,idx[i])<2) {
			xprintf("missing compatible prop\n");
			continue;
		}
		struct module_desc_t *md = lookup_mod(ptr);
		if (!md) {
			xprintf("unknown mod '%s'\n",ptr);
			continue;
		}
		xprintf("call %s at %X\n",ptr,md);
		// TODO: async exec & return val check
		int r = md->run(md,fdt,idx[i]);
		if (r) {
			xprintf("call %s returned %d\n",ptr,r);
		}
	}
}

struct machine_ctx_t *mctx;
