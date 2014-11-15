#define	_MAX_CPU_ 64

#define	SHM_DEVNAME "XWinMSR"
#define	SHM_FILENAME "/dev/"SHM_DEVNAME

#define IA32_THERM_STATUS               0x19c
#define MSR_TEMPERATURE_TARGET          0x1a2

#define	LOOP_MIN_MS	10
#define LOOP_MAX_MS	500
#define	LOOP_DEF_MS	100

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


typedef struct
{
	unsigned int		cpu;
	struct task_struct	*TID;

	int			Temp;
	TJMAX			TjMax;
	THERM_STATUS		ThermStat;
} CORE;


typedef struct
{
	unsigned int		CPUCount, msleep;
	BRAND			Brand;
	char			BrandString[48+1];

	CORE			Core[_MAX_CPU_];
} PROC;
