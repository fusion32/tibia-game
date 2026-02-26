#include "communication.hh"
#include "config.hh"
#include "connections.hh"
#include "containers.hh"
#include "crypto.hh"
#include "query.hh"
#include "threads.hh"
#include "writer.hh"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>

// NOTE(fusion): We seem to add this value of 48 every time `NetLoad` is called,
// and I assume it is to account for IPv4 (20 bytes) and TCP (20 bytes) headers
// although there are still 8 bytes on the table that I'm not sure where are
// coming from.
#define PACKET_AVERAGE_SIZE_OVERHEAD 48

#define MAX_COMMUNICATION_THREADS 1100
#define COMMUNICATION_THREAD_STACK_SIZE ((int)KB(64))

#if TIBIA772
static const int TERMINALVERSION[] = {772, 772, 772};
#else
static const int TERMINALVERSION[] = {770, 770, 770};
#endif

static int TCPSocket;
static ThreadHandle AcceptorThread;
static pid_t AcceptorThreadID;
static int ActiveConnections;

static Semaphore RSAMutex(1);
static TRSAPrivateKey PrivateKey;

static TQueryManagerConnectionPool QueryManagerConnectionPool(10);
static int LoadHistory[360];
static int LoadHistoryPointer;
static int TotalLoad;
static int TotalSend;
static int TotalRecv;
static uint32 LagEnd;
static uint32 EarliestFreeAccountAdmissionRound;
static store<TWaitinglistEntry, 100> Waitinglist;
static TWaitinglistEntry *WaitinglistHead;

static Semaphore CommunicationThreadMutex(1);
static bool UseOwnStacks;
static uint8 CommunicationThreadStacks[MAX_COMMUNICATION_THREADS][COMMUNICATION_THREAD_STACK_SIZE];
static pid_t LastUsingCommunicationThread[MAX_COMMUNICATION_THREADS];
static int FreeCommunicationThreadStacks[MAX_COMMUNICATION_THREADS];
static int NumberOfFreeCommunicationThreadStacks;

// Communication Thread Stacks
// =============================================================================
void GetCommunicationThreadStack(int *StackNumber, void **Stack){
	*StackNumber = -1;
	*Stack = NULL;

	if(!UseOwnStacks){
		error("GetCommunicationThreadStack: Bibliothek unterstützt keine eigenen Stacks.\n");
		return;
	}

	CommunicationThreadMutex.down();
	for(int i = 0; i < NumberOfFreeCommunicationThreadStacks; i += 1){
		int FreeStack = FreeCommunicationThreadStacks[i];
		if(LastUsingCommunicationThread[FreeStack] == 0
				|| tgkill(GetGameProcessID(), LastUsingCommunicationThread[FreeStack], 0) == -1){
			// NOTE(fusion): A little swap and pop action.
			NumberOfFreeCommunicationThreadStacks -= 1;
			FreeCommunicationThreadStacks[i] = FreeCommunicationThreadStacks[NumberOfFreeCommunicationThreadStacks];

			*StackNumber = FreeStack;
			*Stack = CommunicationThreadStacks[FreeStack];
			break;
		}
	}
	CommunicationThreadMutex.up();
}

void AttachCommunicationThreadStack(int StackNumber){
	LastUsingCommunicationThread[StackNumber] = gettid();
}

void ReleaseCommunicationThreadStack(int StackNumber){
	CommunicationThreadMutex.down();
	FreeCommunicationThreadStacks[NumberOfFreeCommunicationThreadStacks] = StackNumber;
	NumberOfFreeCommunicationThreadStacks += 1;
	CommunicationThreadMutex.up();
}

void InitCommunicationThreadStacks(void){
	if(UseOwnStacks){
		memset(CommunicationThreadStacks, 0xAA, sizeof(CommunicationThreadStacks));
		for(int i = 0; i < MAX_COMMUNICATION_THREADS; i += 1){
			LastUsingCommunicationThread[i] = 0;
			FreeCommunicationThreadStacks[i] = i;
		}
		NumberOfFreeCommunicationThreadStacks = MAX_COMMUNICATION_THREADS;
	}
}

void ExitCommunicationThreadStacks(void){
	if(UseOwnStacks){
		if(NumberOfFreeCommunicationThreadStacks != MAX_COMMUNICATION_THREADS){
			error("FreeCommunicationThreadStacks: Nicht alle Stacks freigegeben.\n");
		}

		int HighestStackAddress = -1;
		int LowestStackAddress = COMMUNICATION_THREAD_STACK_SIZE;
		for(int i = 0; i < MAX_COMMUNICATION_THREADS; i += 1){
			for(int Addr = 0; Addr < COMMUNICATION_THREAD_STACK_SIZE; Addr += 1){
				if(CommunicationThreadStacks[i][Addr] != 0xAA){
					if(Addr < LowestStackAddress){
						LowestStackAddress = Addr;
					}

					if(Addr > HighestStackAddress){
						HighestStackAddress = Addr;
					}
				}
			}
		}

		// NOTE(fusion): It seems we want to track whether the stack size is
		// too small but I'd argue the method is not very robust.
		if((HighestStackAddress - LowestStackAddress) > (COMMUNICATION_THREAD_STACK_SIZE / 2)){
			error("Maximale Stack-Ausdehnung: %d..%d\n", LowestStackAddress, HighestStackAddress);
		}
	}
}

// Load History
// =============================================================================
bool LagDetected(void){
	return RoundNr <= LagEnd;
}

void NetLoad(int Amount, bool Send){
	CommunicationThreadMutex.down();
	if(Send){
		TotalSend += Amount;
	}else{
		TotalRecv += Amount;
	}
	CommunicationThreadMutex.up();
}

void NetLoadSummary(void){
	CommunicationThreadMutex.down();
	Log("netload", "gesendet:  %d Bytes.\n", TotalSend);
	Log("netload", "empfangen: %d Bytes.\n", TotalRecv);
	TotalSend = 0;
	TotalRecv = 0;
	CommunicationThreadMutex.up();
}

void NetLoadCheck(void){
	static int LastRecv;

	int DeltaRecv = TotalRecv - LastRecv;
	LastRecv = TotalRecv;
	if(DeltaRecv < 0){
		return;
	}

	int DeltaRecvPerPlayer = 0;
	int PlayersOnline = GetPlayersOnline();
	if(PlayersOnline > 0){
		DeltaRecvPerPlayer = DeltaRecv / PlayersOnline;
	}

	TotalLoad -= LoadHistory[LoadHistoryPointer];
	TotalLoad += DeltaRecvPerPlayer;
	LoadHistory[LoadHistoryPointer] = DeltaRecvPerPlayer;
	LoadHistoryPointer += 1;
	if(LoadHistoryPointer >= NARRAY(LoadHistory)){
		LoadHistoryPointer = 0;
	}

	// NOTE(fusion): Running this lag check only makes sense if `LoadHistory`
	// is filled up which won't be the case until this function executes at
	// least as many times as there are entries in `LoadHistory`.
	//	Looking at `AdvanceGame`, we see that this function is called every
	// 10 rounds, giving us the value of `EarliestLagCheckRound`.
	constexpr uint32 EarliestLagCheckRound = 10 * NARRAY(LoadHistory);
	if(RoundNr >= EarliestLagCheckRound && PlayersOnline >= 50){
		int AvgDeltaRecvPerPlayer = (TotalLoad / NARRAY(LoadHistory));
		if(DeltaRecvPerPlayer < (AvgDeltaRecvPerPlayer / 2)){
			Log("game", "Lag erkannt!\n");
			LagEnd = RoundNr + 30;

			// NOTE(fusion): This formula looks weird but when we take `MaxPlayers` 
			// and `PremiumPlayerBuffer` to be 950 and 150 (taken from the config
			// file, although their values are now loaded from the database), we
			// get a line with negative values when the number of players online
			// is less than 650, and exactly 60 when it is 950. Since the delay
			// is 60 when `PremiumPlayerBuffer` is zero, I don't think this is a
			// coincidence.
			int FreeAccountAdmissionDelay = 60;
			if(PremiumPlayerBuffer != 0){
				FreeAccountAdmissionDelay = (PlayersOnline - (MaxPlayers - PremiumPlayerBuffer * 2));
				FreeAccountAdmissionDelay = (FreeAccountAdmissionDelay * 30) / PremiumPlayerBuffer;
				if(FreeAccountAdmissionDelay < 0){
					FreeAccountAdmissionDelay = 0;
				}
			}

			uint32 FreeAccountAdmissionRound = RoundNr + (uint32)FreeAccountAdmissionDelay;
			if(EarliestFreeAccountAdmissionRound < FreeAccountAdmissionRound){
				EarliestFreeAccountAdmissionRound = FreeAccountAdmissionRound;
			}

			TConnection *Connection = GetFirstConnection();
			while(Connection != NULL){
				if(Connection->Live()){
					Connection->EmergencyPing();
				}
				Connection = GetNextConnection();
			}
		}
	}
}

