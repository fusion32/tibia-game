#include "common.hh"
#include "config.hh"
#include "enums.hh"
#include "threads.hh"
#include "writer.hh"

#include <sys/shm.h>

// NOTE(fusion): This looks like an interface to external tools. Looking at the
// `bin` directory this program was in, there are other programs that probably
// use this interface to control certain aspects of the server. My previous
// assumption that each connection was dispatched into its own process may not
// be correct after all.

struct TSharedMemory {
	int Command;
	char CommandBuffer[256];
	uint32 RoundNr;
	uint32 ObjectCounter;
	uint32 Errors;
	int PlayersOnline;
	int NewbiesOnline;
	int PrintBufferPosition;
	char PrintBuffer[200][128];
	GAMESTATE GameState;
	pid_t GameProcessID;
	pid_t GameThreadID;
};

static TSharedMemory *SHM = NULL;
static bool IsGameServer = false;
static bool VerboseOutput = false;

void StartGame(void){
	if(SHM != NULL){
		if(SHM->GameState == GAME_STARTING){
			SHM->GameState = GAME_RUNNING;
		}
	}else{
		error(Translate("StartGame: SharedMemory existiert nicht.\n",
						"StartGame: SharedMemory does not exist.\n"));
	}
}

void CloseGame(void){
	if(SHM != NULL){
		if(SHM->GameState == GAME_RUNNING){
			SHM->GameState = GAME_CLOSING;
		}
	}else{
		error(Translate("CloseGame: SharedMemory existiert nicht.\n",
						"CloseGame: SharedMemory does not exist.\n"));
	}
}

void EndGame(void){
	if(SHM != NULL){
		SHM->GameState = GAME_ENDING;
	}else{
		error(Translate("EndGame: SharedMemory existiert nicht.\n",
						"EndGame: SharedMemory does not exist.\n"));
	}
}

bool LoginAllowed(void){
	bool Result = false;
	if(SHM != NULL){
		Result = SHM->GameState == GAME_RUNNING;
	}else{
		error(Translate("IsLoginAllowed: SharedMemory existiert nicht.\n",
						"IsLoginAllowed: SharedMemory does not exist.\n"));
	}
	return Result;
}

bool GameRunning(void){
	bool Result = false;
	if(SHM != NULL){
		Result = SHM->GameState == GAME_STARTING
				|| SHM->GameState == GAME_RUNNING
				|| SHM->GameState == GAME_CLOSING;
	}else{
		error(Translate("GameRunning: SharedMemory existiert nicht.\n",
						"GameRunning: SharedMemory does not exist.\n"));
	}
	return Result;
}

bool GameStarting(void){
	bool Result = false;
	if(SHM != NULL){
		Result = SHM->GameState == GAME_STARTING;
	}else{
		error(Translate("GameStarting: SharedMemory existiert nicht.\n",
						"GameStarting: SharedMemory does not exist.\n"));
	}
	return Result;
}

bool GameEnding(void){
	bool Result = false;
	if(SHM != NULL){
		Result = SHM->GameState == GAME_CLOSING
				|| SHM->GameState == GAME_ENDING;
	}else{
		error(Translate("GameEnding: SharedMemory existiert nicht.\n",
						"GameEnding: SharedMemory does not exist.\n"));
	}
	return Result;
}

pid_t GetGameProcessID(void){
	pid_t Pid = 0;
	if(SHM != NULL){
		Pid = SHM->GameProcessID;
	}
	return Pid;
}

pid_t GetGameThreadID(void){
	pid_t Pid = 0;
	if(SHM != NULL){
		Pid = SHM->GameThreadID;
	}
	return Pid;
}

static void ErrorHandler(const char *Text){
	if(VerboseOutput){
		printf("%s", Text);
	}

	if(SHM != NULL){
		SHM->Errors += 1;
		if(SHM->Errors <= 0x8000){
			Log("error", Text);
			if(SHM->Errors == 0x8000){
				Log("error", Translate("Zu viele Fehler. Keine weitere Protokollierung.\n",
									   "Too many errors. No further logging..\n"));
			}
		}
	}
}

