#include <string.h>
#include <stdlib.h>
#include "system.h"
#include "xprintf.h"
#include "coro.h"
#include "fdt.h"

#define MAX_MOD_INST 32

// index of all mod inst subtrees in active FDT sorted
// by string after @ in node name
static uint32_t *idx[MAX_MOD_INST];
static uint8_t thr_map[CORO_POOL];	// map coro_id to thread id
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

static struct boot_param_header *g_fdt;
static int no_par;

// can run concurently in more threads, arg encodes starts id
// in low byte, liveness in second one
static void run_worker(void *_arg)
{
	uint32_t arg = (uint32_t)_arg;
	uint8_t live = (arg >> 8) & 0xff;
	uint8_t thr_id = thr_map[coro_current];
	uint8_t start = arg & 0xff;
//	xprintf("[%d] run worker 0x%X thr_id %d coro %d\n",
//			get_ms_precise(),arg,thr_id,coro_current);
	for (int i=start;i<idx_sz;i++) {
		char *ptr;
		if (lookup_fdt(g_fdt,"compatible",(uint32_t**)&ptr,idx[i])<2) {
			xprintf("missing compatible prop\n");
			continue;
		}
		struct module_desc_t *md = lookup_mod(ptr);
		if (!md) {
			xprintf("unknown mod '%s'\n",ptr);
			continue;
		}
		uint32_t thr=0,wait=0;
		if (!no_par) {
			fetch_fdt_ints(g_fdt,idx[i],"thread",0,1,&thr);
			fetch_fdt_ints(g_fdt,idx[i],"wait",0,1,&wait);
		}
		if (thr_id && thr != thr_id)
			continue; // we are different subthread 
		wait &= 0xfe; // can't wait for main thread
		if (wait) {
			if (((1<<thr_id) & wait) && i>start)
				break; // end of live span
			// we need to join threads in wait bitmask
			for (int c=0;c<CORO_POOL;c++) {
				uint8_t m = 1<<thr_map[c];
				if (!(wait & m)) continue;
//				xprintf("join %d from %d\n",c,coro_current);
				coro_join(c);
				live &= ~m;
			}
		}
		if (!thr_id && thr) {
			if (thr>7) continue;	// bad id, ignore
			if ((live>>thr)&1) continue; // already live
			// need to start as subthread
			live |= 1<<thr;
			uint32_t ag = (live<<8)|i;
			int cid = coro_new(&run_worker,(void*)ag,1,NULL);
			thr_map[cid] = thr;
//			xprintf("new thr %d ag %x\n",cid,ag);
			continue;
		}
		console_sync();		
		xprintf("[%d] call %s, thr %d coro %d live %x\n",
				get_ms_precise(),ptr,thr_id,coro_current,live);
		// TODO: return val check
		int r = md->run(md,g_fdt,idx[i]);
		if (r) {
			xprintf("call %s returned %d\n",ptr,r);
		}
		coro_yield();
	}
//	xprintf("[%d] worker end thr_id %d coro %d\n",
//			get_ms_precise(),thr_id,coro_current);
	coro_done();
}

void run_modules(struct boot_param_header *fdt,uint32_t *root,int _no_par)
{
	memset(thr_map,0,sizeof(thr_map));
	index_modules(fdt,root);
	g_fdt = fdt; no_par = _no_par;
	run_worker((void*)0x100);
}

struct machine_ctx_t *mctx;
