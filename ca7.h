#include <stdint.h>

void cpu_info();

struct bit_names;
extern const struct bit_names *_bn_cpsr;
extern const struct bit_names *_bn_actlr;
extern const struct bit_names *_bn_scr;
extern const struct bit_names *_bn_sctlr;
void dump_reg(const char *name,uint32_t r,const struct bit_names *bn);