void InitLoadHistory(void){
	for(int i = 0; i < NARRAY(LoadHistory); i += 1){
		LoadHistory[i] = 0;
	}
	LoadHistoryPointer = 0;
	TotalLoad = 0;
	TotalSend = 0;
	TotalRecv = 0;
	LagEnd = 0;
	EarliestFreeAccountAdmissionRound = 0;
	InitLog("netload");
}

void ExitLoadHistory(void){
	// no-op
}

// Connection Output
// =============================================================================
static constexpr int GetPacketSize(int DataSize){
	// NOTE(fusion): This can be annoying but we're compiling with C++11 which
	// means constexpr functions can only have a single return statement.
#if __cplusplus > 201103L
	int EncryptedSize = ((DataSize + 2) + 7) & ~7;
	int PacketSize = EncryptedSize + 2;
	return PacketSize;
#else
	return (((DataSize + 2) + 7) & ~7) + 2;
#endif
}

bool WriteToSocket(TConnection *Connection, uint8 *Buffer, int Size, int MaxSize){
	// IMPORTANT(fusion): The final packet will have the following layout:
	//	PLAIN:
	//		0 .. 2 => Encrypted Size
	//	ENCRYPTED:
	//		2 .. 4 => Data Size
	//		4 ..   => Data + Padding
	//
	//	The caller must ensure `Buffer` has four extra bytes at the beginning so
	// the packet and payload sizes can be written. It should also ensure that
	// `(MaxSize % 8) == 2` so we can always add the necessary amount of padding
	// for encryption.
	ASSERT(Size >= 4 && Size <= MaxSize && MaxSize <= UINT16_MAX);

	int DataSize = Size - 4;
	while((Size % 8) != 2 && Size < MaxSize){
		Buffer[Size] = rand_r(&Connection->RandomSeed);
		Size += 1;
	}

	if((Size % 8) != 2){
		error("WriteToSocket: Failed to add padding (Size: %d, MaxSize: %d)\n",
				Size, MaxSize);
		return false;
	}

	TWriteBuffer WriteBuffer(Buffer, 4);
	WriteBuffer.writeWord((uint16)(Size - 2));
	WriteBuffer.writeWord((uint16)(DataSize));
	for(int i = 2; i < Size; i += 8){
		Connection->SymmetricKey.encrypt(&Buffer[i]);
	}

	int Attempts = 50;
	int BytesToWrite = Size;
	uint8 *WritePtr = Buffer;
	while(BytesToWrite > 0){
		int BytesWritten = (int)write(Connection->GetSocket(), WritePtr, BytesToWrite);
		if(BytesWritten > 0){
			BytesToWrite -= BytesWritten;
			WritePtr += BytesWritten;
		}else if(BytesWritten == 0){
			// TODO(fusion): Can this even happen without an error?
			error("WriteToSocket: Fehler %d beim Senden an Socket %d.\n",
					errno, Connection->GetSocket());
			return false;
		}else if(errno != EINTR){
			if(errno != EAGAIN || Attempts <= 0){
				if(errno == ECONNRESET || errno == EPIPE || errno == EAGAIN){
					Log("game", "Verbindung an Socket %d zusammengebrochen.\n",
							Connection->GetSocket());
				}else{
					error("WriteToSocket: Fehler %d beim Senden an Socket %d.\n",
							errno, Connection->GetSocket());
				}
				return false;
			}

			DelayThread(0, 100000);
			Attempts -= 1;
		}
	}

	NetLoad(PACKET_AVERAGE_SIZE_OVERHEAD + Size, true);
	return true;
}

bool SendLoginMessage(TConnection *Connection, int Type, const char *Message, int WaitingTime){
	if(Type != LOGIN_MESSAGE_ERROR
			&& Type != LOGIN_MESSAGE_PREMIUM
			&& Type != LOGIN_MESSAGE_WAITINGLIST){
		error("SendLoginMessage: Ungültiger Meldungstyp %d.\n", Type);
		return false;
	}

	if(Message == NULL){
		error("SendLoginMessage: Message ist NULL.\n");
		return false;
	}

	if(Type == LOGIN_MESSAGE_WAITINGLIST && (WaitingTime < 0 || WaitingTime > UINT8_MAX)){
		error("SendLoginMessage: Ungültige Wartezeit %d.\n", WaitingTime);
		return false;
	}

	if(strlen(Message) > 290){
		error("SendLoginMessage: Botschaft zu lang (%s).\n", Message);
		return false;
	}

	// IMPORTANT(fusion): This is doing the same thing as `SendData` but in a
	// smaller scale and building the packet directly instead of using the
	// connection's write buffer.
	try{
		uint8 Data[GetPacketSize(300)];
		TWriteBuffer WriteBuffer(Data, sizeof(Data));
		WriteBuffer.writeWord(0); // EncryptedSize
		WriteBuffer.writeWord(0); // DataSize
		WriteBuffer.writeByte((uint8)Type);
		WriteBuffer.writeString(Message);
		if(Type == LOGIN_MESSAGE_WAITINGLIST){
			WriteBuffer.writeByte(WaitingTime);
		}

		return WriteToSocket(Connection, Data, WriteBuffer.Position, WriteBuffer.Size);
	}catch(const char *str){
		error("SendLoginMessage: Fehler beim Füllen des Puffers (%s)\n", str);
		return false;
	}
}

