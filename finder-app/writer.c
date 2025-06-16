#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <syslog.h>
#include <fcntl.h>
#include <string.h>

int main(int argc, char const *argv[])
{
	openlog(NULL, 0, LOG_USER);

	if (argc != 3)
	{
		syslog(LOG_ERR, "Should receive 2 args but %d was given", argc -1);
		return 1;
	}
	syslog(LOG_DEBUG, "Wrinting %s to %s", argv[2], argv[1]);

	int fd = open(argv[1], O_RDWR | O_CREAT, 0666);
	if (fd == -1)
	{
		syslog(LOG_ERR, "Failed to open file %s", argv[1]);
		return 1;
	}

	if (write(fd, argv[2], strlen(argv[2])) < strlen(argv[2]))
	{
		syslog(LOG_ERR, "Failed to write the string to the file");
		return 1;
	}

	close(fd);
	return 0;
}
