#include "map.hh"
#include "containers.hh"
#include "config.hh"
#include "enums.hh"
#include "houses.hh"
#include "script.hh"

#include <dirent.h>

int SectorXMin;
int SectorXMax;
int SectorYMin;
int SectorYMax;
int SectorZMin;
int SectorZMax;
int RefreshedCylinders;
int NewbieStartPositionX;
int NewbieStartPositionY;
int NewbieStartPositionZ;
int VeteranStartPositionX;
int VeteranStartPositionY;
int VeteranStartPositionZ;

static int OBCount;
static matrix3d<TSector*> *Sector;
static TObjectBlock **ObjectBlock;
static TObject *FirstFreeObject;
static TObject **HashTableData;
static uint8 *HashTableType;
static uint32 HashTableSize;
static uint32 HashTableMask;
static uint32 HashTableFree;
static uint32 ObjectCounter;

static vector<TCronEntry> CronEntry(0, 256, 256);
static int CronHashTable[2047];
static int CronEntries;

static vector<TDepotInfo> DepotInfo(0, 4, 5);
static vector<TMark> Mark(0, 4, 5);
static int Marks;

static TDynamicWriteBuffer HelpBuffer(KB(64));

// Object
// =============================================================================
bool Object::exists(void){
	if(*this == NONE){
		return false;
	}

	uint32 EntryIndex = this->ObjectID & HashTableMask;
	if(HashTableType[EntryIndex] == STATUS_SWAPPED){
		UnswapSector((uintptr)HashTableData[EntryIndex]);
	}

	return HashTableType[EntryIndex] == STATUS_LOADED
		&& HashTableData[EntryIndex]->ObjectID == this->ObjectID;
}

ObjectType Object::getObjectType(void){
	return AccessObject(*this)->Type;
}

void Object::setObjectType(ObjectType Type){
	AccessObject(*this)->Type = Type;
}

Object Object::getNextObject(void){
	return AccessObject(*this)->NextObject;
}

void Object::setNextObject(Object NextObject){
	AccessObject(*this)->NextObject = NextObject;
}

Object Object::getContainer(void){
	return AccessObject(*this)->Container;
}

void Object::setContainer(Object Container){
	AccessObject(*this)->Container = Container;
}

uint32 Object::getCreatureID(void){
	// TODO(fusion): We call `AccessObject` once in `getObjectType` then again
	// after checking the TypeID, when we could call it once to check both type
	// and access `Attributes[1]`.

	if(!this->getObjectType().isCreatureContainer()){
		error(Translate("Object::getCreatureID: Objekt ist keine Kreatur.\n",
						"Object::getCreatureID: Object is not a creature.\n"));
		return 0;
	}

	return AccessObject(*this)->Attributes[1];
}

uint32 Object::getAttribute(INSTANCEATTRIBUTE Attribute){
	ObjectType ObjType = this->getObjectType();
	int AttributeOffset = ObjType.getAttributeOffset(Attribute);
	if(AttributeOffset == -1){
		error(Translate("Object::getAttribute: Flag für Attribut %d bei Objekttyp %d nicht gesetzt.\n",
						"Object::getAttribute: Flag for attribute %d not set for object type %d.\n"),
				Attribute, ObjType.TypeID);
		return 0;
	}

	if(AttributeOffset < 0 || AttributeOffset >= NARRAY(TObject::Attributes)){
		error(Translate("Object::getAttribute: Ungültiger Offset %d für Attribut %d bei Objekttyp %d.\n",
						"Object::getAttribute: Invalid offset %d for attribute %d on object type %d.\n"),
				AttributeOffset, Attribute, ObjType.TypeID);
		return 0;
	}

	return AccessObject(*this)->Attributes[AttributeOffset];
}

void Object::setAttribute(INSTANCEATTRIBUTE Attribute, uint32 Value){
	ObjectType ObjType = this->getObjectType();
	int AttributeOffset = ObjType.getAttributeOffset(Attribute);
	if(AttributeOffset == -1){
		error(Translate("Object::setAttribute: Flag für Attribut %d bei Objekttyp %d nicht gesetzt.\n",
						"Object::setAttribute: Flag for attribute %d not set for object type.\n"),
				Attribute, ObjType.TypeID);
		return;
	}

	if(AttributeOffset < 0 || AttributeOffset >= NARRAY(TObject::Attributes)){
		error(Translate("Object::setAttribute: Ungültiger Offset %d für Attribut %d bei Objekttyp %d.\n",
						"Object::setAttribute: Invalid offset %d for attribute %d on object type %d.\n"),
				AttributeOffset, Attribute, ObjType.TypeID);
		return;
	}

	if(Value == 0){
		if(Attribute == AMOUNT || Attribute == POOLLIQUIDTYPE || Attribute == CHARGES){
			Value = 1;
		}else if(Attribute == REMAININGUSES){
			Value = ObjType.getAttribute(TOTALUSES);
		}
	}

	AccessObject(*this)->Attributes[AttributeOffset] = Value;
}

// Cron Management
// =============================================================================
static void CronMove(int Destination, int Source){
	TCronEntry *DestEntry = CronEntry.at(Destination);
	*DestEntry = *CronEntry.at(Source);

	if(DestEntry->Next != -1){
		CronEntry.at(DestEntry->Next)->Previous = Destination;
	}

	if(DestEntry->Previous != -1){
		CronEntry.at(DestEntry->Previous)->Next = Destination;
	}else{
		CronHashTable[DestEntry->Obj.ObjectID % NARRAY(CronHashTable)] = Destination;
	}
}

static void CronHeapify(int Position){
	while(Position > 1){
		int Parent = Position / 2;
		TCronEntry *CurrentEntry = CronEntry.at(Position);
		TCronEntry *ParentEntry = CronEntry.at(Parent);
		if(ParentEntry->RoundNr <= CurrentEntry->RoundNr){
			break;
		}

		// NOTE(fusion): This is emulating a swap, using position 0 as the
		// temporary value. It is needed to maintain hash table links valid.
		CronMove(0, Position);
		CronMove(Position, Parent);
		CronMove(Parent, 0);

		Position = Parent;
	}

	// IMPORTANT(fusion): This is different from `priority_queue::deleteMin` as
	// the last element is still in the heap and needs to be considered.
	int Last = CronEntries;
	while(true){
		int Smallest = Position * 2;
		if(Smallest > Last){
			break;
		}

		TCronEntry *SmallestEntry = CronEntry.at(Smallest);
		if((Smallest + 1) <= Last){
			TCronEntry *OtherEntry = CronEntry.at(Smallest + 1);
			if(OtherEntry->RoundNr < SmallestEntry->RoundNr){
				SmallestEntry = OtherEntry;
				Smallest += 1;
			}
		}

		TCronEntry *CurrentEntry = CronEntry.at(Position);
		if(CurrentEntry->RoundNr <= SmallestEntry->RoundNr){
			break;
		}

		// NOTE(fusion): Same in the first loop.
		CronMove(0, Position);
		CronMove(Position, Smallest);
		CronMove(Smallest, 0);

		Position = Smallest;
	}
}

static void CronSet(Object Obj, uint32 Delay){
	if(!Obj.exists()){
		error(Translate("CronSet: Übergebenes Objekt existiert nicht.\n",
						"CronSet: Passed object does not exist.\n"));
		return;
	}

	// TODO(fusion): We don't check if the object is already in the table.

	CronEntries += 1;

	int Position = CronEntries;
	TCronEntry *Entry = CronEntry.at(Position);
	Entry->Obj = Obj;
	Entry->RoundNr = RoundNr + Delay;
	Entry->Previous = -1;
	Entry->Next = CronHashTable[Obj.ObjectID % NARRAY(CronHashTable)];
	CronHashTable[Obj.ObjectID % NARRAY(CronHashTable)] = Position;
	if(Entry->Next != -1){
		CronEntry.at(Entry->Next)->Previous = Position;
	}

	CronHeapify(Position);
}

static void CronDelete(int Position){
	if(Position < 1 || Position > CronEntries){
		error(Translate("CronDelete: Ungültige Position %d.\n",
						"CronDelete: Invalid position %d.\n"), Position);
		return;
	}

	TCronEntry *Entry = CronEntry.at(Position);

	if(Entry->Next != -1){
		CronEntry.at(Entry->Next)->Previous = Entry->Previous;
	}

	if(Entry->Previous != -1){
		CronEntry.at(Entry->Previous)->Next = Entry->Next;
	}else{
		CronHashTable[Entry->Obj.ObjectID % NARRAY(CronHashTable)] = Entry->Next;
	}

	int Last = CronEntries;
	CronEntries -= 1;
	if(Position != Last){
		CronMove(Position, Last);
		CronHeapify(Position);
	}
}

Object CronCheck(void){
	Object Obj = NONE;
	if(CronEntries != 0){
		TCronEntry *Entry = CronEntry.at(1);
		if(Entry->RoundNr <= RoundNr){
			Obj = Entry->Obj;
		}
	}
	return Obj;
}

void CronExpire(Object Obj, int Delay){
	if(!Obj.exists()){
		error(Translate("CronExpire: Übergebenes Objekt existiert nicht.\n",
						"CronExpire: Passed object does not exist.\n"));
		return;
	}

	ObjectType ObjType = Obj.getObjectType();
	if(ObjType.getFlag(EXPIRE)){
		if(Delay == -1){
			CronSet(Obj, ObjType.getAttribute(TOTALEXPIRETIME));
		}else{
			CronSet(Obj, (uint32)Delay);
		}
	}
}

void CronChange(Object Obj, int NewDelay){
	if(!Obj.exists()){
		error(Translate("CronChange: Übergebenes Objekt existiert nicht.\n",
						"CronChange: Passed object does not exist.\n"));
		return;
	}

	int Position = CronHashTable[Obj.ObjectID % NARRAY(CronHashTable)];
	while(Position != -1){
		TCronEntry *Entry = CronEntry.at(Position);
		if(Entry->Obj == Obj){
			Entry->RoundNr = RoundNr + NewDelay;
			CronHeapify(Position);
			return;
		}
		Position = Entry->Next;
	}

	error(Translate("CronChange: Objekt ist nicht im Cron-System eingetragen.\n",
					"CronChange: Object is not registered in the cron system.\n"));
}

uint32 CronInfo(Object Obj, bool Delete){
	if(!Obj.exists()){
		error(Translate("CronInfo: Übergebenes Objekt existiert nicht.\n",
						"CronInfo: Passed object does not exist.\n"));
		return 0;
	}

	int Position = CronHashTable[Obj.ObjectID % NARRAY(CronHashTable)];
	while(Position != -1){
		TCronEntry *Entry = CronEntry.at(Position);
		if(Entry->Obj == Obj){
			uint32 Remaining = 1;
			if(Entry->RoundNr > RoundNr){
				Remaining = Entry->RoundNr - RoundNr;
			}
			if(Delete){
				CronDelete(Position);
			}
			return Remaining;
		}
		Position = Entry->Next;
	}

	error(Translate("CronInfo: Objekt ist nicht im Cron-System eingetragen.\n",
					"CronInfo: Object is not registered in the cron system.\n"));
	return 0;
}

uint32 CronStop(Object Obj){
	if(!Obj.exists()){
		error(Translate("CronStop: Übergebenes Objekt existiert nicht.\n",
						"CronStop: Passed object does not exist.\n"));
		return 0;
	}

	return CronInfo(Obj, true);
}

