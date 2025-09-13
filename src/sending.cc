#include "connections.hh"
#include "config.hh"
#include "cr.hh"
#include "info.hh"
#include "magic.hh"
#include "map.hh"
#include "writer.hh"

#include <signal.h>

#define MAX_OBJECTS_PER_POINT		10
#define MAX_OBJECTS_PER_CONTAINER	36

static int Skip = -1;
static TConnection *FirstSendingConnection;

void SendAll(void){
	TConnection *Connection = FirstSendingConnection;
	FirstSendingConnection = NULL;
	while(Connection != NULL){
		if(Connection->WillingToSend){
			Connection->WillingToSend = false;
			// NOTE(fusion): `SIGUSR2` is used to signal the connection thread
			// that there is pending data in the connection's output buffer.
			if(Connection->Live() && Connection->NextToCommit > Connection->NextToSend){
				tgkill(GetGameProcessID(), Connection->GetThreadID(), SIGUSR2);
			}
		}else{
			error(Translate("SendAll: Verbindung ist nicht sendewillig.\n",
							"SendAll: Connection is not willing to send.\n"));
		}
		Connection = Connection->NextSendingConnection;
	}
}

bool BeginSendData(TConnection *Connection){
	bool Result = false;
	if(Connection != NULL && Connection->Live() && Connection->State != CONNECTION_LOGIN){
		int OutDataCommitted = (Connection->NextToCommit - Connection->NextToSend);
		int OutDataCapacity = (int)sizeof(Connection->OutData);
		if(OutDataCommitted < OutDataCapacity){
			Connection->NextToWrite = Connection->NextToCommit;
			Connection->Overflow = false;
			Result = true;
		}else{
			print(2, Translate("BeginSendData: Puffer von Verbindung %d ist voll.\n",
							   "BeginSendData: Buffer of connection %d is full.\n"), Connection->GetSocket());
		}
	}
	return Result;
}

void FinishSendData(TConnection *Connection){
	if(Connection == NULL){
		error(Translate("FinishSendData: Verbindung ist NULL.\n",
						"FinishSendData: Connection is NULL.\n"));
		return;
	}

	if(!Connection->Live()){
		error(Translate("FinishSendData: Verbindung ist nicht online.\n",
						"FinishSendData: Connection is not online.\n"));
		return;
	}

	if(Connection->Overflow){
		print(2, Translate("FinishSendData: Puffer ist voll. Paket wird nicht versandt.\n",
						   "FinishSendData: Buffer is full. Package will not be sent.\n"));
		return;
	}

	Connection->NextToCommit = Connection->NextToWrite;
	if(!Connection->WillingToSend){
		Connection->WillingToSend = true;
		Connection->NextSendingConnection = FirstSendingConnection;
		FirstSendingConnection = Connection;
	}
}

static void SendByte(TConnection *Connection, uint8 Value){
	int OutDataWritten = (Connection->NextToWrite - Connection->NextToSend);
	int OutDataCapacity = (int)sizeof(Connection->OutData);
	if((OutDataWritten + 1) > OutDataCapacity){
		Connection->Overflow = true;
		return;
	}

	Connection->OutData[Connection->NextToWrite % OutDataCapacity] = Value;
	Connection->NextToWrite += 1;
}

static void SendWord(TConnection *Connection, uint16 Value){
	int OutDataWritten = (Connection->NextToWrite - Connection->NextToSend);
	int OutDataCapacity = (int)sizeof(Connection->OutData);
	if((OutDataWritten + 2) > OutDataCapacity){
		Connection->Overflow = true;
		return;
	}

	Connection->OutData[(Connection->NextToWrite + 0) % OutDataCapacity] = (uint8)(Value >> 0);
	Connection->OutData[(Connection->NextToWrite + 1) % OutDataCapacity] = (uint8)(Value >> 8);
	Connection->NextToWrite += 2;
}

static void SendQuad(TConnection *Connection, uint32 Value){
	int OutDataWritten = (Connection->NextToWrite - Connection->NextToSend);
	int OutDataCapacity = (int)sizeof(Connection->OutData);
	if((OutDataWritten + 4) > OutDataCapacity){
		Connection->Overflow = true;
		return;
	}

	Connection->OutData[(Connection->NextToWrite + 0) % OutDataCapacity] = (uint8)(Value >>  0);
	Connection->OutData[(Connection->NextToWrite + 1) % OutDataCapacity] = (uint8)(Value >>  8);
	Connection->OutData[(Connection->NextToWrite + 2) % OutDataCapacity] = (uint8)(Value >> 16);
	Connection->OutData[(Connection->NextToWrite + 3) % OutDataCapacity] = (uint8)(Value >> 24);
	Connection->NextToWrite += 4;
}

// NOTE(fusion): This was called `SendText` but since most these sending helpers
// were inlined, I just renamed it to something that made more sense.
static void SendBytes(TConnection *Connection, const uint8 *Buffer, int Count){
	if(Buffer == NULL){
		error(Translate("SendText: Text ist NULL.\n",
						"SendText: Text is NULL.\n"));
		return;
	}

	if(Count <= 0){
		error(Translate("SendText: ungültige Textlänge %d.\n",
						"SendText: invalid text length %d.\n"), Count);
		return;
	}

	int OutDataWritten = (Connection->NextToWrite - Connection->NextToSend);
	int OutDataCapacity = (int)sizeof(Connection->OutData);
	if((OutDataWritten + Count) > OutDataCapacity){
		Connection->Overflow = true;
		return;
	}

	int BufferStart = Connection->NextToWrite % OutDataCapacity;
	int BufferEnd = BufferStart + Count;
	if(BufferEnd < OutDataCapacity){
		memcpy(&Connection->OutData[BufferStart], Buffer, Count);
	}else{
		int Size1 = OutDataCapacity - BufferStart;
		int Size2 = BufferEnd - OutDataCapacity;
		memcpy(&Connection->OutData[BufferStart], &Buffer[0],     Size1);
		memcpy(&Connection->OutData[0],           &Buffer[Size1], Size2);
	}

	Connection->NextToWrite += Count;
}

static void SendString(TConnection *Connection, const char *String){
	if(String == NULL){
		error(Translate("SendString: String ist NULL.\n",
						"SendString: String is NULL.\n"));
		return;
	}

	int StringLength = (int)strlen(String);
	SendWord(Connection, (uint16)StringLength);
	if(StringLength > 0){
		SendBytes(Connection, (const uint8*)String, StringLength);
	}
}

static void SendOutfit(TConnection *Connection, TOutfit Outfit){
	SendWord(Connection, (uint16)Outfit.OutfitID);
	if(Outfit.OutfitID == 0){
		SendWord(Connection, Outfit.ObjectType);
	}else{
		SendBytes(Connection, Outfit.Colors, sizeof(Outfit.Colors));
	}
}

static void SendItem(TConnection *Connection, Object Obj){
	ObjectType ObjType = Obj.getObjectType();
	SendWord(Connection, (uint16)ObjType.getDisguise().TypeID);

	if(ObjType.getFlag(LIQUIDCONTAINER)){
		int LiquidType = (int)Obj.getAttribute(CONTAINERLIQUIDTYPE);
		SendByte(Connection, GetLiquidColor(LiquidType));
	}

	if(ObjType.getFlag(LIQUIDPOOL)){
		int LiquidType = (int)Obj.getAttribute(POOLLIQUIDTYPE);
		SendByte(Connection, GetLiquidColor(LiquidType));
	}

	if(ObjType.getFlag(CUMULATIVE)){
		SendByte(Connection, (uint8)Obj.getAttribute(AMOUNT));
	}
}

void SkipFlush(TConnection *Connection){
	while(Skip >= 0){
		int Count = std::min<int>(Skip, UINT8_MAX);
		SendByte(Connection, (uint8)Count);
		SendByte(Connection, UINT8_MAX);
		Skip -= (Count + 1);
	}
}

