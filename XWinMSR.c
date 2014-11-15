#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <termios.h>
#include <pthread.h>

#include "XWinMSR.h"

const char CLS[6+1]={27,'[','H',27,'[','J',0};

short int flag=0xff;

static void *XWinMSR_threadfn(void *data)
{
	PROC *P=(PROC *) data;

	while(flag)
	{
		int cpu;
		printf("%s%s [%d x CPU]\n\n", CLS, P->BrandString, P->CPUCount);
		for(cpu=0; cpu < P->CPUCount; cpu++)
		{
			P->Core[cpu].Temp=P->Core[cpu].TjMax.Target - P->Core[cpu].ThermStat.DTS;

			printf("\tCore(%02d) @ %d°C\n", cpu, P->Core[cpu].Temp);
		}
		printf("\n(%d ms) [u] Up. [d] Down. [x] Exit.", P->msleep);
		fflush(stdout);
		usleep(P->msleep * 10000);
	}
	return(NULL);
}

int main(void)
{
	PROC *P=NULL;
	pthread_t TID;

	int  fd=open(SHM_FILENAME, O_RDWR);
	if(fd != -1)
	{
		P=mmap(NULL, sizeof(PROC), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0x0);

		if(P != NULL)
		{
			struct termios oldt, newt;
			tcgetattr ( STDIN_FILENO, &oldt );
			newt = oldt;
			newt.c_lflag &= ~( ICANON | ECHO );
			newt.c_cc[VTIME] = 0;
			newt.c_cc[VMIN] = 1;
			tcsetattr ( STDIN_FILENO, TCSANOW, &newt );



			if(!pthread_create(&TID, NULL, XWinMSR_threadfn, P))
			{
				int key=0;
				while(flag)
				{
					key=getchar();
					switch(key)
					{
						case 'u': if(P->msleep < LOOP_MAX_MS) P->msleep+=10;
						break;
						case 'd': if(P->msleep > LOOP_MIN_MS) P->msleep-=10;
						break;
						case 'x': flag=0;
						break;
					}
				}
				pthread_join(TID, NULL);
			}
			tcsetattr ( STDIN_FILENO, TCSANOW, &oldt );
			printf("\n");

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