bool SendData(TConnection *Connection){
	if(Connection == NULL){
		error("SendData: Verbindung ist NULL.\n");
		return false;
	}

	int DataSize = Connection->NextToCommit - Connection->NextToSend;
	int PacketSize = GetPacketSize(DataSize);

	// TODO(fusion): The maximum size of a packet depends on the size of
	// `Connection->OutData` and I don't think we'd have a problem with
	// having a constant size buffer on the stack here although with such
	// a small stack size (64KB with our own stacks) it could become a
	// problem.
	uint8 *Buffer = (uint8*)alloca(PacketSize);
	TWriteBuffer WriteBuffer(Buffer, PacketSize);
	WriteBuffer.writeWord(0); // EncryptedSize
	WriteBuffer.writeWord(0); // DataSize

	// NOTE(fusion): `Connection->OutData` is a ring buffer so we need to check
	// if the data we're currently sending is wrapping around, in which case we'd
	// need to copy two separate regions instead of a single contiguous one.
	constexpr int OutDataSize = sizeof(Connection->OutData);
	int DataStart = Connection->NextToSend % OutDataSize;
	int DataEnd = DataStart + DataSize;
	if(DataEnd < OutDataSize){
		WriteBuffer.writeBytes(&Connection->OutData[DataStart], DataSize);
	}else{
		WriteBuffer.writeBytes(&Connection->OutData[DataStart], OutDataSize - DataStart);
		WriteBuffer.writeBytes(&Connection->OutData[0],         DataEnd - OutDataSize);
	}

	bool Result = WriteToSocket(Connection, Buffer, WriteBuffer.Position, WriteBuffer.Size);
	if(Result){
		Connection->NextToSend += DataSize;
	}
	return Result;
}

// Waiting List
// =============================================================================
bool GetWaitinglistEntry(const char *Name, uint32 *NextTry, bool *FreeAccount, bool *Newbie){
	bool Result = false;
	CommunicationThreadMutex.down();
	TWaitinglistEntry *Entry = WaitinglistHead;
	while(Entry != NULL){
		if(stricmp(Entry->Name, Name) == 0){
			break;
		}
		Entry = Entry->Next;
	}

	if(Entry != NULL){
		*NextTry = Entry->NextTry;
		*FreeAccount = Entry->FreeAccount;
		*Newbie = Entry->Newbie;
		Result = true;
	}
	CommunicationThreadMutex.up();
	return Result;
}

void InsertWaitinglistEntry(const char *Name, uint32 NextTry, bool FreeAccount, bool Newbie){
	bool NewEntry = false;
	CommunicationThreadMutex.down();
	TWaitinglistEntry *Prev = NULL;
	TWaitinglistEntry *Entry = WaitinglistHead;
	while(Entry != NULL){
		if(stricmp(Entry->Name, Name) == 0){
			break;
		}
		Prev = Entry;
		Entry = Entry->Next;
	}

	if(Entry == NULL){
		Entry = Waitinglist.getFreeItem();
		Entry->Next = NULL;
		strcpy(Entry->Name, Name);
		Entry->NextTry = NextTry;
		Entry->FreeAccount = FreeAccount;
		Entry->Newbie = Newbie;
		Entry->Sleeping = false;
		if(Prev != NULL){
			Prev->Next = Entry;
		}else{
			WaitinglistHead = Entry;
		}
		NewEntry = true;
	}else{
		Entry->NextTry = NextTry;
		Entry->FreeAccount = FreeAccount;
		Entry->Newbie = Newbie;
	}
	CommunicationThreadMutex.up();

	if(NewEntry){
		Log("queue", "Füge %s in die Warteschlange ein.\n", Name);
	}
}

void DeleteWaitinglistEntry(const char *Name){
	CommunicationThreadMutex.down();
	TWaitinglistEntry *Prev = NULL;
	TWaitinglistEntry *Entry = WaitinglistHead;
	while(Entry != NULL){
		if(stricmp(Entry->Name, Name) == 0){
			break;
		}
		Prev = Entry;
		Entry = Entry->Next;
	}

	if(Entry != NULL){
		if(Prev != NULL){
			Prev->Next = Entry->Next;
		}else{
			WaitinglistHead = Entry->Next;
		}
		Waitinglist.putFreeItem(Entry);
	}
	CommunicationThreadMutex.up();
}

int GetWaitinglistPosition(const char *Name, bool FreeAccount, bool Newbie){
	int FreeNewbies = 0;
	int FreeVeterans = 0;
	int PremiumNewbies = 0;
	int PremiumVeterans = 0;

	CommunicationThreadMutex.down();
	// NOTE(fusion): Remove inactive entries from the start of the list.
	while(WaitinglistHead != NULL && RoundNr > (WaitinglistHead->NextTry + 60)){
		TWaitinglistEntry *Next = WaitinglistHead->Next;
		Waitinglist.putFreeItem(WaitinglistHead);
		WaitinglistHead = Next;
	}

	// NOTE(fusion): Count players up until we find the player's entry or reach
	// the end of the queue.
	TWaitinglistEntry *Entry = WaitinglistHead;
	while(Entry != NULL){
		if(stricmp(Entry->Name, Name) == 0){
			break;
		}

		if(!Entry->Sleeping){
			if(RoundNr > (Entry->NextTry + 5)){
				Entry->Sleeping = true;
			}else if(Entry->FreeAccount){
				if(Entry->Newbie){
					FreeNewbies += 1;
				}else{
					FreeVeterans += 1;
				}
			}else{
				if(Entry->Newbie){
					PremiumNewbies += 1;
				}else{
					PremiumVeterans += 1;
				}
			}
		}

		Entry = Entry->Next;
	}
	CommunicationThreadMutex.up();

	int Result = 1;
	if(FreeAccount){
		Result += PremiumVeterans + FreeVeterans;
		if(Newbie){
			Result += PremiumNewbies + FreeNewbies;
		}else if(GetNewbiesOnline() < (MaxNewbies - PremiumNewbieBuffer)){
			Result += FreeNewbies;
		}
	}else{
		Result += PremiumVeterans;
		if(Newbie || GetNewbiesOnline() < MaxNewbies){
			Result += PremiumNewbies;
		}
	}
	return Result;
}

int CheckWaitingTime(const char *Name, TConnection *Connection, bool FreeAccount, bool Newbie){
	int WaitingTime = 0;
	const char *Reason = NULL;
	int Position = GetWaitinglistPosition(Name, FreeAccount, Newbie);
	int PlayersOnline = GetPlayersOnline();
	int NewbiesOnline = GetNewbiesOnline();
	if((PlayersOnline + Position) > GetOrderBufferSpace()){
		print(3, "Order-Puffer ist fast voll.\n");
		Reason = "The server is overloaded.";
		WaitingTime = (Position / 2) + 10;
	}else if(FreeAccount){
		if(EarliestFreeAccountAdmissionRound > RoundNr){
			print(3, "Keine FreeAccounts zugelassen nach MassKick.\n");
			Reason = "The server is overloaded.\n"
					"Only players with premium accounts\n"
					"are admitted at the moment.";
			WaitingTime = (int)(EarliestFreeAccountAdmissionRound - RoundNr);
			WaitingTime += Position / 2;
		}else if((PlayersOnline + Position) > (MaxPlayers - PremiumPlayerBuffer)){
			print(3, "Kein Platz mehr für FreeAccounts.\n");
			Reason = "Too many players online.\n"
					"Only players with premium accounts\n"
					"are admitted at the moment.";
			WaitingTime = Position / 2 + 5;
		}else if(Newbie && (NewbiesOnline + Position) > (MaxNewbies - PremiumNewbieBuffer)){
			print(3, "Kein Platz mehr für Newbies mit FreeAccount.\n");
			Reason = "There are too many players online\n"
					"on the beginners' island, Rookgaard.\n"
					"Only players with premium accounts\n"
					"are admitted at the moment.";
			WaitingTime = Position / 2 + 5;
		}
	}else{
		if((PlayersOnline + Position) > MaxPlayers){
			print(3, "Zu viele Spieler online.\n");
			Reason = "There are too many players online.";
			WaitingTime = Position / 2 + 3;
		}else if(Newbie && (NewbiesOnline + Position) > MaxNewbies){
			print(3, "Kein Platz mehr für Newbies.\n");
			Reason = "There are too many players online\n"
					"on the beginners' island, Rookgaard.";
			WaitingTime = Position / 2 + 3;
		}
	}

	if(WaitingTime > 240){
		WaitingTime = 240;
	}

	if(WaitingTime > 0){
		char Message[250];
		snprintf(Message, sizeof(Message),
				"%s\n\nYou are at place %d on the waiting list.",
				Reason, Position);
		SendLoginMessage(Connection, LOGIN_MESSAGE_WAITINGLIST, Message, WaitingTime);
	}

	return WaitingTime;
}