void SendMapObject(TConnection *Connection, Object Obj){
	if(!Obj.exists()){
		error(Translate("SendMapObject: Übergebenes Objekt existiert nicht.\n",
						"SendMapObject: Passed object does not exist.\n"));
		return;
	}

	if(!Obj.getObjectType().isCreatureContainer()){
		SendItem(Connection, Obj);
		return;
	}

	// TODO(fusion): This might also be contained in its own `SendCreature` function?
	TCreature *Creature = GetCreature(Obj);
	if(Creature == NULL){
		error(Translate("SendMapObject: Kreatur hat kein Kreatur-Objekt\n",
						"SendMapObject: Creature has no creature object\n"));
		return;
	}

	KNOWNCREATURESTATE KnownState = Connection->KnownCreature(Creature->ID, true);
	if(KnownState == KNOWNCREATURE_UPTODATE){
		SendWord(Connection, 99);
		SendQuad(Connection, Creature->ID);
		SendByte(Connection, (uint8)Creature->Direction);
	}else if(KnownState == KNOWNCREATURE_OUTDATED
			|| KnownState == KNOWNCREATURE_FREE){
		if(KnownState == KNOWNCREATURE_FREE){
			SendWord(Connection, 97);
			SendQuad(Connection, Connection->NewKnownCreature(Creature->ID));
			SendQuad(Connection, Creature->ID);
			if(Creature->Type == MONSTER){
				// TODO(fusion): Apparently the monster name contains its
				// article. I'm not sure it is a good idea and this probably
				// lead to other bugs such as a monster having a two part name
				// with no article.
				const char *Name = findFirst(Creature->Name, ' ');
				if(Name != NULL){
					Name += 1;
				}else{
					Name = Creature->Name;
				}
				SendString(Connection, Name);
			}else{
				SendString(Connection, Creature->Name);
			}
		}else{
			SendWord(Connection, 98);
			SendQuad(Connection, Creature->ID);
		}

		int Brightness, Color;
		GetCreatureLight(Creature->ID, &Brightness, &Color);

		int PlayerkillingMark = SKULL_NONE;
		int PartyMark = PARTY_SHIELD_NONE;
		if(Creature->Type == PLAYER){
			TPlayer *Player = (TPlayer*)Creature;
			TPlayer *Observer = Connection->GetPlayer();
			PlayerkillingMark = Player->GetPlayerkillingMark(Observer);
			PartyMark = Player->GetPartyMark(Observer);
		}

		SendByte(Connection, (uint8)Creature->GetHealth());
		SendByte(Connection, (uint8)Creature->Direction);
		SendOutfit(Connection, Creature->Outfit);
		SendByte(Connection, (uint8)Brightness);
		SendByte(Connection, (uint8)Color);
		SendWord(Connection, (uint16)Creature->GetSpeed());
		SendByte(Connection, (uint8)PlayerkillingMark);
		SendByte(Connection, (uint8)PartyMark);
	}
}

void SendMapPoint(TConnection *Connection, int x, int y, int z){
	Object Obj = GetFirstObject(x, y, z);
	if(Obj != NONE){
		SkipFlush(Connection);
		int ObjCount = 0;
		while(Obj != NONE && ObjCount < MAX_OBJECTS_PER_POINT){
			SendMapObject(Connection, Obj);
			Obj = Obj.getNextObject();
			ObjCount += 1;
		}
	}
	Skip += 1;
}

void SendResult(TConnection *Connection, RESULT r){
	if(Connection == NULL){
		return;
	}

	const char *Message = NULL;
	switch(r) {
		case NOERROR:				error("SendResult: NOERROR\n"); break;
		case NOTACCESSIBLE:			Message = "Sorry, not possible."; break;
		case NOTMOVABLE:			Message = "You cannot move this object."; break;
		case NOTTAKABLE:			Message = "You cannot take this object."; break;
		case NOROOM:				Message = "There is not enough room."; break;
		case OUTOFRANGE:			Message = "Destination is out of range."; break;
		case OUTOFHOME:				error("SendResult: OUTOFHOME\n"); break;
		case CANNOTTHROW:			Message = "You cannot throw there."; break;
		case TOOHEAVY:				Message = "This object is too heavy."; break;
		case CROSSREFERENCE:		Message = "This is impossible."; break;
		case CONTAINERFULL:			Message = "You cannot put more objects in this container."; break;
		case WRONGPOSITION:			Message = "Put this object in your hand."; break;
		case WRONGPOSITION2:		Message = "Put this object in both hands."; break;
		case WRONGCLOTHES:			Message = "You cannot dress this object there."; break;
		case HANDSNOTFREE:			Message = "Both hands have to be free."; break;
		case HANDBLOCKED:			Message = "Drop the double-handed object first."; break;
		case ONEWEAPONONLY:			Message = "You may only use one weapon."; break;
		case NOMATCH:				error("SendResult: NOMATCH\n"); break;
		case NOTCUMULABLE:			error("SendResult: NOTCUMULABLE\n"); break;
		case TOOMANYPARTS:			error("SendResult: TOOMANYPARTS\n"); break;
		case EMPTYCONTAINER:		error("SendResult: EMPTYCONTAINER\n"); break;
		case SPLITOBJECT:			error("SendResult: SPLITOBJECT\n"); break;
		case NOKEYMATCH:			Message = "The key does not match."; break;
		case UPSTAIRS:				Message = "First go upstairs."; break;
		case DOWNSTAIRS:			Message = "First go downstairs."; break;
		case CREATURENOTEXISTING:	Message = "A creature with this name does not exist."; break;
		case PLAYERNOTEXISTING:		Message = "A player with this name does not exist."; break;
		case PLAYERNOTONLINE:		Message = "A player with this name is not online."; break;
		case NAMEAMBIGUOUS:			Message = "Playername is ambiguous."; break;
		case NOTUSABLE:				Message = "You cannot use this object."; break;
		case FEDUP:					Message = "You are full."; break;
		case SPELLUNKNOWN:			Message = "You must learn this spell first."; break;
		case LOWMAGICLEVEL:			Message = "Your magic level is too low."; break;
		case MAGICITEM:				Message = "A magic item is necessary to cast this spell."; break;
		case NOTENOUGHMANA:			Message = "You do not have enough mana."; break;
		case NOSKILL:				error("SendResult: NOSKILL\n"); break;
		case TARGETLOST:			Message = "Target lost."; break;
		case NOCREATURE:			Message = "You can only use this rune on creatures."; break;
		case TOOLONG:				error("SendResult: TOOLONG\n"); break;
		case ATTACKNOTALLOWED:		Message = "You may not attack this person."; break;
		case NOWAY:					Message = "There is no way."; break;
		case LOGINERROR:			Message = "An error occured while logging in."; break;
		case PROTECTIONZONE:		Message = "This action is not permitted in a protection zone."; break;
		case ENTERPROTECTIONZONE:	Message = "Characters who attacked other players may not enter a protection zone."; break;
		case EXHAUSTED:				Message = "You are exhausted."; break;
		case NOTINVITED:			Message = "You are not invited."; break;
		case NOPREMIUMACCOUNT:		Message = "You need a premium account."; break;
		case MOVENOTPOSSIBLE:		Message = "Sorry, not possible."; break;
		case ALREADYTRADING:		Message = "You are already trading. Finish this trade first."; break;
		case PARTNERTRADING:		Message = "This person is already trading."; break;
		case TOOMANYOBJECTS:		Message = "You can only trade up to 100 objects at one time."; break;
		case TOOMANYSLAVES:			Message = "You cannot control more creatures."; break;
		case NOTTURNABLE:			Message = "You cannot turn this object."; break;
		case SECUREMODE:			Message = "Turn secure mode off if you really want to attack unmarked players."; break;
		case NOTENOUGHSOULPOINTS:	Message = "You do not have enough soulpoints."; break;
		case LOWLEVEL:				Message = "Your level is too low."; break;
		default:					break;
	}

	if(Message != NULL){
		SendMessage(Connection, TALK_FAILURE_MESSAGE, Message);
		if(r == ENTERPROTECTIONZONE || r == NOTINVITED || r == MOVENOTPOSSIBLE){
			SendSnapback(Connection);
		}
	}
}

void SendRefresh(TConnection *Connection){
	SendFullScreen(Connection);
	SendAmbiente(Connection);
	SendPlayerData(Connection);
	SendPlayerSkills(Connection);
}

void SendInitGame(TConnection *Connection, uint32 CreatureID){
	if(!BeginSendData(Connection)){
		return;
	}

	SendByte(Connection, SV_CMD_INIT_GAME);
	SendQuad(Connection, CreatureID);
	SendWord(Connection, (uint16)Beat);
	SendByte(Connection, CheckRight(CreatureID, SEND_BUGREPORTS) ? 1 : 0);
	FinishSendData(Connection);
}

