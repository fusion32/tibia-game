#ifndef TIBIA_COMMON_HH_
#define TIBIA_COMMON_HH_ 1

#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <algorithm>

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef int64_t int64;
typedef uint64_t uint64;
typedef uintptr_t uintptr;
typedef size_t usize;

#define STATIC_ASSERT(expr) static_assert((expr), #expr)
#define NARRAY(arr) (int)(sizeof(arr) / sizeof(arr[0]))
#define ISPOW2(x) ((x) != 0 && ((x) & ((x) - 1)) == 0)
#define KB(x) ((usize)(x) << 10)
#define MB(x) ((usize)(x) << 20)
#define GB(x) ((usize)(x) << 30)

#if !OS_WINDOWS && !OS_LINUX
#	if defined(_WIN32)
#		define OS_WINDOWS 1
#	elif defined(__linux__) || defined(__gnu_linux__)
#		define OS_LINUX 1
#	else
#		error "Operating system not supported."
#	endif
#endif

#if !COMPILER_MSVC && !COMPILER_GCC && !COMPILER_CLANG
#	if defined(_MSC_VER)
#		define COMPILER_MSVC 1
#	elif defined(__GNUC__)
#		define COMPILER_GCC 1
#	elif defined(__clang__)
#		define COMPILER_CLANG 1
#	endif
#endif

#if COMPILER_GCC || COMPILER_CLANG
#	define ATTR_FALLTHROUGH __attribute__((fallthrough))
#	define ATTR_PRINTF(x, y) __attribute__((format(printf, x, y)))
#else
#	define ATTR_FALLTHROUGH
#	define ATTR_PRINTF(x, y)
#endif

#if COMPILER_MSVC
#	define TRAP() __debugbreak()
#elif COMPILER_GCC || COMPILER_CLANG
#	define TRAP() __builtin_trap()
#else
#	define TRAP() abort()
#endif

#define ASSERT_ALWAYS(expr) if(!(expr)) { TRAP(); }
#if ENABLE_ASSERTIONS
#	define ASSERT(expr) ASSERT_ALWAYS(expr)
#else
#	define ASSERT(expr) ((void)(expr))
#endif

// NOTE(fusion): The server will only compile on Linux due to a few Linux specific
// features being used. Making it compile on Windows shouldn't be too difficult but
// would require a few design changes.
STATIC_ASSERT(OS_LINUX);
#include <errno.h>
#include <unistd.h>

// NOTE(fusion): This is the member name for the thread id in `struct sigevent`
// when `sigev_notify` is `SIGEV_THREAD_ID` but for whatever reason glibc doesn't
// define it.
#ifndef sigev_notify_thread_id
#	define sigev_notify_thread_id _sigev_un._tid
#endif

// Constants
// =============================================================================
// TODO(fusion): There are many constants that are hardcoded as decompilation
// artifacts. We should define them here and use when appropriate. It is not
// as simple because I've been using `NARRAY` in some cases and they're used
// essentially everywhere.

//#define MAX_NAME 30 // used with most short strings (should replace MAX_IDENT_LENGTH too)
//#define MAX_IPADDRESS 16
//#define MAX_BUDDIES 100
//#define MAX_SKILLS 25
//#define MAX_SPELLS 256
//#define MAX_QUESTS 500
#define MAX_DEPOTS 9
//#define MAX_OPEN_CONTAINERS 16
#define MAX_SPELL_SYLLABLES 10

// shm.cc
// =============================================================================
void StartGame(void);
void CloseGame(void);
void EndGame(void);
bool LoginAllowed(void);
bool GameRunning(void);
bool GameStarting(void);
bool GameEnding(void);
pid_t GetGameProcessID(void);
pid_t GetGameThreadID(void);
int GetPrintlogPosition(void);
char *GetPrintlogLine(int Line);
void IncrementObjectCounter(void);
void DecrementObjectCounter(void);
uint32 GetObjectCounter(void);
void IncrementPlayersOnline(void);
void DecrementPlayersOnline(void);
int GetPlayersOnline(void);
void IncrementNewbiesOnline(void);
void DecrementNewbiesOnline(void);
int GetNewbiesOnline(void);
void SetRoundNr(uint32 RoundNr);
uint32 GetRoundNr(void);
void SetCommand(int Command, char *Text);
int GetCommand(void);
char *GetCommandBuffer(void);
void InitSHM(bool Verbose);
void ExitSHM(void);
void InitSHMExtern(bool Verbose);
void ExitSHMExtern(void);

// strings.cc
// =============================================================================
const char *AddStaticString(const char *String);
uint32 AddDynamicString(const char *String);
const char *GetDynamicString(uint32 Number);
void DeleteDynamicString(uint32 Number);
void CleanupDynamicStrings(void);
void InitStrings(void);
void ExitStrings(void);

bool IsCountable(const char *s);
const char *Plural(const char *s, int Count);
const char *SearchForWord(const char *Pattern, const char *Text);
const char *SearchForNumber(int Count, const char *Text);
bool MatchString(const char *Pattern, const char *String);
void AddSlashes(char *Destination, const char *Source);
void Trim(char *Text);
void Trim(char *Destination, const char *Source);
char *Capitals(char *Text);

