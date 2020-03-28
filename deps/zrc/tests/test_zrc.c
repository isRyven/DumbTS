#include <stdio.h>
#include <stdlib.h>
#include <zrc/zrc.h>

zrc_import_lib(assets);

int main(int argc, const char **argv)
{
	zrc_lib_t rc;
	void *buff;
	unsigned int size;
	zrc_open_lib(&rc, zrc_get_lib(assets), NULL, NULL);
	if (!zrc_file_exists(&rc, "assets/manifest.txt")) {
		return 1;
	}
	buff = zrc_read_file(&rc, "assets/controllers/controller.txt", &size);
	printf("%s\n", (const char*)buff);
	zrc_close_lib(&rc);
	free(buff);

	return 0;
}