// Connection Input
// =============================================================================
int ReadFromSocket(TConnection *Connection, uint8 *Buffer, int Size){
	int Attempts = 50;
	int BytesToRead = Size;
	uint8 *ReadPtr = Buffer;
	while(BytesToRead > 0){
		int BytesRead = (int)read(Connection->GetSocket(), ReadPtr, BytesToRead);
		if(BytesRead > 0){
			BytesToRead -= BytesRead;
			ReadPtr += BytesRead;
		}else if(BytesRead == 0){
			// NOTE(fusion): TCP FIN with no more data to read.
			break;
		}else if(errno != EINTR){
			// TODO(fusion): This is probably overkill. We only need a way to
			// differentiate between an ERROR/TIMEOUT and NODATA.
			if(errno != EAGAIN){
				return -errno;
			}else if(Attempts <= 0){
				return -ETIMEDOUT;
			}else if(BytesToRead == Size){
				return -EAGAIN;
			}

			DelayThread(0, 100000);
			Attempts -= 1;
		}
	}
	return Size - BytesToRead;
}

bool CallGameThread(TConnection *Connection){
	if(GameRunning()){
		Connection->WaitingForACK = true;
		if(tgkill(GetGameProcessID(), GetGameThreadID(), SIGUSR1) == -1){
			error("CallGameThread: Can't send SIGUSR1 to thread %d/%d: (%d) %s\n",
					GetGameProcessID(), GetGameThreadID(), errno, strerrordesc_np(errno));
			SendLoginMessage(Connection, LOGIN_MESSAGE_ERROR,
					"The server is not online.\nPlease try again later.", -1);
			return false;
		}
	}
	return true;
}

bool CheckConnection(TConnection *Connection){
	// TODO(fusion): Check if there is no input data?
	struct pollfd pollfd = {};
	pollfd.fd = Connection->GetSocket();
	pollfd.events = POLLIN;
	return poll(&pollfd, 1, 0) >= 0
		&& (pollfd.revents & POLLIN) == 0;
}

// NOTE(fusion): `PlayerName` is an input and output parameter. It will contain
// the correct player name with upper and lower case letters if the player is
// actually logged in.
TPlayerData *PerformRegistration(TConnection *Connection, char *PlayerName,
		uint32 AccountID, const char *PlayerPassword, bool GamemasterClient){
	TQueryManagerPoolConnection QueryManagerConnection(&QueryManagerConnectionPool);
	if(!QueryManagerConnection){
		error("PerformRegistration: Kann Verbindung zum Query-Manager nicht herstellen.\n");
		SendLoginMessage(Connection, LOGIN_MESSAGE_ERROR, "Internal error, closing connection.", -1);
		return NULL;
	}

	if(!CheckConnection(Connection)){
		return NULL;
	}

	// TODO(fusion): What a disaster. The size of these arrays are are hardcoded
	// and should be declared constants somewhere.
	uint32 CharacterID;
	int Sex;
	char Guild[31];				// MAX_NAME_LENGTH ?
	char Rank[31];				// MAX_NAME_LENGTH ?
	char Title[31];				// MAX_NAME_LENGTH ?
	int NumberOfBuddies;
	uint32 BuddyIDs[100];		// MAX_BUDDIES ?
	char BuddyNames[100][30];	// MAX_BUDDIES, MAX_NAME_LENGTH ?
	uint8 Rights[12];			// MAX_RIGHT_BYTES ?
	bool PremiumAccountActivated;
	int LoginCode = QueryManagerConnection->loginGame(AccountID, PlayerName,
			PlayerPassword, Connection->GetIPAddress(), PrivateWorld, false,
			GamemasterClient, &CharacterID, &Sex, Guild, Rank, Title,
			&NumberOfBuddies, BuddyIDs, BuddyNames, Rights,
			&PremiumAccountActivated);
	switch(LoginCode){
		case 0:{
			// NOTE(fusion): Login successful.
			break;
		}

		case 1:{
			print(3, "Spieler existiert nicht.\n");
			SendLoginMessage(Connection, LOGIN_MESSAGE_ERROR,
					"Character doesn't exist.\n"
					"Create a new character on the Tibia website\n"
					"at \"www.tibia.com\".", -1);
			return NULL;
		}

		case 2:{
			print(3, "Spieler wurde gelöscht.\n");
			SendLoginMessage(Connection, LOGIN_MESSAGE_ERROR,
					"Character doesn't exist.\n"
					"Create a new character on the Tibia website.", -1);
			return NULL;
		}

		case 3:{
			print(3, "Spieler lebt nicht auf dieser Welt.\n");
			SendLoginMessage(Connection, LOGIN_MESSAGE_ERROR,
					"Character doesn't live on this world.\n"
					"Please login on the right world.", -1);
			return NULL;
		}

		case 4:{
			print(3, "Spieler ist nicht eingeladen.\n");
			SendLoginMessage(Connection, LOGIN_MESSAGE_ERROR,
					"This world is private and you have not been invited to play on it.", -1);
			return NULL;
		}

		case 6:{
			Log("game", "Falsches Paßwort für Spieler %s; Login von %s.\n",
					PlayerName, Connection->GetIPAddress());
			SendLoginMessage(Connection, LOGIN_MESSAGE_ERROR,
					"Accountnumber or password is not correct.", -1);
			return NULL;
		}

		case 7:{
			Log("game", "Spieler %s blockiert; Login von %s.\n",
					PlayerName, Connection->GetIPAddress());
			SendLoginMessage(Connection, LOGIN_MESSAGE_ERROR,
					"Account disabled for five minutes. Please wait.", -1);
			return NULL;
		}

		case 8:{
			Log("game", "Account von Spieler %s wurde gelöscht.\n", PlayerName);
			SendLoginMessage(Connection, LOGIN_MESSAGE_ERROR,
					"Accountnumber or password is not correct.", -1);
			return NULL;
		}

		case 9:{
			Log("game", "IP-Adresse %s für Spieler %s blockiert.\n",
					Connection->GetIPAddress(), PlayerName);
			SendLoginMessage(Connection, LOGIN_MESSAGE_ERROR,
					"IP address blocked for 30 minutes. Please wait.", -1);
			return NULL;
		}

		case 10:{
			print(3, "Account ist verbannt.\n");
			SendLoginMessage(Connection, LOGIN_MESSAGE_ERROR,
					"Your account is banished.", -1);
			return NULL;
		}

		case 11:{
			print(3, "Character muss umbenannt werden.\n");
			SendLoginMessage(Connection, LOGIN_MESSAGE_ERROR,
					"Your character is banished because of his/her name.", -1);
			return NULL;
		}

		case 12:{
			print(3, "IP-Adresse ist gesperrt.\n");
			SendLoginMessage(Connection, LOGIN_MESSAGE_ERROR,
					"Your IP address is banished.", -1);
			return NULL;
		}

		case 13:{
			print(3, "Schon andere Charaktere desselben Accounts eingeloggt.\n");
			SendLoginMessage(Connection, LOGIN_MESSAGE_ERROR,
					"You may only login with one character\n"
					"of your account at the same time.", -1);
			return NULL;
		}

		case 14:{
			print(3, "Login mit Gamemaster-Client auf Nicht-Gamemaster-Account.\n");
			SendLoginMessage(Connection, LOGIN_MESSAGE_ERROR,
					"You may only login with a Gamemaster account.", -1);
			return NULL;
		}

		case 15:{
			print(3, "Falsche Accountnummer %u für %s.\n", AccountID, PlayerName);
			SendLoginMessage(Connection, LOGIN_MESSAGE_ERROR,
					"Login failed due to corrupt data.", -1);
			return NULL;
		}

		case -1:{
			SendLoginMessage(Connection, LOGIN_MESSAGE_ERROR,
					"Internal error, closing connection.", -1);
			return NULL;
		}

		default:{
			error("PerformRegistration: Unbekannter Rückgabewert vom QueryManager.\n");
			SendLoginMessage(Connection, LOGIN_MESSAGE_ERROR,
					"Internal error, closing connection.", -1);
			return NULL;
		}
	}

	// TODO(fusion): Again?
	if(!CheckConnection(Connection)){
		QueryManagerConnection->decrementIsOnline(CharacterID);
		return NULL;
	}

	// TODO(fusion): This should probably checked beforehand or also dispatch
	// `decrementIsOnline`.
	if(AccountID == 0){
		error("PerformRegistration: Spieler %s wurde noch keinem Account zugewiesen.\n", PlayerName);
		SendLoginMessage(Connection, LOGIN_MESSAGE_ERROR,
				"Character is not assigned to an account.\n"
				"Perform this on the Tibia website\n"
				"at \"www.tibia.com\".", -1);
		return NULL;
	}

	// TODO(fusion): We were also adding a null terminator to `PlayerName` here
	// for whatever reason. I assume it is a compiler artifact because we already
	// use it in the condition block just above so...
	// PlayerName[29] = 0;

	if(PremiumAccountActivated){
		SendLoginMessage(Connection, LOGIN_MESSAGE_PREMIUM,
				"Your Premium Account is now activated.\n"
				"Have a lot of fun in Tibia.", -1);
	}

	Log("game", "Spieler %s loggt ein an Socket %d von %s.\n",
			PlayerName, Connection->GetSocket(), Connection->GetIPAddress());

	TPlayerData *PlayerData = AssignPlayerPoolSlot(CharacterID, true);
	if(PlayerData == NULL){
		error("PerformRegistration: Kann keinen Slot für Spielerdaten zuweisen.\n");
		QueryManagerConnection->decrementIsOnline(CharacterID);
		SendLoginMessage(Connection, LOGIN_MESSAGE_ERROR,
				"There are too many players online.\n"
				"Please try again later.", -1);
		return NULL;
	}

	bool Locked = (PlayerData->Locked == gettid());

	// TODO(fusion): How come this isn't a race condition? Perhaps these are
	// constant and can only change when loaded from the database?
	if(Locked || PlayerData->AccountID == 0){
		PlayerData->AccountID = AccountID;
		PlayerData->Sex = Sex;
		strcpy(PlayerData->Name, PlayerName);
		memcpy(PlayerData->Rights, Rights, sizeof(Rights));
		strcpy(PlayerData->Guild, Guild);
		strcpy(PlayerData->Rank, Rank);
		strcpy(PlayerData->Title, Title);
	}

	if(Locked && PlayerData->Buddies == 0){
		PlayerData->Buddies = NumberOfBuddies;
		for(int i = 0; i < NumberOfBuddies; i += 1){
			PlayerData->Buddy[i] = BuddyIDs[i];
			strcpy(PlayerData->BuddyName[i], BuddyNames[i]);
		}
	}

	return PlayerData;
}