// Map Management
// =============================================================================
static void ReadMapConfig(void){
	OBCount = 0xA0000;
	SectorXMin = 1000;
	SectorXMax = 1015;
	SectorYMin = 1000;
	SectorYMax = 1015;
	SectorZMin = 0;
	SectorZMax = 15;
	RefreshedCylinders = 1;
	NewbieStartPositionX = 0;
	NewbieStartPositionY = 0;
	NewbieStartPositionZ = 0;
	VeteranStartPositionX = 0;
	VeteranStartPositionY = 0;
	VeteranStartPositionZ = 0;
	HashTableSize = 0x100000;
	Marks = 0;

	char FileName[4096];
	snprintf(FileName, sizeof(FileName), "%s/map.dat", DATAPATH);

	TReadScriptFile Script;
	Script.open(FileName);
	while(true){
		Script.nextToken();
		if(Script.Token == ENDOFFILE){
			Script.close();
			break;
		}

		char Identifier[MAX_IDENT_LENGTH];
		strcpy(Identifier, Script.getIdentifier());
		Script.readSymbol('=');

		if(strcmp(Identifier, "sectorxmin") == 0){
			SectorXMin = Script.readNumber();
		}else if(strcmp(Identifier, "sectorxmax") == 0){
			SectorXMax = Script.readNumber();
		}else if(strcmp(Identifier, "sectorymin") == 0){
			SectorYMin = Script.readNumber();
		}else if(strcmp(Identifier, "sectorymax") == 0){
			SectorYMax = Script.readNumber();
		}else if(strcmp(Identifier, "sectorzmin") == 0){
			SectorZMin = Script.readNumber();
		}else if(strcmp(Identifier, "sectorzmax") == 0){
			SectorZMax = Script.readNumber();
		}else if(strcmp(Identifier, "refreshedcylinders") == 0){
			RefreshedCylinders = Script.readNumber();
		}else if(strcmp(Identifier, "objects") == 0){
			HashTableSize = (uint32)Script.readNumber();
		}else if(strcmp(Identifier, "cachesize") == 0){
			OBCount = Script.readNumber();
		}else if(strcmp(Identifier, "depot") == 0){
			int DepotIndex = 0;
			TDepotInfo TempInfo = {};
			Script.readSymbol('(');
			DepotIndex = Script.readNumber();
			Script.readSymbol(',');
			const char *Town = Script.readString();
			if(strlen(Town) >= NARRAY(TempInfo.Town)){
				Script.error("town name too long");
			}
			strcpy(TempInfo.Town, Town);
			Script.readSymbol(',');
			TempInfo.Size = Script.readNumber();
			Script.readSymbol(')');
			*DepotInfo.at(DepotIndex) = TempInfo;
		}else if(strcmp(Identifier, "mark") == 0){
			TMark TempMark = {};
			Script.readSymbol('(');
			const char *Name = Script.readString();
			if(strlen(Name) >= NARRAY(TempMark.Name)){
				Script.error("mark name too long");
			}
			strcpy(TempMark.Name, Name);
			Script.readSymbol(',');
			Script.readCoordinate(&TempMark.x, &TempMark.y, &TempMark.z);
			Script.readSymbol(')');
			*Mark.at(Marks) = TempMark;
			Marks += 1;
		}else if(strcmp(Identifier, "newbiestart") == 0){
			 Script.readCoordinate(
					&NewbieStartPositionX,
					&NewbieStartPositionY,
					&NewbieStartPositionZ);
		}else if(strcmp(Identifier, "veteranstart") == 0){
			 Script.readCoordinate(
					&VeteranStartPositionX,
					&VeteranStartPositionY,
					&VeteranStartPositionZ);
		}else{
			// TODO(fusion):
			//error("Unknown map configuration key \"%s\"", Identifier);
		}
	}

	// NOTE(fusion): If each sector is 32 x 32 tiles and the whole world is
	// 65535 x 65535 tiles, then the maximum number of sectors is 65535 / 32
	// which is the 2047 used below. We should probably define these contants.
	//	Notice that sectors at the edge of the XY-plane are also considered
	// invalid, probably to add some buffer to avoid wrapping or other types
	// of problems.

	if(SectorXMin <= 0){
		throw "illegal value for SectorXMin";
	}

	if(SectorXMax >= 2047){
		throw "illegal value for SectorXMax";
	}

	if(SectorYMin <= 0){
		throw "illegal value for SectorYMin";
	}

	if(SectorYMax >= 2047){
		throw "illegal value for SectorYMax";
	}

	if(SectorZMin < 0){
		throw "illegal value for SectorZMin";
	}

	if(SectorZMax > 15){
		throw "illegal value for SectorZMax";
	}

	if(SectorXMin > SectorXMax){
		throw "SectorXMin is greater than SectorXMax";
	}

	if(SectorYMin > SectorYMax){
		throw "SectorYMin is greater than SectorYMax";
	}

	if(SectorZMin > SectorZMax){
		throw "SectorZMin is greater than SectorZMax";
	}

	// TODO(fusion): Just align up from whatever value we got? And use `ObjectsPerBlock`?
	if(OBCount % 32768 != 0){
		throw "CacheSize must be a multiple of 32768";
	}

	if(OBCount <= 0){
		throw "illegal value for CacheSize";
	}

	OBCount /= 32768;

	if(HashTableSize <= 0){
		throw "illegal value for Objects";
	}

	if(!ISPOW2(HashTableSize)){
		throw "Objects must be a power of 2";
	}

	if(NewbieStartPositionX == 0){
		throw "no start position for newbies specified";
	}

	if(VeteranStartPositionX == 0){
		throw "no start position for veterans specified";
	}
}

static void ResizeHashTable(void){
	uint32 OldSize = HashTableSize;
	uint32 NewSize = OldSize * 2;
	ASSERT(ISPOW2(OldSize));
	ASSERT(NewSize > OldSize);

	// TODO(fusion): See note below.
	error("FATAL ERROR in ResizeHashTable: Resizing the object hash table is"
			" currently disabled. You may increase `Objects` in the map config"
			" from %d to %d, to achieve the same effect.", OldSize, NewSize);
	abort();

	error(Translate("INFO: HashTabelle zu klein. Größe wird verdoppelt auf %d.\n",
					"INFO: HashTable too small. Size will be doubled to %d.\n"), NewSize);

	uint32 NewMask = NewSize - 1;
	TObject **NewData = (TObject**)malloc(NewSize * sizeof(TObject*));
	uint8 *NewType = (uint8*)malloc(NewSize * sizeof(uint8));
	memset(NewType, 0, NewSize * sizeof(uint8));

	// TODO(fusion): This rehash loop doesn't make a lot of sense. It wants to
	// access all existing objects but doing so would cause all sectors to be
	// swapped in at some time or another. This may be bad for performance but
	// the real problem is that `UnswapSector` may swap out some other sector
	// whose objects were already put into `NewType` and `NewData`, causing
	// multiple object ids to reference the same `TObject`.
	//	Looking at some of the logs included with this executable, it doesn't
	// look like this function was ever called which explains why it wasn't fixed
	// earlier.
	//	It would be possible to do this rehashing without swapping anything out,
	// if we stored ObjectID somewhere (maybe `HashTableData`). Doubling the size
	// of the table means there are two possible indices for each previous entry,
	// and we can only determine which one to actually move it with the ObjectID.

	NewType[0] = STATUS_PERMANENT;
	NewData[0] = HashTableData[0];
	for(uint32 i = 1; i < NewSize; i += 1){
		if(HashTableType[i] != STATUS_FREE){
			if(HashTableType[i] == STATUS_SWAPPED){
				UnswapSector((uintptr)HashTableData[i]);
			}

			if(HashTableType[i] == STATUS_LOADED){
				TObject *Entry = HashTableData[i];
				NewType[Entry->ObjectID & NewMask] = STATUS_LOADED;
				NewData[Entry->ObjectID & NewMask] = Entry;
			}else{
				error(Translate("ResizeHashTable: Fehler beim Reorganisieren der HashTabelle.\n",
								"ResizeHashTable: Error reorganizing the HashTable.\n"));
			}
		}
	}

	free(HashTableData);
	free(HashTableType);

	HashTableData = NewData;
	HashTableType = NewType;
	HashTableSize = NewSize;
	HashTableMask = NewMask;
	HashTableFree += (NewSize - OldSize);
}

static TObject *GetFreeObjectSlot(void){
	if(FirstFreeObject == NULL){
		SwapSector();
	}

	TObject *Entry = FirstFreeObject;
	if(Entry == NULL){
		error(Translate("GetFreeObjectSlot: Kein freier Platz mehr.\n",
						"GetFreeObjectSlot: No more free space.\n"));
		return NULL;
	}

	// NOTE(fusion): The next object pointer was originally stored in `Entry->NextObject.ObjectID`
	// which is a problem when compiling in 64 bits mode. For this reason, I've changed it to be
	// stored at the beggining of `TObject`.
	FirstFreeObject = *((TObject**)Entry);

	// TODO(fusion): Using `memset` here will trigger a compiler warning because `TObject` contains
	// a few `Object`s and I've made them non PODs by adding a few constructors.
	//memset(Entry, 0, sizeof(TObject));

	*Entry = TObject{};
	return Entry;
}

static void PutFreeObjectSlot(TObject *Entry){
	if(Entry == NULL){
		error(Translate("PutFreeObjectSlot: Entry ist NULL.\n",
						"PutFreeObjectSlot: Entry is NULL.\n"));
		return;
	}

	// NOTE(fusion): See note in `GetFreeObjectSlot`, just above.
	*((TObject**)Entry) = FirstFreeObject;
	FirstFreeObject = Entry;
}

void SwapObject(TWriteBinaryFile *File, Object Obj, uintptr FileNumber){
	ASSERT(Obj != NONE);

	// NOTE(fusion): Does it make sense to swap an object that isn't loaded? We
	// were originally calling `Object::exists` that would swap in the object's
	// sector if it was swapped out. We should probably have an assertion here.
	uint32 EntryIndex = Obj.ObjectID & HashTableMask;
	if(HashTableType[EntryIndex] != STATUS_LOADED){
		error("SwapObject: Object doesn't exist or is not currently loaded.\n");
		return;
	}

	TObject *Entry = HashTableData[EntryIndex];
	if(Entry->ObjectID != Obj.ObjectID){
		error(Translate("SwapObject: Übergebenes Objekt existiert nicht.\n",
						"SwapObject: Passed object does not exist.\n"));
		return;
	}

	File->writeBytes((const uint8*)Entry, sizeof(TObject));
	if(Entry->Type.getFlag(CONTAINER) || Entry->Type.getFlag(CHEST)){
		Object Current = Object(Obj.getAttribute(CONTENT));
		while(Current != NONE){
			Object Next = Current.getNextObject();
			SwapObject(File, Current, FileNumber);
			Current = Next;
		}
	}

	PutFreeObjectSlot(Entry);
	HashTableType[EntryIndex] = STATUS_SWAPPED;
	HashTableData[EntryIndex] = (TObject*)FileNumber;
}

