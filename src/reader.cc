#include "reader.hh"
#include "config.hh"
#include "cr.hh"
#include "map.hh"
#include "threads.hh"

static ThreadHandle ReaderThread;

static TReaderThreadOrder OrderBuffer[200];
static int OrderPointerWrite;
static int OrderPointerRead;
static Semaphore OrderBufferEmpty(NARRAY(OrderBuffer));
static Semaphore OrderBufferFull(0);

static TReaderThreadReply ReplyBuffer[200];
static int ReplyPointerWrite;
static int ReplyPointerRead;

static TDynamicWriteBuffer HelpBuffer(KB(64));

// Reader Orders
// =============================================================================
void InitReaderBuffers(void){
	OrderPointerWrite = 0;
	OrderPointerRead = 0;
	ReplyPointerWrite = 0;
	ReplyPointerRead = 0;
}

void InsertOrder(TReaderThreadOrderType OrderType,
		int SectorX, int SectorY, int SectorZ, uint32 CharacterID){
	int Orders = (OrderPointerWrite - OrderPointerRead);
	if(Orders >= NARRAY(OrderBuffer)){
		error(Translate("InsertOrder (Reader): Order-Puffer ist voll => Vergrößern.\n",
						"InsertOrder (Reader): Order buffer is full => Enlarge.\n"));
	}

	OrderBufferEmpty.down();
	int WritePos = OrderPointerWrite % NARRAY(OrderBuffer);
	OrderBuffer[WritePos].OrderType = OrderType;
	OrderBuffer[WritePos].SectorX = SectorX;
	OrderBuffer[WritePos].SectorY = SectorY;
	OrderBuffer[WritePos].SectorZ = SectorZ;
	OrderBuffer[WritePos].CharacterID = CharacterID;
	OrderPointerWrite += 1;
	OrderBufferFull.up();
}

void GetOrder(TReaderThreadOrder *Order){
	OrderBufferFull.down();
	*Order = OrderBuffer[OrderPointerRead % NARRAY(OrderBuffer)];
	OrderPointerRead += 1;
	OrderBufferEmpty.up();
}

void TerminateReaderOrder(void){
	InsertOrder(READER_ORDER_TERMINATE, 0, 0, 0, 0);
}

void LoadSectorOrder(int SectorX, int SectorY, int SectorZ){
	InsertOrder(READER_ORDER_LOADSECTOR, SectorX, SectorY, SectorZ, 0);
}

void LoadCharacterOrder(uint32 CharacterID){
	InsertOrder(READER_ORDER_LOADCHARACTER, 0, 0, 0, CharacterID);
}

void ProcessLoadSectorOrder(int SectorX, int SectorY, int SectorZ){
	// TODO(fusion): We parsed sector files way too many times now. And there
	// is also a drop in loader quality.
	char FileName[4096];
	snprintf(FileName, sizeof(FileName), "%s/%04d-%04d-%02d.sec",
			ORIGMAPPATH, SectorX, SectorY, SectorZ);
	if(!FileExists(FileName)){
		return;
	}

	int OffsetX = -1;
	int OffsetY = -1;
	bool Refreshable = false;
	HelpBuffer.Position = 0;

	TReadScriptFile Script;
	Script.open(FileName);
	while(true){
		Script.nextToken();
		if(Script.Token == ENDOFFILE){
			Script.close();
			break;
		}

		if(Script.Token == SPECIAL && Script.getSpecial() == ','){
			continue;
		}

		if(Script.Token == BYTES){
			uint8 *Offset = Script.getBytesequence();
			OffsetX = (int)Offset[0];
			OffsetY = (int)Offset[1];
			Refreshable = false;
			Script.readSymbol(':');
		}else if(Script.Token == IDENTIFIER){
			if(OffsetX == -1 || OffsetY == -1){
				Script.error("coordinate expected");
			}

			const char *Identifier = Script.getIdentifier();
			if(strcmp(Identifier, "refresh") == 0){
				Refreshable = true;
			}else if(strcmp(Identifier, "content") == 0){
				Script.readSymbol('=');

				if(Refreshable){
					HelpBuffer.writeByte((uint8)OffsetX);
					HelpBuffer.writeByte((uint8)OffsetY);
				}

				LoadObjects(&Script, &HelpBuffer, !Refreshable);
			}
		}
	}

	int Size = HelpBuffer.Position;
	if(Size > 0){
		uint8 *Data = new uint8[Size];
		memcpy(Data, HelpBuffer.Data, Size);
		SectorReply(SectorX, SectorY, SectorZ, Data, Size);
	}
}

void ProcessLoadCharacterOrder(uint32 CharacterID){
	while(true){
		TPlayerData *Slot = AssignPlayerPoolSlot(CharacterID, true);
		if(Slot == NULL){
			error(Translate("ProcessLoadCharacterOrder: Kann keinen Slot für Spielerdaten zuweisen.\n",
							"ProcessLoadCharacterOrder: Cannot allocate a slot for player data.\n"));
			break;
		}

		if(Slot->Locked == GetGameThreadID()){
			break;
		}

		if(Slot->Locked == gettid()){
			IncreasePlayerPoolSlotSticky(Slot);
			ReleasePlayerPoolSlot(Slot);
			CharacterReply(CharacterID);
			break;
		}

		DelayThread(1, 0);
	}
}