void SendRights(TConnection *Connection){
	if(!BeginSendData(Connection)){
		return;
	}

	SendByte(Connection, SV_CMD_RIGHTS);

	// NOTE(fusion): See `CheckBanishmentRight`.
	bool Count = 0;
	for(int Reason = 0; Reason <= 31; Reason += 1){
		uint8 Actions = 0;
		if(CheckRight(Connection->CharacterID, (RIGHT)(Reason + 18))){
			for(int Action = 0; Action <= 6; Action += 1){
				if(CheckBanishmentRight(Connection->CharacterID, Reason, Action)){
					Actions |= (1 << Action);
				}
			}

			if(Actions != 0){
				if(CheckRight(Connection->CharacterID, IP_BANISHMENT)){
					Actions |= 0x80;
				}
				Count += 1;
			}
		}

		SendByte(Connection, Actions);
	}

	if(Count > 0){
		FinishSendData(Connection);
	}
}

void SendPing(TConnection *Connection){
	if(!BeginSendData(Connection)){
		return;
	}

	SendByte(Connection, SV_CMD_PING);
	FinishSendData(Connection);
}

void SendFullScreen(TConnection *Connection){
	if(!BeginSendData(Connection)){
		return;
	}

	int PlayerX, PlayerY, PlayerZ;
	Connection->GetPosition(&PlayerX, &PlayerY, &PlayerZ);
	int MinX = PlayerX - Connection->TerminalOffsetX;
	int MinY = PlayerY - Connection->TerminalOffsetY;
	int MaxX = MinX + Connection->TerminalWidth - 1;
	int MaxY = MinY + Connection->TerminalHeight - 1;

	// NOTE(fusion): `EndZ` is exclusive.
	int StartZ, EndZ, StepZ;
	if(PlayerZ <= 7){
		StepZ  = -1;
		StartZ = 7;
		EndZ   = 0 + StepZ;
	}else{
		StepZ  = 1;
		StartZ = PlayerZ - 2;
		EndZ   = std::min<int>(PlayerZ + 2, 15) + StepZ;
	}

	SendByte(Connection, SV_CMD_FULLSCREEN);
	SendWord(Connection, (uint16)PlayerX);
	SendWord(Connection, (uint16)PlayerY);
	SendByte(Connection, (uint8)PlayerZ);

	Skip = -1;
	for(int PointZ = StartZ; PointZ != EndZ; PointZ += StepZ){
		int ZOffset = (PlayerZ - PointZ);
		for(int PointX = MinX; PointX <= MaxX; PointX += 1)
		for(int PointY = MinY; PointY <= MaxY; PointY += 1){
			SendMapPoint(Connection, PointX + ZOffset, PointY + ZOffset, PointZ);
		}
	}
	SkipFlush(Connection);

	FinishSendData(Connection);
}

void SendRow(TConnection *Connection, int Direction){
	if(!BeginSendData(Connection)){
		return;
	}

	int PlayerX, PlayerY, PlayerZ;
	Connection->GetPosition(&PlayerX, &PlayerY, &PlayerZ);
	int MinX = PlayerX - Connection->TerminalOffsetX;
	int MinY = PlayerY - Connection->TerminalOffsetY;
	int MaxX = MinX + Connection->TerminalWidth - 1;
	int MaxY = MinY + Connection->TerminalHeight - 1;

	// NOTE(fusion): `EndZ` is exclusive.
	int StartZ, EndZ, StepZ;
	if(PlayerZ <= 7){
		StepZ  = -1;
		StartZ = 7;
		EndZ   = 0 + StepZ;
	}else{
		StepZ  = 1;
		StartZ = PlayerZ - 2;
		EndZ   = std::min<int>(PlayerZ + 2, 15) + StepZ;
	}

	if(Direction == DIRECTION_NORTH){
		SendByte(Connection, SV_CMD_ROW_NORTH);
		MaxY = MinY;
	}else if(Direction == DIRECTION_EAST){
		SendByte(Connection, SV_CMD_ROW_EAST);
		MinX = MaxX;
	}else if(Direction == DIRECTION_SOUTH){
		SendByte(Connection, SV_CMD_ROW_SOUTH);
		MinY = MaxY;
	}else if(Direction == DIRECTION_WEST){
		SendByte(Connection, SV_CMD_ROW_WEST);
		MaxX = MinX;
	}else{
		error(Translate("SendRow: Ungültige Richtungsangabe %d.\n",
						"SendRow: Invalid direction %d.\n"), Direction);
		return;
	}

	Skip = -1;
	for(int PointZ = StartZ; PointZ != EndZ; PointZ += StepZ){
		int ZOffset = (PlayerZ - PointZ);
		for(int PointX = MinX; PointX <= MaxX; PointX += 1)
		for(int PointY = MinY; PointY <= MaxY; PointY += 1){
			SendMapPoint(Connection, PointX + ZOffset, PointY + ZOffset, PointZ);
		}
	}
	SkipFlush(Connection);

	FinishSendData(Connection);
}

void SendFloors(TConnection *Connection, bool Up){
	if(!BeginSendData(Connection)){
		return;
	}

	int PlayerX, PlayerY, PlayerZ;
	Connection->GetPosition(&PlayerX, &PlayerY, &PlayerZ);

	// NOTE(fusion): `EndZ` is exclusive.
	int StartZ = -1;
	int EndZ = -1;
	int StepZ = 0;
	if(Up){
		SendByte(Connection, SV_CMD_FLOOR_UP);
		if(PlayerZ == 7){
			// NOTE(fusion): Going to surface (8 -> 7). The client currently has
			// floors [10, 6] so we only need to send floors [5, 0].
			StepZ = -1;
			StartZ = 5;
			EndZ = 0 + StepZ;
		}else if(PlayerZ > 7){
			// NOTE(fusion): Going up but still underground. Send next floor up.
			StepZ = -1;
			StartZ = PlayerZ - 2;
			EndZ = StartZ + StepZ;
		}
	}else{
		SendByte(Connection, SV_CMD_FLOOR_DOWN);
		if(PlayerZ == 8){
			// NOTE(fusion): Going underground (7 -> 8). The client currently has
			// floors [7, 0] so we only need to send floors [10, 8].
			StepZ = 1;
			StartZ = 8;
			EndZ = 10 + StepZ;
		}else if(PlayerZ > 8 && (PlayerZ + 2) <= 15){
			// NOTE(fusion): Going down while underground. Send next floor down
			// if it exists.
			StepZ = 1;
			StartZ = PlayerZ + 2;
			EndZ = StartZ + StepZ;
		}
	}

	if(StartZ != EndZ){
		int MinX = PlayerX - Connection->TerminalOffsetX;
		int MinY = PlayerY - Connection->TerminalOffsetY;
		int MaxX = MinX + Connection->TerminalWidth - 1;
		int MaxY = MinY + Connection->TerminalHeight - 1;

		Skip = -1;
		for(int PointZ = StartZ; PointZ != EndZ; PointZ += StepZ){
			int ZOffset = (PlayerZ - PointZ);
			for(int PointX = MinX; PointX <= MaxX; PointX += 1)
			for(int PointY = MinY; PointY <= MaxY; PointY += 1){
				SendMapPoint(Connection, PointX + ZOffset, PointY + ZOffset, PointZ);
			}
		}
		SkipFlush(Connection);
	}

	FinishSendData(Connection);
}

void SendFieldData(TConnection *Connection, int x, int y, int z){
	if(!BeginSendData(Connection)){
		return;
	}

	SendByte(Connection, SV_CMD_FIELD_DATA);
	SendWord(Connection, (uint16)x);
	SendWord(Connection, (uint16)y);
	SendByte(Connection, (uint8)z);

	Skip = -1;
	SendMapPoint(Connection, x, y, z);
	SkipFlush(Connection);

	FinishSendData(Connection);
}

void SendAddField(TConnection *Connection, int x, int y, int z, Object Obj){
	if(!BeginSendData(Connection)){
		return;
	}

	if(!Obj.exists()){
		error(Translate("SendAddField: Übergebenes Objekt existiert nicht.\n",
						"SendAddField: Passed object does not exist.\n"));
		return;
	}

	SendByte(Connection, SV_CMD_ADD_FIELD);
	SendWord(Connection, (uint16)x);
	SendWord(Connection, (uint16)y);
	SendByte(Connection, (uint8)z);
	SendMapObject(Connection, Obj);
	FinishSendData(Connection);
}

