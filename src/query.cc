#include "query.hh"
#include "config.hh"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>

static int ApplicationType;
static char LoginData[30];

void SetQueryManagerLoginData(int Type, const char *Data){
	ApplicationType = Type;
	if(Data != NULL){
		strncpy(LoginData, Data, sizeof(LoginData));
		LoginData[sizeof(LoginData) - 1] = 0;
	}else{
		LoginData[0] = 0;
	}
}

// TQueryManagerConnection
// =============================================================================
// TODO(fusion): An initializer list executes in the same order fields are
// declared. This initialization should work as long as `BufferSize` is the
// first field declared in `TQueryManagerConnection`, which it is.
TQueryManagerConnection::TQueryManagerConnection(int QueryBufferSize) :
		BufferSize(std::max<int>(QueryBufferSize, KB(16))),
		Buffer(new uint8[this->BufferSize]),
		ReadBuffer(this->Buffer, this->BufferSize),
		WriteBuffer(this->Buffer, this->BufferSize),
		Socket(-1),
		QueryOk(false)
{
	this->connect();
}

TQueryManagerConnection::~TQueryManagerConnection(void){
	this->disconnect();
	delete[] this->Buffer;
}

bool ResolveHostNameAddress(const char *HostName, in_addr_t *OutAddr){
	// TODO(fusion): Just use `getaddrinfo`?
	int ErrorCode;
	hostent HostEnt;
	hostent *HostEntResult;
	char Buffer[2048];
	int Ret = gethostbyname_r(HostName, &HostEnt,
			Buffer, sizeof(Buffer), &HostEntResult, &ErrorCode);
	if(Ret != 0 || HostEntResult == NULL){
		return false;
	}

	if(OutAddr){
		*OutAddr = ((in_addr_t)HostEntResult->h_addr_list[0][0] << 24)
				| ((in_addr_t)HostEntResult->h_addr_list[0][1] << 16)
				| ((in_addr_t)HostEntResult->h_addr_list[0][2] <<  8)
				| ((in_addr_t)HostEntResult->h_addr_list[0][3] <<  0);
	}

	return true;
}

void TQueryManagerConnection::connect(void){
	for(int i = 0; i < NumberOfQueryManagers; i += 1){
		in_addr_t Addr = inet_addr(QUERY_MANAGER[i].Host);
		if(Addr == INADDR_NONE && !ResolveHostNameAddress(QUERY_MANAGER[i].Host, &Addr)){
			print(2, "TQueryManagerConnection::connect: Kann Rechnernamen nicht auflösen.\n");
			continue;
		}

		this->Socket = socket(AF_INET, SOCK_STREAM, 0);
		if(this->Socket == -1){
			print(2, "TQueryManagerConnection::connect: Kann Socket nicht öffnen.\n");
			continue;
		}

		struct sockaddr_in QueryManagerAddress = {};
		QueryManagerAddress.sin_family = AF_INET;
		QueryManagerAddress.sin_port = htons(QUERY_MANAGER[i].Port);
		QueryManagerAddress.sin_addr.s_addr = Addr;
		if(::connect(this->Socket, (struct sockaddr*)&QueryManagerAddress, sizeof(QueryManagerAddress)) == -1){
			print(2, "TQueryManagerConnection::connect: Kann Verbindung nicht herstellen.\n");
			this->disconnect();
			continue;
		}

		// NOTE(fusion): Make socket non-blocking AFTER connecting. This is to
		// better align the behaviour of both TQueryManagerConnection::read and
		// TQueryManagerConnection::write.
		{
			int SocketFlags = fcntl(this->Socket, F_GETFL);
			if(SocketFlags == -1 || fcntl(this->Socket, F_SETFL, (SocketFlags | O_NONBLOCK)) == -1){
				error("TQueryManagerConnection::connect: Failed to set socket as non-blocking: (%d) %s\n",
						errno, strerrordesc_np(errno));
				this->disconnect();
				continue;
			}
		}

		this->prepareQuery(0);
		this->sendByte((uint8)ApplicationType);
		this->sendString(QUERY_MANAGER[i].Password);
		if(ApplicationType == 1){
			this->sendString(LoginData);
		}

		int Status = this->executeQuery(30, false);
		if(Status != QUERY_STATUS_OK){
			print(2, "TQueryManagerConnection::connect: Anmeldung fehlgeschlagen (%d).\n", Status);
			this->disconnect();
			continue;
		}

		break;
	}

	if(!this->isConnected()){
		print(2, "TQueryManagerConnection::connect: Kein Query-Manager verfügbar.\n");
	}
}

void TQueryManagerConnection::disconnect(void){
	if(this->Socket != -1){
		if(close(this->Socket) == -1){
			error("TQueryManagerConnection::disconnect: Fehler %d beim Schließen der Socket.\n", errno);
		}
		this->Socket = -1;
	}
}

int TQueryManagerConnection::write(const uint8 *Buffer, int Size){
	int Attempts = 50;
	int BytesToWrite = Size;
	const uint8 *WritePtr = Buffer;
	while(BytesToWrite > 0){
		int BytesWritten = (int)::write(this->Socket, WritePtr, BytesToWrite);
		if(BytesWritten > 0){
			BytesToWrite -= BytesWritten;
			WritePtr += BytesWritten;
		}else if(BytesWritten == 0){
			break;
		}else if(errno != EINTR){
			if(errno != EAGAIN){
				return -errno;
			}else if(Attempts <= 0){
				return -ETIMEDOUT;
			}

			DelayThread(0, 100000);
			Attempts -= 1;
		}
	}
	return Size - BytesToWrite;
}

int TQueryManagerConnection::read(uint8 *Buffer, int Size, int Timeout){
	// NOTE(fusion): Wait until there is any inbound data then read everything
	// in one go. Data from the query manager is sent in a burst so we don't need
	// to be polling every step along the way.
	int TimeoutMS = Timeout * 1000;
	int64 Deadline = GetClockMonotonicMS() + TimeoutMS;
	while(true){
		struct pollfd pollfd = {};
		pollfd.fd = this->Socket;
		pollfd.events = POLLIN;
		pollfd.revents = 0;
		int ret = poll(&pollfd, 1, TimeoutMS);
		if(ret == 1) break;
		if(ret == 0) return -ETIMEDOUT;
		if(ret == -1 && errno != EINTR) return -errno;

		// NOTE(fusion): We should get a timeout before this turns negative, but
		// just in case... We don't want poll to block indefinitely...
		TimeoutMS = (int)(Deadline - GetClockMonotonicMS());
		if(TimeoutMS < 0){
			return -ETIMEDOUT;
		}
	}

	int Attempts = 50;
	int BytesToRead = Size;
	uint8 *ReadPtr = Buffer;
	while(BytesToRead > 0){
		int BytesRead = ::read(this->Socket, ReadPtr, BytesToRead);
		if(BytesRead > 0){
			BytesToRead -= BytesRead;
			ReadPtr += BytesRead;
		}else if(BytesRead == 0){
			break;
		}else if(errno != EINTR){
			if(errno != EAGAIN){
				return -errno;
			}else if(Attempts <= 0){
				return -ETIMEDOUT;
			}

			DelayThread(0, 100000);
			Attempts -= 1;
		}
	}
	return Size - BytesToRead;
}

void TQueryManagerConnection::prepareQuery(int QueryType){
	this->QueryOk = true;
	this->WriteBuffer.Position = 0;

	try{
		this->WriteBuffer.writeWord(0);
		this->WriteBuffer.writeByte((uint8)QueryType);
	}catch(const char *str){
		error("TQueryManagerConnection::prepareQuery: %s.\n", str);
		this->QueryOk = false;
	}
}

void TQueryManagerConnection::sendFlag(bool Flag){
	this->sendByte(Flag ? 0x01 : 0x00);
}

void TQueryManagerConnection::sendByte(uint8 Byte){
	try{
		this->WriteBuffer.writeByte(Byte);
	}catch(const char *str){
		error("TQueryManagerConnection::sendByte: %s.\n", str);
		this->QueryOk = false;
	}
}

void TQueryManagerConnection::sendWord(uint16 Word){
	try{
		this->WriteBuffer.writeWord(Word);
	}catch(const char *str){
		error("TQueryManagerConnection::sendWord: %s.\n", str);
		this->QueryOk = false;
	}
}