void SwapSector(void){
	static uintptr FileNumber = 0;

	TSector *Oldest = NULL;
	int OldestSectorX = 0;
	int OldestSectorY = 0;
	int OldestSectorZ = 0;
	uint32 OldestTimeStamp = RoundNr + 1;

	ASSERT(Sector != NULL);
	for(int SectorZ = SectorZMin; SectorZ <= SectorZMax; SectorZ += 1)
	for(int SectorY = SectorYMin; SectorY <= SectorYMax; SectorY += 1)
	for(int SectorX = SectorXMin; SectorX <= SectorXMax; SectorX += 1){
		TSector *CurrentSector = *Sector->at(SectorX, SectorY, SectorZ);
		if(CurrentSector != NULL
				&& CurrentSector->Status == STATUS_LOADED
				&& CurrentSector->TimeStamp < OldestTimeStamp){
			Oldest = CurrentSector;
			OldestSectorX = SectorX;
			OldestSectorY = SectorY;
			OldestSectorZ = SectorZ;
			OldestTimeStamp = CurrentSector->TimeStamp;
		}
	}

	if(Oldest == NULL){
		error(Translate("FATAL ERROR in SwapSector: Es kann kein Sektor ausgelagert werden.\n",
						"FATAL ERROR in SwapSector: No sector can be swapped out.\n"));
		abort();
	}

	char FileName[4096];
	do{
		FileNumber += 1;
		if(FileNumber > 99999999){
			FileNumber = 1;
		}
		snprintf(FileName, sizeof(FileName), "%s/%08u.swp", SAVEPATH, (uint32)FileNumber);
	}while(FileExists(FileName));

	TWriteBinaryFile File;
	try{
		File.open(FileName);
		print(2, Translate("Lagere Sektor %d/%d/%d aus...\n",
						   "Storage sector %d/%d/%d...\n"), OldestSectorX, OldestSectorY, OldestSectorZ);
		File.writeQuad((uint32)OldestSectorX);
		File.writeQuad((uint32)OldestSectorY);
		File.writeQuad((uint32)OldestSectorZ);
		// TODO(fusion): I think tiles are stored in column major order but it doesn't
		// really matter as long as optimize for sequential access.
		for(int X = 0; X < 32; X += 1){
			for(int Y = 0; Y < 32; Y += 1){
				SwapObject(&File, Oldest->MapCon[X][Y], FileNumber);
			}
		}
		Oldest->Status = STATUS_SWAPPED;
		File.close();
	}catch(const char *str){
		error(Translate("FATAL ERROR in SwapSector: Kann Datei \"%s\" nicht anlegen.\n",
						"FATAL ERROR in SwapSector: Cannot create file \"%s\".\n"), FileName);
		error(Translate("# Fehler: %s\n",
						"# Error: %s\n"), str);
		abort();
	}
}

void UnswapSector(uintptr FileNumber){
	char FileName[4096];
	snprintf(FileName, sizeof(FileName), "%s/%08u.swp", SAVEPATH, (uint32)FileNumber);

	TReadBinaryFile File;
	try{
		File.open(FileName);
		int SectorX = (int)File.readQuad();
		int SectorY = (int)File.readQuad();
		int SectorZ = (int)File.readQuad();
		print(2, Translate("Lagere Sector %d/%d/%d ein...\n",
						   "Store sector %d/%d/%d...\n"), SectorX, SectorY, SectorZ);

		ASSERT(Sector != NULL);
		TSector *LoadingSector = *Sector->at(SectorX, SectorY, SectorZ);
		if(LoadingSector == NULL){
			error(Translate("UnswapSector: Sektor %d/%d/%d existiert nicht.\n",
							"UnswapSector: Sector %d/%d/%d does not exist.\n"), SectorX, SectorY, SectorZ);
			File.close();
			return;
		}

		if(LoadingSector->Status != STATUS_SWAPPED){
			error(Translate("UnswapSector: Sektor %d/%d/%d ist nicht ausgelagert.\n",
							"UnswapSector: Sector %d/%d/%d is not swapped out.\n"), SectorX, SectorY, SectorZ);
			File.close();
			return;
		}

		int Size = File.getSize();
		while(File.getPosition() < Size){
			TObject Entry;
			File.readBytes((uint8*)&Entry, sizeof(TObject));

			uint32 EntryIndex = Entry.ObjectID & HashTableMask;
			if(HashTableType[EntryIndex] == STATUS_SWAPPED){
				// NOTE(fusion): Make sure we only allocate the object if we confirm
				// its status. The original code would call `readBytes` on the result
				// from `GetFreeObjectSlot()` directly and would then leak it if the
				// entry status was not `STATUS_SWAPPED`.
				TObject *EntryPointer = GetFreeObjectSlot();
				*EntryPointer = Entry;
				HashTableData[EntryIndex] = EntryPointer;
				HashTableType[EntryIndex] = STATUS_LOADED;
			}else{
				error(Translate("UnswapSector: Objekt %u existiert schon.\n",
								"UnswapSector: Object %u already exists.\n"), Entry.ObjectID);
			}
		}
		LoadingSector->Status = STATUS_LOADED;
		File.close();
		unlink(FileName);
	}catch(const char *str){
		error(Translate("FATAL ERROR in UnswapSector: Kann Datei \"%s\" nicht lesen.\n",
						"FATAL ERROR in UnswapSector: Cannot read file \"%s\".\n"), FileName);
		error(Translate("# Fehler: %s\n",
						"# Error: %s\n"), str);
		abort();
	}
}

void DeleteSwappedSectors(void){
	DIR *SwapDir = opendir(SAVEPATH);
	if(SwapDir == NULL){
		error(Translate("DeleteSwappedSectors: Unterverzeichnis %s nicht gefunden\n",
						"DeleteSwappedSectors: Subdirectory %s not found\n"), SAVEPATH);
		return;
	}

	char FileName[4096];
	while(dirent *DirEntry = readdir(SwapDir)){
		if(DirEntry->d_type != DT_REG){
			continue;
		}

		// NOTE(fusion): `DirEntry->d_name` will only contain the filename
		// so we need to assemble the actual file path (relative in this
		// case) to properly unlink it. Windows has the same behavior with
		// its `FindFirstFile`/`FindNextFile` API.
		const char *FileExt = findLast(DirEntry->d_name, '.');
		if(FileExt == NULL || strcmp(FileExt, ".swp") != 0){
			continue;
		}

		snprintf(FileName, sizeof(FileName), "%s/%s", SAVEPATH, DirEntry->d_name);
		unlink(FileName);
	}

	closedir(SwapDir);
}

void LoadObjects(TReadScriptFile *Script, TWriteStream *Stream, bool Skip){
	int Depth = 1;
	bool ProcessObjects = true;
	Script->readSymbol('{');
	Script->nextToken();
	while(true){
		if(ProcessObjects){
			if(Script->Token != SPECIAL){
				int TypeID = Script->getNumber();
				if(!ObjectTypeExists(TypeID)){
					Script->error("unknown object type");
				}

				if(!Skip){
					Stream->writeWord((uint16)TypeID);
				}

				ProcessObjects = false;
			}else{
				char Special = Script->getSpecial();
				if(Special == '}'){
					if(!Skip){
						Stream->writeWord(0xFFFF);
					}

					Depth -= 1;
					ProcessObjects = false;
					if(Depth <= 0){
						break;
					}
				}else if(Special != ','){
					Script->error("expected comma");
				}
			}
			Script->nextToken();
		}else{
			if(Script->Token != SPECIAL){
				int Attribute = GetInstanceAttributeByName(Script->getIdentifier());
				if(Attribute == -1){
					Script->error("unknown attribute");
				}

				Script->readSymbol('=');
				if(!Skip){
					Stream->writeByte((uint8)Attribute);
				}

				if(Attribute == CONTENT){
					Script->readSymbol('{');
					Depth += 1;
					ProcessObjects = true;
				}else if(Attribute == TEXTSTRING || Attribute == EDITOR){
					const char *String = Script->readString();
					if(!Skip){
						Stream->writeString(String);
					}
				}else{
					int Number = Script->readNumber();
					if(!Skip){
						Stream->writeQuad((uint32)Number);
					}
				}

				Script->nextToken();
			}else{
				// NOTE(fusion): Attributes are key-value pairs separated by whitespace.
				// If we find a special token (probably ',' or '}'), then we're done
				// parsing attributes for the current object.
				if(!Skip){
					Stream->writeByte(0xFF);
				}
				ProcessObjects = true;
			}
		}
	}
}

void LoadObjects(TReadStream *Stream, Object Con){
	int Depth = 1;
	Object Obj = NONE;
	while(true){
		if(Obj == NONE){
			int TypeID = (int)Stream->readWord();
			if(TypeID != 0xFFFF){
				ObjectCounter += 1;
				ObjectType ObjType(TypeID);

				if(ObjType.isBodyContainer()){
					Obj = GetContainerObject(Con, (TypeID - 1));
				}else{
					Obj = AppendObject(Con, ObjType);
				}
			}else{
				Obj = Con;
				Con = Con.getContainer();
				Depth -= 1;
				if(Depth <= 0){
					break;
				}
			}
		}else{
			int Attribute = (int)Stream->readByte();
			if(Attribute != 0xFF){
				if(Attribute == CONTENT){
					Con = Obj;
					Obj = NONE;
					Depth += 1;
				}else if(Attribute == TEXTSTRING || Attribute == EDITOR){
					char String[4096];
					Stream->readString(String, sizeof(String));
					Obj.setAttribute((INSTANCEATTRIBUTE)Attribute, AddDynamicString(String));
				}else if(Attribute == REMAININGEXPIRETIME){
					uint32 Value = Stream->readQuad();
					if(Value != 0){
						CronChange(Obj, (int)Value);
					}
				}else{
					uint32 Value = Stream->readQuad();
					Obj.setAttribute((INSTANCEATTRIBUTE)Attribute, Value);
				}
			}else{
				Obj = NONE;
			}
		}
	}
}

void InitSector(int SectorX, int SectorY, int SectorZ){
	ASSERT(Sector);
	if(*Sector->at(SectorX, SectorY, SectorZ) != NULL){
		error(Translate("InitSector: Sektor %d/%d/%d existiert schon.\n",
						"InitSector: Sector %d/%d/%d already exists.\n"), SectorX, SectorY, SectorZ);
		return;
	}

	TSector *NewSector = (TSector*)malloc(sizeof(TSector));
	for(int X = 0; X < 32; X += 1){
		for(int Y = 0; Y < 32; Y += 1){
			Object MapCon = CreateObject();
			// NOTE(fusion): `Attributes[0]` is probably the object id of the
			// first object in the container.
			AccessObject(MapCon)->Attributes[1] = SectorX * 32 + X;
			AccessObject(MapCon)->Attributes[2] = SectorY * 32 + Y;
			AccessObject(MapCon)->Attributes[3] = SectorZ;
			NewSector->MapCon[X][Y] = MapCon;
		}
	}
	NewSector->TimeStamp = RoundNr;
	NewSector->Status = STATUS_LOADED;
	NewSector->MapFlags = 0;

	*Sector->at(SectorX, SectorY, SectorZ) = NewSector;
}