void SendChangeField(TConnection *Connection, int x, int y, int z, Object Obj){
	if(!BeginSendData(Connection)){
		return;
	}

	if(!Obj.exists()){
		error(Translate("SendChangeField: Übergebenes Objekt existiert nicht.\n",
						"SendChangeField: Passed object does not exist.\n"));
		return;
	}

	int ObjIndex = GetObjectRNum(Obj);
	if(ObjIndex < MAX_OBJECTS_PER_POINT){
		SendByte(Connection, SV_CMD_CHANGE_FIELD);
		SendWord(Connection, (uint16)x);
		SendWord(Connection, (uint16)y);
		SendByte(Connection, (uint8)z);
		SendByte(Connection, (uint8)ObjIndex);
		SendMapObject(Connection, Obj);
		FinishSendData(Connection);
	}
}

void SendDeleteField(TConnection *Connection, int x, int y, int z, Object Obj){
	if(!BeginSendData(Connection)){
		return;
	}

	if(!Obj.exists()){
		error(Translate("SendDeleteField: Übergebenes Objekt existiert nicht.\n",
						"SendDeleteField: Passed object does not exist.\n"));
		return;
	}

	int ObjIndex = GetObjectRNum(Obj);
	if(ObjIndex < MAX_OBJECTS_PER_POINT){
		SendByte(Connection, SV_CMD_DELETE_FIELD);
		SendWord(Connection, (uint16)x);
		SendWord(Connection, (uint16)y);
		SendByte(Connection, (uint8)z);
		SendByte(Connection, (uint8)ObjIndex);
		FinishSendData(Connection);
	}
}

void SendMoveCreature(TConnection *Connection,
		uint32 CreatureID, int DestX, int DestY, int DestZ){
	if(Connection == NULL){
		return;
	}

	TCreature *Creature = GetCreature(CreatureID);
	if(Creature == NULL){
		error(Translate("SendMoveCreature: Kreatur existiert nicht.\n",
						"SendMoveCreature: Creature does not exist.\n"));
		return;
	}

	int OrigX = Creature->posx;
	int OrigY = Creature->posy;
	int OrigZ = Creature->posz;
	int OrigIndex = GetObjectRNum(Creature->CrObject);
	bool IsVisible = Connection->IsVisible(DestX, DestY, DestZ);
	bool WasVisible = OrigIndex < MAX_OBJECTS_PER_POINT
			&& Connection->IsVisible(OrigX, OrigY, OrigZ);
	if(IsVisible && WasVisible){
		if(BeginSendData(Connection)){
			SendByte(Connection, SV_CMD_MOVE_CREATURE);
			SendWord(Connection, (uint16)OrigX);
			SendWord(Connection, (uint16)OrigY);
			SendByte(Connection, (uint8)OrigZ);
			SendByte(Connection, (uint8)OrigIndex);
			SendWord(Connection, (uint16)DestX);
			SendWord(Connection, (uint16)DestY);
			SendByte(Connection, (uint8)DestZ);
			FinishSendData(Connection);
		}
	}else if(IsVisible){
		SendAddField(Connection, DestX, DestY, DestZ, Creature->CrObject);
	}else if(WasVisible){
		SendDeleteField(Connection, OrigX, OrigY, OrigZ, Creature->CrObject);
	}
}

void SendContainer(TConnection *Connection, int ContainerNr){
	if(!BeginSendData(Connection)){
		return;
	}

	TPlayer *Player = Connection->GetPlayer();
	if(Player == NULL){
		error(Translate("SendContainer: Zu dieser Verbindung gehört kein Spieler.\n",
						"SendContainer: No player belongs to this connection.\n"));
		return;
	}

	Object Con = Player->GetOpenContainer(ContainerNr);
	if(!Con.exists()){
		error(Translate("SendContainer: Container %d existiert nicht.\n",
						"SendContainer: Container %d does not exist.\n"), ContainerNr);
		return;
	}

	ObjectType ConType = Con.getObjectType();
	bool HasUpContainer = (Con.getContainer() != NONE
			&& !Con.getContainer().getObjectType().isBodyContainer());

	int ConObjects = CountObjectsInContainer(Con);
	if(ConObjects > MAX_OBJECTS_PER_CONTAINER){
		ConObjects = MAX_OBJECTS_PER_CONTAINER;
	}

	SendByte(Connection, SV_CMD_CONTAINER);
	SendByte(Connection, (uint8)ContainerNr);
	SendWord(Connection, (uint16)ConType.getDisguise().TypeID);
	SendString(Connection, ConType.getName(-1));
	SendByte(Connection, (uint8)ConType.getAttribute(CAPACITY));
	SendByte(Connection, HasUpContainer ? 1 : 0);
	SendByte(Connection, (uint8)ConObjects);

	Object Obj = GetFirstContainerObject(Con);
	while(Obj != NONE && ConObjects > 0){
		SendItem(Connection, Obj);
		Obj = Obj.getNextObject();
		ConObjects -= 1;
	}

	FinishSendData(Connection);
}

void SendCloseContainer(TConnection *Connection, int ContainerNr){
	if(!BeginSendData(Connection)){
		return;
	}

	SendByte(Connection, SV_CMD_CLOSE_CONTAINER);
	SendByte(Connection, (uint8)ContainerNr);
	FinishSendData(Connection);
}

void SendCreateInContainer(TConnection *Connection, int ContainerNr, Object Obj){
	if(!BeginSendData(Connection)){
		return;
	}

	SendByte(Connection, SV_CMD_CREATE_IN_CONTAINER);
	SendByte(Connection, (uint8)ContainerNr);
	SendItem(Connection, Obj);
	FinishSendData(Connection);
}

void SendChangeInContainer(TConnection *Connection, int ContainerNr, Object Obj){
	if(!BeginSendData(Connection)){
		return;
	}

	if(!Obj.exists()){
		error(Translate("SendChangeInContainer: Übergebenes Objekt existiert nicht.\n",
						"SendChangeInContainer: Passed object does not exist.\n"));
		return;
	}

	int ObjIndex = GetObjectRNum(Obj);
	if(ObjIndex < MAX_OBJECTS_PER_CONTAINER){
		SendByte(Connection, SV_CMD_CHANGE_IN_CONTAINER);
		SendByte(Connection, (uint8)ContainerNr);
		SendByte(Connection, (uint8)ObjIndex);
		SendItem(Connection, Obj);
		FinishSendData(Connection);
	}
}

void SendDeleteInContainer(TConnection *Connection, int ContainerNr, Object Obj){
	if(!BeginSendData(Connection)){
		return;
	}

	if(!Obj.exists()){
		error(Translate("SendDeleteInContainer: Übergebenes Objekt existiert nicht.\n",
						"SendDeleteInContainer: Passed object does not exist.\n"));
		return;
	}

	int ObjIndex = GetObjectRNum(Obj);
	if(ObjIndex < MAX_OBJECTS_PER_CONTAINER){
		SendByte(Connection, SV_CMD_DELETE_IN_CONTAINER);
		SendByte(Connection, (uint8)ContainerNr);
		SendByte(Connection, (uint8)ObjIndex);
		FinishSendData(Connection);
	}
}

void SendBodyInventory(TConnection *Connection, uint32 CreatureID){
	for(int Position = INVENTORY_FIRST;
			Position <= INVENTORY_LAST;
			Position += 1){
		Object Obj = GetBodyObject(CreatureID, Position);
		if(Obj != NONE){
			SendSetInventory(Connection, Position, Obj);
		}
	}
}

void SendSetInventory(TConnection *Connection, int Position, Object Obj){
	if(!BeginSendData(Connection)){
		return;
	}

	if(!Obj.exists()){
		error(Translate("SendSetInventory: Objekt existiert nicht.\n",
						"SendSetInventory: Object does not exist.\n"));
		return;
	}

	SendByte(Connection, SV_CMD_SET_INVENTORY);
	SendByte(Connection, (uint8)Position);
	SendItem(Connection, Obj);
	FinishSendData(Connection);
}

void SendDeleteInventory(TConnection *Connection, int Position){
	if(!BeginSendData(Connection)){
		return;
	}

	SendByte(Connection, SV_CMD_DELETE_INVENTORY);
	SendByte(Connection, (uint8)Position);
	FinishSendData(Connection);
}

