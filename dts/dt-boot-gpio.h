#ifndef _DT_BOOT_GPIO_
#define _DT_BOOT_GPIO_

#define PM_A(N) (0x10|(N))
#define PM_B(N) (0x20|(N))
#define PM_C(N) (0x30|(N))
#define PM_D(N) (0x40|(N))
#define PM_E(N) (0x50|(N))
#define PM_F(N) (0x60|(N))
#define PM_G(N) (0x70|(N))
#define PM_H(N) (0x80|(N))
#define PM_I(N) (0x90|(N))
#define PM_J(N) (0xa0|(N))
#define PM_K(N) (0xb0|(N))
#define PM_Z(N) (0xf0|(N))
#define PM_IN  0x1
#define PM_OUT 0x2
#define PM_AN  0x3
#define PM_PU  0x4
#define PM_PD  0x8
#define PM_SPD_Pos 4
#define PM_SPD(N)  ((N)<<PM_SPD_Pos)
#define PM_OD  0x40
#define PM_ALT 0x80
#define PM_DFLT_Pos 8
#define PM_DFLT(N) (((N)+2)<<PM_DFLT_Pos)  // default output value

#define PM_MUX(GPIO,MODE,ALT) ((GPIO)|((ALT)<<8)|((MODE)<<16))

#endif