bool HandleLogin(TConnection *Connection){
	// TODO(fusion): Exception handling keeps getting crazier.

	TReadBuffer InputBuffer(Connection->InData, Connection->InDataSize);
	try{
		uint8 Command = InputBuffer.readByte();
		if(Command != CL_CMD_LOGIN_REQUEST){
			print(3, "Ungültiges Init-Kommando %d.\n", Command);
			return false;
		}
	}catch(const char *str){
		error("HandleLogin: Fehler beim Auslesen des Kommandos (%s).\n", str);
		return false;
	}

	int TerminalType;
	int TerminalVersion;
	bool GamemasterClient;
	uint32 AccountID;
	char PlayerName[30];
	char PlayerPassword[30];
	try{
#if TIBIA772
		// IMPORTANT(fusion): With 7.72, the terminal type and version are brought
		// outside the asymmetric data. This is probably to maintain some level of
		// backwards compatibility with versions before 7.7, given that it was the
		// first encrypted protocol.
		TerminalType = (int)InputBuffer.readWord();
		TerminalVersion = (int)InputBuffer.readWord();
#endif

		// IMPORTANT(fusion): Without a checksum, there is no way of validating the
		// asymmetric data. The best we can do is to verify that the first plaintext
		// byte is ZERO, but that alone isn't enough.
		// TODO(fusion): Using `SendLoginMessage` before initializing the symmetric
		// key will result in gibberish being sent back to the client.
		uint8 AsymmetricData[128];
		InputBuffer.readBytes(AsymmetricData, 128);
		RSAMutex.down();
		if(!PrivateKey.decrypt(AsymmetricData) || AsymmetricData[0] != 0){
			RSAMutex.up();
			error("HandleLogin: Fehler beim Entschlüsseln.\n");
			SendLoginMessage(Connection, LOGIN_MESSAGE_ERROR,
					"Login failed due to corrupt data.", -1);
			return false;
		}
		RSAMutex.up();

		TReadBuffer ReadBuffer(AsymmetricData, 128);
		ReadBuffer.readByte(); // always zero
		Connection->SymmetricKey.init(&ReadBuffer);
#if !TIBIA772
		TerminalType = (int)ReadBuffer.readWord();
		TerminalVersion = (int)ReadBuffer.readWord();
#endif
		GamemasterClient = ReadBuffer.readByte() != 0;
		AccountID = ReadBuffer.readQuad();
		ReadBuffer.readString(PlayerName, sizeof(PlayerName));
		ReadBuffer.readString(PlayerPassword, sizeof(PlayerPassword));
	}catch(const char *str){
		print(3, "Fehler beim Auslesen der Login-Daten (%s).\n", str);
		SendLoginMessage(Connection, LOGIN_MESSAGE_ERROR,
				"Login failed due to corrupt data.",-1);
		return false;
	}

	if(PlayerName[0] == 0){
		SendLoginMessage(Connection, LOGIN_MESSAGE_ERROR,
				"You must enter a character name.", -1);
		return false;
	}

	if(TerminalType < 0 || TerminalType >= NARRAY(TERMINALVERSION)
			|| TERMINALVERSION[TerminalType] != TerminalVersion){
		SendLoginMessage(Connection, LOGIN_MESSAGE_ERROR,
				"Your terminal version is too old.\n"
				"Please get a new version at\n"
				"http://www.tibia.com.", -1);
		return false;
	}

	if(!GameRunning()){
		SendLoginMessage(Connection, LOGIN_MESSAGE_ERROR,
				"The server is not online.\n"
				"Please try again later.", -1);
		return false;
	}

	if(GameStarting()){
		SendLoginMessage(Connection, LOGIN_MESSAGE_ERROR,
				"The game is just starting.\n"
				"Please try again later.", -1);
		return false;
	}

	if(GameEnding()){
		SendLoginMessage(Connection, LOGIN_MESSAGE_ERROR,
				"The game is just going down.\n"
				"Please try again later.", -1);
		return false;
	}

	// NOTE(fusion): Check waitlist entry.
	bool WasInWaitingList = false;
	while(true){
		uint32 NextTry;
		bool FreeAccount;
		bool Newbie;
		if(!GetWaitinglistEntry(PlayerName, &NextTry, &FreeAccount, &Newbie)){
			print(3, "Spieler nicht auf Warteliste.\n");
			break;
		}

		print(3, "Spieler auf Warteliste.\n");
		if(NextTry > RoundNr){
			int WaitingTime = (int)(NextTry - RoundNr);
			Log("queue", "%s meldet sich %d Sekunden zu früh an.\n",
					PlayerName, WaitingTime);
			SendLoginMessage(Connection, LOGIN_MESSAGE_WAITINGLIST,
					"It's not your turn yet.", WaitingTime);
			return false;
		}

		if(RoundNr > (NextTry + 60)){
			Log("queue", "%s meldet sich %d Sekunden zu spät an.\n",
					PlayerName, (RoundNr - NextTry));
			DeleteWaitinglistEntry(PlayerName);
			continue;
		}

		int WaitingTime = CheckWaitingTime(PlayerName, Connection, FreeAccount, Newbie);
		if(WaitingTime > 0){
			NextTry = RoundNr + (uint32)WaitingTime;
			InsertWaitinglistEntry(PlayerName, NextTry, FreeAccount, Newbie);
			return false;
		}

		DeleteWaitinglistEntry(PlayerName);
		WasInWaitingList = true;
		break;
	}

	TPlayerData *Slot = PerformRegistration(Connection,
			PlayerName, AccountID, PlayerPassword, GamemasterClient);
	if(Slot == NULL){
		return false;
	}

	bool SlotLocked = (Slot->Locked == gettid());

	// NOTE(fusion): These checks would have been already made if the player was
	// in the waiting list so they'd be redundant.
	if(!WasInWaitingList){
		bool BlockLogin = false;
		bool FreeAccount = !CheckBit(Slot->Rights, PREMIUM_ACCOUNT);
		bool Newbie = Slot->Profession == PROFESSION_NONE
				&& !CheckBit(Slot->Rights, NO_LOGOUT_BLOCK);

		bool PremiumOnly = (MaxPlayers == PremiumPlayerBuffer)
				|| (Newbie && MaxNewbies == PremiumNewbieBuffer);
		if(!BlockLogin && FreeAccount && PremiumOnly){
			SendLoginMessage(Connection, LOGIN_MESSAGE_ERROR,
					"Only players with premium accounts\n"
					"are allowed to enter this world.", -1);
			BlockLogin = true;
		}

		if(!BlockLogin && !IsPlayerOnline(PlayerName)){
			int WaitingTime = CheckWaitingTime(PlayerName, Connection, FreeAccount, Newbie);
			if(WaitingTime > 0){
				uint32 NextTry = RoundNr + (uint32)WaitingTime;
				InsertWaitinglistEntry(PlayerName, NextTry, FreeAccount, Newbie);
				BlockLogin = true;
			}
		}

		if(BlockLogin){
			// TODO(fusion): This is probably an inlined function.
			TQueryManagerPoolConnection QueryManagerConnection(&QueryManagerConnectionPool);
			if(QueryManagerConnection){
				QueryManagerConnection->decrementIsOnline(Slot->CharacterID);
			}else{
				error("HandleLogin: Kann Verbindung zum Query-Manager nicht herstellen.\n");
			}

			if(SlotLocked){
				ReleasePlayerPoolSlot(Slot);
			}else{
				DecreasePlayerPoolSlotSticky(Slot);
			}
			return false;
		}
	}

	if(SlotLocked){
		IncreasePlayerPoolSlotSticky(Slot);
		ReleasePlayerPoolSlot(Slot);
	}

	// NOTE(fusion): Rewrite packet in a different format, ready for the main
	// thread to interpret.
	TWriteBuffer WriteBuffer(Connection->InData + 2,
					sizeof(Connection->InData) - 2);
	try{
		WriteBuffer.writeByte(CL_CMD_LOGIN);
		WriteBuffer.writeWord((int)TerminalType);
		WriteBuffer.writeWord((int)TerminalVersion);
		WriteBuffer.writeQuad(Slot->CharacterID);
	}catch(const char *str){
		error("HandleLogin: Fehler beim Zusammenbauen des Login-Pakets (%s).\n", str);
		SendLoginMessage(Connection, LOGIN_MESSAGE_ERROR,
				"Internal error, closing connection.",-1);
		return false;
	}

	Connection->NextToSend = 0;
	Connection->NextToCommit = 0;
	Connection->InDataSize = WriteBuffer.Position;
	Connection->NextToWrite = 0;

	Connection->Login();
	return CallGameThread(Connection);
}