static void SendTradeObjects(TConnection *Connection, Object Obj){
	if(!Obj.exists()){
		return;
	}

	SendItem(Connection, Obj);
	if(Obj.getObjectType().getFlag(CONTAINER)){
		Object Help = GetFirstContainerObject(Obj);
		while(Help != NONE){
			SendTradeObjects(Connection, Help);
			Help = Help.getNextObject();
		}
	}
}

void SendTradeOffer(TConnection *Connection, const char *Name, bool OwnOffer, Object Obj){
	if(!BeginSendData(Connection)){
		return;
	}

	if(Name == NULL){
		error(Translate("SendTradeOffer: Name ist NULL.\n",
						"SendTradeOffer: Name is NULL.\n"));
		return;
	}

	if(!Obj.exists()){
		error(Translate("SendTradeOffer: Objekt existiert nicht.\n",
						"SendTradeOffer: Object does not exist.\n"));
		return;
	}

	int ObjCount = CountObjects(Obj);
	if(ObjCount > UINT8_MAX){
		error("SendTradeOffer: Too many objects.\n");
		return;
	}

	if(OwnOffer){
		SendByte(Connection, SV_CMD_TRADE_OFFER_OWN);
	}else{
		SendByte(Connection, SV_CMD_TRADE_OFFER_PARTNER);
	}

	SendString(Connection, Name);
	SendByte(Connection, (uint8)ObjCount);
	SendTradeObjects(Connection, Obj);
	FinishSendData(Connection);
}

void SendCloseTrade(TConnection *Connection){
	if(!BeginSendData(Connection)){
		return;
	}

	SendByte(Connection, SV_CMD_CLOSE_TRADE);
	FinishSendData(Connection);
}

void SendAmbiente(TConnection *Connection){
	if(!BeginSendData(Connection)){
		return;
	}

	int Brightness, Color;
	GetAmbiente(&Brightness, &Color);

	SendByte(Connection, SV_CMD_AMBIENTE);
	SendByte(Connection, (uint8)Brightness);
	SendByte(Connection, (uint8)Color);
	FinishSendData(Connection);
}

void SendGraphicalEffect(TConnection *Connection, int x, int y, int z, int Type){
	if(!BeginSendData(Connection)){
		return;
	}

	SendByte(Connection, SV_CMD_GRAPHICAL_EFFECT);
	SendWord(Connection, (uint16)x);
	SendWord(Connection, (uint16)y);
	SendByte(Connection, (uint8)z);
	SendByte(Connection, (uint8)Type);
	FinishSendData(Connection);
}

void SendTextualEffect(TConnection *Connection, int x, int y, int z, int Color, const char *Text){
	if(Text == NULL){
		error(Translate("SendTextualEffect: Text ist NULL.\n",
						"SendTextualEffect: Text is NULL.\n"));
		return;
	}

	if(Text[0] == 0){
		error(Translate("SendTextualEffect: Text ist leer.\n",
						"SendTextualEffect: Text is empty.\n"));
		return;
	}

	if(!BeginSendData(Connection)){
		return;
	}

	SendByte(Connection, SV_CMD_TEXTUAL_EFFECT);
	SendWord(Connection, (uint16)x);
	SendWord(Connection, (uint16)y);
	SendByte(Connection, (uint8)z);
	SendByte(Connection, (uint8)Color);
	SendString(Connection, Text);
	FinishSendData(Connection);
}

void SendMissileEffect(TConnection *Connection, int OrigX, int OrigY, int OrigZ,
		int DestX, int DestY, int DestZ, int Type){
	if(!BeginSendData(Connection)){
		return;
	}

	SendByte(Connection, SV_CMD_MISSILE_EFFECT);
	SendWord(Connection, (uint16)OrigX);
	SendWord(Connection, (uint16)OrigY);
	SendByte(Connection, (uint8)OrigZ);
	SendWord(Connection, (uint16)DestX);
	SendWord(Connection, (uint16)DestY);
	SendByte(Connection, (uint8)DestZ);
	SendByte(Connection, (uint8)Type);
	FinishSendData(Connection);
}

void SendMarkCreature(TConnection *Connection, uint32 CreatureID, int Color){
	if(!BeginSendData(Connection)){
		return;
	}

	SendByte(Connection, SV_CMD_MARK_CREATURE);
	SendQuad(Connection, CreatureID);
	SendByte(Connection, (uint8)Color);
	FinishSendData(Connection);
}

void SendCreatureHealth(TConnection *Connection, uint32 CreatureID){
	if(!BeginSendData(Connection)){
		return;
	}

	TCreature *Creature = GetCreature(CreatureID);
	if(Creature == NULL){
		error(Translate("SendCreatureHealth: Kreatur %u existiert nicht.\n",
						"SendCreatureHealth: Creature %u does not exist.\n"), CreatureID);
		return;
	}

	SendByte(Connection, SV_CMD_CREATURE_HEALTH);
	SendQuad(Connection, CreatureID);
	SendByte(Connection, (uint8)Creature->GetHealth());
	FinishSendData(Connection);
}

void SendCreatureLight(TConnection *Connection, uint32 CreatureID){
	if(!BeginSendData(Connection)){
		return;
	}

	TCreature *Creature = GetCreature(CreatureID);
	if(Creature == NULL){
		error(Translate("SendCreatureLight: Kreatur %u existiert nicht.\n",
						"SendCreatureLight: Creature %u does not exist.\n"), CreatureID);
		return;
	}

	int Brightness, Color;
	GetCreatureLight(CreatureID, &Brightness, &Color);

	SendByte(Connection, SV_CMD_CREATURE_LIGHT);
	SendQuad(Connection, CreatureID);
	SendByte(Connection, (uint8)Brightness);
	SendByte(Connection, (uint8)Color);
	FinishSendData(Connection);
}

void SendCreatureOutfit(TConnection *Connection, uint32 CreatureID){
	if(!BeginSendData(Connection)){
		return;
	}

	TCreature *Creature = GetCreature(CreatureID);
	if(Creature == NULL){
		error(Translate("SendCreatureOutfit: Kreatur %u existiert nicht.\n",
						"SendCreatureOutfit: Creature %u does not exist.\n"), CreatureID);
		return;
	}

	SendByte(Connection, SV_CMD_CREATURE_OUTFIT);
	SendQuad(Connection, CreatureID);
	SendOutfit(Connection, Creature->Outfit);
	FinishSendData(Connection);
}

void SendCreatureSpeed(TConnection *Connection, uint32 CreatureID){
	if(!BeginSendData(Connection)){
		return;
	}

	TCreature *Creature = GetCreature(CreatureID);
	if(Creature == NULL){
		error(Translate("SendCreatureSpeed: Kreatur %u existiert nicht.\n",
						"SendCreatureSpeed: Creature %u does not exist.\n"), CreatureID);
		return;
	}

	SendByte(Connection, SV_CMD_CREATURE_SPEED);
	SendQuad(Connection, CreatureID);
	SendWord(Connection, (uint16)Creature->GetSpeed());
	FinishSendData(Connection);
}

void SendCreatureSkull(TConnection *Connection, uint32 CreatureID){
	if(!BeginSendData(Connection)){
		return;
	}

	TPlayer *Player = GetPlayer(CreatureID);
	if(Player == NULL){
		error(Translate("SendCreatureSkull: Kreatur %u existiert nicht.\n",
						"SendCreatureSkull: Creature %u does not exist.\n"), CreatureID);
		return;
	}

	SendByte(Connection, SV_CMD_CREATURE_SKULL);
	SendQuad(Connection, CreatureID);
	SendByte(Connection, Player->GetPlayerkillingMark(Connection->GetPlayer()));
	FinishSendData(Connection);
}

void SendCreatureParty(TConnection *Connection, uint32 CreatureID){
	if(!BeginSendData(Connection)){
		return;
	}

	TPlayer *Player = GetPlayer(CreatureID);
	if(Player == NULL){
		error(Translate("SendCreatureParty: Kreatur %u existiert nicht.\n",
						"SendCreatureParty: Creature %u does not exist.\n"), CreatureID);
		return;
	}

	SendByte(Connection, SV_CMD_CREATURE_PARTY);
	SendQuad(Connection, CreatureID);
	SendByte(Connection, Player->GetPartyMark(Connection->GetPlayer()));
	FinishSendData(Connection);
}

