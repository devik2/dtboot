struct ddr_regtab_t {
	const char *name;
	uint16_t off,flags;
	uint32_t val,val3;
} const ddr_regtab[] = { 
	{ "mstr", 0x0, 0x380, 0x00041004, 0x00041401 },
	{ "stat", 0x4, 0x0, 0x00000000, 0x00000000 },
	{ "mrctrl0", 0x10, 0x380, 0x00000010, 0x00000010 },
	{ "mrctrl1", 0x14, 0x380, 0x00000000, 0x00000000 },
	{ "mrstat", 0x18, 0x0, 0x00000000, 0x00000000 },
	{ "derateen", 0x20, 0x380, 0x00000001, 0x00000000 },
	{ "derateint", 0x24, 0x380, 0x00800000, 0x00800000 },
	{ "pwrctl", 0x30, 0x380, 0x00000000, 0x00000000 },
	{ "pwrtmg", 0x34, 0x380, 0x00400010, 0x00400010 },
	{ "hwlpctl", 0x38, 0x380, 0x00000000, 0x00000000 },
	{ "rfshctl0", 0x50, 0x380, 0x00210000, 0x00210000 },
	{ "rfshctl3", 0x60, 0x380, 0x00000000, 0x00000000 },
	{ "rfshtmg", 0x64, 0x580, 0x00400045, 0x004a0050 },
	{ "crcparctl0", 0xc0, 0x380, 0x00000000, 0x00000000 },
	{ "crcparstat", 0xcc, 0x0, 0x00000000, 0x00000000 },
	{ "init0", 0xd0, 0x0, 0x00000000, 0x00000000 },
	{ "init1", 0xd4, 0x0, 0x00000000, 0x00000000 },
	{ "init2", 0xd8, 0x0, 0x00000000, 0x00000000 },
	{ "init3", 0xdc, 0x0, 0x00000000, 0x00000000 },
	{ "init4", 0xe0, 0x0, 0x00000000, 0x00000000 },
	{ "init5", 0xe4, 0x0, 0x00000000, 0x00000000 },
	{ "dimmctl", 0xf0, 0x0, 0x00000000, 0x00000000 },
	{ "dramtmg0", 0x100, 0x580, 0x111b1217, 0x0f10140d },
	{ "dramtmg1", 0x104, 0x580, 0x00040623, 0x000a0512 },
	{ "dramtmg2", 0x108, 0x580, 0x04080c0f, 0x0506090f },
	{ "dramtmg3", 0x10c, 0x580, 0x0050500c, 0x0050400c },
	{ "dramtmg4", 0x110, 0x580, 0x0a02060c, 0x06040506 },
	{ "dramtmg5", 0x114, 0x580, 0x05050408, 0x05050303 },
	{ "dramtmg6", 0x118, 0x580, 0x02020006, 0x02020002 },
	{ "dramtmg7", 0x11c, 0x580, 0x00000202, 0x00000202 },
	{ "dramtmg8", 0x120, 0x580, 0x00004405, 0x00001005 },
	{ "dramtmg14", 0x138, 0x580, 0x0000004a, 0x000000a0 },
	{ "dramtmg15", 0x13C, 0x0, 0x00000000, 0x00000000 },
	{ "zqctl0", 0x180, 0x380, 0xc2000040, 0xc2000040 },
	{ "zqctl1", 0x184, 0x0, 0x00000000, 0x00000000 },
	{ "zqctl2", 0x188, 0x0, 0x00000000, 0x00000000 },
	{ "zqstat", 0x18c, 0x0, 0x00000000, 0x00000000 },
	{ "dfitmg0", 0x190, 0x380, 0x02030104, 0x02040104 },
	{ "dfitmg1", 0x194, 0x380, 0x00000202, 0x00000202 },
	{ "dfilpcfg0", 0x198, 0x380, 0x07000000, 0x07000000 },
	{ "dfiupd0", 0x1a0, 0x380, 0xc0400003, 0xc0400003 },
	{ "dfiupd1", 0x1a4, 0x380, 0x00000000, 0x00000000 },
	{ "dfiupd2", 0x1a8, 0x380, 0x00000000, 0x00000000 },
	{ "dfimisc", 0x1b0, 0x0, 0x00000000, 0x00000000 },
	{ "dfistat", 0x1bc, 0x0, 0x00000000, 0x00000000 },
	{ "dfiphymstr", 0x1c4, 0x380, 0x00000001, 0x00000000 },
	{ "addrmap1", 0x204, 0x980, 0x00070707, 0x00070707 },
	{ "addrmap2", 0x208, 0x980, 0x00000000, 0x00000000 },
	{ "addrmap3", 0x20c, 0x980, 0x1f000000, 0x1f000000 },
	{ "addrmap4", 0x210, 0x980, 0x00001f1f, 0x00001f1f },
	{ "addrmap5", 0x214, 0x980, 0x06060606, 0x06060606 },
	{ "addrmap6", 0x218, 0x980, 0x0f0f0f06, 0x06060606 },
	{ "addrmap9", 0x224, 0x980, 0x00000000, 0x00000000 },
	{ "addrmap10", 0x228, 0x980, 0x00000000, 0x00000000 },
	{ "addrmap11", 0x22C, 0x980, 0x00000000, 0x00000000 },
	{ "odtcfg", 0x240, 0x580, 0x04000400, 0x06000600 },
	{ "odtmap", 0x244, 0x380, 0x00000000, 0x00000001 },
	{ "sched", 0x250, 0x1180, 0x00000c01, 0x00000c01 },
	{ "sched1", 0x254, 0x1180, 0x00000000, 0x00000000 },
	{ "perfhpr1", 0x25c, 0x1180, 0x01000001, 0x01000001 },
	{ "perflpr1", 0x264, 0x1180, 0x08000200, 0x08000200 },
	{ "perfwr1", 0x26c, 0x1180, 0x08000400, 0x08000400 },
	{ "dbg0", 0x300, 0x380, 0x00000000, 0x00000000 },
	{ "dbg1", 0x304, 0x380, 0x00000000, 0x00000000 },
	{ "dbgcam", 0x308, 0x0, 0x00000000, 0x00000000 },
	{ "dbgcmd", 0x30c, 0x380, 0x00000000, 0x00000000 },
	{ "dbgstat", 0x310, 0x0, 0x00000000, 0x00000000 },
	{ "swctl", 0x320, 0x0, 0x00000000, 0x00000000 },
	{ "swstat", 0x324, 0x0, 0x00000000, 0x00000000 },
	{ "poisoncfg", 0x36c, 0x380, 0x00000000, 0x00000000 },
	{ "poisonstat", 0x370, 0x0, 0x00000000, 0x00000000 },
	{ "pstat", 0x3fc, 0x0, 0x00000000, 0x00000000 },
	{ "pccfg", 0x400, 0x380, 0x00000010, 0x00000010 },
	{ "pcfgr_0", 0x404, 0x1180, 0x00010000, 0x00010000 },
	{ "pcfgw_0", 0x408, 0x1180, 0x00000000, 0x00000000 },
	{ "pctrl_0", 0x490, 0x0, 0x00000000, 0x00000000 },
	{ "pcfgqos0_0", 0x494, 0x1180, 0x02100c03, 0x02100c03 },
	{ "pcfgqos1_0", 0x498, 0x1180, 0x00800100, 0x00800100 },
	{ "pcfgwqos0_0", 0x49c, 0x1180, 0x01100c03, 0x01100c03 },
	{ "pcfgwqos1_0", 0x4a0, 0x1180, 0x01000200, 0x01000200 },
	{ "pcfgr_1", 0x4b4, 0x1180, 0x00010000, 0x00010000 },
	{ "pcfgw_1", 0x4b8, 0x1180, 0x00000000, 0x00000000 },
	{ "pctrl_1", 0x540, 0x0, 0x00000000, 0x00000000 },
	{ "pcfgqos0_1", 0x544, 0x1180, 0x02100c03, 0x02100c03 },
	{ "pcfgqos1_1", 0x548, 0x1180, 0x00800040, 0x00800040 },
	{ "pcfgwqos0_1", 0x54c, 0x1180, 0x01100c03, 0x01100c03 },
	{ "pcfgwqos1_1", 0x550, 0x1180, 0x01000200, 0x01000200 },
	{ "ridr", 0x00, 0x1, 0x00000000, 0x00000000 },
	{ "pir", 0x04, 0x1, 0x00000000, 0x00000000 },
	{ "pgcr", 0x08, 0x2181, 0x01442e02, 0x01442e02 },
	{ "dllgcr", 0x10, 0x1, 0x00000000, 0x00000000 },
//	{ "acdllcr", 0x14, 0x1, 0x80000000, 0x00000000 },
	{ "acdllcr", 0x14, 0x1, 0x00000000, 0x00000000 },
	{ "ptr0", 0x18, 0x4181, 0x0022a41b, 0x00218550 },
	{ "ptr1", 0x1C, 0x4181, 0x01a99c80, 0x029a51c0 },
	{ "ptr2", 0x20, 0x4181, 0x042016b0, 0x042ced81 },
	{ "aciocr", 0x24, 0x2181, 0x38400812, 0x10400812 },
	{ "dxccr", 0x28, 0x2181, 0x00000910, 0x00000c40 },
	{ "dsgcr", 0x2C, 0x2181, 0x9100025f, 0xf200001f },
	{ "dcr", 0x30, 0x2181, 0x0000000c, 0x0000000b },
	{ "dtpr0", 0x34, 0x4181, 0x46d7abd8, 0xa4ad66f4 },
	{ "dtpr1", 0x38, 0x4181, 0x114500d8, 0x09500084 },
	{ "dtpr2", 0x3C, 0x4181, 0x0004104a, 0x10022a00 },
	{ "mr0", 0x40, 0x4081, 0x00000000, 0x00000220 },
	{ "mr1", 0x44, 0x4181, 0x00000083, 0x00000000 },
	{ "mr2", 0x48, 0x4181, 0x00000006, 0x00000200 },
	{ "mr3", 0x4C, 0x4181, 0x00000003, 0x00000000 },
	{ "odtcr", 0x50, 0x2181, 0x00000000, 0x00010000 },
	{ "zq0cr0", 0x180, 0x1, 0x00000000, 0x00000000 },
	{ "zq0cr1", 0x184, 0x2181, 0x00000016, 0x00000028 },
	{ "zq0sr0", 0x188, 0x1, 0x00000000, 0x00000000 },
	{ "zq0sr1", 0x18C, 0x1, 0x00000000, 0x00000000 },
	{ "dx0gcr", 0x1c0, 0x2181, 0x0000c881, 0x0000ce81 },
	{ "dx0gsr0", 0x1c4, 0x1, 0x00000000, 0x00000000 },
	{ "dx0gsr1", 0x1c8, 0x1, 0x00000000, 0x00000000 },
//	{ "dx0dllcr", 0x1cc, 0x8181, 0x80000000, 0x40000000 },
	{ "dx0dllcr", 0x1cc, 0x8181, 0x40000000, 0x40000000 },
	{ "dx0dqtr", 0x1d0, 0x8181, 0xffffffff, 0xffffffff },
	{ "dx0dqstr", 0x1d4, 0x8181, 0x3db02000, 0x3db02000 },
	{ "dx1gcr", 0x200, 0x2181, 0x0000c881, 0x0000ce81 },
	{ "dx1gsr0", 0x204, 0x1, 0x00000000, 0x00000000 },
	{ "dx1gsr1", 0x208, 0x1, 0x00000000, 0x00000000 },
//	{ "dx1dllcr", 0x20c, 0x8181, 0x80000000, 0x40000000 },
	{ "dx1dllcr", 0x20c, 0x8181, 0x40000000, 0x40000000 },
	{ "dx1dqtr", 0x210, 0x8181, 0xffffffff, 0xffffffff },
	{ "dx1dqstr", 0x214, 0x8181, 0x3db02000, 0x3db02000 },
	//{ "dx2gcr", 0x240, 0x2181, 0x0000c881, 0x0000c881 },
	{ "dx2gcr", 0x240, 0x2181, 0x0000c880, 0x0000c880 },
	{ "dx2gsr0", 0x244, 0x1, 0x00000000, 0x00000000 },
	{ "dx2gsr1", 0x248, 0x1, 0x00000000, 0x00000000 },
	{ "dx2dllcr", 0x24c, 0x8181, 0x40000000, 0x40000000 },
	{ "dx2dqtr", 0x250, 0x8181, 0xffffffff, 0xffffffff },
	{ "dx2dqstr", 0x254, 0x8181, 0x3db02000, 0x3db02000 },
	//{ "dx3gcr", 0x280, 0x2181, 0x0000c881, 0x0000c881 },
	{ "dx3gcr", 0x280, 0x2181, 0x0000c880, 0x0000c880 },
	{ "dx3gsr0", 0x284, 0x1, 0x00000000, 0x00000000 },
	{ "dx3gsr1", 0x288, 0x1, 0x00000000, 0x00000000 },
	{ "dx3dllcr", 0x28c, 0x8181, 0x40000000, 0x40000000 },
	{ "dx3dqtr", 0x290, 0x8181, 0xffffffff, 0xffffffff },
	{ "dx3dqstr", 0x294, 0x8181, 0x3db02000, 0x3db02000 },
	{ NULL,}
};