void TQueryManagerConnection::sendQuad(uint32 Quad){
	try{
		this->WriteBuffer.writeQuad(Quad);
	}catch(const char *str){
		error("TQueryManagerConnection::sendQuad: %s.\n", str);
		this->QueryOk = false;
	}
}

void TQueryManagerConnection::sendString(const char *String){
	try{
		this->WriteBuffer.writeString(String);
	}catch(const char *str){
		error("TQueryManagerConnection::sendString: %s.\n", str);
		this->QueryOk = false;
	}
}

void TQueryManagerConnection::sendBytes(const uint8 *Buffer, int Count){
	try{
		this->WriteBuffer.writeBytes(Buffer, Count);
	}catch(const char *str){
		error("TQueryManagerConnection::sendBytes: %s.\n", str);
		this->QueryOk = false;
	}
}

bool TQueryManagerConnection::getFlag(void){
	return this->getByte() != 0;
}

uint8 TQueryManagerConnection::getByte(void){
	uint8 Result = 0;
	try{
		Result = this->ReadBuffer.readByte();
	}catch(const char *str){
		error("TQueryManagerConnection::getByte: %s.\n", str);
	}
	return Result;
}

uint16 TQueryManagerConnection::getWord(void){
	uint16 Result = 0;
	try{
		Result = this->ReadBuffer.readWord();
	}catch(const char *str){
		error("TQueryManagerConnection::getWord: %s.\n", str);
	}
	return Result;
}

uint32 TQueryManagerConnection::getQuad(void){
	uint32 Result = 0;
	try{
		Result = this->ReadBuffer.readQuad();
	}catch(const char *str){
		error("TQueryManagerConnection::getQuad: %s.\n", str);
	}
	return Result;
}

void TQueryManagerConnection::getString(char *Buffer, int MaxLength){
	try{
		this->ReadBuffer.readString(Buffer, MaxLength);
	}catch(const char *str){
		error("TQueryManagerConnection::getString: %s.\n", str);
		if(MaxLength > 0){
			Buffer[0] = 0;
		}
	}
}

void TQueryManagerConnection::getBytes(uint8 *Buffer, int Count){
	try{
		this->ReadBuffer.readBytes(Buffer, Count);
	}catch(const char *str){
		error("TQueryManagerConnection::getBytes: %s.\n", str);
	}
}

int TQueryManagerConnection::executeQuery(int Timeout, bool AutoReconnect){
	// NOTE(fusion): `prepareQuery` will already leave two extra bytes at the
	// beginning of the buffer for writing the request's length.
	int PacketSize = this->WriteBuffer.Position;
	int PayloadSize = PacketSize - 2;
	this->WriteBuffer.Position = 0;
	if(PayloadSize < 0xFFFF){
		this->sendWord((uint16)PayloadSize);
	}else{
		PacketSize += 4;
		if(PacketSize > this->BufferSize){
			error("TQueryManagerConnection::executeQuery: Puffer zu klein.\n");
			return QUERY_STATUS_FAILED;
		}

		memmove(&this->Buffer[6], &this->Buffer[2], PayloadSize);
		this->sendWord(0xFFFF);
		this->sendQuad((uint32)PayloadSize);
	}

	if(!this->QueryOk){
		error("TQueryManagerConnection::executeQuery: Fehler beim Zusammenbauen der Anfrage.\n");
		return QUERY_STATUS_FAILED;
	}

	const int MaxAttempts = 2;
	for(int Attempt = 1; true; Attempt += 1){
		if(!this->isConnected()){
			if(!AutoReconnect){
				return QUERY_STATUS_FAILED;
			}

			// TODO(fusion): There was also a "fast" path that allocated on the
			// stack with variable length arrays. It would be a problem to mimic
			// that with `alloca` since its memory is only released at the end
			// of the function and we're in a loop.
			uint8 *TempBuffer = new uint8[PacketSize];
			memcpy(TempBuffer, this->Buffer, PacketSize);
			this->connect();
			memcpy(this->Buffer, TempBuffer, PacketSize);
			delete[] TempBuffer;

			if(!this->isConnected()){
				return QUERY_STATUS_FAILED;
			}
		}

		if(this->write(this->Buffer, PacketSize) != PacketSize){
			this->disconnect();
			if(Attempt >= MaxAttempts){
				error("TQueryManagerConnection::executeQuery: Fehler beim Abschicken der Anfrage.\n");
				return QUERY_STATUS_FAILED;
			}
			continue;
		}

		uint8 Help[4];
		int BytesRead = this->read(Help, 2, Timeout);
		if(BytesRead != 2){
			this->disconnect();
			if(BytesRead == -ETIMEDOUT || Attempt >= MaxAttempts){
				return QUERY_STATUS_FAILED;
			}
			continue;
		}

		int ResponseSize = ((int)Help[0]) | ((int)Help[1] << 8);
		if(ResponseSize == 0xFFFF){
			if(this->read(Help, 4, Timeout) != 4){
				this->disconnect();
				return QUERY_STATUS_FAILED;
			}

			ResponseSize = ((int)Help[0]) | ((int)Help[1] << 8)
					| ((int)Help[2] << 16) | ((int)Help[3] << 24);
		}

		if(ResponseSize <= 0 || ResponseSize > this->BufferSize){
			this->disconnect();
			error("TQueryManagerConnection::executeQuery: Ungültige Datengröße %d.\n", ResponseSize);
			return QUERY_STATUS_FAILED;
		}

		if(this->read(this->Buffer, ResponseSize, Timeout) != ResponseSize){
			this->disconnect();
			error("TQueryManagerConnection::executeQuery: Fehler beim Auslesen der Daten.\n");
			return QUERY_STATUS_FAILED;
		}

		this->ReadBuffer.Size = ResponseSize;
		this->ReadBuffer.Position = 0;
		int Status = this->getByte();
		if(Status == QUERY_STATUS_FAILED){
			error("TQueryManagerConnection::executeQuery: Anfrage fehlgeschlagen.\n");
		}

		return Status;
	}
}

int TQueryManagerConnection::checkAccountPassword(uint32 AccountID,
		const char *Password, const char *IPAddress){
	this->prepareQuery(10);
	this->sendQuad(AccountID);
	this->sendString(Password);
	this->sendString(IPAddress);
	int Status = this->executeQuery(30, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_ERROR){
		int ErrorCode = this->getByte();
		if(ErrorCode >= 1 && ErrorCode <= 4){
			Result = ErrorCode;
		}else{
			error("TQueryManagerConnection::checkAccountPassword: Ungültiger Fehlercode %d.\n", ErrorCode);
		}
	}
	return Result;
}

int TQueryManagerConnection::loginAdmin(uint32 AccountID, bool PrivateWorld,
		int *NumberOfCharacters, char (*Characters)[30], char (*Worlds)[30],
		uint8 (*IPAddresses)[4], uint16 *Ports, uint16 *PremiumDaysLeft){
	this->prepareQuery(12);
	this->sendQuad(AccountID);
	this->sendFlag(PrivateWorld);
	int Status = this->executeQuery(30, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_OK){
		*NumberOfCharacters = this->getByte();
		for(int i = 0; i < *NumberOfCharacters; i += 1){
			this->getString(Characters[i], 30);
			this->getString(Worlds[i], 30);
			this->getBytes(IPAddresses[i], 4);
			Ports[i] = this->getWord();
		}
		*PremiumDaysLeft = this->getWord();
	}else if(Status == QUERY_STATUS_ERROR){
		int ErrorCode = this->getByte();
		if(ErrorCode == 1){
			Result = ErrorCode;
		}else{
			error("TQueryManagerConnection::loginAdmin: Ungültiger Fehlercode %d.\n", ErrorCode);
		}
	}
	return Result;
}

int TQueryManagerConnection::loadWorldConfig(int *WorldType, int *RebootTime,
		int *IPAddress, int *Port, int *MaxPlayers, int *PremiumPlayerBuffer,
		int *MaxNewbies, int *PremiumNewbieBuffer){
	this->prepareQuery(53);
	int Status = this->executeQuery(30, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_OK){
		*WorldType = this->getByte();
		*RebootTime = (int)this->getByte() * 60;
		IPAddress[0] = this->getByte();
		IPAddress[1] = this->getByte();
		IPAddress[2] = this->getByte();
		IPAddress[3] = this->getByte();
		*Port = this->getWord();
		*MaxPlayers = this->getWord();
		*PremiumPlayerBuffer = this->getWord();
		*MaxNewbies = this->getWord();
		*PremiumNewbieBuffer = this->getWord();
	}
	return Result;
}