bool ReceiveCommand(TConnection *Connection){
	// IMPORTANT(fusion): The return value of this function is used to determine
	// whether the connection should be closed. Returning true will maintain it
	// open. It is a weird convention but w/e.

	if(Connection == NULL){
		error("ReceiveCommand: Connection ist NULL.\n");
		return false;
	}

	while(!Connection->WaitingForACK){
		uint8 Help[2];
		int BytesRead = ReadFromSocket(Connection, Help, 2);
		if(BytesRead == 0){
			// NOTE(fusion): Peer has closed the connection and there was no
			// more data to read.
			return false;
		}else if(BytesRead < 0){
			// NOTE(fusion): There was either no data, some data then a timeout,
			// or a connection error. We only want to keep the connection open
			// in case there was no data, to allow us to return to the connection
			// loop in a consistent state.
			return (BytesRead == -EAGAIN);
		}

		// NOTE(fusion): It doesn't make sense to continue if we couldn't read
		// the packet's length completely. We're already using TCP which doesn't
		// drop data so it would only compound into more errors.
		if(BytesRead != 2){
			NetLoad(PACKET_AVERAGE_SIZE_OVERHEAD + BytesRead, false);
			print(3, "Zu wenig Daten an Socket %d.\n", BytesRead);
			return false;
		}

		// TODO(fusion): Size is encoded as a little endian uint16. We should
		// have a few helper functions to assist with buffer reading.
		int Size = ((uint16)Help[0] | ((uint16)Help[1] << 8));

		// NOTE(fusion): It doesn't make sense to continue if the client didn't
		// correctly size its packet.
		if(Size == 0 || Size > (int)sizeof(Connection->InData)){
			print(3, "Paket an Socket %d zu groß oder leer, wird verworfen (%d Bytes)\n",
					Connection->GetSocket(), Size);
			return false;
		}

		BytesRead = ReadFromSocket(Connection, &Connection->InData[0], Size);
		if(BytesRead < 0){
			// NOTE(fusion): It doesn't make sense to call `SendLoginMessage` before
			// the key exchange has completed, which happens after login.
			if(Connection->State != CONNECTION_CONNECTED){
				SendLoginMessage(Connection, LOGIN_MESSAGE_ERROR,
					"Internal error, closing connection.", -1);
			}
			return false;
		}

		NetLoad(PACKET_AVERAGE_SIZE_OVERHEAD + BytesRead, false);

		// NOTE(fusion): It doesn't make sense to continue if we didn't receive
		// the whole packet. We're already using TCP which doesn't drop data so
		// it would only compound into more errors.
		if(BytesRead != Size){
			return false;
		}

		if(Connection->State == CONNECTION_CONNECTED){
			Connection->StopLoginTimer();
			Connection->InDataSize = Size;
			if(!HandleLogin(Connection)){
				return false;
			}
		}else{
			// NOTE(fusion): It doesn't make sense to continue if the client didn't
			// correctly size its packet.
			if((Size % 8) != 0){
				print(3, "Ungültige Paketlänge %d für verschlüsseltes Paket von %s.\n",
						Size, Connection->GetName());
				return false;
			}

			for(int i = 0; i < Size; i += 8){
				Connection->SymmetricKey.decrypt(&Connection->InData[i]);
			}

			// NOTE(fusion): It doesn't make sense to continue if the client didn't
			// correctly size its payload.
			int PlainSize = ((uint16)Connection->InData[0])
					| ((uint16)Connection->InData[1] << 8);
			if(PlainSize == 0 || (PlainSize + 2) > Size){
				print(3, "Nutzdaten (%d Bytes) von Paket an Socket %d zu groß oder leer.\n",
						PlainSize, Connection->GetSocket());
				return false;
			}

			Connection->InDataSize = PlainSize;
			if(!CallGameThread(Connection)){
				return false;
			}
		}
	}

	// NOTE(fusion): We set `SigIOPending` here to signal that there could be more
	// packets already queued up, in which case `CommunicationThread` will attempt
	// to read without waiting for another `SIGIO`.
	//	The only way to know there is no more data on a socket's receive buffer is
	// when `read` returns `EAGAIN` which is handled when `ReadFromSocket` returns
	// a negative value.
	Connection->SigIOPending = true;
	return true;
}