void SendEditText(TConnection *Connection, Object Obj){
	if(!BeginSendData(Connection)){
		return;
	}

	if(!Obj.exists()){
		error(Translate("SendEditText: Übergebenes Objekt existiert nicht.\n",
						"SendEditText: Passed object does not exist.\n"));
		return;
	}

	ObjectType ObjType = Obj.getObjectType();
	int TypeID = ObjType.getDisguise().TypeID;
	int MaxLength = 0;
	const char *Text = NULL;
	const char *Editor = NULL;
	char SpellbookBuffer[4096] = {};
	if(ObjType.getFlag(WRITE) || ObjType.getFlag(WRITEONCE)){
		MaxLength = (ObjType.getFlag(WRITE)
				? (int)ObjType.getAttribute(MAXLENGTH)
				: (int)ObjType.getAttribute(MAXLENGTHONCE));
		MaxLength -= 1;
		Text = GetDynamicString(Obj.getAttribute(TEXTSTRING));
		Editor = GetDynamicString(Obj.getAttribute(EDITOR));
	}else if(ObjType.getFlag(INFORMATION)
			&& ObjType.getAttribute(INFORMATIONTYPE) == 4){ // INFORMATION_SPELLBOOK ?
		TPlayer *Player = Connection->GetPlayer();
		if(Player == NULL){
			error(Translate("SendEditText: Zu dieser Verbindung gehört kein Spieler.\n",
							"SendEditText: No player belongs to this connection.\n"));
			return;
		}

		GetSpellbook(Player->ID, SpellbookBuffer);
		Text = SpellbookBuffer;
		MaxLength = strlen(Text);
	}else{
		Text = GetDynamicString(Obj.getAttribute(TEXTSTRING));
		Editor = GetDynamicString(Obj.getAttribute(EDITOR));
		if(Text != NULL){
			MaxLength = strlen(Text);
		}
	}

	SendByte(Connection, SV_CMD_EDIT_TEXT);
	SendQuad(Connection, Obj.ObjectID);
	SendWord(Connection, (uint16)TypeID);
	SendWord(Connection, (uint16)MaxLength);
	if(Text != NULL){
		SendString(Connection, Text);
		SendString(Connection, (Editor != NULL ? Editor : ""));
	}else{
		SendString(Connection, "");
		SendString(Connection, "");
	}

	FinishSendData(Connection);
}

void SendEditList(TConnection *Connection, uint8 Type, uint32 ID, const char *Text){
	if(!BeginSendData(Connection)){
		return;
	}

	if(Text == NULL){
		error(Translate("SendEditList: Text ist NULL.\n",
						"SendEditList: Text is NULL.\n"));
		return;
	}

	if(strlen(Text) >= 4000){
		error(Translate("SendEditList: Text ist zu lang.\n",
						"SendEditList: Text is too long.\n"));
		return;
	}

	SendByte(Connection, SV_CMD_EDIT_LIST);
	SendByte(Connection, Type);
	SendQuad(Connection, ID);
	SendString(Connection, Text);
	FinishSendData(Connection);
}

void SendPlayerData(TConnection *Connection){
	if(!BeginSendData(Connection)){
		return;
	}

	TPlayer *Player = Connection->GetPlayer();
	if(Player == NULL){
		error(Translate("SendStatus: Zu dieser Verbindung gehört kein Spieler.\n",
						"SendStatus: No player belongs to this connection.\n"));
		return;
	}

	// TODO(fusion): I don't even think skills can even be NULL.
	int Level = 0;
	int LevelPercent = 0;
	int Experience = 0;
	int MagicLevel = 0;
	int MagicLevelPercent = 0;
	int HitPoints = 0;
	int MaxHitPoints = 0;
	int ManaPoints = 0;
	int MaxManaPoints = 0;
	int Capacity = 0;
	int SoulPoints = 0;

	if(Player->Skills[SKILL_LEVEL] != NULL){
		Level = Player->Skills[SKILL_LEVEL]->Get();
		LevelPercent = Player->Skills[SKILL_LEVEL]->GetProgress();
		Experience = Player->Skills[SKILL_LEVEL]->Exp;
	}

	if(Player->Skills[SKILL_MAGIC_LEVEL] != NULL){
		MagicLevel = Player->Skills[SKILL_MAGIC_LEVEL]->Get();
		MagicLevelPercent = Player->Skills[SKILL_MAGIC_LEVEL]->GetProgress();
	}

	if(Player->Skills[SKILL_HITPOINTS] != NULL){
		HitPoints = Player->Skills[SKILL_HITPOINTS]->Get();
		MaxHitPoints = Player->Skills[SKILL_HITPOINTS]->Max;
	}

	if(Player->Skills[SKILL_MANA] != NULL){
		ManaPoints = Player->Skills[SKILL_MANA]->Get();
		MaxManaPoints = Player->Skills[SKILL_MANA]->Max;
	}

	if(Player->Skills[SKILL_CARRY_STRENGTH] != NULL
			&& !CheckRight(Player->ID, ZERO_CAPACITY)){
		int InventoryWeight = GetInventoryWeight(Player->ID);
		int MaxWeight = Player->Skills[SKILL_CARRY_STRENGTH]->Get() * 100;
		Capacity = (MaxWeight - InventoryWeight) / 100;
	}

	if(Player->Skills[SKILL_SOUL] != NULL){
		SoulPoints = Player->Skills[SKILL_SOUL]->Get();
	}

	SendByte(Connection, SV_CMD_PLAYER_DATA);
	SendWord(Connection, (uint16)HitPoints);
	SendWord(Connection, (uint16)MaxHitPoints);
	SendWord(Connection, (uint16)Capacity);
	SendQuad(Connection, (uint32)Experience);
	SendWord(Connection, (uint16)Level);
	SendByte(Connection, (uint8)LevelPercent);
	SendWord(Connection, (uint16)ManaPoints);
	SendWord(Connection, (uint16)MaxManaPoints);
	SendByte(Connection, (uint8)MagicLevel);
	SendByte(Connection, (uint8)MagicLevelPercent);
	SendByte(Connection, (uint8)SoulPoints);
	FinishSendData(Connection);
}

void SendPlayerSkills(TConnection *Connection){
	if(!BeginSendData(Connection)){
		return;
	}

	TPlayer *Player = Connection->GetPlayer();
	if(Player == NULL){
		error(Translate("SendPlayerSkills: Zu dieser Verbindung gehört kein Spieler.\n",
						"SendPlayerSkills: No player belongs to this connection.\n"));
		return;
	}

	// TODO(fusion): I don't even think skills can even be NULL.
	int FistLevel = 0;
	int FistPercent = 0;
	int ClubLevel = 0;
	int ClubPercent = 0;
	int SwordLevel = 0;
	int SwordPercent = 0;
	int AxeLevel = 0;
	int AxePercent = 0;
	int DistanceLevel = 0;
	int DistancePercent = 0;
	int ShieldingLevel = 0;
	int ShieldingPercent = 0;
	int FishingLevel = 0;
	int FishingPercent = 0;

	if(Player->Skills[SKILL_FIST] != NULL){
		FistLevel = Player->Skills[SKILL_FIST]->Get();
		FistPercent = Player->Skills[SKILL_FIST]->GetProgress();
	}

	if(Player->Skills[SKILL_CLUB] != NULL){
		ClubLevel = Player->Skills[SKILL_CLUB]->Get();
		ClubPercent = Player->Skills[SKILL_CLUB]->GetProgress();
	}

	if(Player->Skills[SKILL_SWORD] != NULL){
		SwordLevel = Player->Skills[SKILL_SWORD]->Get();
		SwordPercent = Player->Skills[SKILL_SWORD]->GetProgress();
	}

	if(Player->Skills[SKILL_AXE] != NULL){
		AxeLevel = Player->Skills[SKILL_AXE]->Get();
		AxePercent = Player->Skills[SKILL_AXE]->GetProgress();
	}

	if(Player->Skills[SKILL_DISTANCE] != NULL){
		DistanceLevel = Player->Skills[SKILL_DISTANCE]->Get();
		DistancePercent = Player->Skills[SKILL_DISTANCE]->GetProgress();
	}

	if(Player->Skills[SKILL_SHIELDING] != NULL){
		ShieldingLevel = Player->Skills[SKILL_SHIELDING]->Get();
		ShieldingPercent = Player->Skills[SKILL_SHIELDING]->GetProgress();
	}

	if(Player->Skills[SKILL_FISHING] != NULL){
		FishingLevel = Player->Skills[SKILL_FISHING]->Get();
		FishingPercent = Player->Skills[SKILL_FISHING]->GetProgress();
	}

	SendByte(Connection, SV_CMD_PLAYER_SKILLS);
	SendByte(Connection, (uint8)FistLevel);
	SendByte(Connection, (uint8)FistPercent);
	SendByte(Connection, (uint8)ClubLevel);
	SendByte(Connection, (uint8)ClubPercent);
	SendByte(Connection, (uint8)SwordLevel);
	SendByte(Connection, (uint8)SwordPercent);
	SendByte(Connection, (uint8)AxeLevel);
	SendByte(Connection, (uint8)AxePercent);
	SendByte(Connection, (uint8)DistanceLevel);
	SendByte(Connection, (uint8)DistancePercent);
	SendByte(Connection, (uint8)ShieldingLevel);
	SendByte(Connection, (uint8)ShieldingPercent);
	SendByte(Connection, (uint8)FishingLevel);
	SendByte(Connection, (uint8)FishingPercent);
	FinishSendData(Connection);
}

