#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>

#include "XWinMSR.h"

int main(void)
{
	PROC *P=NULL;

	int  fd=open(SHM_FILENAME, O_RDWR);
	if(fd != -1)
	{
		P=mmap(NULL, sizeof(PROC), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0x0);

		if(P != NULL)
		{
			printf("%s [%d x CPU]\n", P->BrandString, P->CPUCount);

			do
			{
				int cpu;
				for(cpu=0; cpu < P->CPUCount; cpu++)
				{
					P->Core[cpu].Temp=P->Core[cpu].TjMax.Target - P->Core[cpu].ThermStat.DTS;

					printf("XWinMSR: Core(%02d) @ %d°C\n", cpu, P->Core[cpu].Temp);
				}
				printf("\nInput [x] to Exit or [Enter] to update Core Temps:");
			} while(getchar() != 'x') ;
			munmap(P, sizeof(PROC));
		}
		else
			printf("XWinMSR:mmap(fd:%d):KO\n", fd);

		close(fd);
	}
	else
		printf("XWinMSR:open('%s', O_RDWR):%d\n", SHM_FILENAME, errno);

	return 0;
}