// Communication Thread
// =============================================================================
void IncrementActiveConnections(void){
	CommunicationThreadMutex.down();
	ActiveConnections += 1;
	CommunicationThreadMutex.up();
}

void DecrementActiveConnections(void){
	CommunicationThreadMutex.down();
	ActiveConnections -= 1;
	CommunicationThreadMutex.up();
}

void CommunicationThread(int Socket){
	TConnection *Connection = AssignFreeConnection();
	if(Connection == NULL){
		print(2, "Keine Verbindung mehr frei.\n");
		if(close(Socket) == -1){
			error("CommunicationThread: Fehler %d beim Schließen der Socket (1).\n", errno);
		}
		return;
	}

	ASSERT(Connection->ThreadID == gettid());
	Connection->Connect(Socket);
	Connection->WaitingForACK = false;

	{
		struct f_owner_ex FOwnerEx = {};
		FOwnerEx.type = F_OWNER_TID;
		FOwnerEx.pid = Connection->ThreadID;
		if(fcntl(Socket, F_SETOWN_EX, &FOwnerEx) == -1){
			error("CommunicationThread: F_SETOWN_EX fehlgeschlagen für Socket %d.\n", Socket);
			if(close(Socket) == -1){
				error("CommunicationThread: Fehler %d beim Schließen der Socket (2).\n", errno);
			}
			Connection->Free();
			return;
		}
	}

	{
		int SocketFlags = fcntl(Socket, F_GETFL);
		if(SocketFlags == -1 || fcntl(Socket, F_SETFL, (SocketFlags | O_NONBLOCK | O_ASYNC)) == -1){
			error("ConnectionThread: F_SETFL fehlgeschlagen für Socket %d.\n", Socket);
			if(close(Socket) == -1){
				error("CommunicationThread: Fehler %d beim Schließen der Socket (3).\n", errno);
			}
			Connection->Free();
			return;
		}
	}

	// NOTE(fusion): In some systems, the accepted socket will inherit TCP_NODELAY
	// from the acceptor, making this next call redundant then. Nevertheless it is
	// probably better to set it anyways to be sure.
	{
		int NoDelay = 1;
		if(setsockopt(Socket, IPPROTO_TCP, TCP_NODELAY, &NoDelay, sizeof(NoDelay)) == -1){
			error("ConnectionThread: Failed to set TCP_NODELAY=1 on socket %d.\n", Socket);
			if(close(Socket) == -1){
				error("CommunicationThread: Fehler %d beim Schließen der Socket (3.5).\n", errno);
			}
			Connection->Free();
			return;
		}
	}

	sigset_t SignalSet;
	sigfillset(&SignalSet);
	sigprocmask(SIG_SETMASK, &SignalSet, NULL);
	if(!Connection->SetLoginTimer(5)){
		error("CommunicationThread: Failed to set login timer.\n");
		if(close(Socket) == -1){
			error("CommunicationThread: Fehler %d beim Schließen der Socket (4).\n", errno);
		}
		Connection->Free();
		return;
	}

	if(!ReceiveCommand(Connection)){
		Connection->Close(true);
	}

	Connection->SigIOPending = false;
	while(GameRunning() && Connection->ConnectionIsOk){
		int Signal;
		sigwait(&SignalSet, &Signal);
		switch(Signal){
			case SIGHUP:
			case SIGPIPE:{
				Connection->Close(false);
				break;
			}

			case SIGUSR1:
			case SIGIO:{
				if(Signal == SIGIO || Connection->SigIOPending){
					if(!Connection->WaitingForACK){
						Connection->SigIOPending = false;
						if(!ReceiveCommand(Connection)){
							Connection->Close(true);
						}
					}else{
						Connection->SigIOPending = true;
					}
				}
				break;
			}

			case SIGUSR2:{
				if(!SendData(Connection)){
					Connection->Close(false);
				}
				break;
			}

			case SIGALRM:{
				// NOTE(fusion): Login timeout.
				Connection->StopLoginTimer();
				if(Connection->State == CONNECTION_CONNECTED){
					print(2, "Login-TimeOut für Socket %d.\n", Socket);
					Connection->Close(false);
				}
				break;
			}

			default:{
				// no-op
				break;
			}
		}
	}

	while(Connection->Live()){
		DelayThread(1, 0);
	}

	// TODO(fusion): Is this done to allow for queued data to be sent, almost
	// like `SO_LINGER`?
	if(Connection->ClosingIsDelayed){
		DelayThread(2, 0);
	}

	if(close(Socket) == -1){
		error("CommunicationThread: Fehler %d beim Schließen der Socket (4).\n", errno);
	}

	Connection->Free();
}

int HandleConnection(void *Data){
	int Socket		= (uint16)((uintptr)Data);
	int StackNumber	= (uint16)((uintptr)Data >> 16);

	if(UseOwnStacks){
		AttachCommunicationThreadStack(StackNumber);
	}

	try{
		CommunicationThread(Socket);
	}catch(RESULT r){
		error("HandleConnection: Nicht abgefangene Exception %d.\n", r);
	}catch(const char *str){
		error("HandleConnection: Nicht abgefangene Exception \"%s\".\n", str);
	}catch(const std::exception &e){
		error("HandleConnection: Nicht abgefangene Exception %s.\n", e.what());
	}catch(...){
		error("HandleConnection: Nicht abgefangene Exception unbekannten Typs.\n");
	}

	DecrementActiveConnections();

	if(UseOwnStacks){
		ReleaseCommunicationThreadStack(StackNumber);
	}

	return 0;
}