void SendPlayerState(TConnection *Connection, uint8 State){
	if(!BeginSendData(Connection)){
		return;
	}

	SendByte(Connection, SV_CMD_PLAYER_STATE);
	SendByte(Connection, State);
	FinishSendData(Connection);
}

void SendClearTarget(TConnection *Connection){
	if(!BeginSendData(Connection)){
		return;
	}

	SendByte(Connection, SV_CMD_CLEAR_TARGET);
	FinishSendData(Connection);
}

void SendTalk(TConnection *Connection, uint32 StatementID,
		const char *Sender, int Mode, const char *Text, int Data){
	if(Mode != TALK_PRIVATE_MESSAGE
			&& Mode != TALK_GAMEMASTER_REQUEST
			&& Mode != TALK_GAMEMASTER_ANSWER
			&& Mode != TALK_PLAYER_ANSWER
			&& Mode != TALK_GAMEMASTER_BROADCAST
			&& Mode != TALK_GAMEMASTER_MESSAGE){
		error(Translate("SendTalk (-): Ungültiger Modus %d.\n",
						"SendTalk (-): Invalid mode %d.\n"), Mode);
		return;
	}

	if(Sender == NULL){
		error(Translate("SendTalk (-): Sender ist NULL.\n",
						"SendTalk (-): Sender is NULL.\n"));
		return;
	}

	if(Text == NULL){
		error(Translate("SendTalk (-): Text ist NULL.\n",
						"SendTalk (-): Text is NULL.\n"));
		return;
	}

	if(!BeginSendData(Connection)){
		return;
	}

	SendByte(Connection, SV_CMD_TALK);
	SendQuad(Connection, StatementID);
	SendString(Connection, Sender);
	SendByte(Connection, (uint8)Mode);
	if(Mode == TALK_GAMEMASTER_REQUEST){
		SendQuad(Connection, (uint32)Data);
	}
	SendString(Connection, Text);
	FinishSendData(Connection);
}

void SendTalk(TConnection *Connection, uint32 StatementID,
		const char *Sender, int Mode, int Channel, const char *Text){
	if(Mode != TALK_CHANNEL_CALL
			&& Mode != TALK_GAMEMASTER_CHANNELCALL
			&& Mode != TALK_HIGHLIGHT_CHANNELCALL
			&& Mode != TALK_ANONYMOUS_CHANNELCALL){
		error(Translate("SendTalk (C): Ungültiger Modus %d.\n",
						"SendTalk (C): Invalid mode %d.\n"), Mode);
		return;
	}

	if(Sender == NULL){
		error(Translate("SendTalk (C): Sender ist NULL.\n",
						"SendTalk (C): Sender is NULL.\n"));
		return;
	}

	if(Text == NULL){
		error(Translate("SendTalk (C): Text ist NULL.\n",
						"SendTalk (C): Text is NULL.\n"));
		return;
	}

	if(!BeginSendData(Connection)){
		return;
	}

	SendByte(Connection, SV_CMD_TALK);
	SendQuad(Connection, StatementID);
	if(Mode != TALK_ANONYMOUS_CHANNELCALL){
		SendString(Connection, Sender);
	}else{
		SendString(Connection, "");
	}
	SendByte(Connection, (uint8)Mode);
	SendWord(Connection, (uint16)Channel);
	SendString(Connection, Text);
	FinishSendData(Connection);
}

void SendTalk(TConnection *Connection, uint32 StatementID,
		const char *Sender, int Mode, int x, int y, int z, const char *Text){
	if(Mode != TALK_SAY
			&& Mode != TALK_WHISPER
			&& Mode != TALK_YELL
			&& Mode != TALK_ANIMAL_LOW
			&& Mode != TALK_ANIMAL_LOUD){
		error(Translate("SendTalk: Ungültiger Modus %d.\n",
						"SendTalk: Invalid mode %d.\n"), Mode);
		return;
	}

	if(Sender == NULL){
		error(Translate("SendTalk (K): Sender ist NULL.\n",
						"SendTalk (K): Sender is NULL.\n"));
		return;
	}

	if(Text == NULL){
		error(Translate("SendTalk (K): Text ist NULL.\n",
						"SendTalk (K): Text is NULL.\n"));
		return;
	}

	if(!BeginSendData(Connection)){
		return;
	}

	SendByte(Connection, SV_CMD_TALK);
	SendQuad(Connection, StatementID);
	SendString(Connection, Sender);
	SendByte(Connection, (uint8)Mode);
	SendWord(Connection, (uint16)x);
	SendWord(Connection, (uint16)y);
	SendByte(Connection, (uint8)z);
	SendString(Connection, Text);
	FinishSendData(Connection);
}

void SendChannels(TConnection *Connection){
	if(!BeginSendData(Connection)){
		return;
	}

	TPlayer *Player = Connection->GetPlayer();
	if(Player == NULL){
		return;
	}

	int Channels = GetNumberOfChannels();
	bool OwnChannel = MayOpenChannel(Player->ID);
	int NumAvailable = 0;
	bool *Available = (bool*)alloca(Channels * sizeof(bool));
	for(int Channel = 0; Channel < Channels; Channel += 1){
		Available[Channel] = ChannelAvailable(Channel, Player->ID);
		if(Available[Channel]){
			NumAvailable += 1;
		}
	}

	if(OwnChannel){
		NumAvailable += 1;
	}

	// TODO(fusion): The original function wouldn't limit the maximum number
	// of available channels. It is unlikely any player would be invited to
	// more channels than an uint8 can hold but there is no reason not to
	// maintain packet coherency. If this value wraps and we still add all
	// available channels, the whole packet will get dropped and I'm not sure
	// but the client may also assert.
	if(NumAvailable > UINT8_MAX){
		NumAvailable = UINT8_MAX;
	}

	SendByte(Connection, SV_CMD_CHANNELS);
	SendByte(Connection, (uint8)NumAvailable);

	if(OwnChannel){
		SendWord(Connection, 0xFFFF);
		SendString(Connection, "Private Chat Channel");
		NumAvailable -= 1;
	}

	for(int Channel = 0; Channel < Channels; Channel += 1){
		if(NumAvailable <= 0){
			break;
		}

		if(Available[Channel]){
			SendWord(Connection, (uint16)Channel);
			SendString(Connection, GetChannelName(Channel, Player->ID));
			NumAvailable -= 1;
		}
	}

	FinishSendData(Connection);
}

void SendOpenChannel(TConnection *Connection, int Channel){
	if(!BeginSendData(Connection)){
		return;
	}

	if(Channel == CHANNEL_RULEVIOLATIONS){
		error(Translate("SendOpenChannel: Kanal ist GM-Request-Queue.\n",
						"SendOpenChannel: Channel is GM request queue"));
		return;
	}

	if(Channel < 0 || Channel >= GetNumberOfChannels()){
		error(Translate("SendOpenChannel: Ungültiger Kanal %d.\n",
						"SendOpenChannel: Invalid channel %d.\n"), Channel);
		return;
	}

	TPlayer *Player = Connection->GetPlayer();
	if(Player == NULL){
		return;
	}

	SendByte(Connection, SV_CMD_OPEN_CHANNEL);
	SendWord(Connection, (uint16)Channel);
	SendString(Connection, GetChannelName(Channel, Player->ID));
	FinishSendData(Connection);
}

