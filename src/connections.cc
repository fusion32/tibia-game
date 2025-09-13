#include "connections.hh"
#include "cr.hh"
#include "info.hh"
#include "threads.hh"
#include "writer.hh"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>

static Semaphore ConnectionMutex(1);
static int ConnectionIterator;
static TConnection Connections[MAX_CONNECTIONS];

// TConnection
// =============================================================================
TConnection::TConnection(void){
	this->State = CONNECTION_FREE;
}

void TConnection::Process(void){
	if(this->InGame()){
		uint32 LastCommand = (RoundNr - this->TimeStamp);
		if(LastCommand == 30 || LastCommand == 60){
			SendPing(this);
		}

		uint32 LastAction = (RoundNr - this->TimeStampAction);
		if(LastAction == 900 && !CheckRight(this->CharacterID, NO_LOGOUT_BLOCK)){
			SendMessage(this, TALK_ADMIN_MESSAGE,
					"You have been idle for %d minutes. You will be disconnected"
					" in one minute if you are still idle then.", 15);
		}

		if(LastAction >= 960 && !CheckRight(this->CharacterID, NO_LOGOUT_BLOCK)){
			this->Logout(0, true);
		}else if(!GameRunning() || !this->ConnectionIsOk || (LastCommand >= 90)){
			this->Logout(0, false);
		}
	}else if(this->State == CONNECTION_LOGIN){
		if(!GameRunning() || !this->ConnectionIsOk){
			this->Disconnect();
		}
	}else if(this->State == CONNECTION_LOGOUT){
		// NOTE(fusion): `TimeStamp` has the logout round which is set by
		// `TConnection::Logout`.
		if(!GameRunning() || !this->ConnectionIsOk || this->TimeStamp <= RoundNr){
			this->Disconnect();
		}
	}
}

void TConnection::ResetTimer(int Command){
	if(this->InGame()){
		this->TimeStamp = RoundNr;
		if(Command != CL_CMD_PING
				&& Command != CL_CMD_GO_STOP
				&& Command != CL_CMD_CANCEL
				&& Command != CL_CMD_REFRESH_FIELD
				&& Command != CL_CMD_REFRESH_CONTAINER){
			this->TimeStampAction = RoundNr;
		}
	}
}

void TConnection::EmergencyPing(void){
	if(this->InGame()){
		uint32 LastCommand = (RoundNr - this->TimeStamp);
		if(LastCommand < 80){
			// TODO(fusion): This is only called by `NetLoadCheck`, when it detects
			// lag, which can only happen after some set number of rounds, usually
			// higher than 100. This is all to say this subtraction below is very
			// unlikely to wrap. Nevertheless, we should also have some helper
			// functions to do saturating addition or subtraction for both signed
			// and unsigned integers.
			this->TimeStamp = RoundNr - 100;
		}
		SendPing(this);
	}
}

pid_t TConnection::GetThreadID(void){
	if(this->State == CONNECTION_FREE){
		error(Translate("TConnection::GetThreadID: Verbindung ist nicht zugewiesen.\n",
						"TConnection::GetThreadID: Connection is not assigned.\n"));
		return 0;
	}

	return this->ThreadID;
}

bool TConnection::SetLoginTimer(int Timeout){
	if(this->State == CONNECTION_FREE){
		error(Translate("TConnection::SetLoginTimer: Verbindung ist nicht zugewiesen.\n",
						"TConnection::SetLoginTimer: Connection is not assigned.\n"));
		return false;
	}

	if(this->LoginTimer != 0){
		error("TConnection::SetLoginTimer: Timer already set.\n");
		return false;
	}

	struct sigevent SigEvent = {};
	SigEvent.sigev_notify = SIGEV_THREAD_ID;
	SigEvent.sigev_signo = SIGALRM;
	SigEvent.sigev_notify_thread_id = this->ThreadID;
	if(timer_create(CLOCK_MONOTONIC, &SigEvent, &this->LoginTimer) == -1){
		error("TConnection::SetLoginTimer: Failed to create timer: (%d) %s\n",
				errno, strerrordesc_np(errno));
		return false;
	}

	struct itimerspec TimerSpec = {};
	TimerSpec.it_value.tv_sec = Timeout;
	if(timer_settime(this->LoginTimer, 0, &TimerSpec, NULL) == -1){
		error("TConnection::SetLoginTimer: Failed to start timer: (%d) %s\n",
				errno, strerrordesc_np(errno));
		return false;
	}

	return true;
}