void LoadSector(const char *FileName, int SectorX, int SectorY, int SectorZ){
	if(SectorX < SectorXMin || SectorXMax < SectorX
			|| SectorY < SectorYMin || SectorYMax < SectorY
			|| SectorZ < SectorZMin || SectorZMax < SectorZ){
		return;
	}

	InitSector(SectorX, SectorY, SectorZ);

	ASSERT(Sector != NULL);
	TSector *LoadingSector = *Sector->at(SectorX, SectorY, SectorZ);
	ASSERT(LoadingSector != NULL);

	TReadScriptFile Script;
	try{
		Script.open(FileName);
		print(1, Translate("Lade Sektor %d/%d/%d ...\n",
						   "Loading sector %d/%d/%d ...\n"), SectorX, SectorY, SectorZ);

		int OffsetX = -1;
		int OffsetY = -1;
		while(true){
			Script.nextToken();
			if(Script.Token == ENDOFFILE){
				Script.close();
				return;
			}

			if(Script.Token == SPECIAL && Script.getSpecial() == ','){
				continue;
			}

			if(Script.Token == BYTES){
				uint8 *SectorOffset = Script.getBytesequence();
				OffsetX = (int)SectorOffset[0];
				OffsetY = (int)SectorOffset[1];
				Script.readSymbol(':');
				// TODO(fusion): Probably check if offsets are within bounds?
				continue;
			}

			if(Script.Token != IDENTIFIER){
				Script.error("next map point expected");
			}

			if(OffsetX == -1 || OffsetY == -1){
				Script.error("coordinate expected");
			}

			const char *Identifier = Script.getIdentifier();
			if(strcmp(Identifier, "refresh") == 0){
				LoadingSector->MapFlags |= 1;
				AccessObject(LoadingSector->MapCon[OffsetX][OffsetY])->Attributes[3] |= 0x100;
			}else if(strcmp(Identifier, "nologout") == 0){
				LoadingSector->MapFlags |= 2;
				AccessObject(LoadingSector->MapCon[OffsetX][OffsetY])->Attributes[3] |= 0x200;
			}else if(strcmp(Identifier, "protectionzone") == 0){
				LoadingSector->MapFlags |= 4;
				AccessObject(LoadingSector->MapCon[OffsetX][OffsetY])->Attributes[3] |= 0x400;
			}else if(strcmp(Identifier, "content") == 0){
				Script.readSymbol('=');
				HelpBuffer.Position = 0;
				LoadObjects(&Script, &HelpBuffer, false);
				TReadBuffer ReadBuffer(HelpBuffer.Data, HelpBuffer.Position);
				LoadObjects(&ReadBuffer, LoadingSector->MapCon[OffsetX][OffsetY]);
			}else{
				Script.error("unknown map flag");
			}
		}
	}catch(const char *str){
		error(Translate("LoadSector: Kann Datei \"%s\" nicht lesen.\n",
						"LoadSector: Cannot read file \"%s\".\n"), FileName);
		error(Translate("# Fehler: %s\n",
						"# Error: %s\n"), str);
		throw "Cannot load sector";
	}
}

void LoadMap(void){
	DIR *MapDir = opendir(MAPPATH);
	if(MapDir == NULL){
		error(Translate("LoadMap: Unterverzeichnis %s nicht gefunden\n",
						"LoadMap: Subdirectory %s not found\n"), MAPPATH);
		throw "Cannot load map";
	}

	print(1, Translate("Lade Karte ...\n",
					   "Loading map ...\n"));
	ObjectCounter = 0;

	int SectorCounter = 0;
	char FileName[4096];
	while(dirent *DirEntry = readdir(MapDir)){
		if(DirEntry->d_type != DT_REG){
			continue;
		}

		// NOTE(fusion): See note in `DeleteSwappedSectors`.
		const char *FileExt = findLast(DirEntry->d_name, '.');
		if(FileExt == NULL || strcmp(FileExt, ".sec") != 0){
			continue;
		}

		int SectorX, SectorY, SectorZ;
		if(sscanf(DirEntry->d_name, "%d-%d-%d.sec", &SectorX, &SectorY, &SectorZ) == 3){
			snprintf(FileName, sizeof(FileName), "%s/%s", MAPPATH, DirEntry->d_name);
			LoadSector(FileName, SectorX, SectorY, SectorZ);
			SectorCounter += 1;
		}
	}

	closedir(MapDir);
	print(1, Translate("%d Sektoren geladen.\n",
					   "%d Sectors loaded.\n"), SectorCounter);
	print(1, Translate("%d Objekte geladen.\n",
					   "%d Objects loaded.\n"), ObjectCounter);
}

void SaveObjects(Object Obj, TWriteStream *Stream, bool Stop){
	// TODO(fusion): Just use a recursive algorithm for both `SaveObjects` and
	// `LoadObjects`. Regardless of performance differences, this iterative version
	// is just unreadable and mostly disconnected.
	int Depth = 1;
	bool ProcessObjects = true;
	Object Prev = NONE;
	while(true){
		if(ProcessObjects){
			if(Obj != NONE){
				ObjectType ObjType = Obj.getObjectType();
				if(!ObjType.isCreatureContainer()){
					Stream->writeWord((uint16)ObjType.TypeID);
					ProcessObjects = false;
				}else{
					if(Stop && Depth == 1){
						break;
					}
					Prev = Obj;
					Obj = Obj.getNextObject();
				}
			}else{
				Stream->writeWord(0xFFFF);
				Depth -= 1;
				if(Depth <= 0){
					break;
				}

				Stream->writeByte(0xFF);
				ObjectCounter += 1;
				if(Stop && Depth == 1){
					break;
				}

				if(Prev == NONE){
					error(Translate("LastObj ist NONE (1)\n",
									"LastObj ist NONE (1)\n"));
				}

				Prev = Prev.getContainer();
				if(Prev == NONE){
					error(Translate("LastObj ist NONE (2)\n",
									"LastObj ist NONE (2)\n"));
				}

				Obj = Prev.getNextObject();
			}
		}else{
			ASSERT(Obj != NONE);
			ObjectType ObjType = Obj.getObjectType();
			for(int Attribute = 1; Attribute <= 17; Attribute += 1){
				if(ObjType.getAttributeOffset((INSTANCEATTRIBUTE)Attribute) != -1){
					uint32 Value = 0;
					if(Attribute == REMAININGEXPIRETIME){
						Value = CronInfo(Obj, false);
					}else{
						Value = Obj.getAttribute((INSTANCEATTRIBUTE)Attribute);
					}

					if(Value != 0){
						Stream->writeByte((uint8)Attribute);
						if(Attribute == TEXTSTRING || Attribute == EDITOR){
							Stream->writeString(GetDynamicString(Value));
						}else{
							Stream->writeQuad(Value);
						}
					}
				}
			}

			Object First = NONE;
			if(ObjType.getAttributeOffset(CONTENT) != -1){
				First = Object(Obj.getAttribute(CONTENT));
			}

			if(First != NONE){
				Depth += 1;
				Prev = NONE;
				Obj = First;
				Stream->writeByte((uint8)CONTENT);
			}else{
				Stream->writeByte(0xFF);
				ObjectCounter += 1;
				if(Stop && Depth == 1){
					break;
				}

				Prev = Obj;
				Obj = Obj.getNextObject();
			}

			ProcessObjects = true;
		}
	}
}

void SaveObjects(TReadStream *Stream, TWriteScriptFile *Script){
	int Depth = 1;
	bool ProcessObjects = true;
	bool FirstObject = true;
	Script->writeText("{");
	while(true){
		if(ProcessObjects){
			int TypeID = (int)Stream->readWord();
			if(TypeID != 0xFFFF){
				if(!FirstObject){
					Script->writeText(", ");
				}

				Script->writeNumber(TypeID);
			}else{
				Script->writeText("}");
				Depth -= 1;
				if(Depth <= 0){
					break;
				}
			}

			ProcessObjects = false;
		}else{
			int Attribute = (int)Stream->readByte();
			if(Attribute != 0xFF){
				Script->writeText(" ");
				Script->writeText(GetInstanceAttributeName(Attribute));
				Script->writeText("=");
				if(Attribute == CONTENT){
					Depth += 1;
					ProcessObjects = true;
					FirstObject = true;
					Script->writeText("{");
				}else if(Attribute == TEXTSTRING || Attribute == EDITOR){
					char String[4096];
					Stream->readString(String, sizeof(String));
					Script->writeString(String);
				}else{
					Script->writeNumber((int)Stream->readQuad());
				}
			}else{
				ProcessObjects = true;
				FirstObject = false;
			}
		}
	}
}

void SaveSector(char *FileName, int SectorX, int SectorY, int SectorZ){
	ASSERT(Sector);
	TSector *SavingSector = *Sector->at(SectorX, SectorY, SectorZ);
	if(!SavingSector){
		return;
	}

	bool Empty = true;
	TWriteScriptFile Script;
	try{
		Script.open(FileName);
		print(1, Translate("Speichere Sektor %d/%d/%d ...\n",
						   "Saving sector %d/%d/%d ...\n"), SectorX, SectorY, SectorZ);

		Script.writeText("# Tibia - graphical Multi-User-Dungeon");
		Script.writeLn();
		Script.writeText("# Data for sector ");
		Script.writeNumber(SectorX);
		Script.writeText("/");
		Script.writeNumber(SectorY);
		Script.writeText("/");
		Script.writeNumber(SectorZ);
		Script.writeLn();
		Script.writeLn();

		for(int X = 0; X < 32; X += 1){
			for(int Y = 0; Y < 32; Y += 1){
				Object First = Object(SavingSector->MapCon[X][Y].getAttribute(CONTENT));
				uint8 Flags = GetMapContainerFlags(SavingSector->MapCon[X][Y]);
				if(First != NONE || Flags != 0){
					Script.writeNumber(X);
					Script.writeText("-");
					Script.writeNumber(Y);
					Script.writeText(": ");

					int AttrCount = 0;

					if(Flags & 1){
						if(AttrCount > 0){
							Script.writeText(", ");
						}
						Script.writeText("Refresh");
						AttrCount += 1;
					}

					if(Flags & 2){
						if(AttrCount > 0){
							Script.writeText(", ");
						}
						Script.writeText("NoLogout");
						AttrCount += 1;
					}

					if(Flags & 4){
						if(AttrCount > 0){
							Script.writeText(", ");
						}
						Script.writeText("ProtectionZone");
						AttrCount += 1;
					}

					if(First != NONE){
						if(AttrCount > 0){
							Script.writeText(", ");
						}
						Script.writeText("Content=");
						HelpBuffer.Position = 0;
						SaveObjects(First, &HelpBuffer, false);
						TReadBuffer ReadBuffer(HelpBuffer.Data, HelpBuffer.Position);
						SaveObjects(&ReadBuffer, &Script);
						AttrCount += 1;
					}

					Script.writeLn();
					Empty = false;
				}
			}
		}

		Script.close();
		if(Empty){
			error(Translate("SaveSector: Sektor %d/%d/%d ist leer.\n",
							"SaveSector: Sector %d/%d/%d is empty.\n"), SectorX, SectorY, SectorZ);
			unlink(FileName);
		}
	}catch(const char *str){
		error(Translate("SaveSector: Kann Datei %s nicht schreiben.\n",
						"SaveSector: Cannot write file %s.\n"), FileName);
		error(Translate("# Fehler: %s\n",
						"# Error: %s\n"), str);
	}
}

void SaveMap(void){
	// NOTE(fusion): I guess this could happen if we're already saving the map
	// and a signal causes `exit` to execute cleanup functions registered with
	// `atexit`, among which is `ExitAll` which may call `SaveMap` throught
	// `ExitMap`.
	static bool SavingMap = false;
	if(SavingMap){
		error(Translate("SaveMap: Karte wird schon gespeichert.\n",
						"SaveMap: Map is already being saved.\n"));
		return;
	}

	SavingMap = true;
	print(1, Translate("Speichere Karte ...\n",
					   "Saving map ...\n"));
	ObjectCounter = 0;

	char FileName[4096];
	for(int SectorZ = SectorZMin; SectorZ <= SectorZMax; SectorZ += 1)
	for(int SectorY = SectorYMin; SectorY <= SectorYMax; SectorY += 1)
	for(int SectorX = SectorXMin; SectorX <= SectorXMax; SectorX += 1){
		snprintf(FileName, sizeof(FileName), "%s/%04d-%04d-%02d.sec",
				MAPPATH, SectorX, SectorY, SectorZ);
		SaveSector(FileName, SectorX, SectorY, SectorZ);
	}

	print(1, Translate("%d Objekte gespeichert.\n",
					   "%d Objects saved.\n"), ObjectCounter);
	SavingMap = false;
}

