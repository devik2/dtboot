#include<stdint.h>

struct boot_param_header {
	uint32_t     magic;                  /* magic word OF_DT_HEADER */
	uint32_t     totalsize;              /* total size of DT block */
	uint32_t     off_dt_struct;          /* offset to structure */
	uint32_t     off_dt_strings;         /* offset to strings */
	uint32_t     off_mem_rsvmap;         /* offset to memory reserve map
					 */
	uint32_t     version;                /* format version */
	uint32_t     last_comp_version;      /* last compatible version */

	/* version 2 fields below */
	uint32_t     boot_cpuid_phys;        /* Which physical CPU id we're
					   booting on */
	/* version 3 fields below */
	uint32_t     size_dt_strings;        /* size of the strings block */

	/* version 17 fields below */
	uint32_t	size_dt_struct;		/* size of the DT structure block */
};

/* Definitions used by the flattened device tree */
#define OF_DT_HEADER            0xd00dfeed      /* 4: version, 4: total size */
#define OF_DT_BEGIN_NODE        0x1      /* Start node: full name */
#define OF_DT_END_NODE          0x2      /* End node */
#define OF_DT_PROP              0x3      /* Property: name off, size, content */
#define OF_DT_END               0x9

struct fdt_scan_data {
	struct boot_param_header *hdr;
	char path[126];
	char verbose;
	uint8_t depth;
	void *arg1;
	uint32_t arg2[4];
	// -x as error (stop)
	int (*prop_found)(struct fdt_scan_data *dat,const char *nm,
			const char *val,int sz,uint32_t *node);
	// new node entered, return 1 to skip it
	int (*begin)(struct fdt_scan_data *dat,uint32_t *node);
};

int scan_fdt(struct fdt_scan_data *dat,uint32_t *ptr);
const char *fdt_get_node_name(struct fdt_scan_data *dat,uint32_t *p);

// returns size of property and optionally sets ptr to its address, -1=not found
// root (if nonzero) is subtree/section pointer
int lookup_fdt(struct boot_param_header *hdr,const char *path,
		uint32_t **ptr,uint32_t *root);
uint32_t *lookup_fdt_by_phandle(struct boot_param_header *hdr,uint32_t phandle,
		uint32_t *root);
int fetch_fdt_ints(struct boot_param_header *fdt,uint32_t *root,
		const char *path, int mincnt,int maxcnt,uint32_t *tgt);
// return value (if nonzero) can be used as root to subsequent lookup_fdt
uint32_t *lookup_fdt_section(struct boot_param_header *hdr,
		const char *path,uint32_t *root);
int clamp_int_warn(int x,int min,int max);

static inline uint32_t be_cpu(uint32_t x)
{
	return __builtin_bswap32(x);
}

static inline uint32_t align4(uint32_t x)
{
	return (x+3) & 0xfffffffc;
}

