//==============================================================================
// Copyright (C) 2023, Advanced Micro Devices, Inc. All rights reserved.
// SPDX-License-Identifier: MIT
//
//==============================================================================


#include "dma_utils.h"

#define DEVICE_NAME_DEFAULT "/dev/qdma01000-MM-0"
#define SIZE_DEFAULT (32)
#define COUNT_DEFAULT (1)

extern int verbose;

static struct option const long_opts[] = {
	{"device", required_argument, NULL, 'd'},
	{"address", required_argument, NULL, 'a'},
	{"size", required_argument, NULL, 's'},
	{"offset", required_argument, NULL, 'o'},
	{"count", required_argument, NULL, 'c'},
	{"data infile", required_argument, NULL, 'f'},
	{"data outfile", required_argument, NULL, 'w'},
	{"help", no_argument, NULL, 'h'},
	{"verbose", no_argument, NULL, 'v'},
	{"read", no_argument, NULL, 'r'},
	{0, 0, 0, 0}
};

static int test_dma(char *devname, uint64_t addr, uint64_t size,
		    uint64_t offset, uint64_t count, char *infname, char *, int scenario, int stress_test, int no_stress_test_queues);

static void usage(const char *name)
{
	int i = 0;

	fprintf(stdout, "usage: %s [OPTIONS]\n\n", name);

	fprintf(stdout, "  -%c (--%s) device name\n",
		long_opts[i].val, long_opts[i].name);
	i++;
	fprintf(stdout, "  -%c (--%s) the start address on the AXI bus\n",
		long_opts[i].val, long_opts[i].name);
	i++;
	fprintf(stdout,
		"  -%c (--%s) size of a single transfer in bytes, default %d,\n",
		long_opts[i].val, long_opts[i].name, SIZE_DEFAULT);
	i++;
	fprintf(stdout, "  -%c (--%s) page offset of transfer\n",
		long_opts[i].val, long_opts[i].name);
	i++;
	fprintf(stdout, "  -%c (--%s) number of transfers, default %d\n",
		long_opts[i].val, long_opts[i].name, COUNT_DEFAULT);
	i++;
	fprintf(stdout, "  -%c (--%s) filename to read the data from (ignored for read scenario)\n",
		long_opts[i].val, long_opts[i].name);
	i++;
	fprintf(stdout,
		"  -%c (--%s) filename to write the data of the transfers\n",
		long_opts[i].val, long_opts[i].name);
	i++;
	fprintf(stdout, "  -%c (--%s) print usage help and exit\n",
		long_opts[i].val, long_opts[i].name);
	i++;
	fprintf(stdout, "  -%c (--%s) verbose output\n",
		long_opts[i].val, long_opts[i].name);
	i++;
	fprintf(stdout, "  -%c (--%s) use read scenario (write scenario without this flag) \n",
		long_opts[i].val, long_opts[i].name);
}

int main(int argc, char *argv[])
{
	int cmd_opt;
	char *device = DEVICE_NAME_DEFAULT;
	uint64_t address = 0;
	uint64_t size = SIZE_DEFAULT;
	uint64_t offset = 0;
	uint64_t count = COUNT_DEFAULT;
	char *infname = NULL;
	char *ofname = NULL;

	int scenario = 0; // 0 write 1 read

	// unused for now
	int no_stress_test_queues = 1;
	int stress_test = 0;

	while ((cmd_opt =
		getopt_long(argc, argv, "vhc:f:d:a:s:o:w:rq:i:", long_opts,
			    NULL)) != -1) {
		switch (cmd_opt) {
		case 0:
			/* long option */
			break;
		case 'r':
			/* read scenario */
			/* no -r flag = write scenario */
			scenario = 1;
			break;
		case 'd':
			/* device node name */
			//fprintf(stdout, "'%s'\n", optarg);
			device = strdup(optarg);
			break;
		case 'a':
			/* RAM address on the AXI bus in bytes */
			address = getopt_integer(optarg);
			break;
		case 's':
			/* size in bytes */
			size = getopt_integer(optarg);
			break;
		case 'o':
			offset = getopt_integer(optarg) & 4095;
			break;
			/* count */
		case 'c':
			count = getopt_integer(optarg);
			break;
			/* count */
		case 'f':
			infname = strdup(optarg);
			break;
		case 'w':
			ofname = strdup(optarg);
			break;
			/* print usage help and exit */
		case 'v':
			verbose = 1;
			break;
		case 'h':
		default:
			usage(argv[0]);
			exit(0);
			break;
		}
	}

	if (verbose)
		fprintf(stdout,
		"dev %s, address 0x%lx, size 0x%lx, offset 0x%lx, count %lu\n",
		device, address, size, offset, count);
	
	return test_dma(device, address, size, offset, count, infname, ofname, scenario, stress_test, no_stress_test_queues);
}