void TConnection::StopLoginTimer(void){
	if(this->State == CONNECTION_FREE){
		error(Translate("TConnection::SetLoginTimer: Verbindung ist nicht zugewiesen.\n",
						"TConnection::SetLoginTimer: Connection is not assigned.\n"));
		return;
	}

	if(this->LoginTimer == 0){
		error("TConnection::StopLoginTimer: Timer not set.\n");
		return;
	}

	if(timer_delete(this->LoginTimer) == -1){
		error("TConnection::StopLoginTimer: Failed to delete timer: (%d) %s\n",
				errno, strerrordesc_np(errno));
	}

	this->LoginTimer = 0;
}

int TConnection::GetSocket(void){
	if(this->State == CONNECTION_FREE || this->State == CONNECTION_ASSIGNED){
		error(Translate("TConnection::GetSocket: Verbindung ist nicht angeschlossen.\n",
						"TConnection::GetSocket: Connection is not connected.\n"));
		return -1;
	}

	return this->Socket;
}

const char *TConnection::GetIPAddress(void){
	if(this->State == CONNECTION_FREE || this->State == CONNECTION_ASSIGNED){
		error(Translate("TConnection::GetIPAddress: Verbindung ist nicht angeschlossen.\n",
						"TConnection::GetIPAddress: Connection is not connected.\n"));
		return "Unknown";
	}

	return this->IPAddress;
}

void TConnection::Free(void){
	this->State = CONNECTION_FREE;
}

void TConnection::Assign(void){
	if(this->State != CONNECTION_FREE){
		error(Translate("TConnection::Assign: Verbindung ist nicht frei.\n",
						"TConnection::Assign: Connection is not free.\n"));
	}

	this->State = CONNECTION_ASSIGNED;
	this->ThreadID = gettid();
	this->LoginTimer = 0;
}

void TConnection::Connect(int Socket){
	if(this->State != CONNECTION_ASSIGNED){
		error(Translate("TConnection::Connect: Verbindung ist keinem Thread zugewiesen.\n",
						"TConnection::Connect: Connection is not assigned to a thread.\n"));
	}

	this->State = CONNECTION_CONNECTED;
	this->Socket = Socket;
	this->ConnectionIsOk = true;
	this->ClosingIsDelayed = true;
	this->RandomSeed = rand();

	struct sockaddr_in RemoteAddr;
	socklen_t RemoteAddrLen = sizeof(RemoteAddr);
	getpeername(Socket, (struct sockaddr*)&RemoteAddr, &RemoteAddrLen);
	strcpy(this->IPAddress, inet_ntoa(RemoteAddr.sin_addr));
}

void TConnection::Login(void){
	if(this->State != CONNECTION_CONNECTED){
		error(Translate("TConnection::Connect: Ungültiger Verbindungs-Zustand %d.\n",
						"TConnection::Connect: Invalid connection state %d.\n"), this->State);
	}

	this->State = CONNECTION_LOGIN;
}

