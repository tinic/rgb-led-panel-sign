/** \file
 * Userspace interface to the BeagleBone PRU.
 *
 * Wraps the prussdrv library in a sane interface.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#include <prussdrv.h>
#include <pruss_intc_mapping.h>
#include "pru.h"

static unsigned int
proc_read(
    const char * const fname
)
{
	FILE * const f = fopen(fname, "r");
	if (!f)
		die("%s: Unable to open: %s", fname, strerror(errno));
	unsigned int x;
	fscanf(f, "%x", &x);
	fclose(f);
	return x;
}

void
pru_init_driver() {
    prussdrv_init();		
}

pru_t *
pru_init(
	const unsigned short pru_num
)
{
	int ret = prussdrv_open(pru_num == 0 ? PRU_EVTOUT_0 : PRU_EVTOUT_1);
	if (ret)
		die("prussdrv_open open failed\n");

	tpruss_intc_initdata pruss_intc_initdata = PRUSS_INTC_INITDATA;
	prussdrv_pruintc_init(&pruss_intc_initdata);

	void * pru_data_mem;
	prussdrv_map_prumem(pru_num == 0 ? PRUSS0_PRU0_DATARAM : PRUSS0_PRU1_DATARAM, &pru_data_mem );

    void *addr = 0;
    prussdrv_map_extmem(&addr);

	const uintptr_t ddr_addr = proc_read("/sys/class/uio/uio0/maps/map1/addr");
	const uintptr_t ddr_size = proc_read("/sys/class/uio/uio0/maps/map1/size");

    void *ddr_mem = prussdrv_get_virt_addr(ddr_addr);

	pru_t * const pru = (pru_t *)calloc(1, sizeof(*pru));
	if (!pru) {
		die("calloc failed: %s", strerror(errno));
	}

    pru->pru_num	= pru_num;
	pru->data_ram	= pru_data_mem;
	pru->data_ram_size	= 8192; // how to determine?
	pru->ddr_addr	= ddr_addr;
	pru->ddr		= ddr_mem;
	pru->ddr_size	= ddr_size;
    
	fprintf(stderr,
		"%s: PRU %d: data %p @ %zu bytes, DMA %p / %p @ %zu bytes\n",
		__func__,
		pru_num,
		pru->data_ram,
		pru->data_ram_size,
		pru->ddr,
		pru->ddr_addr,
		pru->ddr_size
	);

	return pru;
}


void
pru_exec(
	pru_t * const pru,
	const char * const program
)
{
	char * program_unconst = (char*)(uintptr_t) program;
	if (prussdrv_exec_program(pru->pru_num, program_unconst) < 0)
		die("%s failed", program);
}


void
pru_close(
	pru_t * const pru
)
{
	// \todo unmap memory
	prussdrv_pru_wait_event(pru->pru_num == 0 ? PRU_EVTOUT_0 : PRU_EVTOUT_1);
	prussdrv_pru_clear_event(PRU0_ARM_INTERRUPT);
	prussdrv_pru_disable(pru->pru_num); 
}

void 
pru_exit_driver() {
    prussdrv_exit();
}

int
pru_gpio(
	const unsigned gpio,
	const unsigned pin,
	const unsigned direction,
	const unsigned initial_value
)
{
	const unsigned pin_num = gpio * 32 + pin;
	const char * export_name = "/sys/class/gpio/export";
	FILE * const export_fp = fopen(export_name, "w");
	if (!export_fp)
		die("%s: Unable to open? %s\n",
			export_name,
			strerror(errno)
		);

	fprintf(export_fp, "%d\n", pin_num);
	fclose(export_fp);

	char value_name[64];
	snprintf(value_name, sizeof(value_name),
		"/sys/class/gpio/gpio%u/value",
		pin_num
	);

	FILE * const value = fopen(value_name, "w");
	if (!value)
		die("%s: Unable to open? %s\n",
			value_name,
			strerror(errno)
		);

	fprintf(value, "%d\n", initial_value);
	fclose(value);

	char dir_name[64];
	snprintf(dir_name, sizeof(dir_name),
		"/sys/class/gpio/gpio%u/direction",
		pin_num
	);

	FILE * const dir = fopen(dir_name, "w");
	if (!dir)
		die("%s: Unable to open? %s\n",
			dir_name,
			strerror(errno)
		);

	fprintf(dir, "%s\n", direction ? "out" : "in");
	fclose(dir);

	return 0;
}
