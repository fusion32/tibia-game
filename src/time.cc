#include "common.hh"

// IMPORTANT(fusion): `RoundNr` is just the number of seconds since startup which
// is why `GetRoundAtTime` and `GetRoundForNextMinute` straight up uses it as
// seconds. It is incremented every 1 second inside `AdvanceGame`.
uint32 RoundNr = 0;

uint32 ServerMilliseconds = 0;

struct tm GetLocalTimeTM(time_t t){
	struct tm result;
#if COMPILER_MSVC
	localtime_s(&result, &t);
#else
	localtime_r(&t, &result);
#endif
	return result;
}

int64 GetClockMonotonicMS(void){
#if OS_WINDOWS
	LARGE_INTEGER Counter, Frequency;
	QueryPerformanceCounter(&Counter);
	QueryPerformanceFrequency(&Frequency);
	return (int64)((Counter.QuadPart * 1000) / Frequency.QuadPart);
#else
	// NOTE(fusion): The coarse monotonic clock has a larger resolution but is
	// supposed to be faster, even avoiding system calls in some cases. It should
	// be fine for millisecond precision which is what we're using.
	struct timespec Time;
	clock_gettime(CLOCK_MONOTONIC_COARSE, &Time);
	return ((int64)Time.tv_sec * 1000)
		+ ((int64)Time.tv_nsec / 1000000);
#endif
}

void GetRealTime(int *Hour, int *Minute){
	struct tm LocalTime = GetLocalTimeTM(time(NULL));
	*Hour = LocalTime.tm_hour;
	*Minute = LocalTime.tm_min;
}

void GetTime(int *Hour, int *Minute){
	// NOTE(fusion): This maps each real time hour to a game time day.
	struct tm LocalTime = GetLocalTimeTM(time(NULL));
	int Time = LocalTime.tm_sec + LocalTime.tm_min * 60;
	*Hour = (Time / 150);
	*Minute = (Time % 150) * 2 / 5;
}

void GetDate(int *Year, int *Cycle, int *Day){
	// NOTE(fusion): This maps each real time week to a game time year.
	time_t RealTime = time(NULL);
	struct tm LocalTime = GetLocalTimeTM(RealTime);
	*Year = (int)(((RealTime / 86400) + 4) / 7);
	*Cycle = LocalTime.tm_wday;
	*Day = LocalTime.tm_hour;
}

void GetAmbiente(int *Brightness, int *Color){
	int Hour, Minute;
	GetTime(&Hour, &Minute);

	int Time = Minute + Hour * 60;
	if(Time < 60){
		*Brightness = 0x33;
		*Color = 0xD7;
	}else if(Time < 120){
		*Brightness = 0x66;
		*Color = 0xD7;
	}else if(Time < 180){
		*Brightness = 0x99;
		*Color = 0xAD;
	}else if(Time < 240){
		*Brightness = 0xCC;
		*Color = 0xAD;
	}else if(Time <= 1200){
		*Brightness = 0xFF;
		*Color = 0xD7;
	}else if(Time <= 1260){
		*Brightness = 0xCC;
		*Color = 0xD0;
	}else if(Time <= 1320){
		*Brightness = 0x99;
		*Color = 0xD0;
	}else if(Time <= 1380){
		*Brightness = 0x66;
		*Color = 0xD7;
	}else{
		*Brightness = 0x33;
		*Color = 0xD7;
	}
}

uint32 GetRoundAtTime(int Hour, int Minute){
	struct tm LocalTime = GetLocalTimeTM(time(NULL));
	int SecondsToTime = (Hour - LocalTime.tm_hour) * 3600
					+ (Minute - LocalTime.tm_min) * 60
					+ (0 - LocalTime.tm_sec);
	if(SecondsToTime < 0){
		SecondsToTime += 86400;
	}
	return SecondsToTime + RoundNr;
}

uint32 GetRoundForNextMinute(void){
	struct tm LocalTime = GetLocalTimeTM(time(NULL));
	int SecondsToNextMinute = 60 - LocalTime.tm_sec;
	return SecondsToNextMinute + RoundNr + 30;
}