void SendPrivateChannel(TConnection *Connection, const char *Name){
	if(!BeginSendData(Connection)){
		return;
	}

	if(Name == NULL){
		error(Translate("SendPrivateChannel: Name ist NULL.\n",
						"SendPrivateChannel: Name is NULL.\n"));
		return;
	}

	SendByte(Connection, SV_CMD_PRIVATE_CHANNEL);
	SendString(Connection, Name);
	FinishSendData(Connection);
}

void SendOpenRequestQueue(TConnection *Connection){
	if(!BeginSendData(Connection)){
		return;
	}

	SendByte(Connection, SV_CMD_OPEN_REQUEST_QUEUE);
	SendWord(Connection, (uint16)CHANNEL_RULEVIOLATIONS);
	FinishSendData(Connection);
}

void SendDeleteRequest(TConnection *Connection, const char *Name){
	if(!BeginSendData(Connection)){
		return;
	}

	if(Name == NULL){
		error(Translate("SendDeleteRequest: Name ist NULL.\n",
						"SendDeleteRequest: Name is NULL.\n"));
		return;
	}

	SendByte(Connection, SV_CMD_DELETE_REQUEST);
	SendString(Connection, Name);
	FinishSendData(Connection);
}

void SendFinishRequest(TConnection *Connection, const char *Name){
	if(!BeginSendData(Connection)){
		return;
	}

	if(Name == NULL){
		error(Translate("SendFinishRequest: Name ist NULL.\n",
						"SendFinishRequest: Name is NULL.\n"));
		return;
	}

	SendByte(Connection, SV_CMD_FINISH_REQUEST);
	SendString(Connection, Name);
	FinishSendData(Connection);
}

void SendCloseRequest(TConnection *Connection){
	if(!BeginSendData(Connection)){
		return;
	}

	SendByte(Connection, SV_CMD_CLOSE_REQUEST);
	FinishSendData(Connection);
}

void SendOpenOwnChannel(TConnection *Connection, int Channel){
	if(!BeginSendData(Connection)){
		return;
	}

	if(Channel < 0 || Channel >= GetNumberOfChannels()){
		error(Translate("SendOpenOwnChannel: Ungültiger Kanal %d.\n",
						"SendOpenOwnChannel: Invalid channel %d.\n"), Channel);
		return;
	}

	TPlayer *Player = Connection->GetPlayer();
	if(Player == NULL){
		return;
	}

	SendByte(Connection, SV_CMD_OPEN_OWN_CHANNEL);
	SendWord(Connection, (uint16)Channel);
	SendString(Connection, GetChannelName(Channel, Player->ID));
	FinishSendData(Connection);
}

void SendCloseChannel(TConnection *Connection, int Channel){
	if(!BeginSendData(Connection)){
		return;
	}

	SendByte(Connection, SV_CMD_CLOSE_CHANNEL);
	SendWord(Connection, (uint16)Channel);
	FinishSendData(Connection);
}

void SendMessage(TConnection *Connection, int Mode, const char *Text, ...){
	if(Mode != TALK_ADMIN_MESSAGE
			&& Mode != TALK_EVENT_MESSAGE
			&& Mode != TALK_LOGIN_MESSAGE
			&& Mode != TALK_STATUS_MESSAGE
			&& Mode != TALK_INFO_MESSAGE
			&& Mode != TALK_FAILURE_MESSAGE){
		error(Translate("SendMessage: Ungültiger Modus %d.\n",
						"SendMessage: Invalid mode %d.\n"), Mode);
		return;
	}

	if(!BeginSendData(Connection)){
		return;
	}

	char Buffer[4096];
	va_list ap;
	va_start(ap, Text);
	vsnprintf(Buffer, sizeof(Buffer), Text, ap);
	va_end(ap);

	SendByte(Connection, SV_CMD_MESSAGE);
	SendByte(Connection, (uint8)Mode);
	SendString(Connection, Buffer);
	FinishSendData(Connection);
}

void SendSnapback(TConnection *Connection){
	if(!BeginSendData(Connection)){
		return;
	}

	// TODO(fusion): Sometimes we check, sometimes we don't.
	TPlayer *Player = Connection->GetPlayer();
	if(Player == NULL){
		return;
	}

	SendByte(Connection, SV_CMD_SNAPBACK);
	SendByte(Connection, (uint8)Player->Direction);
	FinishSendData(Connection);
}

void SendOutfit(TConnection *Connection){
	if(!BeginSendData(Connection)){
		return;
	}

	TPlayer *Player = Connection->GetPlayer();
	if(Player == NULL){
		return;
	}

	uint16 FirstOutfit = (Player->Sex == 1) ? 128 : 136;
	uint16 LastOutfit  = (Player->Sex == 1) ? 131 : 139;
	if(CheckRight(Player->ID, PREMIUM_ACCOUNT)){
		LastOutfit += 3;
	}

	SendByte(Connection, SV_CMD_OUTFIT);
	SendOutfit(Connection, Player->OrgOutfit);
	SendWord(Connection, FirstOutfit);
	SendWord(Connection, LastOutfit);
	FinishSendData(Connection);
}

void SendBuddyData(TConnection *Connection, uint32 CharacterID, const char *Name, bool Online){
	if(!BeginSendData(Connection)){
		return;
	}

	if(Name == NULL){
		return;
	}

	SendByte(Connection, SV_CMD_BUDDY_DATA);
	SendQuad(Connection, CharacterID);
	SendString(Connection, Name);
	SendByte(Connection, (Online ? 1 : 0));
	FinishSendData(Connection);
}

void SendBuddyStatus(TConnection *Connection, uint32 CharacterID, bool Online){
	if(!BeginSendData(Connection)){
		return;
	}

	if(Online){
		SendByte(Connection, SV_CMD_BUDDY_ONLINE);
	}else{
		SendByte(Connection, SV_CMD_BUDDY_OFFLINE);
	}
	SendQuad(Connection, CharacterID);
	FinishSendData(Connection);
}

void BroadcastMessage(int Mode, const char *Text, ...){
	char Message[1024];
	va_list ap;
	va_start(ap, Text);
	vsnprintf(Message, sizeof(Message), Text, ap);
	va_end(ap);

	TConnection *Connection = GetFirstConnection();
	while(Connection != NULL){
		if(Connection->Live()){
			SendMessage(Connection, Mode, Message);
		}
		Connection = GetNextConnection();
	}
}

void CreateGamemasterRequest(const char *Name, const char *Text){
	if(Name == NULL){
		error(Translate("CreateGamemasterRequest: Name ist NULL.\n",
						"CreateGamemasterRequest: Name is NULL.\n"));
		return;
	}

	if(Text == NULL){
		error(Translate("CreateGamemasterRequest: Name ist NULL.\n",
						"CreateGamemasterRequest: Name is NULL.\n"));
		return;
	}

	TConnection *Connection = GetFirstConnection();
	while(Connection != NULL){
		if(Connection->Live()){
			TPlayer *Player = Connection->GetPlayer();
			if(Player != NULL && ChannelSubscribed(CHANNEL_RULEVIOLATIONS, Player->ID)){
				SendTalk(Connection, 0, Name, TALK_GAMEMASTER_REQUEST, Text, 0);
			}
		}
		Connection = GetNextConnection();
	}
}

void DeleteGamemasterRequest(const char *Name){
	if(Name == NULL){
		error(Translate("DeleteGamemasterRequest: Name ist NULL.\n",
						"DeleteGamemasterRequest: Name is NULL.\n"));
		return;
	}

	TConnection *Connection = GetFirstConnection();
	while(Connection != NULL){
		if(Connection->Live()){
			TPlayer *Player = Connection->GetPlayer();
			if(Player != NULL && ChannelSubscribed(CHANNEL_RULEVIOLATIONS, Player->ID)){
				SendDeleteRequest(Connection, Name);
			}
		}
		Connection = GetNextConnection();
	}
}

void InitSending(void){
	FirstSendingConnection = NULL;
}

void ExitSending(void){
	// no-op
}