// time.cc
// =============================================================================
extern uint32 RoundNr;
extern uint32 ServerMilliseconds;
struct tm GetLocalTimeTM(time_t t);
int64 GetClockMonotonicMS(void);
void GetRealTime(int *Hour, int *Minute);
void GetTime(int *Hour, int *Minute);
void GetDate(int *Year, int *Cycle, int *Day);
void GetAmbiente(int *Brightness, int *Color);
uint32 GetRoundAtTime(int Hour, int Minute);
uint32 GetRoundForNextMinute(void);

// utils.cc
// =============================================================================
typedef void TErrorFunction(const char *Text);
typedef void TPrintFunction(int Level, const char *Text);
void SetErrorFunction(TErrorFunction *Function);
void SetPrintFunction(TPrintFunction *Function);
void SilentHandler(const char *Text);
void SilentHandler(int Level, const char *Text);
void LogFileHandler(const char *Text);
void LogFileHandler(int Level, const char *Text);
void error(const char *Text, ...) ATTR_PRINTF(1, 2);
void print(int Level, const char *Text, ...) ATTR_PRINTF(2, 3);
int random(int Min, int Max);
bool FileExists(const char *FileName);

bool isSpace(int c);
bool isAlpha(int c);
bool isEngAlpha(int c);
bool isDigit(int c);
int toLower(int c);
int toUpper(int c);
char *strLower(char *s);
char *strUpper(char *s);
int stricmp(const char *s1, const char *s2, int Max = INT_MAX);
char *findFirst(char *s, char c);
char *findLast(char *s, char c);

bool CheckBitIndex(int BitSetBytes, int Index);
bool CheckBit(uint8 *BitSet, int Index);
void SetBit(uint8 *BitSet, int Index);
void ClearBit(uint8 *BitSet, int Index);

template<typename T>
void RandomShuffle(T *Buffer, int Size){
	if(Buffer == NULL){
		error("RandomShuffle: Buffer ist NULL.\n");
		return;
	}

	int Max = Size - 1;
	for(int Min = 0; Min < Max; Min += 1){
		int Swap = random(Min, Max);
		if(Swap != Min){
			std::swap(Buffer[Min], Buffer[Swap]);
		}
	}
}

struct TReadStream {
	// VIRTUAL FUNCTIONS
	// =================
	virtual bool readFlag(void);														// VTABLE[0]
	virtual uint8 readByte(void) = 0;													// VTABLE[1]
	virtual uint16 readWord(void);														// VTABLE[2]
	virtual uint32 readQuad(void);														// VTABLE[3]
	virtual void readString(char *Buffer, int MaxLength);								// VTABLE[4]
	virtual void readBytes(uint8 *Buffer, int Count);									// VTABLE[5]
	virtual bool eof(void) = 0;															// VTABLE[6]
	virtual void skip(int Count) = 0;													// VTABLE[7]
};

struct TReadBuffer: TReadStream {
	TReadBuffer(const uint8 *Data, int Size);

	// VIRTUAL FUNCTIONS
	// =================
	uint8 readByte(void) override;
	uint16 readWord(void) override;
	uint32 readQuad(void) override;
	void readBytes(uint8 *Buffer, int Count) override;
	bool eof(void) override;
	void skip(int Count) override;

	// DATA
	// =================
	const uint8 *Data;
	int Size;
	int Position;
};

struct TWriteStream {
	// VIRTUAL FUNCTIONS
	// =================
	virtual void writeFlag(bool Flag);													// VTABLE[0]
	virtual void writeByte(uint8 Byte) = 0;												// VTABLE[1]
	virtual void writeWord(uint16 Word);												// VTABLE[2]
	virtual void writeQuad(uint32 Quad);												// VTABLE[3]
	virtual void writeString(const char *String);										// VTABLE[4]
	virtual void writeBytes(const uint8 *Buffer, int Count);							// VTABLE[5]
};

struct TWriteBuffer: TWriteStream {
	TWriteBuffer(uint8 *Data, int Size);

	// VIRTUAL FUNCTIONS
	// =================
	void writeByte(uint8 Byte) override;
	void writeWord(uint16 Word) override;
	void writeQuad(uint32 Quad) override;
	void writeBytes(const uint8 *Buffer, int Count) override;

	// DATA
	// =================
	uint8 *Data;
	int Size;
	int Position;
};

struct TDynamicWriteBuffer: TWriteBuffer {
	TDynamicWriteBuffer(int InitialSize);
	void resizeBuffer(void);

	// VIRTUAL FUNCTIONS
	// =================
	void writeByte(uint8 Byte) override;
	void writeWord(uint16 Word) override;
	void writeQuad(uint32 Quad) override;
	void writeBytes(const uint8 *Buffer, int Count) override;

	// TODO(fusion): Appended virtual functions. These are not in the base class
	// VTABLE which can be problematic if we intend to use polymorphism, although
	// that doesn't seem to be case.
	virtual ~TDynamicWriteBuffer(void);													// VTABLE[6]
	// Duplicate destructor that also calls operator delete.							// VTABLE[7]
};

#endif //TIBIA_COMMON_HH_