bool TConnection::JoinGame(TReadBuffer *Buffer){
	if(this->State != CONNECTION_LOGIN){
		error(Translate("TConnection::JoinGame: Ungültiger Verbindungszustand %d.\n",
						"TConnection::JoinGame: Invalid connection state %d.\n"), this->State);
	}

	this->ClearKnownCreatureTable(false);

	try{
		this->TerminalType = (int)Buffer->readWord();
		this->TerminalVersion = (int)Buffer->readWord();
		this->CharacterID = Buffer->readQuad();
	}catch(const char *str){
		error(Translate("TConnection::JoinGame: Fehler beim Auslesen des Puffers (%s).\n",
						"TConnection::JoinGame: Error reading buffer (%s).\n"), str);
	}

	if(this->TerminalType != 1 && this->TerminalType != 2){
		error(Translate("TConnection::JoinGame: Unbekannter Terminal-Typ %d.\n",
						"TConnection::JoinGame: Unknown terminal type %d.\n"), this->TerminalType);
		return false;
	}

	this->TerminalOffsetX = 8;
	this->TerminalOffsetY = 6;
	this->TerminalWidth = 18;
	this->TerminalHeight = 14;

	TPlayer *Player = ::GetPlayer(this->CharacterID);
	if(Player == NULL){
		Player = new TPlayer(this, this->CharacterID);
		if(Player->ConstructError != NOERROR){
			delete Player;
			return false;
		}
	}else{
		if(Player->IsDead){
			Log("game", Translate("Spieler %s ist gerade am Sterben - Einloggen gescheitert.\n",
								  "Player %s is currently dying - login failed.\n"), Player->Name);
			DecreasePlayerPoolSlotSticky(this->CharacterID);
			return false;
		}

		if(Player->LoggingOut && Player->LogoutPossible() == 0){
			Log("game", Translate("Spieler %s loggt gerade aus - Einloggen gescheitert.\n",
								  "Player %s is currently logging out - Login failed.\n"), Player->Name);
			DecreasePlayerPoolSlotSticky(this->CharacterID);
			return false;
		}

		TConnection *OldConnection = Player->Connection;
		Player->ClearConnection();
		if(OldConnection != NULL){
			OldConnection->CharacterID = 0;
			OldConnection->Logout(0, true);
		}

		DecrementIsOnlineOrder(this->CharacterID);
		Player->TakeOver(this);
	}

	strcpy(this->Name, Player->Name);
	this->TimeStamp = RoundNr;
	this->TimeStampAction = RoundNr;
	return true;
}

void TConnection::EnterGame(void){
	if(this->State != CONNECTION_LOGIN){
		error(Translate("TConnection::EnterGame: Ungültiger Verbindungszustand %d.\n",
						"TConnection::EnterGame: Invalid connection state %d.\n"), this->State);
	}

	this->State = CONNECTION_GAME;
}

void TConnection::Die(void){
	if(this->State == CONNECTION_GAME){
		this->State = CONNECTION_DEAD;
	}
}

void TConnection::Logout(int Delay, bool StopFight){
	if(!this->InGame() && this->State != CONNECTION_LOGOUT){
		error(Translate("TConnection::Logout: Ungültiger Verbindungszustand %d.\n",
						"TConnection::Logout: Invalid connection state %d.\n"), this->State);
	}

	this->State = CONNECTION_LOGOUT;
	if(this->CharacterID != 0){
		TPlayer *Player = ::GetPlayer(this->CharacterID);
		if(Player != NULL){
			Player->ClearConnection();
			Player->StartLogout(false, StopFight);
		}
	}

	if(Delay < 0){
		Delay = 0;
	}

	this->CharacterID = 0;
	this->TimeStamp = RoundNr + (uint32)Delay;
	this->ClosingIsDelayed = false;
}

void TConnection::Close(bool Delay){
	if(this->State == CONNECTION_FREE || this->State == CONNECTION_ASSIGNED){
		error(Translate("TConnection::Close: Ungültiger Verbindungszustand %d.\n",
						"TConnection::Close: Invalid connection state %d.\n"), this->State);
	}

	if(this->State == CONNECTION_CONNECTED){
		this->State = CONNECTION_DISCONNECTED;
	}

	this->ConnectionIsOk = false;
	this->ClosingIsDelayed = Delay;
}

void TConnection::Disconnect(void){
	if(this->State == CONNECTION_FREE || this->State == CONNECTION_ASSIGNED){
		error(Translate("TConnection::Close: Ungültiger Verbindungszustand %d.\n",
						"TConnection::Close: Invalid connection state %d.\n"), this->State);
	}

	this->ClearKnownCreatureTable(true);
	this->ConnectionIsOk = false;
	this->State = CONNECTION_DISCONNECTED;
	tgkill(GetGameProcessID(), this->ThreadID, SIGHUP);
}

