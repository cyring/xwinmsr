#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/delay.h>

MODULE_AUTHOR ("CyrIng");
MODULE_DESCRIPTION ("MSR");
MODULE_SUPPORTED_DEVICE ("all");
MODULE_LICENSE ("GPL");

#include "XWinMSR.h"

static struct
{
	int Major;
	struct cdev *kcdev;
	dev_t nmdev, mkdev;
	struct class *clsdev;
} XWinMSR;

static PROC *Proc=NULL;

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

int XWinMSR_threadfn(void *data)
{
	if(data != NULL)
	{
		CORE *Core=(CORE *) data;

		while(!kthread_should_stop())
		{
			XWinMSR_CoreTemp(Proc, Core->cpu);

			msleep(Proc->msleep);
		}
	}
	return(0);
}

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

static int XWinMSR_release(struct inode *inode, struct file *file)
{
	if(Proc)
		Proc->msleep=LOOP_DEF_MS;
	return(0);
}

static struct file_operations XWinMSR_fops=
{
	.mmap	= XWinMSR_mmap,
	.open	= nonseekable_open,
	.release= XWinMSR_release
};

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
			else
			{
				printk("XWinMSR_init():device_create():KO\n");
				return(-EBUSY);
			}
		}
		else
		{
			printk("XWinMSR_init():cdev_add():KO\n");
			return(-EBUSY);
		}
	}
	else
	{
		printk("XWinMSR_init():alloc_chrdev_region():KO\n");
		return(-EBUSY);
	}
	return(0);
}

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

module_init(XWinMSR_init);
module_exit(XWinMSR_cleanup);