void RefreshSector(int SectorX, int SectorY, int SectorZ, TReadStream *Stream){
	// TODO(fusion): `matrix3d::at` will return the first entry if the coordinates
	// are out of bounds which that is problematic, specially here.
	if(SectorX < SectorXMin || SectorXMax < SectorX
			|| SectorY < SectorYMin || SectorYMax < SectorY
			|| SectorZ < SectorZMin || SectorZMax < SectorZ){
		error("RefreshSector: Sector %d/%d/%d is out of bounds.",
				SectorX, SectorY, SectorZ);
		return;
	}

	ASSERT(Sector);
	TSector *Sec = *Sector->at(SectorX, SectorY, SectorZ);
	if(Sec && (Sec->MapFlags & 0x01) != 0){
		print(3, Translate("Refreshe Sektor %d/%d/%d ...\n",
						   "Refresh sector %d/%d/%d ...\n"), SectorX, SectorY, SectorZ);
		while(!Stream->eof()){
			uint8 OffsetX = Stream->readByte();
			uint8 OffsetY = Stream->readByte();
			if(OffsetX < 32 && OffsetY < 32){
				Object Con = Sec->MapCon[OffsetX][OffsetY];

				// TODO(fusion): This loop was done a bit differently but I suppose
				// iterating it directly is clearer and as long as we don't access
				// the object after `DeleteObject`, it should work the same.
				Object Obj = Object(Con.getAttribute(CONTENT));
				while(Obj != NONE){
					Object Next = Obj.getNextObject();
					if(!Obj.getObjectType().isCreatureContainer()){
						DeleteObject(Obj);
					}
					Obj = Next;
				}

				LoadObjects(Stream, Con);
			}
		}
	}
}

void PatchSector(int SectorX, int SectorY, int SectorZ, bool FullSector,
		TReadScriptFile *Script, bool SaveHouses){
	if(SectorX < SectorXMin || SectorXMax < SectorX
			|| SectorY < SectorYMin || SectorYMax < SectorY
			|| SectorZ < SectorZMin || SectorZMax < SectorZ){
		error("PatchSector: Sector %d/%d/%d is out of bounds.",
				SectorX, SectorY, SectorZ);
		return;
	}

	ASSERT(Sector);
	TSector *Sec = *Sector->at(SectorX, SectorY, SectorZ);
	bool NewSector = (Sec == NULL);
	if(NewSector){
		print(2, Translate("Lege Sektor %d/%d/%d neu an.\n",
						   "Recreate sector %d/%d/%d.\n"), SectorX, SectorY, SectorZ);
		InitSector(SectorX, SectorY, SectorZ);
		Sec = *Sector->at(SectorX, SectorY, SectorZ);
		ASSERT(Sec != NULL);
	}

	bool FieldTreated[32][32] = {};
	bool FieldPatched[32][32] = {};

	// NOTE(fusion): Step 1.
	//	Patch fields specified in the input script. House fields are NOT patched
	// if `SaveHouses` is set.
	{
		bool House = false;
		int OffsetX = -1;
		int OffsetY = -1;
		while(true){
			Script->nextToken();
			if(Script->Token == ENDOFFILE){
				break;
			}

			if(Script->Token == SPECIAL && Script->getSpecial() == ','){
				continue;
			}

			if(Script->Token == BYTES){
				uint8 *SectorOffset = Script->getBytesequence();
				OffsetX = (int)SectorOffset[0];
				OffsetY = (int)SectorOffset[1];
				Script->readSymbol(':');

				// TODO(fusion): Probably check if offsets are within bounds?
				FieldTreated[OffsetX][OffsetY] = true;
				int CoordX = SectorX * 32 + OffsetX;
				int CoordY = SectorY * 32 + OffsetY;
				int CoordZ = SectorZ;

				// TODO(fusion): Maybe some inlined function?
				House = IsHouse(CoordX, CoordY, CoordZ);
				if(!House && CoordinateFlag(CoordX, CoordY, CoordZ, HOOKSOUTH)){
					House = IsHouse(CoordX - 1, CoordY + 1, CoordZ)
						||  IsHouse(CoordX,     CoordY + 1, CoordZ)
						||  IsHouse(CoordX + 1, CoordY + 1, CoordZ);
				}
				if(!House && CoordinateFlag(CoordX, CoordY, CoordZ, HOOKEAST)){
					House = IsHouse(CoordX + 1, CoordY - 1, CoordZ)
						||  IsHouse(CoordX + 1, CoordY,     CoordZ)
						||  IsHouse(CoordX + 1, CoordY + 1, CoordZ);
				}

				if(House){
					if(SaveHouses){
						continue;
					}
					CleanHouseField(CoordX, CoordY, CoordZ);
				}

				// NOTE(fusion): Similar to `RefreshSector`.
				Object Con = Sec->MapCon[OffsetX][OffsetY];
				Object Obj = Object(Con.getAttribute(CONTENT));
				while(Obj != NONE){
					Object Next = Obj.getNextObject();
					if(!Obj.getObjectType().isCreatureContainer()){
						DeleteObject(Obj);
					}
					Obj = Next;
				}
				// NOTE(fusion): Clear map container flags. See note in `GetObjectCoordinates`.
				AccessObject(Con)->Attributes[3] &= 0xFFFF00FF;
				FieldPatched[OffsetX][OffsetY] = true;
				continue;
			}

			if(Script->Token != IDENTIFIER){
				Script->error("next map point expected");
			}

			if(OffsetX == -1 || OffsetY == -1){
				Script->error("coordinate expected");
			}

			const char *Identifier = Script->getIdentifier();
			if(strcmp(Identifier, "refresh") == 0){
				Sec->MapFlags |= 1;
				if(!House || !SaveHouses){
					AccessObject(Sec->MapCon[OffsetX][OffsetY])->Attributes[3] |= 0x100;
				}
			}else if(strcmp(Identifier, "nologout") == 0){
				Sec->MapFlags |= 2;
				if(!House || !SaveHouses){
					AccessObject(Sec->MapCon[OffsetX][OffsetY])->Attributes[3] |= 0x200;
				}
			}else if(strcmp(Identifier, "protectionzone") == 0){
				Sec->MapFlags |= 4;
				if(!House || !SaveHouses){
					AccessObject(Sec->MapCon[OffsetX][OffsetY])->Attributes[3] |= 0x400;
				}
			}else if(strcmp(Identifier, "content") == 0){
				Script->readSymbol('=');
				HelpBuffer.Position = 0;
				if(!House || !SaveHouses){
					LoadObjects(Script, &HelpBuffer, false);
					TReadBuffer ReadBuffer(HelpBuffer.Data, HelpBuffer.Position);
					LoadObjects(&ReadBuffer, Sec->MapCon[OffsetX][OffsetY]);
				}else{
					// NOTE(fusion): Skip content.
					LoadObjects(Script, &HelpBuffer, true);
				}
			}else{
				Script->error("unknown map flag");
			}
		}
	}

	// NOTE(fusion): Step 2.
	//	Patch fields not specified in the input script if `FullSector` is set.
	// Note that patching in this case is simply deleting a field's objects.
	// House fields are NOT patched if `SaveHouses` is set.
	if(FullSector){
		for(int OffsetX = 0; OffsetX < 32; OffsetX += 1)
		for(int OffsetY = 0; OffsetY < 32; OffsetY += 1){
			if(FieldTreated[OffsetX][OffsetY]){
				continue;
			}

			int CoordX = SectorX * 32 + OffsetX;
			int CoordY = SectorY * 32 + OffsetY;
			int CoordZ = SectorZ;

			// TODO(fusion): Maybe some inlined function?
			bool House = IsHouse(CoordX, CoordY, CoordZ);
			if(!House && CoordinateFlag(CoordX, CoordY, CoordZ, HOOKSOUTH)){
				House = IsHouse(CoordX - 1, CoordY + 1, CoordZ)
					||  IsHouse(CoordX,     CoordY + 1, CoordZ)
					||  IsHouse(CoordX + 1, CoordY + 1, CoordZ);
			}
			if(!House && CoordinateFlag(CoordX, CoordY, CoordZ, HOOKEAST)){
				House = IsHouse(CoordX + 1, CoordY - 1, CoordZ)
					||  IsHouse(CoordX + 1, CoordY,     CoordZ)
					||  IsHouse(CoordX + 1, CoordY + 1, CoordZ);
			}

			if(House){
				if(SaveHouses){
					continue;
				}
				CleanHouseField(CoordX, CoordY, CoordZ);
			}

			// NOTE(fusion): Same as in the parsing loop above.
			Object Con = Sec->MapCon[OffsetX][OffsetY];
			Object Obj = Object(Con.getAttribute(CONTENT));
			while(Obj != NONE){
				Object Next = Obj.getNextObject();
				if(!Obj.getObjectType().isCreatureContainer()){
					DeleteObject(Obj);
				}
				Obj = Next;
			}
			AccessObject(Con)->Attributes[3] &= 0xFFFF00FF;
			FieldPatched[OffsetX][OffsetY] = true;
		}
	}

	// NOTE(fusion): Step 3.
	//	Parse original sector file, transfering non patched fields to a new sector file.
	char FileName[4096];
	char FileNameBak[4096];
	snprintf(FileName, sizeof(FileName), "%s/%04d-%04d-%02d.sec",
			ORIGMAPPATH, SectorX, SectorY, SectorZ);
	snprintf(FileNameBak, sizeof(FileNameBak), "%s/%04d-%04d-%02d.sec~",
			ORIGMAPPATH, SectorX, SectorY, SectorZ);

	TWriteScriptFile OUT;
	OUT.open(FileNameBak);
	OUT.writeText("# Tibia - graphical Multi-User-Dungeon");
	OUT.writeLn();
	OUT.writeText("# Data for sector ");
	OUT.writeNumber(SectorX);
	OUT.writeText("/");
	OUT.writeNumber(SectorY);
	OUT.writeText("/");
	OUT.writeNumber(SectorZ);
	OUT.writeLn();
	OUT.writeLn();

	if(!NewSector){
		TReadScriptFile IN;
		IN.open(FileName);

		int OffsetX = -1;
		int OffsetY = -1;
		int AttrCount = 0;
		while(true){
			IN.nextToken();
			if(IN.Token == ENDOFFILE){
				IN.close();
				break;
			}

			if(IN.Token == SPECIAL && IN.getSpecial() == ','){
				continue;
			}

			if(IN.Token == BYTES){
				uint8 *SectorOffset = IN.getBytesequence();
				OffsetX = (int)SectorOffset[0];
				OffsetY = (int)SectorOffset[1];
				IN.readSymbol(':');
				AttrCount = 0;
				// TODO(fusion): Probably check if offsets are within bounds?
				if(!FieldPatched[OffsetX][OffsetY]){
					OUT.writeNumber(OffsetX);
					OUT.writeText("-");
					OUT.writeNumber(OffsetY);
					OUT.writeText(": ");
				}
				continue;
			}

			if(IN.Token != IDENTIFIER){
				IN.error("next map point expected");
			}

			if(OffsetX == -1 || OffsetY == -1){
				IN.error("coordinate expected");
			}

			const char *Identifier = IN.getIdentifier();
			if(strcmp(Identifier, "refresh") == 0){
				if(!FieldPatched[OffsetX][OffsetY]){
					if(AttrCount > 0){
						OUT.writeText(", ");
					}
					OUT.writeText("Refresh");
					AttrCount += 1;
				}
			}else if(strcmp(Identifier, "nologout") == 0){
				if(!FieldPatched[OffsetX][OffsetY]){
					if(AttrCount > 0){
						OUT.writeText(", ");
					}
					OUT.writeText("NoLogout");
					AttrCount += 1;
				}
			}else if(strcmp(Identifier, "protectionzone") == 0){
				if(!FieldPatched[OffsetX][OffsetY]){
					if(AttrCount > 0){
						OUT.writeText(", ");
					}
					OUT.writeText("ProtectionZone");
					AttrCount += 1;
				}
			}else if(strcmp(Identifier, "content") == 0){
				IN.readSymbol('=');
				HelpBuffer.Position = 0;
				LoadObjects(&IN, &HelpBuffer, false);
				if(!FieldPatched[OffsetX][OffsetY]){
					if(AttrCount > 0){
						OUT.writeText(", ");
					}
					OUT.writeText("Content=");
					TReadBuffer ReadBuffer(HelpBuffer.Data, HelpBuffer.Position);
					SaveObjects(&ReadBuffer, &OUT);
					AttrCount += 1;
				}
			}else{
				IN.error("unknown map flag");
			}
		}
	}

	// NOTE(fusion): Step 4.
	//	Transfer patched fields from memory into the new sector file.
	for(int OffsetX = 0; OffsetX < 32; OffsetX += 1)
	for(int OffsetY = 0; OffsetY < 32; OffsetY += 1){
		if(!FieldPatched[OffsetX][OffsetY]){
			continue;
		}

		Object Con = Sec->MapCon[OffsetX][OffsetY];
		Object First = Object(Con.getAttribute(CONTENT));
		uint8 Flags = GetMapContainerFlags(Con);
		if(First != NONE || Flags != 0){
			OUT.writeNumber(OffsetX);
			OUT.writeText("-");
			OUT.writeNumber(OffsetY);
			OUT.writeText(": ");

			int AttrCount = 0;

			if(Flags & 1){
				if(AttrCount > 0){
					OUT.writeText(", ");
				}
				OUT.writeText("Refresh");
				AttrCount += 1;
			}

			if(Flags & 2){
				if(AttrCount > 0){
					OUT.writeText(", ");
				}
				OUT.writeText("NoLogout");
				AttrCount += 1;
			}

			if(Flags & 4){
				if(AttrCount > 0){
					OUT.writeText(", ");
				}
				OUT.writeText("ProtectionZone");
				AttrCount += 1;
			}

			if(First != NONE){
				if(AttrCount > 0){
					OUT.writeText(", ");
				}
				OUT.writeText("Content=");
				HelpBuffer.Position = 0;
				SaveObjects(First, &HelpBuffer, false);
				TReadBuffer ReadBuffer(HelpBuffer.Data, HelpBuffer.Position);
				SaveObjects(&ReadBuffer, &OUT);
				AttrCount += 1;
			}

			OUT.writeLn();
		}
	}
	OUT.close();

	// NOTE(fusion): Step 5.
	//	Replace original sector file with the new one.
	if(!NewSector){
		unlink(FileName);
	}

	if(rename(FileNameBak, FileName) != 0){
		int ErrCode = errno;
		error(Translate("PatchSector: Fehler %d beim Umbenennen von %s.\n",
						"PatchSector: Error %d renaming %s.\n"), ErrCode, FileNameBak);
		error(Translate("# Fehler %d: %s.\n",
						"# Error %d: %s.\n"), ErrCode, strerror(ErrCode));
		throw "cannot patch ORIGMAP";
	}
}

