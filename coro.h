
typedef struct _coro_t coro_t;
typedef void (*coro_fn)(void *arg);

void coro_init();
void coro_done();
void coro_kill();
void coro_join(int cidx);
void coro_yield();
int coro_new(coro_fn fn,void *arg,int joinable,const char *name);
extern uint8_t coro_current;
