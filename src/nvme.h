#include <sys/types.h>
#include <fcntl.h>
#include <libnvme.h>
#include <unistd.h>
#include <cstring>

struct device_config {
	__u32 namespace_id;
	char *name;
	__le64 number_of_logical_blocks;
	int logical_block_size;
} config;

void dump_config() {
	printf("device: %s\n", config.name);
	printf("namespace id: %d\n", config.namespace_id);
	printf("number of logical blocks: %lld\n", config.number_of_logical_blocks);
	printf("logical block size: %d bytes\n", config.logical_block_size);
}

const char *nvme_strerror(int errnum) {
	if (errnum >= ENVME_CONNECT_RESOLVE)
		return nvme_errno_to_string(errnum);
	return std::strerror(errnum);
}

void nvme_show_status(__u16 status) {
	fprintf(stderr, "NVMe status: %s(%#x)\n",
		nvme_status_to_string(status, false), status);
}

int open_bdev(char *dev, int flags) {
	int fd = open(dev, flags);
	if (fd < 0)
		perror("open block device failed");
	return fd;
}

int init_device_config() {
	struct nvme_id_ns ns;
	int fd, err;

	fd = open_bdev(config.name, O_RDONLY);

	err = nvme_get_nsid(fd, &config.namespace_id);
	if (err) {
		perror("get nsid failed");
		goto ret;
	}

	err = nvme_get_logical_block_size(fd, config.namespace_id, &config.logical_block_size);
	if (err) {
		perror("get logical block size failed");
		goto ret;
	}

	err = nvme_identify_ns(fd, config.namespace_id, &ns);
	if (err) {
		perror("identify namespace failed");
		goto ret;
	}

	config.number_of_logical_blocks = ns.nsze;

	printf("init ok!\n\n");
	dump_config();

ret:
	close(fd);

	return 0;
}

struct nvme_data_packet {
	int start_block;
	
};