static int GetRightByName(const char *RightName){
	int Result = -1;
	if(strcmp(RightName, "PREMIUM_ACCOUNT") == 0){
		Result = PREMIUM_ACCOUNT;
	}else if(strcmp(RightName, "NOTATION") == 0){
		Result = NOTATION;
	}else if(strcmp(RightName, "NAMELOCK") == 0){
		Result = NAMELOCK;
	}else if(strcmp(RightName, "STATEMENT_REPORT") == 0){
		Result = STATEMENT_REPORT;
	}else if(strcmp(RightName, "BANISHMENT") == 0){
		Result = BANISHMENT;
	}else if(strcmp(RightName, "FINAL_WARNING") == 0){
		Result = FINAL_WARNING;
	}else if(strcmp(RightName, "IP_BANISHMENT") == 0){
		Result = IP_BANISHMENT;
	}else if(strcmp(RightName, "KICK") == 0){
		Result = KICK;
	}else if(strcmp(RightName, "HOME_TELEPORT") == 0){
		Result = HOME_TELEPORT;
	}else if(strcmp(RightName, "GAMEMASTER_BROADCAST") == 0){
		Result = GAMEMASTER_BROADCAST;
	}else if(strcmp(RightName, "ANONYMOUS_BROADCAST") == 0){
		Result = ANONYMOUS_BROADCAST;
	}else if(strcmp(RightName, "NO_BANISHMENT") == 0){
		Result = NO_BANISHMENT;
	}else if(strcmp(RightName, "ALLOW_MULTICLIENT") == 0){
		Result = ALLOW_MULTICLIENT;
	}else if(strcmp(RightName, "LOG_COMMUNICATION") == 0){
		Result = LOG_COMMUNICATION;
	}else if(strcmp(RightName, "READ_GAMEMASTER_CHANNEL") == 0){
		Result = READ_GAMEMASTER_CHANNEL;
	}else if(strcmp(RightName, "READ_TUTOR_CHANNEL") == 0){
		Result = READ_TUTOR_CHANNEL;
	}else if(strcmp(RightName, "HIGHLIGHT_HELP_CHANNEL") == 0){
		Result = HIGHLIGHT_HELP_CHANNEL;
	}else if(strcmp(RightName, "SEND_BUGREPORTS") == 0){
		Result = SEND_BUGREPORTS;
	}else if(strcmp(RightName, "NAME_INSULTING") == 0){
		Result = NAME_INSULTING;
	}else if(strcmp(RightName, "NAME_SENTENCE") == 0){
		Result = NAME_SENTENCE;
	}else if(strcmp(RightName, "NAME_NONSENSICAL_LETTERS") == 0){
		Result = NAME_NONSENSICAL_LETTERS;
	}else if(strcmp(RightName, "NAME_BADLY_FORMATTED") == 0){
		Result = NAME_BADLY_FORMATTED;
	}else if(strcmp(RightName, "NAME_NO_PERSON") == 0){
		Result = NAME_NO_PERSON;
	}else if(strcmp(RightName, "NAME_CELEBRITY") == 0){
		Result = NAME_CELEBRITY;
	}else if(strcmp(RightName, "NAME_COUNTRY") == 0){
		Result = NAME_COUNTRY;
	}else if(strcmp(RightName, "NAME_FAKE_IDENTITY") == 0){
		Result = NAME_FAKE_IDENTITY;
	}else if(strcmp(RightName, "NAME_FAKE_POSITION") == 0){
		Result = NAME_FAKE_POSITION;
	}else if(strcmp(RightName, "STATEMENT_INSULTING") == 0){
		Result = STATEMENT_INSULTING;
	}else if(strcmp(RightName, "STATEMENT_SPAMMING") == 0){
		Result = STATEMENT_SPAMMING;
	}else if(strcmp(RightName, "STATEMENT_ADVERT_OFFTOPIC") == 0){
		Result = STATEMENT_ADVERT_OFFTOPIC;
	}else if(strcmp(RightName, "STATEMENT_ADVERT_MONEY") == 0){
		Result = STATEMENT_ADVERT_MONEY;
	}else if(strcmp(RightName, "STATEMENT_NON_ENGLISH") == 0){
		Result = STATEMENT_NON_ENGLISH;
	}else if(strcmp(RightName, "STATEMENT_CHANNEL_OFFTOPIC") == 0){
		Result = STATEMENT_CHANNEL_OFFTOPIC;
	}else if(strcmp(RightName, "STATEMENT_VIOLATION_INCITING") == 0){
		Result = STATEMENT_VIOLATION_INCITING;
	}else if(strcmp(RightName, "CHEATING_BUG_ABUSE") == 0){
		Result = CHEATING_BUG_ABUSE;
	}else if(strcmp(RightName, "CHEATING_GAME_WEAKNESS") == 0){
		Result = CHEATING_GAME_WEAKNESS;
	}else if(strcmp(RightName, "CHEATING_MACRO_USE") == 0){
		Result = CHEATING_MACRO_USE;
	}else if(strcmp(RightName, "CHEATING_MODIFIED_CLIENT") == 0){
		Result = CHEATING_MODIFIED_CLIENT;
	}else if(strcmp(RightName, "CHEATING_HACKING") == 0){
		Result = CHEATING_HACKING;
	}else if(strcmp(RightName, "CHEATING_MULTI_CLIENT") == 0){
		Result = CHEATING_MULTI_CLIENT;
	}else if(strcmp(RightName, "CHEATING_ACCOUNT_TRADING") == 0){
		Result = CHEATING_ACCOUNT_TRADING;
	}else if(strcmp(RightName, "CHEATING_ACCOUNT_SHARING") == 0){
		Result = CHEATING_ACCOUNT_SHARING;
	}else if(strcmp(RightName, "GAMEMASTER_THREATENING") == 0){
		Result = GAMEMASTER_THREATENING;
	}else if(strcmp(RightName, "GAMEMASTER_PRETENDING") == 0){
		Result = GAMEMASTER_PRETENDING;
	}else if(strcmp(RightName, "GAMEMASTER_INFLUENCE") == 0){
		Result = GAMEMASTER_INFLUENCE;
	}else if(strcmp(RightName, "GAMEMASTER_FALSE_REPORTS") == 0){
		Result = GAMEMASTER_FALSE_REPORTS;
	}else if(strcmp(RightName, "KILLING_EXCESSIVE_UNJUSTIFIED") == 0){
		Result = KILLING_EXCESSIVE_UNJUSTIFIED;
	}else if(strcmp(RightName, "DESTRUCTIVE_BEHAVIOUR") == 0){
		Result = DESTRUCTIVE_BEHAVIOUR;
	}else if(strcmp(RightName, "SPOILING_AUCTION") == 0){
		Result = SPOILING_AUCTION;
	}else if(strcmp(RightName, "INVALID_PAYMENT") == 0){
		Result = INVALID_PAYMENT;
	}else if(strcmp(RightName, "TELEPORT_TO_CHARACTER") == 0){
		Result = TELEPORT_TO_CHARACTER;
	}else if(strcmp(RightName, "TELEPORT_TO_MARK") == 0){
		Result = TELEPORT_TO_MARK;
	}else if(strcmp(RightName, "TELEPORT_VERTICAL") == 0){
		Result = TELEPORT_VERTICAL;
	}else if(strcmp(RightName, "TELEPORT_TO_COORDINATE") == 0){
		Result = TELEPORT_TO_COORDINATE;
	}else if(strcmp(RightName, "LEVITATE") == 0){
		Result = LEVITATE;
	}else if(strcmp(RightName, "SPECIAL_MOVEUSE") == 0){
		Result = SPECIAL_MOVEUSE;
	}else if(strcmp(RightName, "MODIFY_GOSTRENGTH") == 0){
		Result = MODIFY_GOSTRENGTH;
	}else if(strcmp(RightName, "SHOW_COORDINATE") == 0){
		Result = SHOW_COORDINATE;
	}else if(strcmp(RightName, "RETRIEVE") == 0){
		Result = RETRIEVE;
	}else if(strcmp(RightName, "ENTER_HOUSES") == 0){
		Result = ENTER_HOUSES;
	}else if(strcmp(RightName, "OPEN_NAMEDOORS") == 0){
		// TODO(fusion): The actual name was "OPEN_NAMEDDOORS" but could be a typo?
		// Either way, it doesn't make that much of a difference since there is no
		// actual query manager outside the ones we might build ourselves.
		Result = OPEN_NAMEDOORS;
	}else if(strcmp(RightName, "INVULNERABLE") == 0){
		Result = INVULNERABLE;
	}else if(strcmp(RightName, "UNLIMITED_MANA") == 0){
		Result = UNLIMITED_MANA;
	}else if(strcmp(RightName, "KEEP_INVENTORY") == 0){
		Result = KEEP_INVENTORY;
	}else if(strcmp(RightName, "ALL_SPELLS") == 0){
		Result = ALL_SPELLS;
	}else if(strcmp(RightName, "UNLIMITED_CAPACITY") == 0){
		Result = UNLIMITED_CAPACITY;
	}else if(strcmp(RightName, "ZERO_CAPACITY") == 0){
		Result = ZERO_CAPACITY;
	}else if(strcmp(RightName, "ATTACK_EVERYWHERE") == 0){
		Result = ATTACK_EVERYWHERE;
	}else if(strcmp(RightName, "NO_ATTACK") == 0){
		Result = NO_ATTACK;
	}else if(strcmp(RightName, "NO_RUNES") == 0){
		Result = NO_RUNES;
	}else if(strcmp(RightName, "NO_LOGOUT_BLOCK") == 0){
		Result = NO_LOGOUT_BLOCK;
	}else if(strcmp(RightName, "GAMEMASTER_OUTFIT") == 0){
		Result = GAMEMASTER_OUTFIT;
	}else if(strcmp(RightName, "ILLUMINATE") == 0){
		Result = ILLUMINATE;
	}else if(strcmp(RightName, "CHANGE_PROFESSION") == 0){
		Result = CHANGE_PROFESSION;
	}else if(strcmp(RightName, "IGNORED_BY_MONSTERS") == 0){
		Result = IGNORED_BY_MONSTERS;
	}else if(strcmp(RightName, "SHOW_KEYHOLE_NUMBERS") == 0){
		Result = SHOW_KEYHOLE_NUMBERS;
	}else if(strcmp(RightName, "CREATE_OBJECTS") == 0){
		Result = CREATE_OBJECTS;
	}else if(strcmp(RightName, "CREATE_MONEY") == 0){
		Result = CREATE_MONEY;
	}else if(strcmp(RightName, "CREATE_MONSTERS") == 0){
		Result = CREATE_MONSTERS;
	}else if(strcmp(RightName, "CHANGE_SKILLS") == 0){
		Result = CHANGE_SKILLS;
	}else if(strcmp(RightName, "CLEANUP_FIELDS") == 0){
		Result = CLEANUP_FIELDS;
	}else if(strcmp(RightName, "NO_STATISTICS") == 0){
		Result = NO_STATISTICS;
	}
	return Result;
}

