#include "system.h"
#include "xprintf.h"
#include "coro.h"

#define CORO_SSZ (512-3)	// stack in words
#define CORF_USED 1		// can't reuse slot
#define CORF_RUN 2		// runable
#define CORF_JOIN 4		// joinable
struct _coro_t {
        uint32_t *sp;
	const char *name;
        void *arg;
        int flags; // CORF_XXX
        uint32_t stack[CORO_SSZ];
} coro_pool[CORO_POOL];
uint8_t coro_current;

void coro_switch(coro_t *a,coro_t *b);
int coro_free_index()
{
        int i;
        for (i=0;i<CORO_POOL;i++)
                if (!(coro_pool[i].flags & CORF_USED)) return i;
        return -1;
}

static void coro_create(coro_t *a,coro_fn fn,void *arg,const char *name)
{
        a->sp = a->stack + CORO_SSZ - 10;
        a->sp[8] = (uint32_t)fn;
        a->arg = arg;
	a->name = name;
        a->flags = CORF_USED|CORF_RUN;
//        xprintf("coro_create a=%X fn=%X sp=%X\n",a,fn,a->sp);
}

int coro_new(coro_fn fn,void *arg,int joinable,const char *name)
{
        int idx = coro_free_index();
        if (idx<0) {
		xprintf("can't alloc new coroutine! fn=%p\n",fn);
		return -1;
	}
        coro_create(coro_pool+idx,fn,arg,name);
	if (joinable) coro_pool[idx].flags |= CORF_JOIN;
        return idx;
}

void coro_yield()
{
        int i,cur = coro_current;
        for (i=cur+1;i!=cur;i++) {
                if (i>=CORO_POOL) i = 0; 
                if (!(coro_pool[i].flags & CORF_RUN)) continue;
                // runable task
                coro_current = i;
                coro_switch(coro_pool+cur,coro_pool+i);
                break;
        }
        if (!(coro_pool[coro_current].flags & CORF_RUN)) {
                xprintf("all are dead\n");
		asm("bkpt");
        }
}

// ends task except if this is main one
void coro_done()
{
	coro_kill(coro_current);
	coro_yield();
}

// removes task 
void coro_kill(int cidx)
{
	if (!cidx) return;
	coro_t *a = coro_pool + cidx;
	a->flags &= ~CORF_RUN;
	if (!(a->flags & CORF_JOIN)) a->flags &= ~CORF_USED;
}

void coro_join(int cidx)
{
	if (cidx == coro_current) return;
	while (coro_pool[cidx].flags & CORF_RUN) coro_yield();
	coro_pool[cidx].flags &= ~(CORF_USED|CORF_JOIN);
}

void coro_init()
{
        // mark itself as task 0
        coro_current = 0;
        coro_pool[coro_current].flags = CORF_USED|CORF_RUN;
}

