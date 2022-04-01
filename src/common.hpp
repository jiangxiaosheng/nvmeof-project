#include <sys/types.h>
#include <fcntl.h>
#include <libnvme.h>

struct device_config
{
	__u32 namespace_id;
	char *name;
	uint32_t number_of_logical_blocks;
} config;

int open_bdev(char *dev, int flags)
{
	auto device_name = basename(dev);
	int fd = open(device_name, flags);
	if (fd < 0)
		perror("open block device failed");
	return fd;
}

int init_device_config() {
	struct nvme_id_ns ns;
	__u32 namespace_id;
	int fd;

	fd = open_bdev(config.name, O_RDWR | O_DIRECT);

	nvme_get_nsid(fd, &namespace_id);
	printf("namespace id is %d\n", namespace_id);

	return 0;
}