// NOTE(fusion): These seem to be related to forums and an admin panel.
static bool IsAdministrativeRight(const char *RightName){
	return strcmp(RightName, "CLEAR_CHARACTER_INFO") == 0
		|| strcmp(RightName, "CLEAR_GUILDS") == 0
		|| strcmp(RightName, "DELETE_GUILDS") == 0
		|| strcmp(RightName, "BOARD_REPORT") == 0
		|| strcmp(RightName, "BOARD_MODERATION") == 0
		|| strcmp(RightName, "BOARD_ANONYMOUS_EDIT") == 0
		|| strcmp(RightName, "BOARD_PRECONFIRMED") == 0
		|| strcmp(RightName, "KEEP_ACCOUNT") == 0
		|| strcmp(RightName, "INVITED") == 0
		|| strcmp(RightName, "CIPWATCH_ADMIN") == 0
		|| strcmp(RightName, "CIPWATCH_USER") == 0
		|| strcmp(RightName, "CREATECHAR_GAMEMASTER") == 0
		|| strcmp(RightName, "CREATECHAR_GOD") == 0
		|| strcmp(RightName, "CREATECHAR_TEST") == 0
		|| strcmp(RightName, "VIEW_ACCOUNT") == 0
		|| strcmp(RightName, "VIEW_GAMEMASTER_RECORD") == 0
		|| strcmp(RightName, "VIEW_CRIMINAL_RECORD") == 0
		|| strcmp(RightName, "VIEW_LOG_FILES") == 0
		|| strcmp(RightName, "MODIFY_BANISHMENT") == 0
		|| strcmp(RightName, "APPOINT_CIP") == 0
		|| strcmp(RightName, "APPOINT_SGM") == 0
		|| strcmp(RightName, "APPOINT_JGM") == 0
		|| strcmp(RightName, "APPOINT_SENATOR") == 0
		|| strcmp(RightName, "SET_ACCOUNT_RIGHTS") == 0
		|| strcmp(RightName, "SET_CHARACTER_RIGHTS") == 0
		|| strcmp(RightName, "SET_ACCOUNTGROUP_RIGHTS") == 0
		|| strcmp(RightName, "SET_CHARACTERGROUP_RIGHTS") == 0
		|| strcmp(RightName, "KEEP_CHARACTER") == 0
		|| strcmp(RightName, "EXTRA_CHARACTER") == 0;
}

// NOTE(fusion): `PlayerName` is an input and output parameter. It will contain
// the correct player name with upper and lower case letters if the operation is
// successful.
int TQueryManagerConnection::loginGame(uint32 AccountID, char *PlayerName,
		const char *Password, const char *IPAddress, bool PrivateWorld,
		bool PremiumAccountRequired, bool GamemasterRequired, uint32 *CharacterID,
		int *Sex, char *Guild, char *Rank, char *Title, int *NumberOfBuddies,
		uint32 *BuddyIDs, char (*BuddyNames)[30], uint8 *Rights,
		bool *PremiumAccountActivated){
	this->prepareQuery(20);
	this->sendQuad(AccountID);
	this->sendString(PlayerName);
	this->sendString(Password);
	this->sendString(IPAddress);
	this->sendFlag(PrivateWorld);
	this->sendFlag(PremiumAccountRequired);
	this->sendFlag(GamemasterRequired);
	int Status = this->executeQuery(120, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_OK){
		*CharacterID = this->getQuad();
		this->getString(PlayerName, 30);
		*Sex = this->getByte();
		this->getString(Guild, 30);
		this->getString(Rank, 30);
		this->getString(Title, 30);
		*NumberOfBuddies = this->getByte();

		int SkipBuddies = 0;
		if(*NumberOfBuddies > 100){ // MAX_BUDDIES
			error("TQueryManagerConnection::loginGame: zu viele Buddys (%d) für %s.\n",
					*NumberOfBuddies, PlayerName);
			SkipBuddies = *NumberOfBuddies - 100;
			*NumberOfBuddies = 100;
		}

		for(int i = 0; i < *NumberOfBuddies; i += 1){
			BuddyIDs[i] = this->getQuad();
			this->getString(BuddyNames[i], 30);
		}

		if(SkipBuddies > 0){
			char BuddyName[30];
			for(int i = 0; i < SkipBuddies; i += 1){
				this->getQuad();
				this->getString(BuddyName, 30);
			}
		}

		int NumberOfRights = this->getByte();
		memset(Rights, 0, 12); // MAX_RIGHT_BYTES ?
		for(int i = 0; i < NumberOfRights; i += 1){
			char RightName[30];
			this->getString(RightName, 30);
			int Right = GetRightByName(RightName);
			if(Right != -1){
				SetBit(Rights, Right);
			}else if(!IsAdministrativeRight(RightName)){
				error("TQueryManagerConnection::loginGame: Unbekanntes Recht %s.\n", RightName);
			}
		}

		*PremiumAccountActivated = this->getFlag();
	}else if(Status == QUERY_STATUS_ERROR){
		int ErrorCode = this->getByte();
		if(ErrorCode >= 1 && ErrorCode <= 15){
			Result = ErrorCode;
		}else{
			error("TQueryManagerConnection::loginGame: Ungültiger Fehlercode %d.\n", ErrorCode);
		}
	}
	return Result;
}