static int test_dma(char *devname, uint64_t addr, uint64_t size,
		    uint64_t offset, uint64_t count, char *infname,
		    char *ofname, int scenario, int stress_test, int no_stress_test_queues)
{
	uint64_t i;
	ssize_t rc;
	char *buffer = NULL;
	char *allocated = NULL;
	struct timespec ts_start, ts_end;
	int infile_fd = -1;
	int outfile_fd = -1;
	int fpga_fd = scenario == 1 ? open(devname, O_RDWR | O_NONBLOCK) : open(devname, O_RDWR);
	double total_time = 0;
	double result;
	double avg_time = 0;


	if (fpga_fd < 0) {
		fprintf(stderr, "unable to open device %s, %d.\n",
			devname, fpga_fd);
		perror("open device");
		return -EINVAL;
	}

	if (infname) {
		infile_fd = open(infname, O_RDONLY);
		if (infile_fd < 0) {
			fprintf(stderr, "unable to open input file %s, %d.\n",
				infname, infile_fd);
			perror("open input file");
			rc = -EINVAL;
			goto out;
		}
	}

	if (ofname) {
		outfile_fd =
		    open(ofname, O_RDWR | O_CREAT | O_TRUNC | O_SYNC,
			 0666);
		if (outfile_fd < 0) {
			fprintf(stderr, "unable to open output file %s, %d.\n",
				ofname, outfile_fd);
			perror("open output file");
			rc = -EINVAL;
			goto out;
		}
	}

	posix_memalign((void **)&allocated, 4096 /*alignment */ , size + 4096);
	if (!allocated) {
		fprintf(stderr, "OOM %lu.\n", size + 4096);
		rc = -ENOMEM;
		goto out;
	}

	//int *target_queue = allocated;
	//*target_queue = target_mm_queue;

	buffer = allocated + offset;
	if (verbose)
		fprintf(stdout, "host buffer 0x%lx = %p\n",
			size + 4096, buffer);

	if (infile_fd >= 0 && scenario == 0) { // do when write only
		rc = read_to_buffer(infname, infile_fd, buffer, size, 0);
		if (rc < 0)
			goto out;
	}

	if(scenario == 1){
		printf("Read scenario\n");
	}
	else{
		printf("Write scenario\n");
	}

	for (i = 0; i < count; i++) {
		if(scenario == 1){
	        clock_gettime(CLOCK_MONOTONIC, &ts_start);
        /* lseek & read data from AXI MM into buffer using SGDMA */
	        rc = read_to_buffer(devname, fpga_fd, buffer, size, addr);
	        if (rc < 0)
	            goto out;
	        clock_gettime(CLOCK_MONOTONIC, &ts_end);
		}
		else{
			clock_gettime(CLOCK_MONOTONIC, &ts_start);
			rc = write_from_buffer(devname, fpga_fd, buffer, size, addr);
			if (rc < 0)
				goto out;
			rc = clock_gettime(CLOCK_MONOTONIC, &ts_end);
		}
		
		/* subtract the start time from the end time */
		timespec_sub(&ts_end, &ts_start);
		total_time += (ts_end.tv_sec + ((double)ts_end.tv_nsec/NSEC_DIV));
		/* a bit less accurate but side-effects are accounted for */
		if (verbose) {
			fprintf(stdout, "#%lu: CLOCK_MONOTONIC %ld.%09ld sec. write %lu bytes\n",
			i, ts_end.tv_sec, ts_end.tv_nsec, size);
		}

		if ((outfile_fd >= 0) && (scenario == 1)) {
			rc = write_from_buffer(ofname, outfile_fd, buffer,
						 size, i * size);
			if (rc < 0)
				goto out;
		}
	}
	avg_time = (double)total_time/(double)count;
	result = ((double)size)/avg_time;
	if (verbose) {
		printf("** Avg time device %s, total time %f sec, avg_time = %f sec, size = %lu bytes, BW = %f bytes/sec\n",	devname, total_time, avg_time, size, result);
	}
	dump_throughput_result(size, result);

	rc = 0;

out:
	close(fpga_fd);
	if (infile_fd >= 0)
		close(infile_fd);
	if (outfile_fd >= 0)
		close(outfile_fd);
	free(allocated);

	return rc;
}