void InitMap(void){
	ReadMapConfig();

	Sector = new matrix3d<TSector*>(SectorXMin, SectorXMax,
			SectorYMin, SectorYMax, SectorZMin, SectorZMax, NULL);

	DeleteSwappedSectors();

	// NOTE(fusion): Object storage is FIXED and determined at startup.
	ObjectBlock = (TObjectBlock**)malloc(OBCount * sizeof(TObjectBlock*));
	for(int i = 0; i < OBCount; i += 1){
		ObjectBlock[i] = (TObjectBlock*)malloc(sizeof(TObjectBlock));
	}

	// NOTE(fusion): Setup free object list. See note in `GetFreeObjectSlot`.
	constexpr int ObjectsPerBlock = NARRAY(TObjectBlock::Object);
	for(int i = 0; i < OBCount; i += 1){
		for(int j = 0; j < ObjectsPerBlock; j += 1){
			*((TObject**)&ObjectBlock[i]->Object[j]) = &ObjectBlock[i]->Object[j + 1];
		}

		if(i < (OBCount - 1)){
			// NOTE(fusion): Link last entry of this block, with the first entry of the next one.
			*((TObject**)&ObjectBlock[i]->Object[ObjectsPerBlock - 1]) = &ObjectBlock[i + 1]->Object[0];
		}else{
			// NOTE(fusion): End of free object list.
			*((TObject**)&ObjectBlock[i]->Object[ObjectsPerBlock - 1]) = NULL;
		}
	}
	FirstFreeObject = &ObjectBlock[0]->Object[0];

	// NOTE(fusion): Initialize object hash table.
	ASSERT(ISPOW2(HashTableSize));
	HashTableMask = HashTableSize - 1;
	HashTableData = (TObject**)malloc(HashTableSize * sizeof(TObject*));
	HashTableType = (uint8*)malloc(HashTableSize * sizeof(uint8));
	memset(HashTableType, 0, HashTableSize * sizeof(uint8));
	HashTableFree = HashTableSize - 1;
	// NOTE(fusion): This is probably reserved for `NONE`.
	HashTableType[0] = STATUS_PERMANENT;
	HashTableData[0] = GetFreeObjectSlot();

	// NOTE(fusion): Initialize cron hash table (whatever that is).
	for(int i = 0; i < NARRAY(CronHashTable); i += 1){
		CronHashTable[i] = -1;
	}
	CronEntries = 0;

	LoadMap();
}

void ExitMap(bool Save){
	if(Save){
		SaveMap();
	}

	free(HashTableData);
	free(HashTableType);

	for(int i = 0; i < OBCount; i += 1){
		free(ObjectBlock[i]);
	}
	free(ObjectBlock);

	if(Sector != NULL){
		for(int SectorZ = SectorZMin; SectorZ <= SectorZMax; SectorZ += 1)
		for(int SectorY = SectorYMin; SectorY <= SectorYMax; SectorY += 1)
		for(int SectorX = SectorXMin; SectorX <= SectorXMax; SectorX += 1){
			TSector *CurrentSector = *Sector->at(SectorX, SectorY, SectorZ);
			if(CurrentSector != NULL){
				free(CurrentSector);
			}
		}
		delete Sector;
	}

	DeleteSwappedSectors();
}

// Object Functions
// =============================================================================
TObject *AccessObject(Object Obj){
	if(Obj == NONE){
		error(Translate("AccessObject: Ungültige Objektnummer Null.\n",
						"AccessObject: Invalid object number Null.\n"));
		return HashTableData[0];
	}

	uint32 EntryIndex = Obj.ObjectID & HashTableMask;
	if(HashTableType[EntryIndex] == STATUS_SWAPPED){
		UnswapSector((uintptr)HashTableData[EntryIndex]);
	}

	if(HashTableType[EntryIndex] == STATUS_LOADED
	&& HashTableData[EntryIndex]->ObjectID == Obj.ObjectID){
		return HashTableData[EntryIndex];
	}else{
		return HashTableData[0];
	}
}

Object CreateObject(void){
	static uint32 NextObjectID = 1;

	// NOTE(fusion): Load factor of 1/16.
	if(HashTableFree < (HashTableSize / 16)){
		ResizeHashTable();
	}

	// NOTE(fusion): If we properly manage the load factor and the number of free
	// entries, we should have no trouble finding an empty table entry here. Use
	// a bounded loop nevertheless, just to be safe.
	for(uint32 i = 0; i < HashTableSize; i += 1){
		if(HashTableType[NextObjectID & HashTableMask] == 0)
			break;
		NextObjectID += 1;
	}
	ASSERT(HashTableType[NextObjectID & HashTableMask] == 0);

	TObject *Entry = GetFreeObjectSlot();
	if(Entry == NULL){
		error(Translate("CreateObject: Kann Objekt nicht anlegen.\n",
						"CreateObject: Cannot create object.\n"));
		return NONE;
	}

	Entry->ObjectID = NextObjectID;
	HashTableData[NextObjectID & HashTableMask] = Entry;
	HashTableType[NextObjectID & HashTableMask] = STATUS_LOADED;
	HashTableFree -= 1;
	IncrementObjectCounter();

	return Object(NextObjectID);
}

static void DestroyObject(Object Obj){
	if(!Obj.exists()){
		error(Translate("DestroyObject: Übergebenes Objekt existiert nicht.\n",
						"DestroyObject: Passed object does not exist.\n"));
		return;
	}

	ObjectType ObjType = Obj.getObjectType();
	if(ObjType.getFlag(TEXT)){
		DeleteDynamicString(Obj.getAttribute(TEXTSTRING));
		DeleteDynamicString(Obj.getAttribute(EDITOR));
	}

	if(ObjType.getFlag(CONTAINER) || ObjType.getFlag(CHEST)){
		while(true){
			Object Inner = Object(Obj.getAttribute(CONTENT));
			if(Inner == NONE){
				break;
			}
			DeleteObject(Inner);
		}
	}

	if(ObjType.getFlag(EXPIRE)){
		CronStop(Obj);
	}

	// TODO(fusion): I feel this should be checked up front?
	if(Obj == NONE){
		error(Translate("DestroyObject: Ungültige Objektnummer %d.\n",
						"DestroyObject: Invalid object number %d.\n"), NONE.ObjectID);
		return;
	}

	uint32 EntryIndex = Obj.ObjectID & HashTableMask;
	if(HashTableType[EntryIndex] != STATUS_LOADED){
		error(Translate("DestroyObject: Objekt steht nicht im Speicher.\n",
						"DestroyObject: Object is not in memory.\n"));
		return;
	}

	PutFreeObjectSlot(HashTableData[EntryIndex]);
	HashTableType[EntryIndex] = STATUS_FREE;
	HashTableFree += 1;
	DecrementObjectCounter();
}

void DeleteObject(Object Obj){
	if(!Obj.exists()){
		error(Translate("DeleteObject: Übergebenes Objekt existiert nicht.\n",
						"DeleteObject: Passed object does not exist.\n"));
		return;
	}

	CutObject(Obj);
	DestroyObject(Obj);
}

void ChangeObject(Object Obj, ObjectType NewType){
	if(!Obj.exists()){
		error(Translate("ChangeObject: Übergebenes Objekt existiert nicht.\n",
						"ChangeObject: Passed object does not exist.\n"));
		return;
	}

	uint32 Amount = 0;
	uint32 SavedExpireTime = 0;
	int Delay = -1;

	ObjectType OldType = Obj.getObjectType();
	if(!OldType.isMapContainer()){
		if(OldType.getFlag(CUMULATIVE)){
			Amount = Obj.getAttribute(AMOUNT);
		}

		if(OldType.getFlag(EXPIRE)){
			SavedExpireTime = CronStop(Obj);
			if(NewType.getFlag(CUMULATIVE)){
				Amount = 1;
			}
		}

		if(OldType.getFlag(TEXT) && !NewType.getFlag(TEXT)){
			DeleteDynamicString(Obj.getAttribute(TEXTSTRING));
			Obj.setAttribute(TEXTSTRING, 0);

			DeleteDynamicString(Obj.getAttribute(EDITOR));
			Obj.setAttribute(EDITOR, 0);
		}

		if(OldType.getFlag(EXPIRESTOP)){
			if(Obj.getAttribute(SAVEDEXPIRETIME) != 0){
				Delay = (int)Obj.getAttribute(SAVEDEXPIRETIME);
			}
		}

		if(OldType.getFlag(MAGICFIELD)){
			Obj.setAttribute(RESPONSIBLE, 0);
		}
	}

	Obj.setObjectType(NewType);

	if(NewType.getFlag(CUMULATIVE)){
		if(Amount <= 0){
			Amount = 1;
		}
		Obj.setAttribute(AMOUNT, Amount);
	}

	if(NewType.getFlag(RUNE)){
		if(Obj.getAttribute(CHARGES) == 0){
			Obj.setAttribute(CHARGES, 1);
		}
	}

	if(NewType.getFlag(LIQUIDPOOL)){
		if(Obj.getAttribute(POOLLIQUIDTYPE) == 0){
			Obj.setAttribute(POOLLIQUIDTYPE, LIQUID_WATER);
		}
	}

	if(NewType.getFlag(WEAROUT)){
		if(Obj.getAttribute(REMAININGUSES) == 0){
			Obj.setAttribute(REMAININGUSES, NewType.getAttribute(TOTALUSES));
		}
	}

	if(NewType.getFlag(EXPIRESTOP)){
		Obj.setAttribute(SAVEDEXPIRETIME, SavedExpireTime);
	}

	CronExpire(Obj, Delay);
}

