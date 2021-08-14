#include "../kernel/drivers/disk/fs.h"

void main() {
	set_superblock();

	file_list();
}
