
struct disp_stim {
	uint16_t sync,bp,act,fp;
};

struct disp_timing {
	struct disp_stim ht,vt;
	uint16_t pix_khz;
	uint16_t dsi_mhz;
};

struct fbuf_t {
	uint8_t *fba;
	uint32_t bytes;
};

void ltdc_layer_setup(int idx,const struct disp_timing *tm,
		uint32_t pfcr,uint32_t fba,
		unsigned xoff,unsigned yoff,unsigned xsz,unsigned ysz);
void init_dsi(const struct disp_timing *tm,uint32_t fba,uint32_t bgclr);
int dsi_start_pll(int khz);
void soft_dsi_stop();