void ChangeObject(Object Obj, INSTANCEATTRIBUTE Attribute, uint32 Value){
	if(!Obj.exists()){
		error(Translate("ChangeObject: Übergebenes Objekt existiert nicht.\n",
						"ChangeObject: Passed object does not exist.\n"));
		return;
	}

	Obj.setAttribute(Attribute, Value);
}

int GetObjectPriority(Object Obj){
	if(!Obj.exists()){
		error(Translate("GetObjectPriority: Übergebenes Objekt existiert nicht.\n",
						"GetObjectPriority: Passed object does not exist.\n"));
		return -1;
	}

	int ObjPriority;
	ObjectType ObjType = Obj.getObjectType();
	if(ObjType.getFlag(BANK)){
		ObjPriority = PRIORITY_BANK;
	}else if(ObjType.getFlag(CLIP)){
		ObjPriority = PRIORITY_CLIP;
	}else if(ObjType.getFlag(BOTTOM)){
		ObjPriority = PRIORITY_BOTTOM;
	}else if(ObjType.getFlag(TOP)){
		ObjPriority = PRIORITY_TOP;
	}else if(ObjType.isCreatureContainer()){
		ObjPriority = PRIORITY_CREATURE;
	}else{
		ObjPriority = PRIORITY_LOW;
	}
	return ObjPriority;
}

void PlaceObject(Object Obj, Object Con, bool Append){
	if(!Obj.exists()){
		error(Translate("PlaceObject: Übergebenes Objekt existiert nicht.\n",
						"PlaceObject: Passed object does not exist.\n"));
		return;
	}

	if(!Con.exists()){
		error(Translate("PlaceObject: Übergebener Container existiert nicht.\n",
						"PlaceObject: Transferred container does not exist.\n"));
		return;
	}

	ObjectType ConType = Con.getObjectType();
	if(!ConType.getFlag(CONTAINER) && !ConType.getFlag(CHEST)){
		error(Translate("PlaceObject: Con (%d) ist kein Container.\n",
						"PlaceObject: Con (%d) is not a container.\n"), ConType.TypeID);
		return;
	}

	Object Prev = NONE;
	Object Cur = Object(Con.getAttribute(CONTENT));
	if(ConType.isMapContainer()){
		// TODO(fusion): Review. The loop below was a bit rough but it seems that
		// append is forced for non PRIORITY_CREATURE and PRIORITY_LOW.
		int ObjPriority = GetObjectPriority(Obj);
		Append = Append
			|| (ObjPriority != PRIORITY_CREATURE
				&& ObjPriority != PRIORITY_LOW);

		while(Cur != NONE){
			int CurPriority = GetObjectPriority(Cur);
			if(CurPriority == ObjPriority
					&& (ObjPriority == PRIORITY_BANK
						|| ObjPriority == PRIORITY_BOTTOM
						|| ObjPriority == PRIORITY_TOP)){
				// TODO(fusion): Replace item? I think the client might assert
				// if there are two of these objects on a single tile.
				const char *PriorityString = "";
				if(ObjPriority == PRIORITY_BANK){
					PriorityString = "BANK";
				}else if(ObjPriority == PRIORITY_BOTTOM){
					PriorityString = "BOTTOM";
				}else if(ObjPriority == PRIORITY_TOP){
					PriorityString = "TOP";
				}

				int CoordX, CoordY, CoordZ;
				GetObjectCoordinates(Con, &CoordX, &CoordY, &CoordZ);

				ObjectType ObjType = Obj.getObjectType();
				ObjectType CurType = Cur.getObjectType();
				error(Translate("PlaceObject: Zwei %s-Objekte (%d und %d) auf Feld [%d,%d,%d].\n",
								"PlaceObject: Two %s objects (%d and %d) on array [%d,%d,%d].\n"),
					PriorityString, ObjType.TypeID, CurType.TypeID, CoordX, CoordY, CoordZ);
			}

			if(Append && CurPriority > ObjPriority) break;
			if(!Append && CurPriority >= ObjPriority) break;

			Prev = Cur;
			Cur = Cur.getNextObject();
		}
	}else{
		if(Append){
			while(Cur != NONE){
				Prev = Cur;
				Cur = Cur.getNextObject();
			}
		}
	}

	if(Prev != NONE){
		Prev.setNextObject(Obj);
	}else{
		Con.setAttribute(CONTENT, Obj.ObjectID);
	}
	Obj.setNextObject(Cur);
	Obj.setContainer(Con);
}

// NOTE(fusion): This is the opposite of `PlaceObject`.
void CutObject(Object Obj){
	if(!Obj.exists()){
		error(Translate("CutObject: Übergebenes Objekt existiert nicht.\n",
						"CutObject: Passed object does not exist.\n"));
		return;
	}

	Object Con = Obj.getContainer();
	Object Cur = GetFirstContainerObject(Con);
	if(Cur == Obj){
		Object Next = Obj.getNextObject();
		Con.setAttribute(CONTENT, Next.ObjectID);
	}else{
		Object Prev;
		do{
			Prev = Cur;
			Cur = Cur.getNextObject();
		}while(Cur != Obj);
		Prev.setNextObject(Cur.getNextObject());
	}

	Obj.setNextObject(NONE);
	Obj.setContainer(NONE);
}

void MoveObject(Object Obj, Object Con){
	if(!Obj.exists()){
		error(Translate("MoveObject: Übergebenes Objekt existiert nicht.\n",
						"MoveObject: Passed object does not exist.\n"));
		return;
	}

	if(!Con.exists()){
		error(Translate("MoveObject: Übergebener Zielcontainer existiert nicht.\n",
						"MoveObject: Passed target container does not exist.\n"));
		return;
	}

	CutObject(Obj);
	PlaceObject(Obj, Con, false);
}

Object AppendObject(Object Con, ObjectType Type){
	if(!Con.exists()){
		error(Translate("AppendObject: Übergebener Container existiert nicht.\n",
						"AppendObject: Transferred container does not exist.\n"));
		return NONE;
	}

	Object Obj = CreateObject();
	ChangeObject(Obj, Type);
	PlaceObject(Obj, Con, true);
	return Obj;
}

Object SetObject(Object Con, ObjectType Type, uint32 CreatureID){
	if(!Con.exists()){
		error(Translate("SetObject: Übergebener Container existiert nicht.\n",
						"SetObject: Transferred container does not exist.\n"));
		return NONE;
	}

	Object Obj = CreateObject();
	ChangeObject(Obj, Type);
	PlaceObject(Obj, Con, false);
	if(Type.isCreatureContainer()){
		if(CreatureID == 0){
			error(Translate("SetObject: Ungültige Kreatur-ID.\n",
							"SetObject: Invalid creature ID.\n"));
		}
		AccessObject(Obj)->Attributes[1] = CreatureID;
	}
	return Obj;
}

Object CopyObject(Object Con, Object Source){
	if(!Source.exists()){
		error(Translate("CopyObject: Vorlage existiert nicht.\n",
						"CopyObject: Template does not exist.\n"));
		return NONE;
	}

	if(!Con.exists()){
		error(Translate("CopyObject: Zielcontainer existiert nicht.\n",
						"CopyObject: Target container does not exist.\n"));
		return NONE;
	}

	ObjectType SourceType = Source.getObjectType();
	if(SourceType.isCreatureContainer()){
		error(Translate("CopyObject: Kreaturen dürfen nicht kopiert werden.\n",
						"CopyObject: Creatures may not be copied.\n"));
		return NONE;
	}

	Object NewObj = SetObject(Con, SourceType, 0);
	for(int i = 0; i < NARRAY(TObject::Attributes); i += 1){
		AccessObject(NewObj)->Attributes[i] = AccessObject(Source)->Attributes[i];
	}

	if(SourceType.getFlag(CONTAINER) || SourceType.getFlag(CHEST)){
		NewObj.setAttribute(CONTENT, NONE.ObjectID);
	}

	if(SourceType.getFlag(TEXT)){
		// NOTE(fusion): Both `NewObj` and `Source` share the same strings. We
		// need to duplicate them so both objects can "own" and manage their own
		// strings separately.
		uint32 TextString = NewObj.getAttribute(TEXTSTRING);
		if(TextString != 0){
			TextString = AddDynamicString(GetDynamicString(TextString));
			NewObj.setAttribute(TEXTSTRING, TextString);
		}

		uint32 Editor = NewObj.getAttribute(EDITOR);
		if(Editor != 0){
			Editor = AddDynamicString(GetDynamicString(TextString));
			NewObj.setAttribute(EDITOR, Editor);
		}
	}

	return NewObj;
}

Object SplitObject(Object Obj, int Count){
	if(!Obj.exists()){
		error(Translate("SplitObject: Übergebenes Objekt existiert nicht.\n",
						"SplitObject: Passed object does not exist.\n"));
		return NONE;
	}

	ObjectType ObjType = Obj.getObjectType();
	if(!ObjType.getFlag(CUMULATIVE)){
		error(Translate("SplitObject: Objekt ist nicht kumulierbar.\n",
						"SplitObject: Object is not cumulative.\n"));
		return Obj; // TODO(fusion): Probably return NONE?
	}

	uint32 Amount = Obj.getAttribute(AMOUNT);
	if(Count <= 0 || (uint32)Count > Amount){
		error(Translate("SplitObject: Ungültiger Zähler %d.\n",
						"SplitObject: Invalid counter %d.\n"), Count);
		return Obj; // TODO(fusion): Probably return NONE?
	}

	Object Res = Obj;
	if((uint32)Count != Amount){
		Res = CopyObject(Obj.getContainer(), Obj);
		Res.setAttribute(AMOUNT, (uint32)Count);
		Obj.setAttribute(AMOUNT, Amount - (uint32)Count);
	}
	return Res;
}

