# Purpose #
XWinMSR is a Linux Kernel Driver which provides access to the cpuid instruction and the msr registers in the processor ring 0.

The example below returns the temperature of the Intel i7 Processor Cores.
# Usage #
  * [Download](http://code.google.com/p/xwinmsr/source/browse) the source code
  1. Makefile
  1. XWinMSRk.c
  1. XWinMSR.h
  1. XWinMSR.c
  * Build driver and program
```
make
```
  * Load the kernel module
```
insmod XWinMSRk.ko
```
  * Run as root the program
```
./XWinMSR
```
![http://blog.cyring.free.fr/images/XWinMSR.png](http://blog.cyring.free.fr/images/XWinMSR.png)
# Kernel Module #
## XWinMSRk.c ##
XWinMSRk.ko is the kernel device driver which provides a shared memory based protocol to the user program to retreive the Processor information data.
### Function `XWinMSR_CPUCount` ###
> _the maximum number of processor cores in this physical processor package. Encoded with a “plus 1” encoding. Add one to the value in this register field. This value does not change when cores are disabled by software._ <sup>[1]</sup>
```
unsigned int XWinMSR_CPUCount(void)
{
	unsigned int CPUCount=0;

	__asm__ volatile
	(
		"movq	$0x4, %%rax;"
		"xorq	%%rcx, %%rcx;"
		"cpuid;"
		"shr	$26, %%rax;"
		"and	$0x3f, %%rax;"
		"add	$1, %%rax;"
		: "=a"	(CPUCount)
	);
	return(CPUCount);
}
```
### Function `XWinMSR_CPUBrand` ###
> _Functions 80000002h, 80000003h, and 80000004h each return up to 16 ASCII bytes of the processor name in the EAX, EBX, ECX, and EDX registers. The processor name is constructed by concatenating each 16-byte ASCII string returned by the three functions. The processor name is right justified with leading space characters. It is returned in little-endian format and NULL terminated. The processor name can be a maximum of 48 bytes including the NULL terminator character._ <sup>[1]</sup>
The resulted string is a compact string without the leading space characters.
```
void XWinMSR_CPUBrand(PROC *P)
{
	char tmpString[48+1]={0x20};
	int ix=0, jx=0, px=0;

	for(ix=0; ix<3; ix++)
	{
		__asm__ volatile
		(
			"cpuid ;"
			: "=a"  (P->Brand.AX),
			  "=b"  (P->Brand.BX),
			  "=c"  (P->Brand.CX),
			  "=d"  (P->Brand.DX)
			: "a"   (0x80000002 + ix)
		);
		for(jx=0; jx<4; jx++, px++)
			tmpString[px]=P->Brand.AX.Chr[jx];
		for(jx=0; jx<4; jx++, px++)
			tmpString[px]=P->Brand.BX.Chr[jx];
		for(jx=0; jx<4; jx++, px++)
			tmpString[px]=P->Brand.CX.Chr[jx];
		for(jx=0; jx<4; jx++, px++)
			tmpString[px]=P->Brand.DX.Chr[jx];
	}
	for(ix=jx=0; jx < px; jx++)
		if(!(tmpString[jx] == 0x20 && tmpString[jx+1] == 0x20))
			P->BrandString[ix++]=tmpString[jx];
}
```
### Function `XWinMSR_CoreTemp` ###
#### MSR\_TEMPERATURE\_TARGET ####
> _The minimum temperature at which PROCHOT# will be asserted. The value is degree C._ <sup>[2]</sup>
Bit fields [23:16] of the EAX register contain the _Temperature Target_ `[TARGET]`
#### MSR IA32\_THERM\_STATUS ####
> _Thermal Status Information (RO) Contains status information about the processor’s thermal sensor and automatic thermal monitoring facilities._ <sup>[2]</sup>
Bit fields [22:16] of the EAX register contain the _Digital Readout_ `[DTS]`
```
void XWinMSR_CoreTemp(PROC *P, int cpu)
{
	__asm__ volatile
	(
		"rdmsr ;"
                : "=a" (P->Core[cpu].TjMax.Lo),
		  "=d" (P->Core[cpu].TjMax.Hi)
		: "c" (MSR_TEMPERATURE_TARGET)
	);

	__asm__ volatile
	(
		"rdmsr ;"
		: "=a" (P->Core[cpu].ThermStat.Lo),
		  "=d" (P->Core[cpu].ThermStat.Hi)
		: "c" (IA32_THERM_STATUS)
	);
}
```
### Function `XWinMSR_threadfn` ###
Each Core is assigned to a kernel thread of the function `XWinMSR_threadfn` which loops over the `XWinMSR_CoreTemp` function every 100 ms
```
int XWinMSR_threadfn(void *data)
{
	if(data != NULL)
	{
		CORE *Core=(CORE *) data;

		while(!kthread_should_stop())
		{
			XWinMSR_CoreTemp(Proc, Core->cpu);

			msleep(100);
		}
	}
	return(0);
}
```
### Function `XWinMSR_mmap` ###
  * share the driver memory with the user program <sup>[4]</sup>
From the first to the last Core:
  * starts the associated Core thread <sup>[3]</sup>
```
static int XWinMSR_mmap(struct file *filp, struct vm_area_struct *vma)
{
        if(Proc && !remap_vmalloc_range(vma, Proc, 0))
        {
                unsigned int cpu=0;
                for(cpu=0; cpu < Proc->CPUCount; cpu++)
                        wake_up_process(Proc->Core[cpu].TID);
        }
        return(0);
}
```
### Function `XWinMSR_release` ###
  * reset global variables
```
static int XWinMSR_release(struct inode *inode, struct file *file)
{
        if(Proc)
                Proc->msleep=LOOP_DEF_MS;
        return(0);
}
```
### Function `XWinMSR_init` ###
When the driver starts, the `XWinMSR_init function` does the followings:
  * allocates the shared memory
  * creates a device file in /dev ( automatically defines its major # )
  * gets the number of CPU Cores
  * reads the processor brand string
From the first to the last Core:
  1. creates one thread per Core whom thread's argument is a pointer to the CPU slot in the shared memory
  1. binds the thread to the Core (CPU affinity)
```
static int __init XWinMSR_init(void)
{
        Proc=vmalloc_user(sizeof(PROC));

        XWinMSR.kcdev=cdev_alloc();
        XWinMSR.kcdev->ops=&XWinMSR_fops;
        XWinMSR.kcdev->owner=THIS_MODULE;

        if(alloc_chrdev_region(&XWinMSR.nmdev, 0, 1, SHM_FILENAME) >= 0)
        {
                XWinMSR.Major=MAJOR(XWinMSR.nmdev);
                XWinMSR.mkdev=MKDEV(XWinMSR.Major,0);

                if(cdev_add(XWinMSR.kcdev, XWinMSR.mkdev, 1) >= 0)
                {
                        struct device *tmpDev;

                        XWinMSR.clsdev=class_create(THIS_MODULE, SHM_DEVNAME);

                        if((tmpDev=device_create(XWinMSR.clsdev, NULL, XWinMSR.mkdev, NULL, SHM_DEVNAME)) != NULL)
                        {
                                unsigned int cpu=0, CPUCount=0;

                                CPUCount=XWinMSR_CPUCount();
                                Proc->CPUCount=(!CPUCount) ? 1 : CPUCount;

                                XWinMSR_CPUBrand(Proc);

                                printk("XWinMSR:%s [%d x CPU]\n", Proc->BrandString, Proc->CPUCount);

                                Proc->msleep=LOOP_DEF_MS;

                                for(cpu=0; cpu < Proc->CPUCount; cpu++)
                                {
                                        Proc->Core[cpu].cpu=cpu;

                                        Proc->Core[cpu].TID=kthread_create(XWinMSR_threadfn, &Proc->Core[cpu], "XWinMSRthread%02d", Proc->Core[cpu].cpu);

                                        kthread_bind(Proc->Core[cpu].TID, cpu);

                                }
                        }
//...
                }
//...
        }
//...
        return(0);
}
```
### Function `XWinMSR_cleanup` ###
When removing the kernel module the device file is deleted and the shared memory is freed.
All threads are terminated.
```
static void __exit XWinMSR_cleanup(void)
{
        device_destroy(XWinMSR.clsdev, XWinMSR.mkdev);
        class_destroy(XWinMSR.clsdev);
        cdev_del(XWinMSR.kcdev);
        unregister_chrdev_region(XWinMSR.mkdev, 1);

        if(Proc)
        {
                unsigned int cpu;
                for(cpu=0; cpu < Proc->CPUCount; cpu++)
                        kthread_stop(Proc->Core[cpu].TID);

                vfree(Proc);
        }
}
```
### Driver functionnalities ###
The file\_operations structure establishes links between the driver services and the user program.
```
static struct file_operations XWinMSR_fops=
{
        .mmap   = XWinMSR_mmap,
        .open   = nonseekable_open,
        .release= XWinMSR_release
};
```

|Service|XWinMSRk.c|XWinMSR.c|
|:------|:---------|:--------|
|.mmap|[XWinMSR\_mmap()](#Function_XWinMSR_mmap.md)|[P=mmap(...)](#XWinMSR.c.md)|
|.open|N/A|[fd=open(...)](#XWinMSR.c.md)|
|.release|[XWinMSR\_release()](#Function_XWinMSR_release.md)|[close(fd)](#XWinMSR.c.md)|

Load the driver module with `insmod XWinMSRk.ko` will call the `XWinMSR_init` function, whereas stopping it with `rmmod XWinMSRk.ko` will invoke `XWinMSR_cleanup`
```
module_init(XWinMSR_init);
module_exit(XWinMSR_cleanup);
```
## XWinMSR.h ##
This the header file with common definitions to both driver and user program.
### General driver definitions ###
```
#define _MAX_CPU_ 64

#define SHM_DEVNAME "XWinMSR"
#define SHM_FILENAME "/dev/"SHM_DEVNAME

#define IA32_THERM_STATUS               0x19c
#define MSR_TEMPERATURE_TARGET          0x1a2

#define LOOP_MIN_MS     10
#define LOOP_MAX_MS     500
#define LOOP_DEF_MS     100
```
### CPUID and Model Specific Registers ###
```
typedef struct
{
        struct
        {
                unsigned char Chr[4];
        } AX, BX, CX, DX;
} BRAND;


typedef struct
{
        union
        {
                struct
                {
                        unsigned int
                                StatusBit       :  1-0,
                                StatusLog       :  2-1,
                                PROCHOT         :  3-2,
                                PROCHOTLog      :  4-3,
                                CriticalTemp    :  5-4,
                                CriticalTempLog :  6-5,
                                Threshold1      :  7-6,
                                Threshold1Log   :  8-7,
                                Threshold2      :  9-8,
                                Threshold2Log   : 10-9,
                                PowerLimit      : 11-10,
                                PowerLimitLog   : 12-11,
                                ReservedBits1   : 16-12,
                                DTS             : 23-16,
                                ReservedBits2   : 27-23,
                                Resolution      : 31-27,
                                ReadingValid    : 32-31;
                };
                        unsigned int Lo     : 32-0;
        };
                        unsigned int Hi     : 32-0;
} THERM_STATUS;


typedef struct
{
        union
        {
                struct
                {
                        unsigned int
                                ReservedBits1   : 16-0,
                                Target          : 24-16,
                                ReservedBits2   : 32-24;
                };
                        unsigned int Lo : 32-0;
        };
                        unsigned int Hi : 32-0;
} TJMAX;
```
### The shared Memory Structure ###
```
typedef struct
{
        unsigned int            cpu;
        struct task_struct      *TID;

        int                     Temp;
        TJMAX                   TjMax;
        THERM_STATUS            ThermStat;
} CORE;


typedef struct
{
        unsigned int            CPUCount, msleep;
        BRAND                   Brand;
        char                    BrandString[48+1];

        CORE                    Core[_MAX_CPU_];
} PROC;
```
## XWinMSR.c ##
The user program does the followings:
  * opens the device driver file => [Function XWinMSR\_init](#Function_XWinMSR_init.md)
  * maps the kernel shared memory in the user space => [Function XWinMSR\_mmap](#Function_XWinMSR_mmap.md)
  * starts a thread to display the Processor features and the temperature per CPU.
```
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
//...
                close(fd);
        }
//...
        return 0;
}
```
# References #
  * <sup>[1]</sup> Intel® Processor Identification and the CPUID Instruction
  * <sup>[2]</sup> Intel® 64 and IA-32 Architectures Software Developer’s Manual: Table 35-7 MSRS IN THE INTEL® MICROARCHITECTURE CODE NAME NEHALEM
  * <sup>[3]</sup> [Chapter 1. Driver Basics](http://www.kernel.org/doc/htmldocs/device-drivers/ch01s03.html)
  * <sup>[4]</sup> [Chapter 4. Memory Management in Linux](http://www.kernel.org/doc/htmldocs/kernel-api/ch04s03.html)
# Author #
_`CyrIng`_
> 
---