TPlayer *TConnection::GetPlayer(void){
	if(!this->Live()){
		error(Translate("TConnection::GetPlayer: Ungültiger Verbindungszustand %d.\n",
						"TConnection::GetPlayer: Invalid connection state %d.\n"), this->State);
		return NULL;
	}

	TPlayer *Player = NULL;
	if(this->CharacterID != 0){
		Player = ::GetPlayer(this->CharacterID);
	}
	return Player;
}

const char *TConnection::GetName(void){
	if(!this->Live()){
		error(Translate("TConnection::GetName: Ungültiger Verbindungszustand %d.\n",
						"TConnection::GetName: Invalid connection state %d.\n"), this->State);
		return "";
	}

	return this->Name;
}

void TConnection::GetPosition(int *x, int *y, int *z){
	TPlayer *Player = this->GetPlayer();
	if(Player != NULL){
		*x = Player->posx;
		*y = Player->posy;
		*z = Player->posz;
	}else{
		*x = 0;
		*y = 0;
		*z = 0;
	}
}

bool TConnection::IsVisible(int x, int y, int z){
	int PlayerX, PlayerY, PlayerZ;
	this->GetPosition(&PlayerX, &PlayerY, &PlayerZ);

	// TODO(fusion): Have a standalone version of `TCreature::CanSeeFloor`?
	if(PlayerZ <= 7){
		if(z > 7){
			return false;
		}
	}else{
		if(std::abs(PlayerZ - z) > 2){
			return false;
		}
	}

	int MinX = (PlayerX - this->TerminalOffsetX) + (PlayerZ - z);
	int MinY = (PlayerY - this->TerminalOffsetY) + (PlayerZ - z);
	int MaxX = MinX + this->TerminalWidth - 1;
	int MaxY = MinY + this->TerminalHeight - 1;
	return x >= MinX && x <= MaxX
		&& y >= MinY && y <= MaxY;
}

KNOWNCREATURESTATE TConnection::KnownCreature(uint32 ID, bool UpdateFollows){
	int EntryIndex = -1;
	for(int i = 0; i < NARRAY(this->KnownCreatureTable); i += 1){
		if(this->KnownCreatureTable[i].CreatureID == ID){
			EntryIndex = i;
			break;
		}
	}

	if(EntryIndex == -1){
		return KNOWNCREATURE_FREE;
	}

	KNOWNCREATURESTATE Result = this->KnownCreatureTable[EntryIndex].State;
	if(Result == KNOWNCREATURE_OUTDATED && UpdateFollows){
		this->KnownCreatureTable[EntryIndex].State = KNOWNCREATURE_UPTODATE;
	}
	return Result;
}

uint32 TConnection::NewKnownCreature(uint32 NewID){
	uint32 OldID = 0;
	int EntryIndex = -1;
	for(int i = 0; i < NARRAY(this->KnownCreatureTable); i += 1){
		if(this->KnownCreatureTable[i].CreatureID == NewID){
			OldID = NewID;
			EntryIndex = i;
			break;
		}
	}

	if(EntryIndex == -1){
		for(int i = 0; i < NARRAY(this->KnownCreatureTable); i += 1){
			if(this->KnownCreatureTable[i].State == KNOWNCREATURE_FREE){
				OldID = this->KnownCreatureTable[i].CreatureID;
				EntryIndex = i;
				break;
			}
		}
	}

	if(EntryIndex == -1){
		for(int i = 0; i < NARRAY(this->KnownCreatureTable); i += 1){
			TCreature *Creature = GetCreature(this->KnownCreatureTable[i].CreatureID);
			if(Creature == NULL || !this->IsVisible(Creature->posx, Creature->posy, Creature->posz)){
				OldID = this->KnownCreatureTable[i].CreatureID;
				EntryIndex = i;
				this->UnchainKnownCreature(OldID);
				break;
			}
		}
	}

	if(EntryIndex == -1){
		print(3, Translate("KnownCreatureTable ausgelastet.\n",
						   "KnownCreatureTable full.\n"));
		return 0;
	}

	if(this->KnownCreatureTable[EntryIndex].State != KNOWNCREATURE_FREE){
		error(Translate("TUserCom::NewKnownCreature: Slot ist nicht gelöscht.\n",
						"TUserCom::NewKnownCreature: Slot is not free.\n"));
	}

	this->KnownCreatureTable[EntryIndex].State = KNOWNCREATURE_UPTODATE;
	this->KnownCreatureTable[EntryIndex].CreatureID = NewID;

	TCreature *Creature = GetCreature(NewID);
	if(Creature != NULL){
		this->KnownCreatureTable[EntryIndex].Next = Creature->FirstKnowingConnection;
		Creature->FirstKnowingConnection = &this->KnownCreatureTable[EntryIndex];
	}else{
		error(Translate("TUserCom::NewKnownCreature: Kreatur %u existiert nicht.\n",
						"TUserCom::NewKnownCreature: Creature %u does not exist.\n"), NewID);
	}

	return OldID;
}