void MergeObjects(Object Obj, Object Dest){
	if(!Obj.exists()){
		error(Translate("MergeObjects: Übergebenes Objekt existiert nicht.\n",
						"MergeObjects: Passed object does not exist.\n"));
		return;
	}

	if(!Dest.exists()){
		error(Translate("MergeObjects: Übergebenes Ziel existiert nicht.\n",
						"MergeObjects: Passed target does not exist.\n"));
		return;
	}

	ObjectType ObjType = Obj.getObjectType();
	ObjectType DestType = Dest.getObjectType();
	if(ObjType != DestType){
		error(Translate("MergeObjects: Objekttypen %d und %d sind nicht identisch.\n",
						"MergeObjects: Object types %d and %d are not identical.\n"),
				ObjType.TypeID, DestType.TypeID);
		return;
	}

	if(!ObjType.getFlag(CUMULATIVE)){
		error(Translate("MergeObjects: Objekttyp %d ist nicht kumulierbar.\n",
						"MergeObjects: Object type %d is not cumulative.\n"), ObjType.TypeID);
		return;
	}

	uint32 ObjAmount = Obj.getAttribute(AMOUNT);
	if(ObjAmount == 0){
		error(Translate("MergeObjects: Objekt enthält 0 Teile.\n",
						"MergeObjects: Object contains 0 parts.\n"));
		ObjAmount = 1;
	}

	uint32 DestAmount = Dest.getAttribute(AMOUNT);
	if(DestAmount == 0){
		error(Translate("MergeObjects: Zielobjekt enthält 0 Teile.\n",
						"MergeObjects: Target object contains 0 parts.\n"));
		DestAmount = 1;
	}

	DestAmount += ObjAmount;
	if(DestAmount > 100){
		error(Translate("MergeObjects: Neues Objekt enthält mehr als 100 Teile.\n",
						"MergeObjects: New object contains more than 100 parts.\n"));
		DestAmount = 100;
	}

	Dest.setAttribute(AMOUNT, DestAmount);
	DeleteObject(Obj);
}

Object GetFirstContainerObject(Object Con){
	if(Con == NONE){
		error(Translate("GetFirstContainerObject: Übergebener Container existiert nicht.\n",
						"GetFirstContainerObject: Transferred Container does not exist.\n"));
		return NONE;
	}

	ObjectType ConType = Con.getObjectType();
	if(!ConType.getFlag(CONTAINER) && !ConType.getFlag(CHEST)){
		error(Translate("GetFirstContainerObject: Con (%d) ist kein Container.\n",
						"GetFirstContainerObject: Con (%d) is not a Container.\n"), ConType.TypeID);
		return NONE;
	}

	return Object(Con.getAttribute(CONTENT));
}

Object GetContainerObject(Object Con, int Index){
	if(Index < 0){
		error(Translate("GetContainerObject: Ungültige laufende Nummer %d.\n",
						"GetContainerObject: Invalid serial number %d.\n"), Index);
		return NONE;
	}

	if(!Con.exists()){
		error(Translate("GetContainerObject: Übergebener Container existiert nicht.\n",
						"GetContainerObject: Transferred Container does not exist.\n"));
		return NONE;
	}

	Object Obj = GetFirstContainerObject(Con);
	while(Index > 0 && Obj != NONE){
		Index -= 1;
		Obj = Obj.getNextObject();
	}

	return Obj;
}

Object GetMapContainer(int x, int y, int z){
	int SectorX = x / 32;
	int SectorY = y / 32;
	int SectorZ = z;

	if(SectorX < SectorXMin || SectorXMax < SectorX
			|| SectorY < SectorYMin || SectorYMax < SectorY
			|| SectorZ < SectorZMin || SectorZMax < SectorZ){
		return NONE;
	}

	ASSERT(Sector != NULL);
	TSector *ConSector = *Sector->at(SectorX, SectorY, SectorZ);
	if(ConSector == NULL){
		return NONE;
	}

	int OffsetX = x % 32;
	int OffsetY = y % 32;
	ConSector->TimeStamp = RoundNr;
	return ConSector->MapCon[OffsetX][OffsetY];
}

Object GetMapContainer(Object Obj){
	if(!Obj.exists()){
		error(Translate("GetMapContainer: Übergebenes Objekt existiert nicht\n",
						"GetMapContainer: Passed object does not exist\n"));
		return NONE;
	}

	while(Obj != NONE){
		if(Obj.getObjectType().isMapContainer())
			break;
		Obj = Obj.getContainer();
	}

	return Obj;
}

Object GetFirstObject(int x, int y, int z){
	Object MapCon = GetMapContainer(x, y, z);
	if(MapCon != NONE){
		return Object(MapCon.getAttribute(CONTENT));
	}else{
		return NONE;
	}
}

Object GetFirstSpecObject(int x, int y, int z, ObjectType Type){
	Object Obj = GetFirstObject(x, y, z);
	while(Obj != NONE){
		if(Obj.getObjectType() == Type){
			break;
		}
		Obj = Obj.getNextObject();
	}
	return Obj;
}

uint8 GetMapContainerFlags(Object Obj){
	if(!Obj.exists() || !Obj.getObjectType().isMapContainer()){
		error(Translate("GetMapContainerFlags: Objekt ist kein MapContainer.\n",
						"GetMapContainerFlags: Object is not a MapContainer.\n"));
		return 0;
	}

	// NOTE(fusion): See note in `GetObjectCoordinates`.
	return (uint8)(AccessObject(Obj)->Attributes[3] >> 8);
}

void GetObjectCoordinates(Object Obj, int *x, int *y, int *z){
	if(!Obj.exists()){
		error(Translate("GetObjectCoordinates: Übergebenes Objekt existiert nicht.\n",
						"GetObjectCoordinates: Passed object does not exist.\n"));
		*x = 0;
		*y = 0;
		*z = 0;
		return;
	}

	// TODO(fusion): I'm not sure I like this approach with calling `AccessObject`
	// multiple times for the same object. Furthermore, using `Object::getObjectType`
	// (at least until now) is very weird when we could just get the same information
	// from the `TObject` which we'll access ANYWAYS.

	while(true){
		if(Obj.getObjectType().isMapContainer())
			break;
		Obj = Obj.getContainer();
	}

	*x = AccessObject(Obj)->Attributes[1];
	*y = AccessObject(Obj)->Attributes[2];

	// NOTE(fusion): The first 8 bits of `Attributes[3]` holds the Z coordinate
	// of a map container. The next 8 bits holds its flags and the last 16 bits
	// holds its house id.
	*z = AccessObject(Obj)->Attributes[3] & 0xFF;
}

bool CoordinateFlag(int x, int y, int z, FLAG Flag){
	bool Result = false;
	Object Obj = GetFirstObject(x, y, z);
	while(Obj != NONE){
		if(Obj.getObjectType().getFlag(Flag)){
			Result = true;
			break;
		}
		Obj = Obj.getNextObject();
	}
	return Result;
}

bool IsOnMap(int x, int y, int z){
	int SectorX = x / 32;
	int SectorY = y / 32;
	int SectorZ = z;

	return SectorXMin <= SectorX && SectorX <= SectorXMax
		&& SectorYMin <= SectorY && SectorY <= SectorYMax
		&& SectorZMin <= SectorZ && SectorZ <= SectorZMax;
}

bool IsPremiumArea(int x, int y, int z){
	int SectorX = x / 32;
	int SectorY = y / 32;

	// TODO(fusion): This is checking if the position lies within certain bounding
	// boxes but it is difficult to unwind the condition due to optimizations. The
	// best way to understand the outline of the region is to plot this function.
	bool Result = true;
	if(SectorX < 0x40C
	&& (SectorX < 0x408 || SectorY > 0x3E6)
	&& (SectorX < 0x406 || SectorY < 0x3F0)
	&& (SectorX < 0x405 || SectorY < 0x3F1)
	&& (SectorX < 0x3FE || SectorY < 0x3F6)){
		Result = SectorY > 0x3F7;
	}
	return Result;
}

bool IsNoLogoutField(int x, int y, int z){
	bool Result = false;
	Object Con = GetMapContainer(x, y, z);
	if(Con != NONE){
		Result = (GetMapContainerFlags(Con) & 0x02) != 0;
	}
	return Result;
}

bool IsProtectionZone(int x, int y, int z){
	bool Result = false;
	Object Con = GetMapContainer(x, y, z);
	if(Con != NONE){
		Result = (GetMapContainerFlags(Con) & 0x04) != 0;
	}
	return Result;
}

bool IsHouse(int x, int y, int z){
	return GetHouseID(x, y, z) != 0;
}

uint16 GetHouseID(int x, int y, int z){
	Object Con = GetMapContainer(x, y, z);
	if(Con == NONE){
		return 0;
	}

	if(!Con.exists()){
		error(Translate("GetHouseID: Kartencontainer für Punkt [%d,%d,%d] existiert nicht.\n",
						"GetHouseID: Map container for point [%d,%d,%d] does not exist.\n"), x, y, z);
		return 0;
	}

	// NOTE(fusion): See note in `GetObjectCoordinates`.
	return (uint16)(AccessObject(Con)->Attributes[3] >> 16);
}

void SetHouseID(int x, int y, int z, uint16 ID){
	if(!IsOnMap(x, y, z)){
		return;
	}

	Object Con = GetMapContainer(x, y, z);
	if(Con == NONE || !Con.exists()){
		error(Translate("SetHouseID: Kartencontainer für Punkt [%d,%d,%d] existiert nicht.\n",
						"SetHouseID: Map container for point [%d,%d,%d] does not exist.\n"), x, y, z);
		return;
	}

	// NOTE(fusion): See note in `GetObjectCoordinates`.
	uint16 PrevID = (uint16)(AccessObject(Con)->Attributes[3] >> 16);
	if(PrevID != 0){
		error(Translate("SetHouseID: Feld [%d,%d,%d] gehört schon zu einem Haus.\n",
						"SetHouseID: Field [%d,%d,%d] already belongs to a house.\n"), x, y, z);
		return;
	}

	AccessObject(Con)->Attributes[3] |= ((uint32)ID << 16);
}

int GetDepotNumber(const char *Town){
	if(Town == NULL){
		error(Translate("GetDepotNumber: Town ist NULL.\n",
						"GetDepotNumber: Town is NULL.\n"));
		return -1;
	}

	for(int DepotNumber = DepotInfo.min;
			DepotNumber <= DepotInfo.max;
			DepotNumber += 1){
		if(stricmp(DepotInfo.at(DepotNumber)->Town, Town) == 0){
			return DepotNumber;
		}
	}
	return -1;
}

const char *GetDepotName(int DepotNumber){
	if(DepotNumber < DepotInfo.min || DepotNumber > DepotInfo.max){
		error(Translate("GetDepotName: Ungültiger Name für Depot %d.\n",
						"GetDepotName: Invalid name for depot %d.\n"), DepotNumber);
		return "unknown";
	}

	return DepotInfo.at(DepotNumber)->Town;
}

int GetDepotSize(int DepotNumber, bool PremiumAccount){
	if(DepotNumber < DepotInfo.min || DepotNumber > DepotInfo.max){
		error(Translate("GetDepotSize: Ungültige Depotnummer %d.\n",
						"GetDepotSize: Invalid depot number %d.\n"), DepotNumber);
		return 1;
	}

	TDepotInfo *Info = DepotInfo.at(DepotNumber);
	if(Info->Size < 1){
		error(Translate("GetDepotSize: Ungültige Depotgröße %d für Depot %d.\n",
						"GetDepotSize: Invalid depot size %d for depot %d.\n"), Info->Size, DepotNumber);
		Info->Size = 1;
	}

	int Size = Info->Size;
	if(PremiumAccount){
		Size *= 2;
	}

	return Size;
}

bool GetMarkPosition(const char *Name, int *x, int *y, int *z){
	bool Result = false;
	for(int i = 0; i < Marks; i += 1){
		TMark *MarkPointer = Mark.at(i);
		if(stricmp(MarkPointer->Name, Name) == 0){
			*x = MarkPointer->x;
			*y = MarkPointer->y;
			*z = MarkPointer->z;
			Result = true;
			break;
		}
	}
	return Result;
}

void GetStartPosition(int *x, int *y, int *z, bool Newbie){
	if(Newbie){
		*x = NewbieStartPositionX;
		*y = NewbieStartPositionY;
		*z = NewbieStartPositionZ;
	}else{
		*x = VeteranStartPositionX;
		*y = VeteranStartPositionY;
		*z = VeteranStartPositionZ;
	}
}