static void PrintHandler(int Level, const char *Text){
	static Semaphore LogfileMutex(1);

	if(Level > DebugLevel){
		return;
	}

	if(VerboseOutput){
		printf("%s", Text);
	}

	if(SHM != NULL){
		// TODO(fusion): Does it even make sense to have this Semaphore here?
		// It controls writes to the print buffer but reads outside this scope
		// may be partial. But then, it doesn't seem like it is read anywhere
		// else. Perhaps some external monitor tool, in which case do we even
		// print from multiple threads?
		LogfileMutex.down();
		int Line = SHM->PrintBufferPosition;
		char *Buffer = SHM->PrintBuffer[Line];
		// TODO(fusion): Ughh...
		strncpy(Buffer, Text, sizeof(SHM->PrintBuffer[0]) - 1);
		Buffer[sizeof(SHM->PrintBuffer[0]) - 2] = 0;
		if(Buffer[0] != 0){
			usize TextLen = strlen(Buffer);
			if(Buffer[TextLen - 1] != '\n'){
				strcat(Buffer, "\n");
			}
		}
		SHM->PrintBufferPosition = (Line + 1) % NARRAY(SHM->PrintBuffer);
		LogfileMutex.up();
	}
}

int GetPrintlogPosition(void){
	int Result = 0;
	if(SHM != NULL){
		Result = SHM->PrintBufferPosition;
	}
	return Result;
}

char *GetPrintlogLine(int Line){
	char *Result = NULL;
	if(SHM != NULL && Line >= 0 && Line < NARRAY(SHM->PrintBuffer)){
		Result = SHM->PrintBuffer[Line];
	}
	return Result;
}

void IncrementObjectCounter(void){
	if(SHM != NULL){
		SHM->ObjectCounter += 1;
	}
}

void DecrementObjectCounter(void){
	if(SHM != NULL){
		SHM->ObjectCounter -= 1;
	}
}

uint32 GetObjectCounter(void){
	uint32 ObjectCounter = 0;
	if(SHM != NULL){
		ObjectCounter = SHM->ObjectCounter;
	}
	return ObjectCounter;
}

void IncrementPlayersOnline(void){
	if(SHM != NULL){
		SHM->PlayersOnline += 1;
	}
}

void DecrementPlayersOnline(void){
	if(SHM != NULL){
		SHM->PlayersOnline -= 1;
	}
}

int GetPlayersOnline(void){
	int PlayersOnline = 0;
	if(SHM != NULL){
		PlayersOnline = SHM->PlayersOnline;
	}
	return PlayersOnline;
}

void IncrementNewbiesOnline(void){
	if(SHM != NULL){
		SHM->NewbiesOnline += 1;
	}
}

void DecrementNewbiesOnline(void){
	if(SHM != NULL){
		SHM->NewbiesOnline -= 1;
	}
}

int GetNewbiesOnline(void){
	int NewbiesOnline = 0;
	if(SHM != NULL){
		NewbiesOnline = SHM->NewbiesOnline;
	}
	return NewbiesOnline;
}

void SetRoundNr(uint32 RoundNr){
	if(SHM != NULL){
		SHM->RoundNr = RoundNr;
	}
}

uint32 GetRoundNr(void){
	uint32 RoundNr = 0;
	if(SHM != NULL){
		RoundNr = SHM->RoundNr;
	}
	return RoundNr;
}

void SetCommand(int Command, char *Text){
	if(SHM != NULL){
		SHM->Command = Command;
		if(Text == NULL){
			SHM->CommandBuffer[0] = 0;
		}else{
			strncpy(SHM->CommandBuffer, Text, sizeof(SHM->CommandBuffer));
			SHM->CommandBuffer[sizeof(SHM->CommandBuffer) - 1] = 0;
		}
	}
}

int GetCommand(void){
	int Command = 0;
	if(SHM != NULL){
		Command = SHM->Command;
	}
	return Command;
}