int ReaderThreadLoop(void *Unused){
	TReaderThreadOrder Order = {};
	while(true){
		GetOrder(&Order);
		if(Order.OrderType == READER_ORDER_TERMINATE){
			break;
		}

		switch(Order.OrderType){
			case READER_ORDER_LOADSECTOR:{
				ProcessLoadSectorOrder(Order.SectorX, Order.SectorY, Order.SectorZ);
				break;
			}

			case READER_ORDER_LOADCHARACTER:{
				ProcessLoadCharacterOrder(Order.CharacterID);
				break;
			}

			default:{
				error(Translate("ReaderThreadLoop: Unbekanntes Kommando %d.\n",
								"ReaderThreadLoop: Unknown command %d.\n"), Order.OrderType);
				break;
			}
		}
	}

	return 0;
}

// Reader Replies
// =============================================================================
void InsertReply(TReaderThreadReplyType ReplyType,
		int SectorX, int SectorY, int SectorZ, uint8 *Data, int Size){
	int Replies = (ReplyPointerWrite - ReplyPointerRead);
	while(Replies > NARRAY(ReplyBuffer)){
		error(Translate("InsertReply (Reader): Puffer ist voll; warte...\n",
						"InsertReply (Reader): Buffer is full; waiting...\n"));
		DelayThread(5, 0);
	}

	int WritePos = ReplyPointerWrite % NARRAY(ReplyBuffer);
	ReplyBuffer[WritePos].ReplyType = ReplyType;
	ReplyBuffer[WritePos].SectorX = SectorX;
	ReplyBuffer[WritePos].SectorY = SectorY;
	ReplyBuffer[WritePos].SectorZ = SectorZ;
	ReplyBuffer[WritePos].Data = Data;
	ReplyBuffer[WritePos].Size = Size;
	ReplyPointerWrite += 1;
}

bool GetReply(TReaderThreadReply *Reply){
	bool Result = (ReplyPointerRead < ReplyPointerWrite);
	if(Result){
		*Reply = ReplyBuffer[ReplyPointerRead % NARRAY(ReplyBuffer)];
		ReplyPointerRead += 1;
	}
	return Result;
}

void SectorReply(int SectorX, int SectorY, int SectorZ, uint8 *Data, int Size){
	InsertReply(READER_REPLY_SECTORDATA, SectorX, SectorY, SectorZ, Data, Size);
}

void CharacterReply(uint32 CharacterID){
	InsertReply(READER_REPLY_CHARACTERDATA, 0, 0, 0, NULL, (int)CharacterID);
}

void ProcessSectorReply(TRefreshSectorFunction *RefreshSector,
		int SectorX, int SectorY, int SectorZ, uint8 *Data, int Size){
	RefreshSector(SectorX, SectorY, SectorZ, Data, Size);
	delete[] Data;
}

void ProcessCharacterReply(TSendMailsFunction *SendMails, uint32 CharacterID){
	TPlayerData *Slot = AttachPlayerPoolSlot(CharacterID, true);
	if(Slot == NULL){
		DecreasePlayerPoolSlotSticky(Slot);
		return;
	}

	SendMails(Slot);
	DecreasePlayerPoolSlotSticky(Slot);
	ReleasePlayerPoolSlot(Slot);
}

void ProcessReaderThreadReplies(TRefreshSectorFunction *RefreshSector, TSendMailsFunction *SendMails){
	TReaderThreadReply Reply = {};
	while(GetReply(&Reply)){
		switch(Reply.ReplyType){
			case READER_REPLY_SECTORDATA:{
				ProcessSectorReply(RefreshSector,
						Reply.SectorX, Reply.SectorY, Reply.SectorZ,
						Reply.Data, Reply.Size);
				break;
			}

			case READER_REPLY_CHARACTERDATA:{
				ProcessCharacterReply(SendMails, (uint32)Reply.Size);
				break;
			}

			default:{
				error(Translate("ProcessReaderThreadReplies: Unbekannte Rückmeldung %d.\n",
								"ProcessReaderThreadReplies: Unknown response %d.\n"), Reply.ReplyType);
				break;
			}
		}
	}
}

// Initialization
// =============================================================================
void InitReader(void){
	InitReaderBuffers();
	ReaderThread = StartThread(ReaderThreadLoop, NULL, false);
	if(ReaderThread == INVALID_THREAD_HANDLE){
		throw "cannot start reader thread";
	}
}

void ExitReader(void){
	if(ReaderThread != INVALID_THREAD_HANDLE){
		TerminateReaderOrder();
		JoinThread(ReaderThread);
		ReaderThread = INVALID_THREAD_HANDLE;
	}
}
