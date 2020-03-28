## CMakeZRC - A Standalone CMake-Based C Resource Compiler

CMakeZRC is a resource compiler provided in a single CMake script that can easily
be included in another project, inspired by the original project https://github.com/vector-of-bool/cmrc.

CMakeZRC is made for the C language, and uses `zip` to compress and uncompress resources.  
No dependencies. Single CMake script. Easy to use.

## Usage

```cmake
	# include cmake script into your project
	include(cmake/CMakeZRC.cmake)
	
	# add resource library
	zrc_add_resource_library(assets 
		WORKING_DIR "${CMAKE_CURRENT_SOURCE_DIR}" # optional working directory (defaults to project source directory) 
		assets/file1.txt assets/file2.txt # resource list (relative to the working directory or project source directory)
	)
	
	# add additional resources to the resource library
	zrc_add_resources(assets
		WORKING_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
		assets/bootstrap.txt
	)

	# link resource library with the executable
	target_link_libraries(target_app assets)
	
```

```c
#include <stdio.h>
#include <errno.h>
#include <zrc/zrc.h> // include zrc header file
// declare assets in the main scope
zrc_import_lib(assets)

int main(int argc, const char **argv) 
{
	zrc_lib_t rc;
	void *buff;
	unsigned int size;
	if (zrc_open_lib(&rc, zrc_get_lib(assets), NULL, NULL)) {
		fprintf(stderr, "%s\n", strerror(errno));
		return 1;
	}
	if (!zrc_file_exists(&rc, "assets/bootstrap.txt")) {
		fprintf(stdout, "%s\n", "could not find bootstrap file");
		return 1;
	}
	buff = zrc_read_file(&rc, "assets/controllers/controller.txt", &size);
	if (!buff) {
		fprintf(stderr, "%s\n", strerror(errno));
		zrc_close_lib(&rc);
		return 1;
	}
	printf("%s\n", (const char*)buff);
	zrc_close_lib(&rc);
	free(buff);
	return 0;
}
```

## Acknowledgment

### CMakeRC

```
MIT License

Copyright (c) 2017 vector-of-bool <vectorofbool@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### nozip

```
MIT License

Copyright (c) 2016 Ivan Vashchaev, 2019 isRyven<ryven.mt@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