char *GetCommandBuffer(void){
	char *Buffer = NULL;
	if(SHM != NULL && SHM->Command != 0){
		Buffer = SHM->CommandBuffer;
	}
	return Buffer;
}

static bool DeleteSHM(void){
	int SHMID = shmget(SHMKey, 0, 0);
	if(SHMID == -1){
		if(VerboseOutput){
			puts(Translate("DeleteSHM: SharedMemory existiert nicht.",
						   "DeleteSHM: SharedMemory does not exist."));
		}
		return true;
	}

	if(shmctl(SHMID, IPC_RMID, NULL) == -1){
		if(VerboseOutput){
			// TODO(fusion): Include `errno` in the error message?
			puts(Translate("DeleteSHM: Kann SharedMemory nicht löschen.",
						   "DeleteSHM: Cannot delete SharedMemory."));
		}
		return false;
	}

	return true;
}

static void CreateSHM(void){
	bool Deleted = false;
	while(true){
		int SHMID = shmget(SHMKey, sizeof(TSharedMemory), IPC_CREAT | IPC_EXCL | 0777);
		if(SHMID != -1){
			return;
		}

		if(errno != EEXIST || Deleted){
			if(VerboseOutput){
				printf(Translate("CreateSHM: Kann SharedMemory nicht anlegen (Fehler %d).\n",
								 "CreateSHM: Cannot create SharedMemory (Error %d).\n"), errno);
			}
			throw "Cannot create SharedMemory";
		}

		if(!DeleteSHM()){
			throw "Cannot delete SharedMemory";
		}

		Deleted = true;
	}
}

static void AttachSHM(void){
	int SHMID = shmget(SHMKey, sizeof(TSharedMemory), 0);
	if(SHMID == -1){
		if(VerboseOutput){
			// TODO(fusion): Include `errno` in the error message?
			puts(Translate("AttachSHM: Kann SharedMemory nicht fassen.",
						   "AttachSHM: Cannot get SharedMemory."));
		}
		SHM = NULL;
		throw "Cannot get SharedMemory";
	}

	SHM = (TSharedMemory*)shmat(SHMID, NULL, 0);
	if(SHM == (void*)-1){
		if(VerboseOutput){
			puts("AttachSHM: Kann SharedMemory nicht anbinden.");
		}

		SHM = NULL;
		throw "Cannot attach SharedMemory";
	}
}

static void DetachSHM(void){
	if(SHM != NULL){
		if(shmdt(SHM) == -1 && VerboseOutput){
			puts(Translate("DetachSHM: Kann SharedMemory nicht löschen.",
						   "DetachSHM: Cannot detach SharedMemory."));
		}
		SHM = NULL;
	}
}

void InitSHM(bool Verbose){
	IsGameServer = true;
	VerboseOutput = Verbose;

	CreateSHM();
	AttachSHM();
	SetErrorFunction(ErrorHandler);
	SetPrintFunction(PrintHandler);

	// NOTE(fusion): `CreateSHM` ensures a new shared memory segment is created
	// and it should be always zero initialized as per the manual. Nevertheless
	// the decompiled version was also clearing SHM, probably just in case.
	memset(SHM, 0, sizeof(TSharedMemory));

	strncpy(SHM->PrintBuffer[0],
			"SHM initialized. System printing is working!\n",
			sizeof(SHM->PrintBuffer[0]));
	SHM->PrintBuffer[0][sizeof(SHM->PrintBuffer[0]) - 1] = 0;

	SHM->PrintBufferPosition = 1;
	SHM->GameState = GAME_STARTING;
	SHM->GameProcessID = getpid();
	SHM->GameThreadID = gettid();
}

void ExitSHM(void){
	SetErrorFunction(NULL);
	SetPrintFunction(NULL);
	DetachSHM();
	DeleteSHM();
}

void InitSHMExtern(bool Verbose){
	VerboseOutput = Verbose;
	AttachSHM();
}

void ExitSHMExtern(void){
	DetachSHM();
}