void TConnection::ClearKnownCreatureTable(bool Unchain){
	for(int i = 0; i < NARRAY(this->KnownCreatureTable); i += 1){
		if(Unchain && this->KnownCreatureTable[i].State != KNOWNCREATURE_FREE){
			this->UnchainKnownCreature(this->KnownCreatureTable[i].CreatureID);
		}
		this->KnownCreatureTable[i].State = KNOWNCREATURE_FREE;
		this->KnownCreatureTable[i].CreatureID = 0;
		this->KnownCreatureTable[i].Connection = this;
	}
}

void TConnection::UnchainKnownCreature(uint32 ID){
	TCreature *Creature = GetCreature(ID);
	if(Creature == NULL){
		error(Translate("TUserCom::UnchainKnownCreature: Kreatur %u existiert nicht.\n",
						"TUserCom::UnchainKnownCreature: Creature %u does not exist.\n"), ID);
		return;
	}

	if(Creature->FirstKnowingConnection == NULL){
		error(Translate("TUserCom::UnchainKnownCreature: Kreatur %u kennt niemand.\n",
						"TUserCom::UnchainKnownCreature: Creature %u is not known by anyone.\n"), ID);
		return;
	}

	TKnownCreature *Prev = NULL;
	TKnownCreature *Cur = Creature->FirstKnowingConnection;
	while(Cur != NULL){
		if(Cur->Connection == this){
			break;
		}
		Prev = Cur;
		Cur = Cur->Next;
	}

	if(Cur == NULL){
		error(Translate("TUserCom::UnchainKnownCreature: Kreatur %u ist nicht bekannt.\n",
						"TUserCom::UnchainKnownCreature: Creature %u is not known.\n"), ID);
		return;
	}

	Cur->State = KNOWNCREATURE_FREE;
	if(Prev == NULL){
		Creature->FirstKnowingConnection = Cur->Next;
	}else{
		Prev->Next = Cur->Next;
	}
}

// Connection Utility
// =============================================================================
TConnection *AssignFreeConnection(void){
	TConnection *Connection = NULL;
	ConnectionMutex.down();
	for(int i = 0; i < MAX_CONNECTIONS; i += 1){
		if(Connections[i].State == CONNECTION_FREE){
			Connection = &Connections[i];
			Connection->Assign();
			break;
		}
	}
	ConnectionMutex.up();
	return Connection;
}

TConnection *GetFirstConnection(void){
	ConnectionIterator = 0;
	return GetNextConnection();
}

TConnection *GetNextConnection(void){
	TConnection *NextConnection = NULL;
	while(ConnectionIterator < MAX_CONNECTIONS){
		TConnection *Connection = &Connections[ConnectionIterator];
		ConnectionIterator += 1;
		if(Connection->State != CONNECTION_FREE){
			NextConnection = Connection;
			break;
		}
	}
	return NextConnection;
}

void ProcessConnections(void){
	TConnection *Connection = GetFirstConnection();
	while(Connection != NULL){
		Connection->Process();
		Connection = GetNextConnection();
	}
}

void InitConnections(void){
	InitSending();
	InitReceiving();

	ConnectionIterator = 0;
	for(int i = 0; i < MAX_CONNECTIONS; i += 1){
		Connections[i].State = CONNECTION_FREE;
	}
}

void ExitConnections(void){
	ExitSending();
	ExitReceiving();
}