int TQueryManagerConnection::logoutGame(uint32 CharacterID, int Level, const char *Profession,
		const char *Residence, time_t LastLoginTime, int TutorActivities){
	this->prepareQuery(21);
	this->sendQuad(CharacterID);
	this->sendWord(Level);
	this->sendString(Profession);
	this->sendString(Residence);
	this->sendQuad((uint32)LastLoginTime);
	this->sendWord((uint16)TutorActivities);
	int Status = this->executeQuery(120, true);
	return (Status == QUERY_STATUS_OK ? 0 : -1);
}

int TQueryManagerConnection::setNotation(uint32 GamemasterID, const char *PlayerName,
		const char *IPAddress, const char *Reason, const char *Comment, uint32 *BanishmentID){
	this->prepareQuery(26);
	this->sendQuad(GamemasterID);
	this->sendString(PlayerName);
	this->sendString(IPAddress);
	this->sendString(Reason);
	this->sendString(Comment);
	int Status = this->executeQuery(30, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_OK){
		*BanishmentID = this->getQuad();
	}else if(Status == QUERY_STATUS_ERROR){
		int ErrorCode = this->getByte();
		if(ErrorCode >= 1 && ErrorCode <= 2){
			Result = ErrorCode;
		}else{
			error("TQueryManagerConnection::setNotation: Ungültiger Fehlercode %d.\n", ErrorCode);
		}
	}else{
		error("TQueryManagerConnection::setNotation: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::setNamelock(uint32 GamemasterID, const char *PlayerName,
		const char *IPAddress, const char *Reason, const char *Comment){
	this->prepareQuery(23);
	this->sendQuad(GamemasterID);
	this->sendString(PlayerName);
	this->sendString(IPAddress);
	this->sendString(Reason);
	this->sendString(Comment);
	int Status = this->executeQuery(30, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_ERROR){
		int ErrorCode = this->getByte();
		if(ErrorCode >= 1 && ErrorCode <= 4){
			Result = ErrorCode;
		}else{
			error("TQueryManagerConnection::setNamelock: Ungültiger Fehlercode %d.\n", ErrorCode);
		}
	}else if(Status != QUERY_STATUS_OK){
		error("TQueryManagerConnection::setNamelock: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::banishAccount(uint32 GamemasterID, const char *PlayerName,
		const char *IPAddress, const char *Reason, const char *Comment, bool *FinalWarning,
		int *Days, uint32 *BanishmentID){
	this->prepareQuery(25);
	this->sendQuad(GamemasterID);
	this->sendString(PlayerName);
	this->sendString(IPAddress);
	this->sendString(Reason);
	this->sendString(Comment);
	this->sendFlag(*FinalWarning);
	int Status = this->executeQuery(30, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_OK){
		*BanishmentID = this->getQuad();
		*Days = this->getByte();
		*FinalWarning = this->getFlag();
		if(*Days == 0xFF){
			*Days = -1;
		}
	}else if(Status == QUERY_STATUS_ERROR){
		int ErrorCode = this->getByte();
		if(ErrorCode >= 1 && ErrorCode <= 3){
			Result = ErrorCode;
		}else{
			error("TQueryManagerConnection::banishAccount: Ungültiger Fehlercode %d.\n", ErrorCode);
		}
	}else{
		error("TQueryManagerConnection::banishAccount: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::reportStatement(uint32 ReporterID, const char *PlayerName,
		const char *Reason, const char *Comment, uint32 BanishmentID, uint32 StatementID,
		int NumberOfStatements, uint32 *StatementIDs, int *TimeStamps, uint32 *CharacterIDs,
		const char (*Channels)[30], const char (*Texts)[256]){
	this->prepareQuery(27);
	this->sendQuad(ReporterID);
	this->sendString(PlayerName);
	this->sendString(Reason);
	this->sendString(Comment);
	this->sendQuad(BanishmentID);
	this->sendQuad(StatementID);

	int NonZeroStatements = 0;
	for(int i = 0; i < NumberOfStatements; i += 1){
		if(StatementIDs[i] != 0){
			NonZeroStatements += 1;
		}
	}

	this->sendWord((uint16)NonZeroStatements);
	for(int i = 0; i < NumberOfStatements; i += 1){
		if(StatementIDs[i] != 0){
			this->sendQuad(StatementIDs[i]);
			this->sendQuad((uint32)TimeStamps[i]);
			this->sendQuad(CharacterIDs[i]);
			this->sendString(Channels[i]);
			this->sendString(Texts[i]);
		}
	}

	int Status = this->executeQuery(180, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_ERROR){
		int ErrorCode = this->getByte();
		if(ErrorCode >= 1 && ErrorCode <= 2){
			Result = ErrorCode;
		}else{
			error("TQueryManagerConnection::reportStatement: Ungültiger Fehlercode %d.\n", ErrorCode);
		}
	}else if(Status != QUERY_STATUS_OK){
		error("TQueryManagerConnection::reportStatement: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::banishIPAddress(uint32 GamemasterID, const char *PlayerName,
		const char *IPAddress, const char *Reason, const char *Comment){
	this->prepareQuery(28);
	this->sendQuad(GamemasterID);
	this->sendString(PlayerName);
	this->sendString(IPAddress);
	this->sendString(Reason);
	this->sendString(Comment);
	int Status = this->executeQuery(30, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_ERROR){
		int ErrorCode = this->getByte();
		if(ErrorCode >= 1 && ErrorCode <= 2){
			Result = ErrorCode;
		}else{
			error("TQueryManagerConnection::banishIPAddress: Ungültiger Fehlercode %d.\n", ErrorCode);
		}
	}else if(Status != QUERY_STATUS_OK){
		error("TQueryManagerConnection::banishIPAddress: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::logCharacterDeath(uint32 CharacterID, int Level,
		uint32 Offender, const char *Remark, bool Unjustified, time_t Time){
	this->prepareQuery(29);
	this->sendQuad(CharacterID);
	this->sendWord((uint16)Level);
	this->sendQuad(Offender);
	this->sendString(Remark);
	this->sendFlag(Unjustified);
	this->sendQuad((uint32)Time);
	int Status = this->executeQuery(90, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status != QUERY_STATUS_OK){
		error("TQueryManagerConnection::logCharacterDeath: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::addBuddy(uint32 AccountID, uint32 Buddy){
	this->prepareQuery(30);
	this->sendQuad(AccountID);
	this->sendQuad(Buddy);
	int Status = this->executeQuery(90, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status != QUERY_STATUS_OK){
		error("TQueryManagerConnection::addBuddy: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::removeBuddy(uint32 AccountID, uint32 Buddy){
	this->prepareQuery(31);
	this->sendQuad(AccountID);
	this->sendQuad(Buddy);
	int Status = this->executeQuery(90, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status != QUERY_STATUS_OK){
		error("TQueryManagerConnection::removeBuddy: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::decrementIsOnline(uint32 CharacterID){
	this->prepareQuery(32);
	this->sendQuad(CharacterID);
	int Status = this->executeQuery(30, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status != QUERY_STATUS_OK){
		error("TQueryManagerConnection::decrementIsOnline: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::finishAuctions(int *NumberOfAuctions, uint16 *HouseIDs,
		uint32 *CharacterIDs, char (*CharacterNames)[30], int *Bids){
	this->prepareQuery(33);
	int Status = this->executeQuery(60, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_OK){
		int MaxNumberOfAuctions = *NumberOfAuctions;
		*NumberOfAuctions = this->getWord();
		if(*NumberOfAuctions > MaxNumberOfAuctions){
			error("TQueryManagerConnection::finishAuctions: zu viele Auktionen (%d>%d).\n",
					*NumberOfAuctions, MaxNumberOfAuctions);
			return -1;
		}

		for(int i = 0; i < *NumberOfAuctions; i += 1){
			HouseIDs[i] = this->getWord();
			CharacterIDs[i] = this->getQuad();
			this->getString(CharacterNames[i], 30);
			Bids[i] = (int)this->getQuad();
		}
	}else{
		error("TQueryManagerConnection::finishAuctions: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::excludeFromAuctions(uint32 CharacterID, bool Banish){
	this->prepareQuery(51);
	this->sendQuad(CharacterID);
	this->sendFlag(Banish);
	int Status = this->executeQuery(30, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status != QUERY_STATUS_OK){
		error("TQueryManagerConnection::excludeFromAuctions: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::transferHouses(int *NumberOfTransfers, uint16 *HouseIDs,
		uint32 *NewOwnerIDs, char (*NewOwnerNames)[30], int *Prices){
	this->prepareQuery(35);
	int Status = this->executeQuery(60, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_OK){
		int MaxNumberOfTransfers = *NumberOfTransfers;
		*NumberOfTransfers = this->getWord();
		if(*NumberOfTransfers > MaxNumberOfTransfers){
			error("TQueryManagerConnection::transferHouses: zu viele Transfers (%d>%d).\n",
					*NumberOfTransfers, MaxNumberOfTransfers);
			return -1;
		}

		for(int i = 0; i < *NumberOfTransfers; i += 1){
			HouseIDs[i] = this->getWord();
			NewOwnerIDs[i] = this->getQuad();
			this->getString(NewOwnerNames[i], 30);
			Prices[i] = (int)this->getQuad();
		}
	}else{
		error("TQueryManagerConnection::transferHouses: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::cancelHouseTransfer(uint16 HouseID){
	this->prepareQuery(52);
	this->sendWord(HouseID);
	int Status = this->executeQuery(30, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status != QUERY_STATUS_OK){
		error("TQueryManagerConnection::cancelHouseTransfer: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::evictFreeAccounts(int *NumberOfEvictions,
		uint16 *HouseIDs, uint32 *OwnerIDs){
	this->prepareQuery(36);
	int Status = this->executeQuery(60, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_OK){
		int MaxNumberOfEvictions = *NumberOfEvictions;
		*NumberOfEvictions = this->getWord();
		if(*NumberOfEvictions > MaxNumberOfEvictions){
			error("TQueryManagerConnection::evictFreeAccounts: zu viele Räumungen (%d>%d).\n",
					*NumberOfEvictions, MaxNumberOfEvictions);
			return -1;
		}

		for(int i = 0; i < *NumberOfEvictions; i += 1){
			HouseIDs[i] = this->getWord();
			OwnerIDs[i] = this->getQuad();
		}
	}else{
		error("TQueryManagerConnection::evictFreeAccounts: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::evictDeletedCharacters(int *NumberOfEvictions, uint16 *HouseIDs){
	this->prepareQuery(37);
	int Status = this->executeQuery(60, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_OK){
		int MaxNumberOfEvictions = *NumberOfEvictions;
		*NumberOfEvictions = this->getWord();
		if(*NumberOfEvictions > MaxNumberOfEvictions){
			error("TQueryManagerConnection::evictDeletedCharacters: zu viele Räumungen (%d>%d).\n",
					*NumberOfEvictions, MaxNumberOfEvictions);
			return -1;
		}

		for(int i = 0; i < *NumberOfEvictions; i += 1){
			HouseIDs[i] = this->getWord();
		}
	}else{
		error("TQueryManagerConnection::evictDeletedCharacters: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::evictExGuildleaders(int NumberOfGuildhouses,
		int *NumberOfEvictions, uint16 *HouseIDs, uint32 *Guildleaders){
	this->prepareQuery(38);

	this->sendWord((uint16)NumberOfGuildhouses);
	for(int i = 0; i < NumberOfGuildhouses; i += 1){
		this->sendWord(HouseIDs[i]);
		this->sendQuad(Guildleaders[i]);
	}

	int Status = this->executeQuery(60, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_OK){
		*NumberOfEvictions = this->getWord();
		for(int i = 0; i < *NumberOfEvictions; i += 1){
			HouseIDs[i] = this->getWord();
		}
	}else{
		error("TQueryManagerConnection::evictExGuildleaders: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::insertHouseOwner(uint16 HouseID, uint32 OwnerID, int PaidUntil){
	this->prepareQuery(39);
	this->sendWord(HouseID);
	this->sendQuad(OwnerID);
	this->sendQuad((uint32)PaidUntil);
	int Status = this->executeQuery(30, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status != QUERY_STATUS_OK){
		error("TQueryManagerConnection::insertHouseOwner: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::updateHouseOwner(uint16 HouseID, uint32 OwnerID, int PaidUntil){
	this->prepareQuery(40);
	this->sendWord(HouseID);
	this->sendQuad(OwnerID);
	this->sendQuad((uint32)PaidUntil);
	int Status = this->executeQuery(30, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status != QUERY_STATUS_OK){
		error("TQueryManagerConnection::updateHouseOwner: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::deleteHouseOwner(uint16 HouseID){
	this->prepareQuery(41);
	this->sendWord(HouseID);
	int Status = this->executeQuery(30, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status != QUERY_STATUS_OK){
		error("TQueryManagerConnection::deleteHouseOwner: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::getHouseOwners(int *NumberOfOwners, uint16 *HouseIDs,
		uint32 *OwnerIDs, char (*OwnerNames)[30], int *PaidUntils){
	this->prepareQuery(42);
	int Status = this->executeQuery(60, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_OK){
		int MaxNumberOfOwners = *NumberOfOwners;
		*NumberOfOwners = this->getWord();
		if(*NumberOfOwners > MaxNumberOfOwners){
			error("TQueryManagerConnection::getHouseOwners: zu viele Häuser (%d>%d).\n",
					*NumberOfOwners, MaxNumberOfOwners);
			return -1;
		}

		for(int i = 0; i < *NumberOfOwners; i += 1){
			HouseIDs[i] = this->getWord();
			OwnerIDs[i] = this->getQuad();
			this->getString(OwnerNames[i], 30);
			PaidUntils[i] = (int)this->getQuad();
		}
	}else{
		error("TQueryManagerConnection::getHouseOwners: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::getAuctions(int *NumberOfAuctions, uint16 *HouseIDs){
	this->prepareQuery(43);
	int Status = this->executeQuery(120, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_OK){
		int MaxNumberOfAuctions = *NumberOfAuctions;
		*NumberOfAuctions = this->getWord();
		if(*NumberOfAuctions > MaxNumberOfAuctions){
			error("TQueryManagerConnection::getAuctions: zu viele Auktionen (%d>%d).\n",
					*NumberOfAuctions, MaxNumberOfAuctions);
			return -1;
		}

		for(int i = 0; i < *NumberOfAuctions; i += 1){
			HouseIDs[i] = this->getWord();
		}
	}else{
		error("TQueryManagerConnection::getAuctions: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::startAuction(uint16 HouseID){
	this->prepareQuery(44);
	this->sendWord(HouseID);
	int Status = this->executeQuery(30, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status != QUERY_STATUS_OK){
		error("TQueryManagerConnection::startAuction: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::insertHouses(int NumberOfHouses, uint16 *HouseIDs,
		const char **Names, int *Rents, const char **Descriptions, int *Sizes,
		int *PositionsX,int *PositionsY,int *PositionsZ, char (*Towns)[30],
		bool *Guildhouses){
	this->prepareQuery(45);

	this->sendWord(NumberOfHouses);
	for(int i = 0; i < NumberOfHouses; i += 1){
		this->sendWord(HouseIDs[i]);
		this->sendString(Names[i]);
		this->sendQuad((int)Rents[i]);
		this->sendString(Descriptions[i]);
		this->sendWord((uint16)Sizes[i]);
		this->sendWord((uint16)PositionsX[i]);
		this->sendWord((uint16)PositionsY[i]);
		this->sendByte((uint8)PositionsZ[i]);
		this->sendString(Towns[i]);
		this->sendFlag(Guildhouses[i]);
	}

	int Status = this->executeQuery(60, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status != QUERY_STATUS_OK){
		error("TQueryManagerConnection::insertHouses: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::clearIsOnline(int *NumberOfAffectedPlayers){
	this->prepareQuery(46);
	int Status = this->executeQuery(120, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_OK){
		*NumberOfAffectedPlayers = this->getWord();
	}else{
		error("TQueryManagerConnection::clearIsOnline: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::createPlayerlist(int NumberOfPlayers, const char **Names,
		int *Levels, const char (*Professions)[30], bool *NewRecord){
	this->prepareQuery(47);

	if(NumberOfPlayers >= 0){
		this->sendWord((uint16)NumberOfPlayers);
		for(int i = 0; i < NumberOfPlayers; i += 1){
			this->sendString(Names[i]);
			this->sendWord((uint16)Levels[i]);
			this->sendString(Professions[i]);
		}
	}else{
		this->sendWord(0xFFFF);
	}

	int Status = this->executeQuery(240, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_OK){
		*NewRecord = this->getFlag();
	}else{
		error("TQueryManagerConnection::createPlayerlist: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::logKilledCreatures(int NumberOfRaces, const char **Names,
		int *KilledPlayers, int *KilledCreatures){
	this->prepareQuery(48);

	this->sendWord((uint16)NumberOfRaces);
	for(int i = 0; i < NumberOfRaces; i += 1){
		this->sendString(Names[i]);
		this->sendQuad((uint32)KilledPlayers[i]);
		this->sendQuad((uint32)KilledCreatures[i]);
	}

	int Status = this->executeQuery(240, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status != QUERY_STATUS_OK){
		error("TQueryManagerConnection::logKilledCreatures: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::loadPlayers(uint32 MinimumCharacterID, int *NumberOfPlayers,
		char (*Names)[30], uint32 *CharacterIDs){
	this->prepareQuery(50);
	this->sendQuad(MinimumCharacterID);
	int Status = this->executeQuery(900, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_OK){
		// TODO(fusion): This is called to fill the player index in `InitPlayerIndex`
		// but there is no explicit parameter that limits how many results are returned
		// which could be a problem.
		*NumberOfPlayers = (int)this->getQuad();
		for(int i = 0; i < *NumberOfPlayers; i += 1){
			this->getString(Names[i], 30);
			CharacterIDs[i] = this->getQuad();
		}
	}else{
		error("TQueryManagerConnection::loadPlayers: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::getKeptCharacters(uint32 MinimumCharacterID,
		int *NumberOfPlayers, uint32 *CharacterIDs){
	this->prepareQuery(200);
	this->sendQuad(MinimumCharacterID);
	int Status = this->executeQuery(1800, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_OK){
		// TODO(fusion): Same as `loadPlayers`.
		*NumberOfPlayers = (int)this->getQuad();
		for(int i = 0; i < *NumberOfPlayers; i += 1){
			CharacterIDs[i] = this->getQuad();
		}
	}else{
		error("TQueryManagerConnection::getKeptCharacters: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::getDeletedCharacters(uint32 MinimumCharacterID,
		int *NumberOfPlayers, uint32 *CharacterIDs){
	this->prepareQuery(201);
	this->sendQuad(MinimumCharacterID);
	int Status = this->executeQuery(900, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_OK){
		// TODO(fusion): Same as `loadPlayers`.
		*NumberOfPlayers = (int)this->getQuad();
		for(int i = 0; i < *NumberOfPlayers; i += 1){
			CharacterIDs[i] = this->getQuad();
		}
	}else{
		error("TQueryManagerConnection::getKeptCharacters: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::deleteOldCharacter(uint32 CharacterID){
	this->prepareQuery(202);
	this->sendQuad(CharacterID);
	int Status = this->executeQuery(30, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status != QUERY_STATUS_OK){
		error("TQueryManagerConnection::deleteOldCharacter: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::getHiddenCharacters(uint32 MinimumCharacterID,
		int *NumberOfPlayers, uint32 *CharacterIDs){
	this->prepareQuery(203);
	this->sendQuad(MinimumCharacterID);
	int Status = this->executeQuery(120, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_OK){
		// TODO(fusion): Same as `loadPlayers`.
		*NumberOfPlayers = (int)this->getQuad();
		for(int i = 0; i < *NumberOfPlayers; i += 1){
			CharacterIDs[i] = this->getQuad();
		}
	}else{
		error("TQueryManagerConnection::getHiddenCharacters: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::createHighscores(int NumberOfPlayers, uint32 *CharacterIDs,
		int *ExpPoints, int *ExpLevel, int *Fist, int *Club, int *Axe,
		int *Sword, int *Distance, int *Shielding, int *Magic, int *Fishing){
	this->prepareQuery(204);

	this->sendQuad((uint32)NumberOfPlayers);
	for(int i = 0; i < NumberOfPlayers; i += 1){
		this->sendQuad(CharacterIDs[i]);
		this->sendQuad((uint32)ExpPoints[i]);
		this->sendWord((uint16)ExpLevel[i]);
		this->sendWord((uint16)Fist[i]);
		this->sendWord((uint16)Club[i]);
		this->sendWord((uint16)Axe[i]);
		this->sendWord((uint16)Sword[i]);
		this->sendWord((uint16)Distance[i]);
		this->sendWord((uint16)Shielding[i]);
		this->sendWord((uint16)Magic[i]);
		this->sendWord((uint16)Fishing[i]);
	}

	int Status = this->executeQuery(120, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status != QUERY_STATUS_OK){
		error("TQueryManagerConnection::createHighscores: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::createCensus(void){
	this->prepareQuery(205);
	int Status = this->executeQuery(600, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status != QUERY_STATUS_OK){
		error("TQueryManagerConnection::createCensus: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::createKillStatistics(void){
	this->prepareQuery(206);
	int Status = this->executeQuery(300, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status != QUERY_STATUS_OK){
		error("TQueryManagerConnection::createKillStatistics: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::getPlayersOnline(int *NumberOfWorlds, char (*Names)[30], uint16 *Players){
	this->prepareQuery(207);
	int Status = this->executeQuery(120, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_OK){
		*NumberOfWorlds = this->getByte();
		for(int i = 0; i < *NumberOfWorlds; i += 1){
			this->getString(Names[i], 30);
			Players[i] = this->getWord();
		}
	}else{
		error("TQueryManagerConnection::getPlayersOnline: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::getWorlds(int *NumberOfWorlds, char (*Names)[30]){
	this->prepareQuery(208);
	int Status = this->executeQuery(120, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_OK){
		*NumberOfWorlds = this->getByte();
		for(int i = 0; i < *NumberOfWorlds; i += 1){
			this->getString(Names[i], 30);
		}
	}else{
		error("TQueryManagerConnection::getWorlds: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::getServerLoad(const char *World, int Period, int *Data){
	this->prepareQuery(209);
	this->sendString(World);
	this->sendByte((uint8)Period);
	int Status = this->executeQuery(360, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_OK){
		// TODO(fusion): Another hardcoded constant?
		for(int i = 0; i < 600; i += 0){
			Data[i] = this->getWord();
			if(Data[i] == 0xFFFF){
				Data[i] = -1;
			}
		}
	}else{
		error("TQueryManagerConnection::getServerLoad: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::insertPaymentDataOld(uint32 PurchaseNr, uint32 ReferenceNr,
		const char *FirstName, const char *LastName, const char *Company,
		const char *Street, const char *Zip, const char *City, const char *Country,
		const char *State, const char *Phone, const char *Fax, const char *EMail,
		const char *PaymentMethod, uint32 ProductID, const char *Registrant,
		uint32 AccountID, uint32 *PaymentID){
	this->prepareQuery(210);
	this->sendQuad(PurchaseNr);
	this->sendQuad(ReferenceNr);
	this->sendString(FirstName);
	this->sendString(LastName);
	this->sendString(Company);
	this->sendString(Street);
	this->sendString(Zip);
	this->sendString(City);
	this->sendString(Country);
	this->sendString(State);
	this->sendString(Phone);
	this->sendString(Fax);
	this->sendString(EMail);
	this->sendString(PaymentMethod);
	this->sendQuad(ProductID);
	this->sendString(Registrant);
	this->sendQuad(AccountID);
	int Status = this->executeQuery(360, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_OK){
		*PaymentID = this->getQuad();
	}else if(Status == QUERY_STATUS_ERROR){
		int ErrorCode = this->getByte();
		if(ErrorCode == 1){
			Result = ErrorCode;
		}else{
			error("TQueryManagerConnection::insertPaymentDataOld: Ungültiger Fehlercode %d.\n", ErrorCode);
		}
	}else{
		error("TQueryManagerConnection::insertPaymentDataOld: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::addPaymentOld(uint32 AccountID, const char *Description,
		uint32 PaymentID, int Days, int *ActionTaken){
	this->prepareQuery(211);
	this->sendQuad(AccountID);
	this->sendString(Description);
	this->sendQuad(PaymentID);
	this->sendWord((uint16)Days);
	int Status = this->executeQuery(360, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_OK){
		*ActionTaken = this->getByte();
	}else if(Status == QUERY_STATUS_ERROR){
		int ErrorCode = this->getByte();
		if(ErrorCode >= 1 && ErrorCode <= 2){
			Result = ErrorCode;
		}else{
			error("TQueryManagerConnection::addPaymentOld: Ungültiger Fehlercode %d.\n", ErrorCode);
		}
	}else{
		error("TQueryManagerConnection::addPaymentOld: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::cancelPaymentOld(uint32 PurchaseNr, uint32 ReferenceNr,
		uint32 AccountID, bool *IllegalUse, char *EMailAddress){
	this->prepareQuery(212);
	this->sendQuad(PurchaseNr);
	this->sendQuad(ReferenceNr);
	this->sendQuad(AccountID);
	int Status = this->executeQuery(360, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_OK){
		*IllegalUse = this->getFlag();
		this->getString(EMailAddress, 50);
	}else if(Status == QUERY_STATUS_ERROR){
		int ErrorCode = this->getByte();
		if(ErrorCode >= 1 && ErrorCode <= 2){
			Result = ErrorCode;
		}else{
			error("TQueryManagerConnection::cancelPaymentOld: Ungültiger Fehlercode %d.\n", ErrorCode);
		}
	}else{
		error("TQueryManagerConnection::cancelPaymentOld: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::insertPaymentDataNew(uint32 PurchaseNr, uint32 ReferenceNr,
		const char *FirstName, const char *LastName, const char *Company,
		const char *Street, const char *Zip, const char *City, const char *Country,
		const char *State, const char *Phone, const char *Fax, const char *EMail,
		const char *PaymentMethod, uint32 ProductID, const char *Registrant,
		const char *PaymentKey, uint32 *PaymentID){
	this->prepareQuery(213);
	this->sendWord(PurchaseNr);
	this->sendWord(ReferenceNr);
	this->sendString(FirstName);
	this->sendString(LastName);
	this->sendString(Company);
	this->sendString(Street);
	this->sendString(Zip);
	this->sendString(City);
	this->sendString(Country);
	this->sendString(State);
	this->sendString(Phone);
	this->sendString(Fax);
	this->sendString(EMail);
	this->sendString(PaymentMethod);
	this->sendWord(ProductID);
	this->sendString(Registrant);
	this->sendString(PaymentKey);
	int Status = this->executeQuery(360, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_OK){
		*PaymentID = this->getQuad();
	}else if(Status == QUERY_STATUS_ERROR){
		int ErrorCode = this->getByte();
		if(ErrorCode == 1){
			Result = ErrorCode;
		}else{
			error("TQueryManagerConnection::insertPaymentDataNew: Ungültiger Fehlercode %d.\n", ErrorCode);
		}
	}else{
		error("TQueryManagerConnection::insertPaymentDataNew: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::addPaymentNew(const char *PaymentKey, uint32 PaymentID,
		int *ActionTaken, char *EMailReceiver){
	this->prepareQuery(214);
	this->sendString(PaymentKey);
	this->sendQuad(PaymentID);
	int Status = this->executeQuery(360, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_OK){
		*ActionTaken = this->getByte();
		if(*ActionTaken == 5){
			this->getString(EMailReceiver, 100);
		}
	}else if(Status == QUERY_STATUS_ERROR){
		int ErrorCode = this->getByte();
		if(ErrorCode >= 1 && ErrorCode <= 3){
			Result = ErrorCode;
		}else{
			error("TQueryManagerConnection::addPaymentNew: Ungültiger Fehlercode %d.\n", ErrorCode);
		}
	}else{
		error("TQueryManagerConnection::addPaymentNew: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

int TQueryManagerConnection::cancelPaymentNew(uint32 PurchaseNr, uint32 ReferenceNr,
		const char *PaymentKey, bool *IllegalUse, bool *Present, char *EMailAddress){
	this->prepareQuery(215);
	this->sendQuad(PurchaseNr);
	this->sendQuad(ReferenceNr);
	this->sendString(PaymentKey);
	int Status = this->executeQuery(360, true);
	int Result = (Status == QUERY_STATUS_OK ? 0 : -1);
	if(Status == QUERY_STATUS_OK){
		*IllegalUse = this->getFlag();
		*Present = this->getFlag();
		this->getString(EMailAddress, 50);
	}else if(Status == QUERY_STATUS_ERROR){
		int ErrorCode = this->getByte();
		if(ErrorCode == 1){
			Result = ErrorCode;
		}else{
			error("TQueryManagerConnection::cancelPaymentNew: Ungültiger Fehlercode %d.\n", ErrorCode);
		}
	}else{
		error("TQueryManagerConnection::cancelPaymentNew: Anfrage fehlgeschlagen.\n");
	}
	return Result;
}

// TQueryManagerConnectionPool
// =============================================================================
// TODO(fusion): Same as `TQueryManagerConnection::TQueryManagerConnection`.
TQueryManagerConnectionPool::TQueryManagerConnectionPool(int Connections) :
		NumberOfConnections(std::max<int>(Connections, 1)),
		QueryManagerConnection(NULL),
		QueryManagerConnectionFree(NULL),
		FreeQueryManagerConnections(this->NumberOfConnections),
		QueryManagerConnectionMutex(1)
{
	if(Connections <= 0){
		error("TQueryManagerConnectionPool::TQueryManagerConnectionPool:"
				" Ungültige Verbindungsanzahl %d.\n", Connections);
	}
}

void TQueryManagerConnectionPool::init(void){
	this->QueryManagerConnection = new TQueryManagerConnection[this->NumberOfConnections];
	this->QueryManagerConnectionFree = new bool[this->NumberOfConnections];
	for(int i = 0; i < this->NumberOfConnections; i += 1){
		if(!this->QueryManagerConnection[i].isConnected()){
			error("TQueryManagerConnectionPool::init: Kann nicht zum Query-Manager verbinden.\n");
			throw "cannot connect to query manager";
		}

		this->QueryManagerConnectionFree[i] = true;
	}
}

void TQueryManagerConnectionPool::exit(void){
	for(int i = 0; i < this->NumberOfConnections; i += 1){
		this->FreeQueryManagerConnections.down();
	}

	delete[] this->QueryManagerConnection;
	delete[] this->QueryManagerConnectionFree;
}

TQueryManagerConnection *TQueryManagerConnectionPool::getConnection(void){
	int ConnectionIndex = -1;
	this->FreeQueryManagerConnections.down();
	this->QueryManagerConnectionMutex.down();
	for(int i = 0; i < this->NumberOfConnections; i += 1){
		if(this->QueryManagerConnectionFree[i]){
			this->QueryManagerConnectionFree[i] = false;
			ConnectionIndex = i;
			break;
		}
	}
	this->QueryManagerConnectionMutex.up();

	if(ConnectionIndex == -1){
		error("TQueryManagerConnectionPool::getConnection: Keine freie Verbindung gefunden.\n");
		return NULL;
	}

	return &this->QueryManagerConnection[ConnectionIndex];
}

void TQueryManagerConnectionPool::releaseConnection(TQueryManagerConnection *Connection){
	int ConnectionIndex = -1;
	for(int i = 0; i < this->NumberOfConnections; i += 1){
		if(&this->QueryManagerConnection[i] == Connection){
			ConnectionIndex = i;
			break;
		}
	}

	if(ConnectionIndex == -1){
		error("TQueryManagerConnectionPool::releaseConnection: Verbindung nicht gefunden.\n");
		return;
	}

	this->QueryManagerConnectionFree[ConnectionIndex] = true;
	this->FreeQueryManagerConnections.up();
}

// TQueryManagerPoolConnection
// =============================================================================
TQueryManagerPoolConnection::TQueryManagerPoolConnection(TQueryManagerConnectionPool *Pool) :
		QueryManagerConnectionPool(Pool),
		QueryManagerConnection(NULL)
{
	if(this->QueryManagerConnectionPool == NULL){
		error("TQueryManagerPoolConnection::TQueryManagerPoolConnection: Pool ist NULL.\n");
		return;
	}

	this->QueryManagerConnection = this->QueryManagerConnectionPool->getConnection();
}

TQueryManagerPoolConnection::~TQueryManagerPoolConnection(void){
	if(this->QueryManagerConnection != NULL){
		ASSERT(this->QueryManagerConnectionPool != NULL);
		this->QueryManagerConnectionPool->releaseConnection(this->QueryManagerConnection);
	}
}