// Acceptor Thread
// =============================================================================
bool OpenSocket(void){
	print(1, "Starte Game-Server...\n");
	print(1, "Pid=%d, Tid=%d - horche an Port %d\n", getpid(), gettid(), GamePort);
	TCPSocket = socket(AF_INET, SOCK_STREAM, 0);
	if(TCPSocket == -1){
		error("LaunchServer: Kann Socket nicht öffnen.\n");
		return false;
	}

	struct linger Linger = {};
	Linger.l_onoff = 0;
	Linger.l_linger = 0;
	if(setsockopt(TCPSocket, SOL_SOCKET, SO_LINGER, &Linger, sizeof(Linger)) == -1){
		error("LaunchServer: Failed to set SO_LINGER=(0, 0): (%d) %s.\n",
				errno, strerrordesc_np(errno));
		return false;
	}

	int ReuseAddr = 1;
	if(setsockopt(TCPSocket, SOL_SOCKET, SO_REUSEADDR, &ReuseAddr, sizeof(ReuseAddr)) == -1){
		error("LaunchServer: Failed to set SO_REUSEADDR=1: (%d) %s.\n",
				errno, strerrordesc_np(errno));
		return false;
	}

	int NoDelay = 1;
	if(setsockopt(TCPSocket, IPPROTO_TCP, TCP_NODELAY, &NoDelay, sizeof(NoDelay)) == -1){
		error("LaunchServer: Failed to set TCP_NODELAY=1: (%d) %s.\n",
				errno, strerrordesc_np(errno));
		return false;
	}

	struct sockaddr_in ServerAddress = {};
	ServerAddress.sin_family = AF_INET;
	ServerAddress.sin_port = htons(GamePort);
#if BIND_ACCEPTOR_TO_GAME_ADDRESS
	ServerAddress.sin_addr.s_addr = inet_addr(GameAddress);
#else
	ServerAddress.sin_addr.s_addr = htonl(INADDR_ANY);
#endif
	if(bind(TCPSocket, (struct sockaddr*)&ServerAddress, sizeof(ServerAddress)) == -1){
		error("LaunchServer: Failed to bind to acceptor to %s:%d: (%d) %s.\n",
				inet_ntoa(ServerAddress.sin_addr), GamePort, errno, strerrordesc_np(errno));
		return false;
	}

	if(listen(TCPSocket, 512) == -1){
		error("LaunchServer: Fehler %d bei listen.\n", errno);
		return false;
	}

	return true;
}

int AcceptorThreadLoop(void *Unused){
	AcceptorThreadID = gettid();
	print(1, "Warte auf Clients...\n");
	while(GameRunning()){
		int Socket = accept(TCPSocket, NULL, NULL);
		if(Socket == -1){
			error("AcceptorThreadLoop: Fehler %d beim Accept.\n", errno);
			continue;
		}

		// TODO(fusion): I don't think anything in here can throw any exception.
		try{
			if(UseOwnStacks){
				int StackNumber;
				void *Stack;
				GetCommunicationThreadStack(&StackNumber, &Stack);
				if(Stack == NULL){
					print(3,"Kein Stack-Bereich mehr frei.\n");
					if(close(Socket) == -1){
						error("AcceptorThreadLoop: Fehler %d beim Schließen der Socket (1).\n", errno);
					}
				}else{
					IncrementActiveConnections();
					void *Argument = (void*)(((uintptr)Socket & 0xFFFF)
									| (((uintptr)StackNumber & 0xFFFF) << 16));
					ThreadHandle ConnectionThread = StartThread(HandleConnection,
							Argument, Stack, COMMUNICATION_THREAD_STACK_SIZE, true);
					if(ConnectionThread == INVALID_THREAD_HANDLE){
						DecrementActiveConnections();
						ReleaseCommunicationThreadStack(StackNumber);
						if(close(Socket) == -1){
							error("AcceptorThreadLoop: Fehler %d beim Schließen der Socket (2).\n", errno);
						}
					}
				}
			}else{
				if(ActiveConnections >= MAX_COMMUNICATION_THREADS){
					print(3,"Keine Verbindung mehr frei.\n");
					if(close(Socket) == -1){
						error("AcceptorThreadLoop: Fehler %d beim Schließen der Socket (3).\n", errno);
					}
				}else{
					IncrementActiveConnections();
					void *Argument = (void*)((uintptr)Socket & 0xFFFF);
					ThreadHandle ConnectionThread = StartThread(HandleConnection,
							Argument, COMMUNICATION_THREAD_STACK_SIZE, true);
					if(ConnectionThread == INVALID_THREAD_HANDLE){
						print(3, "Kann neuen Thread nicht anlegen.\n");
						DecrementActiveConnections();
						if(close(Socket) == -1){
							error("AcceptorThreadLoop: Fehler %d beim Schließen der Socket (4).\n", errno);
						}
					}
				}
			}
		}catch(RESULT r){
			error("AcceptorThreadLoop: Nicht abgefangene Exception %d.\n", r);
		}catch(const char *str){
			error("AcceptorThreadLoop: Nicht abgefangene Exception \"%s\".\n", str);
		}catch(...){
			error("AcceptorThreadLoop: Nicht abgefangene Exception unbekannten Typs.\n");
		}
	}

	AcceptorThreadID = 0;
	if(ActiveConnections > 0){
		print(3, "Warte auf Beendigung von %d Communication-Threads...\n", ActiveConnections);
		while(ActiveConnections > 0){
			DelayThread(1, 0);
		}
	}

	return 0;
}

// Initialization
// =============================================================================
void CheckThreadlibVersion(void){
	// TODO(fusion): We'll probably remove `OwnStacks` support anyway but it
	// seems to be using this file `/etc/image-release` as an heuristic to
	// enable it. Not sure what this file is about.
	UseOwnStacks = FileExists("/etc/image-release");
	if(UseOwnStacks){
		print(2, "Verwende eigene Stacks.\n");
	}else{
		print(2, "Verwende verkleinerte Bibliotheks-Stacks.\n");
	}
}

void InitCommunication(void){
	CheckThreadlibVersion();
	InitCommunicationThreadStacks();
	InitLoadHistory();

	WaitinglistHead = NULL;
	TCPSocket = -1;
	AcceptorThread = INVALID_THREAD_HANDLE;
	AcceptorThreadID = 0;
	ActiveConnections = 0;
	QueryManagerConnectionPool.init();

	// TODO(fusion): This is arbitrary, should probably be set in the config.
	if(!PrivateKey.initFromFile("tibia.pem")){
		throw "cannot load RSA key";
	}

	if(!OpenSocket()){
		throw "cannot open socket";
	}

	AcceptorThread = StartThread(AcceptorThreadLoop, NULL, false);
	if(AcceptorThread == INVALID_THREAD_HANDLE){
		throw "cannot start acceptor thread";
	}
}

void ExitCommunication(void){
	// NOTE(fusion): `SIGHUP` is used to signal the connection thread to close
	// the connection and terminate.
	print(3, "Beende alle Verbindungen...\n");
	TConnection *Connection = GetFirstConnection();
	while(Connection != NULL){
		tgkill(GetGameProcessID(), Connection->GetThreadID(), SIGHUP);
		Connection = GetNextConnection();
	}

	ProcessConnections();
	print(3, "Alle Verbindungen beendet.\n");

	if(TCPSocket != -1){
		if(close(TCPSocket) == -1){
			error("ExitCommunication: Fehler %d beim Schließen der Socket.\n", errno);
		}
		TCPSocket = -1;
	}

	if(AcceptorThread != INVALID_THREAD_HANDLE){
		if(AcceptorThreadID != 0){
			tgkill(GetGameProcessID(), AcceptorThreadID, SIGHUP);
		}
		JoinThread(AcceptorThread);
		AcceptorThread = INVALID_THREAD_HANDLE;
	}

	QueryManagerConnectionPool.exit();
	ExitLoadHistory();
	ExitCommunicationThreadStacks();
}
