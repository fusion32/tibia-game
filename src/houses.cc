#include "houses.hh"
#include "config.hh"
#include "cr.hh"
#include "info.hh"
#include "moveuse.hh"
#include "query.hh"
#include "writer.hh"

static vector<THelpDepot> HelpDepot(0, 9, 10);
static int HelpDepots;

static vector<THouseArea> HouseArea(0, 99, 50);
static int HouseAreas;

static vector<THouse> House(0, 99, 100);
static int Houses;
static int MaxHouseX;
static int MaxHouseY;

static TQueryManagerConnection *QueryManagerConnection;
static int PaymentExtension;

THouse::THouse(void) : Subowner(0, 4, 5), Guest(0, 9, 10) {
	this->ID = 0;
	this->Name[0] = 0;
	this->Description[0] = 0;
	this->Size = 0;
	this->Rent = 0;
	this->DepotNr = 0;
	this->NoAuction = 0;
	this->GuildHouse = 0;
	this->ExitX = 0;
	this->ExitY = 0;
	this->ExitZ = 0;
	this->CenterX = 0;
	this->CenterY = 0;
	this->CenterZ = 0;
	this->OwnerID = 0;
	this->OwnerName[0] = 0;
	this->LastTransition = 0;
	this->PaidUntil = 0;
	this->Subowners = 0;
	this->Guests = 0;
	this->Help = 0;
}

THouse::THouse(const THouse &Other) : THouse() {
	this->operator=(Other);
}

void THouse::operator=(const THouse &Other){
	this->ID = Other.ID;
	memcpy(this->Name, Other.Name, sizeof(this->Name));
	memcpy(this->Description, Other.Description, sizeof(this->Description));
	this->Size = Other.Size;
	this->Rent = Other.Rent;
	this->DepotNr = Other.DepotNr;
	this->NoAuction = Other.NoAuction;
	this->GuildHouse = Other.GuildHouse;
	this->ExitX = Other.ExitX;
	this->ExitY = Other.ExitY;
	this->ExitZ = Other.ExitZ;
	this->CenterX = Other.CenterX;
	this->CenterY = Other.CenterY;
	this->CenterZ = Other.CenterZ;
	this->OwnerID = Other.OwnerID;
	memcpy(this->OwnerName, Other.OwnerName, sizeof(this->OwnerName));
	this->LastTransition = Other.LastTransition;
	this->PaidUntil = Other.PaidUntil;
	this->Help = Other.Help;

	this->Subowners = Other.Subowners;
	for(int i = 0; i < Other.Subowners; i += 1){
		*this->Subowner.at(i) = Other.Subowner.copyAt(i);
	}

	this->Guests = Other.Guests;
	for(int i = 0; i < Other.Guests; i += 1){
		*this->Guest.at(i) = Other.Guest.copyAt(i);
	}
}

THouseArea *GetHouseArea(uint16 ID){
	THouseArea *Result = NULL;
	for(int i = 0; i < HouseAreas; i += 1){
		if(HouseArea.at(i)->ID == ID){
			Result = HouseArea.at(i);
			break;
		}
	}

	if(Result == NULL){
		error("GetHouseArea: Gebiet mit ID %d nicht gefunden.\n", ID);
	}

	return Result;
}

int CheckAccessRight(const char *Rule, TPlayer *Player){
	if(Rule == NULL){
		error("CheckAccessRight: Regel ist NULL.\n");
		return 0; // NOT_APPLICABLE ?
	}

	if(Player == NULL){
		error("CheckAccessRight: pl ist NULL.\n");
		return 0; // NOT_APPLICABLE ?
	}

	bool Negate = Rule[0] == '!';
	if(Negate){
		Rule += 1;
	}

	bool Match;
	if(strchr(Rule, '@') == NULL){
		Match = MatchString(Rule, Player->Name);
	}else{
		if(Player->Guild[0] == 0){
			return 0; // NOT_APPLICABLE ?
		}

		char Help[200];
		if(Player->Rank[0] == 0){
			snprintf(Help, sizeof(Help), "@%s", Player->Guild);
		}else{
			snprintf(Help, sizeof(Help), "%s@%s", Player->Rank, Player->Guild);
		}

		Match = MatchString(Rule, Help);
	}

	if(!Match){
		return 0; // NOT_APPLICABLE ?
	}else if(Negate){
		return -1; // DENY_ACCESS ?
	}else{
		return 1; // ALLOW_ACCESS ?
	}
}

THouse *GetHouse(uint16 ID){
	// NOTE(fusion): A little binary search action.
	int Left = 0;
	int Right = Houses - 1;
	THouse *Result = NULL;
	while(Left <= Right){
		int Current = (Left + Right) / 2;
		uint16 CurrentID = House.at(Current)->ID;
		if(CurrentID < ID){
			Left = Current + 1;
		}else if(CurrentID > ID){
			Right = Current - 1;
		}else{
			Result = House.at(Current);
			break;
		}
	}

	if(Result == NULL){
		error("GetHouse: Haus mit ID %d nicht gefunden.\n", ID);
	}

	return Result;
}

bool IsOwner(uint16 HouseID, TPlayer *Player){
	THouse *House = GetHouse(HouseID);
	if(House == NULL){
		error("IsOwner: Haus mit ID %d existiert nicht.\n", HouseID);
		return false;
	}

	if(Player == NULL){
		error("IsOwner: pl ist NULL.\n");
		return false;
	}

	return House->OwnerID == Player->ID;
}

bool IsSubowner(uint16 HouseID, TPlayer *Player, int TimeStamp){
	THouse *House = GetHouse(HouseID);
	if(House == NULL){
		error("IsSubowner: Haus mit ID %d existiert nicht.\n", HouseID);
		return false;
	}

	if(Player == NULL){
		error("IsSubowner: pl ist NULL.\n");
		return false;
	}

	if(House->LastTransition > TimeStamp){
		return false;
	}

	// TODO(fusion): Should allow on a positive rule but keep looking for a
	// negative rule to make it order independent?
	for(int i = 0; i < House->Subowners; i += 1){
		int Access = CheckAccessRight(House->Subowner.at(i)->Name, Player);
		if(Access == -1){
			return false;
		}else if(Access == 1){
			return true;
		}
	}

	return false;
}

bool IsGuest(uint16 HouseID, TPlayer *Player, int TimeStamp){
	THouse *House = GetHouse(HouseID);
	if(House == NULL){
		error("IsGuest: Haus mit ID %d existiert nicht.\n", HouseID);
		return false;
	}

	if(Player == NULL){
		error("IsGuest: pl ist NULL.\n");
		return false;
	}

	if(House->LastTransition > TimeStamp){
		return false;
	}

	// TODO(fusion): Should allow on a positive rule but keep looking for a
	// negative rule to make it order independent?
	for(int i = 0; i < House->Guests; i += 1){
		int Access = CheckAccessRight(House->Guest.at(i)->Name, Player);
		if(Access == -1){
			return false;
		}else if(Access == 1){
			return true;
		}
	}

	return false;
}

bool IsInvited(uint16 HouseID, TPlayer *Player, int TimeStamp){
	if(Player == NULL){
		error("IsInvited: pl ist NULL.\n");
		return false;
	}

	return IsOwner(HouseID, Player)
		|| IsSubowner(HouseID, Player, TimeStamp)
		|| IsGuest(HouseID, Player, TimeStamp);
}

const char *GetHouseName(uint16 HouseID){
	THouse *House = GetHouse(HouseID);
	if(House == NULL){
		error("GetHouseName: Haus mit ID %d existiert nicht.\n", HouseID);
		return NULL;
	}

	return House->Name;
}

const char *GetHouseOwner(uint16 HouseID){
	THouse *House = GetHouse(HouseID);
	if(House == NULL){
		error("GetHouseOwner: Haus mit ID %d existiert nicht.\n", HouseID);
		return NULL;
	}

	return House->OwnerName;
}

// TODO(fusion): This function is unsafe like `strcpy`.
void ShowSubownerList(uint16 HouseID, TPlayer *Player, char *Buffer){
	THouse *House = GetHouse(HouseID);
	if(House == NULL){
		error("ShowSubownerList: Haus mit ID %d existiert nicht.\n", HouseID);
		throw ERROR;
	}

	if(Player == NULL){
		error("ShowSubownerList: pl ist NULL.\n");
		throw ERROR;
	}

	if(Buffer == NULL){
		error("ShowSubownerList: Buffer ist NULL.\n");
		throw ERROR;
	}

	if(!IsOwner(HouseID, Player)){
		throw NOTACCESSIBLE;
	}

	print(3, "Editiere Untermieterliste von Haus %d.\n", HouseID);
	sprintf(Buffer, "# Subowners of %s\n", House->Name);
	for(int i = 0; i < House->Subowners; i += 1){
		strcat(Buffer, House->Subowner.at(i)->Name);
		strcat(Buffer, "\n");
	}
}

// TODO(fusion): This function is unsafe like `strcpy`.
void ShowGuestList(uint16 HouseID, TPlayer *Player, char *Buffer){
	THouse *House = GetHouse(HouseID);
	if(House == NULL){
		error("ShowGuestList: Haus mit ID %d existiert nicht.\n", HouseID);
		throw ERROR;
	}

	if(Player == NULL){
		error("ShowGuestList: pl ist NULL.\n");
		throw ERROR;
	}

	if(Buffer == NULL){
		error("ShowGuestList: Buffer ist NULL.\n");
		throw ERROR;
	}

	if(!IsOwner(HouseID, Player) && (!IsSubowner(HouseID, Player, INT_MAX)
									|| !CheckRight(Player->ID, PREMIUM_ACCOUNT))){
		throw NOTACCESSIBLE;
	}

	print(3, "Editiere Gästeliste von Haus %d.\n", HouseID);
	sprintf(Buffer, "# Guests of %s\n", House->Name);
	for(int i = 0; i < House->Guests; i += 1){
		strcat(Buffer, House->Guest.at(i)->Name);
		strcat(Buffer, "\n");
	}
}

void ChangeSubowners(uint16 HouseID, TPlayer *Player, const char *Buffer){
	THouse *House = GetHouse(HouseID);
	if(House == NULL){
		error("ChangeSubowners: Haus mit ID %d existiert nicht.\n", HouseID);
		throw ERROR;
	}

	if(Player == NULL){
		error("ChangeSubowners: pl ist NULL.\n");
		throw ERROR;
	}

	if(Buffer == NULL){
		error("ChangeSubowners: Buffer ist NULL.\n");
		throw ERROR;
	}

	if(!IsOwner(HouseID, Player)){
		throw NOTACCESSIBLE;
	}

	Log("houses", "%s ändert Liste der Untermieter von Haus %d.\n", Player->Name, HouseID);

	House->Subowners = 0;
	for(int ReadPos = 0; Buffer[ReadPos] != 0;){
		const char *LineStart = &Buffer[ReadPos];
		const char *LineEnd = strchr(LineStart, '\n');

		int LineLength;
		if(LineEnd != NULL){
			LineLength = (int)(LineEnd - LineStart);
			ReadPos += LineLength + 1;
		}else{
			LineLength = (int)strlen(LineStart);
			ReadPos += LineLength;
		}

		// TODO(fusion): Ignore lines with whitespace only?
		if(LineStart[0] != '#' && LineLength > 0){
			if(LineLength >= MAX_HOUSE_GUEST_NAME){
				LineLength = MAX_HOUSE_GUEST_NAME - 1;
			}

			THouseGuest *Subowner = House->Subowner.at(House->Subowners);
			memcpy(Subowner->Name, LineStart, LineLength);
			Subowner->Name[LineLength] = 0;
			House->Subowners += 1;

			if(House->Subowners >= 10){ // MAX_SUBOWNERS ?
				break;
			}
		}
	}

	KickGuests(HouseID);
}

void ChangeGuests(uint16 HouseID, TPlayer *Player, const char *Buffer){
	THouse *House = GetHouse(HouseID);
	if(House == NULL){
		error("ChangeGuests: Haus mit ID %d existiert nicht.\n", HouseID);
		throw ERROR;
	}

	if(Player == NULL){
		error("ChangeGuests: pl ist NULL.\n");
		throw ERROR;
	}

	if(Buffer == NULL){
		error("ChangeGuests: Buffer ist NULL.\n");
		throw ERROR;
	}

	if(!IsOwner(HouseID, Player) && (!IsSubowner(HouseID, Player, INT_MAX)
									|| !CheckRight(Player->ID, PREMIUM_ACCOUNT))){
		throw NOTACCESSIBLE;
	}

	Log("houses", "%s ändert Liste der Gäste von Haus %d.\n", Player->Name, HouseID);

	House->Guests = 0;
	for(int ReadPos = 0; Buffer[ReadPos] != 0;){
		const char *LineStart = &Buffer[ReadPos];
		const char *LineEnd = strchr(LineStart, '\n');

		int LineLength;
		if(LineEnd != NULL){
			LineLength = (int)(LineEnd - LineStart);
			ReadPos += LineLength + 1;
		}else{
			LineLength = (int)strlen(LineStart);
			ReadPos += LineLength;
		}

		// TODO(fusion): Ignore lines with whitespace only?
		if(LineStart[0] != '#' && LineLength > 0){
			if(LineLength >= MAX_HOUSE_GUEST_NAME){
				LineLength = MAX_HOUSE_GUEST_NAME - 1;
			}

			THouseGuest *Guest = House->Guest.at(House->Guests);
			memcpy(Guest->Name, LineStart, LineLength);
			Guest->Name[LineLength] = 0;
			House->Guests += 1;

			if(House->Guests >= 100){ // MAX_GUESTS ?
				break;
			}
		}
	}

	KickGuests(HouseID);
}

void GetExitPosition(uint16 HouseID, int *x, int *y, int *z){
	THouse *House = GetHouse(HouseID);
	if(House == NULL){
		error("GetExitPosition: Haus mit ID %d existiert nicht.\n", HouseID);
		return;
	}

	*x = House->ExitX;
	*y = House->ExitY;
	*z = House->ExitZ;
}

void KickGuest(uint16 HouseID, TPlayer *Guest){
	THouse *House = GetHouse(HouseID);
	if(House == NULL){
		error("KickGuest(houses 1): Haus mit ID %d existiert nicht.\n", HouseID);
		throw ERROR;
	}

	if(Guest == NULL){
		error("KickGuest(houses 1): Guest ist NULL.\n");
		throw ERROR;
	}

	GraphicalEffect(Guest->CrObject, EFFECT_POFF);

	// TODO(fusion): Not sure what's going on here. Maybe the player is still
	// online but was getting into a bed?
	Object Bed = GetFirstObject(Guest->posx, Guest->posy, Guest->posz);
	while(Bed != NONE){
		if(Bed.getObjectType().getFlag(BED)){
			break;
		}
		Bed = Bed.getNextObject();
	}

	if(Bed != NONE && Bed.getAttribute(TEXTSTRING) != 0){
		print(3, "Spieler steht auf einem Bett, während er aus dem Haus gekickt wird.\n");
		try{
			UseObjects(0, Bed, Bed);
		}catch(RESULT r){
			error("KickGuest: Exception %d beim Aufräumen des Bettes.\n", r);
		}
	}

	Object Exit = GetMapContainer(House->ExitX, House->ExitY, House->ExitZ);
	Move(0, Guest->CrObject, Exit, -1, false, NONE);
	GraphicalEffect(Guest->CrObject, EFFECT_ENERGY);
}

void KickGuest(uint16 HouseID, TPlayer *Host, TPlayer *Guest){
	THouse *House = GetHouse(HouseID);
	if(House == NULL){
		error("KickGuest(houses 2): Haus mit ID %d existiert nicht.\n", HouseID);
		throw ERROR;
	}

	if(Host == NULL){
		error("KickGuest(houses 2): Host ist NULL.\n");
		throw ERROR;
	}

	if(Guest == NULL){
		error("KickGuest(houses 2): Guest ist NULL.\n");
		throw ERROR;
	}

	if(GetHouseID(Guest->posx, Guest->posy, Guest->posz) != HouseID){
		throw NOTACCESSIBLE;
	}

	if(Guest != Host){
		if(CheckRight(Guest->ID, ENTER_HOUSES)){
			throw NOTACCESSIBLE;
		}

		if(!IsOwner(HouseID, Host) && !IsSubowner(HouseID, Host, INT_MAX)){
			throw NOTACCESSIBLE;
		}

		if(IsOwner(HouseID, Guest)){
			throw NOTACCESSIBLE;
		}
	}

	KickGuest(HouseID, Guest);
}

void KickGuests(uint16 HouseID){
	THouse *House = GetHouse(HouseID);
	if(House == NULL){
		error("KickGuests: Haus mit ID %d existiert nicht.\n", HouseID);
		return;
	}

	// TODO(fusion): I think `MaxHouseX` and `MaxHouseY` are the maximum house
	// radii but it is a mistery as to why they're not stored INSIDE `THouse`.
	TFindCreatures Search(MaxHouseX, MaxHouseY, House->CenterX, House->CenterY, FIND_PLAYERS);
	while(true){
		uint32 CharacterID = Search.getNext();
		if(CharacterID == 0){
			break;
		}

		TPlayer *Player = GetPlayer(CharacterID);
		if(GetHouseID(Player->posx, Player->posy, Player->posz) == HouseID
				&& !IsInvited(HouseID, Player, INT_MAX)
				&& !CheckRight(CharacterID, ENTER_HOUSES)){
			KickGuest(HouseID, Player);
		}
	}
}

bool MayOpenDoor(Object Door, TPlayer *Player){
	if(!Door.exists()){
		error("MayOpenDoor: Tür existiert nicht.\n");
		return false;
	}

	if(Player == NULL){
		error("MayOpenDoor: pl ist NULL.\n");
		return false;
	}

	ObjectType DoorType = Door.getObjectType();
	if(!DoorType.getFlag(NAMEDOOR) || !DoorType.getFlag(TEXT)){
		error("MayOpenDoor: Tür ist keine NameDoor.\n");
		return false;
	}

	const char *Text = GetDynamicString(Door.getAttribute(TEXTSTRING));
	if(Text == NULL){
		return false;
	}

	// TODO(fusion): Should allow on a positive rule but keep looking for a
	// negative rule to make it order independent?
	for(int ReadPos = 0; Text[ReadPos] != 0;){
		const char *LineStart = &Text[ReadPos];
		const char *LineEnd = strchr(LineStart, '\n');

		int LineLength;
		if(LineEnd != NULL){
			LineLength = (int)(LineEnd - LineStart);
			ReadPos += LineLength + 1;
		}else{
			LineLength = (int)strlen(LineStart);
			ReadPos += LineLength;
		}

		// TODO(fusion): Ignore lines with whitespace only?
		if(LineStart[0] != '#' && LineLength > 0){
			if(LineLength >= MAX_HOUSE_GUEST_NAME){
				LineLength = MAX_HOUSE_GUEST_NAME - 1;
			}

			char Rule[MAX_HOUSE_GUEST_NAME];
			memcpy(Rule, LineStart, LineLength);
			Rule[LineLength] = 0;

			int Access = CheckAccessRight(Rule, Player);
			if(Access == -1){
				return false;
			}else if(Access == 1){
				return true;
			}
		}
	}

	return false;
}

// TODO(fusion): This function is unsafe like `strcpy`.
void ShowNameDoor(Object Door, TPlayer *Player, char *Buffer){
	if(!Door.exists()){
		error("ShowNameDoor: Tür existiert nicht.\n");
		throw ERROR;
	}

	if(Player == NULL){
		error("ShowNameDoor: pl ist NULL.\n");
		throw ERROR;
	}

	if(Buffer == NULL){
		error("ShowNameDoor: Buffer ist NULL.\n");
		throw ERROR;
	}

	int DoorX, DoorY, DoorZ;
	GetObjectCoordinates(Door, &DoorX, &DoorY, &DoorZ);
	uint16 HouseID = GetHouseID(DoorX, DoorY, DoorZ);
	if(HouseID == 0){
		error("ShowNameDoor: Tür auf Koordinate [%d,%d,%d] gehört zu keinem Haus.\n",
				DoorX, DoorY, DoorZ);
		throw ERROR;
	}

	if(!IsOwner(HouseID, Player)){
		print(3, "Spieler %s ist nicht Mieter des Hauses %d.\n",
				Player->Name, HouseID);
		throw NOTACCESSIBLE;
	}

	print(3, "Editiere NameDoor.\n");

	// TODO(fusion): Check for `NAMEDOOR` flag as well?
	if(!Door.getObjectType().getFlag(TEXT)){
		error("ShowNameDoor: Tür auf Koordinate [%d,%d,%d] enthält keinen Text.\n",
				DoorX, DoorY, DoorZ);
		throw ERROR;
	}

	const char *Text = GetDynamicString(Door.getAttribute(TEXTSTRING));
	if(Text != NULL){
		strcpy(Buffer, Text);
	}else{
		strcpy(Buffer, "# Players allowed to open this door\n");
	}
}

void ChangeNameDoor(Object Door, TPlayer *Player, const char *Buffer){
	if(!Door.exists()){
		error("ChangeNameDoor: Tür existiert nicht.\n");
		throw ERROR;
	}

	if(Player == NULL){
		error("ChangeNameDoor: pl ist NULL.\n");
		throw ERROR;
	}

	if(Buffer == NULL){
		error("ChangeNameDoor: Buffer ist NULL.\n");
		throw ERROR;
	}

	int DoorX, DoorY, DoorZ;
	GetObjectCoordinates(Door, &DoorX, &DoorY, &DoorZ);
	uint16 HouseID = GetHouseID(DoorX, DoorY, DoorZ);
	if(HouseID == 0){
		error("ChangeNameDoor: Tür auf Koordinate [%d,%d,%d] gehört zu keinem Haus.\n",
				DoorX, DoorY, DoorZ);
		throw ERROR;
	}

	if(!IsOwner(HouseID, Player)){
		throw NOTACCESSIBLE;
	}

	// TODO(fusion): Check for `NAMEDOOR` flag as well?
	if(!Door.getObjectType().getFlag(TEXT)){
		error("ChangeNameDoor: Tür auf Koordinate [%d,%d,%d] enthält keinen Text.\n",
				DoorX, DoorY, DoorZ);
		throw ERROR;
	}

	Log("houses", "%s ändert NameDoor von Haus %d auf Koordinate [%d,%d,%d].\n",
			Player->Name, HouseID, DoorX, DoorY, DoorZ);

	DeleteDynamicString(Door.getAttribute(TEXTSTRING));
	Change(Door, TEXTSTRING, AddDynamicString(Buffer));
}

static void CreateCoins(Object Con, ObjectType Type, int Amount){
	// TODO(fusion): Can we guarantee `Amount` is in the [0, 100] range?
	Object Obj = SetObject(Con, Type, 0);
	ChangeObject(Obj, AMOUNT, Amount);
}

static int DeleteCoins(Object Con, ObjectType Type, int Amount){
	Object Obj = GetFirstContainerObject(Con);
	while(Amount > 0 && Obj != NONE){
		Object Next = Obj.getNextObject();
		ObjectType ObjType = Obj.getObjectType();
		if(ObjType == Type){
			// TODO(fusion): `Change` will notify spectators while `DeleteObject`
			// will not, so there is an asymmetry here. We also don't check if
			// `Type` is `CUMULATE` but I suppose coins are implicitly `CUMULATE`.
			int ObjAmount = Obj.getAttribute(AMOUNT);
			if(ObjAmount > Amount){
				Change(Obj, AMOUNT, (ObjAmount - Amount));
				Amount = 0;
			}else{
				DeleteObject(Obj);
				Amount -= ObjAmount;
			}
		}else if(ObjType.getFlag(CONTAINER)){
			Amount = DeleteCoins(Obj, Type, Amount);
		}
		Obj = Next;
	}
	return Amount;
}

static void CreateMoney(Object Con, int Amount){
	int Crystal  = (Amount / 10000);
	int Platinum = (Amount % 10000) / 100;
	int Gold     = (Amount % 10000) % 100;

	while(Crystal > 0){
		int StackAmount = std::min<int>(Crystal, 100);
		CreateCoins(Con, GetSpecialObject(MONEY_TENTHOUSAND), StackAmount);
		Crystal -= StackAmount;
	}

	if(Platinum > 0){
		CreateCoins(Con, GetSpecialObject(MONEY_HUNDRED), Platinum);
	}

	if(Gold > 0){
		CreateCoins(Con, GetSpecialObject(MONEY_ONE), Gold);
	}
}

static void DeleteMoney(Object Con, int Amount){
	int Crystal  = CountObjects(GetFirstContainerObject(Con), GetSpecialObject(MONEY_TENTHOUSAND), 0);
	int Platinum = CountObjects(GetFirstContainerObject(Con), GetSpecialObject(MONEY_HUNDRED), 0);
	int Gold     = CountObjects(GetFirstContainerObject(Con), GetSpecialObject(MONEY_ONE), 0);
	CalculateChange(Amount, &Gold, &Platinum, &Crystal);

	if(Gold > 0){
		DeleteCoins(Con, GetSpecialObject(MONEY_ONE), Gold);
	}else if(Gold < 0){
		CreateCoins(Con, GetSpecialObject(MONEY_ONE), -Gold);
	}

	if(Platinum > 0){
		DeleteCoins(Con, GetSpecialObject(MONEY_HUNDRED), Platinum);
	}else if(Platinum < 0){
		CreateCoins(Con, GetSpecialObject(MONEY_HUNDRED), -Platinum);
	}

	ASSERT(Crystal >= 0);
	if(Crystal > 0){
		DeleteCoins(Con, GetSpecialObject(MONEY_TENTHOUSAND), Crystal);
	}
}

static Object CreateTempDepot(void){
	// TODO(fusion): All house processing functions use this temporary box placed
	// at the starting position in Rookgaard when loading player depots. It could
	// be problematic in the wrong circumstances (mostly exceptions) but doing
	// it differently at this point would just increase complexity. We just need
	// to make sure this box is destroyed at the end of any house processing.
	int TempX, TempY, TempZ;
	GetStartPosition(&TempX, &TempY, &TempZ, true);
	Object TempDepot = SetObject(GetMapContainer(TempX, TempY, TempZ),
							GetSpecialObject(DEPOT_LOCKER), 0);
	return TempDepot;
}

// NOTE(fusion): This is used by house processing functions to clear the temporary
// depot container after processing player depots.
static void DeleteContainerObjects(Object Con){
	Object Obj = GetFirstContainerObject(Con);
	while(Obj != NONE){
		Object Next = Obj.getNextObject();
		DeleteObject(Obj);
		Obj = Next;
	}
}

void CleanField(int x, int y, int z, Object Depot){
	if(Depot == NONE){
		error("CleanField: Depot-Box existiert nicht.\n");
		return;
	}

	Object Obj = GetFirstObject(x, y, z);
	while(Obj != NONE){
		Object Next = Obj.getNextObject();
		ObjectType ObjType = Obj.getObjectType();

		if(ObjType.getFlag(NAMEDOOR) && ObjType.getFlag(TEXT)){
			// TODO(fusion): I don't think we even set `EDITOR` for name doors
			// at all. We're also using `Change` which notifies clients, whereas
			// below we're using non notifying functions like `MoveObject` and
			// `DeleteObject`.
			DeleteDynamicString(Obj.getAttribute(TEXTSTRING));
			DeleteDynamicString(Obj.getAttribute(EDITOR));
			Change(Obj, TEXTSTRING, 0);
			Change(Obj, EDITOR, 0);
		}

		if(!ObjType.getFlag(UNMOVE) && ObjType.getFlag(TAKE)){
			MoveObject(Obj, Depot);
		}else{
			if(ObjType.getFlag(CONTAINER)){
				Object Help = GetFirstContainerObject(Obj);
				while(Help != NONE){
					Object HelpNext = Help.getNextObject();
					MoveObject(Help, Depot);
					Help = HelpNext;
				}
			}

			if(!ObjType.getFlag(UNMOVE)){
				DeleteObject(Obj);
			}
		}

		Obj = Next;
	}
}

void CleanHouse(THouse *House, TPlayerData *PlayerData){
	if(House == NULL){
		error("CleanHouse: house ist NULL.\n");
		return;
	}

	if(House->OwnerID == 0){
		error("CleanHouse: Haus %d hat keinen Besitzer.\n", House->ID);
		return;
	}

	bool PlayerDataAssigned = false;
	if(PlayerData == NULL){
		PlayerData = AssignPlayerPoolSlot(House->OwnerID, false);
		if(PlayerData == NULL){
			error("CleanHouse: Kann keinen Slot für Spielerdaten zuweisen.\n");
			return;
		}

		PlayerDataAssigned = true;
	}

	Object TempDepot = CreateTempDepot();
	LoadDepot(PlayerData, House->DepotNr, TempDepot);

	int MinX = House->CenterX - MaxHouseX;
	int MaxX = House->CenterX + MaxHouseX;
	int MinY = House->CenterY - MaxHouseY;
	int MaxY = House->CenterY + MaxHouseY;
	int MinZ = SectorZMin;
	int MaxZ = SectorZMax;
	for(int FieldZ = MinZ; FieldZ <= MaxZ; FieldZ += 1)
	for(int FieldY = MinY; FieldY <= MaxY; FieldY += 1)
	for(int FieldX = MinX; FieldX <= MaxX; FieldX += 1){
		bool HouseField = (GetHouseID(FieldX, FieldY, FieldZ) == House->ID);

		if(!HouseField && CoordinateFlag(FieldX, FieldY, FieldZ, HOOKSOUTH)){
			HouseField = (GetHouseID(FieldX - 1, FieldY + 1, FieldZ) == House->ID
					||    GetHouseID(FieldX,     FieldY + 1, FieldZ) == House->ID
					||    GetHouseID(FieldX + 1, FieldY + 1, FieldZ) == House->ID);
		}

		if(!HouseField && CoordinateFlag(FieldX, FieldY, FieldZ, HOOKEAST)){
			HouseField = (GetHouseID(FieldX + 1, FieldY - 1, FieldZ) == House->ID
					||    GetHouseID(FieldX + 1, FieldY,     FieldZ) == House->ID
					||    GetHouseID(FieldX + 1, FieldY + 1, FieldZ) == House->ID);
		}

		if(HouseField){
			CleanField(FieldX, FieldY, FieldZ, TempDepot);
		}
	}

	SaveDepot(PlayerData, House->DepotNr, TempDepot);
	DeleteObject(TempDepot);

	if(PlayerDataAssigned){
		ReleasePlayerPoolSlot(PlayerData);
	}
}

void ClearHouse(THouse *House){
	House->OwnerID = 0;
	House->OwnerName[0] = 0;
	House->Subowners = 0;
	House->Guests = 0;
}

bool FinishAuctions(void){
	Log("houses","Bearbeite beendete Auktionen...\n");

	// TODO(fusion): It could be worse.
	int NumberOfAuctions       = Houses;
	uint16 *HouseIDs           = (uint16*)alloca(NumberOfAuctions * sizeof(uint16));
	uint32 *CharacterIDs       = (uint32*)alloca(NumberOfAuctions * sizeof(uint32));
	char (*CharacterNames)[30] = (char(*)[30])alloca(NumberOfAuctions * 30);
	int *Bids                  = (int*)alloca(NumberOfAuctions * sizeof(int));
	int Ret = QueryManagerConnection->finishAuctions(&NumberOfAuctions,
							HouseIDs, CharacterIDs, CharacterNames, Bids);
	if(Ret != 0){
		error("FinishAuctions: Kann Versteigerungen nicht ermitteln.\n");
		return false;
	}

	// TODO(fusion): `AddSlashes` will add escape slashes so it should probably
	// be called `EscapeString` or something similar.
	char HelpWorld[60];
	AddSlashes(HelpWorld, WorldName);

	int TimeStamp = (int)time(NULL);
	Object TempDepot = CreateTempDepot();
	for(int AuctionNr = 0; AuctionNr < NumberOfAuctions; AuctionNr += 1){
		uint16 HouseID = HouseIDs[AuctionNr];
		uint32 CharacterID = CharacterIDs[AuctionNr];
		const char *CharacterName = CharacterNames[AuctionNr];
		int Bid = Bids[AuctionNr];

		Log("houses", "Auktion für Haus %d beendet: Ersteigert von %s für %d Gold.\n",
				HouseID, CharacterName, Bid);
		Log("houses", "bei Reset: INSERT INTO HouseAssignments VALUES ('%s',%d,%d,%d);\n",
				HelpWorld, HouseID, CharacterID, Bid);

		THouse *House = GetHouse(HouseID);
		if(House == NULL){
			error("FinishAuctions: Haus mit Nummer %d existiert nicht.\n",
					HouseID);
			continue;
		}

		if(House->OwnerID != 0){
			error("FinishAuctions: Haus mit Nummer %d gehört schon Charakter %u.\n",
					HouseID, House->OwnerID);
			continue;
		}

		TPlayerData *PlayerData = AssignPlayerPoolSlot(CharacterID, false);
		if(PlayerData == NULL){
			error("FinishAuctions: Kann keinen Slot für Spielerdaten zuweisen.\n");
			continue;
		}

		LoadDepot(PlayerData, House->DepotNr, TempDepot);
		int DepotMoney = CountMoney(GetFirstContainerObject(TempDepot));
		if(DepotMoney < (House->Rent + Bid)){
			Log("houses", "Ersteigerer hat nicht genügend Geld.\n");
			House->OwnerID = CharacterID;
			strcpy(House->OwnerName, CharacterName);
			House->PaidUntil = 0;
		}else{
			Log("houses", "Ersteigerer hat genügend Geld.\n");
			DeleteMoney(TempDepot, (House->Rent + Bid));

			char WelcomeMessage[500];
			snprintf(WelcomeMessage, sizeof(WelcomeMessage),
					"Welcome!\n"
					"\n"
					"Congratulations on your choice\n"
					"for house \"%s\".\n"
					"The rent for the first month\n"
					"has already been debited to your\n"
					"depot. The next rent will be\n"
					"payable in thirty days.\n"
					"Have a good time in your new home!",
					House->Name);
			Object WelcomeLetter = SetObject(TempDepot, GetSpecialObject(LETTER_STAMPED), 0);
			ChangeObject(WelcomeLetter, TEXTSTRING, AddDynamicString(WelcomeMessage));
			SaveDepot(PlayerData, House->DepotNr, TempDepot);

			House->OwnerID = CharacterID;
			strcpy(House->OwnerName, CharacterName);
			House->LastTransition = TimeStamp;
			House->PaidUntil = TimeStamp + (30 * 24 * 60 * 60); // one month
		}

		DeleteContainerObjects(TempDepot);
		ReleasePlayerPoolSlot(PlayerData);
	}

	DeleteObject(TempDepot);

	for(int HouseNr = 0; HouseNr < Houses; HouseNr += 1){
		THouse *H = House.at(HouseNr);
		if(H->PaidUntil == 0 && H->OwnerID != 0){
			if(QueryManagerConnection->excludeFromAuctions(H->OwnerID, true) != 0){
				error("FinishAuctions: Kann Ersteigerer nicht verbannen.\n");
			}

			ClearHouse(H);
		}
	}

	return true;
}

bool TransferHouses(void){
	Log("houses", "Bearbeite freiwillige Kündigungen...\n");
	int NumberOfTransfers     = Houses;
	uint16 *HouseIDs          = (uint16*)alloca(NumberOfTransfers * sizeof(uint16));
	uint32 *NewOwnerIDs       = (uint32*)alloca(NumberOfTransfers * sizeof(uint32));
	char (*NewOwnerNames)[30] = (char(*)[30])alloca(NumberOfTransfers * 30);
	int *Prices               = (int*)alloca(NumberOfTransfers * sizeof(int));
	int Ret = QueryManagerConnection->transferHouses(&NumberOfTransfers,
							HouseIDs, NewOwnerIDs, NewOwnerNames, Prices);
	if(Ret != 0){
		error("CollectRents: Kann Kündigungen nicht ermitteln.\n");
		return false;
	}

	char HelpWorld[60];
	AddSlashes(HelpWorld, WorldName);

	int TimeStamp = (int)time(NULL);
	Object TempDepot = CreateTempDepot();
	for(int TransferNr = 0; TransferNr < NumberOfTransfers; TransferNr += 1){
		uint16 HouseID = HouseIDs[TransferNr];
		uint32 NewOwnerID = NewOwnerIDs[TransferNr];
		const char *NewOwnerName = NewOwnerNames[TransferNr];
		int TransferPrice = Prices[TransferNr];

		THouse *House = GetHouse(HouseID);
		if(House == NULL){
			error("CollectRents: Haus mit Nummer %d existiert nicht.\n", HouseID);
			continue;
		}

		if(NewOwnerID != 0 && TransferPrice > 0){
			Log("houses", "Verkaufe Haus %d an %u für %d Gold.\n",
					HouseID, NewOwnerID, TransferPrice);
			TPlayerData *PlayerData = AssignPlayerPoolSlot(NewOwnerID, false);
			if(PlayerData == NULL){
				error("CollectRents: Kann keinen Slot für Spielerdaten zuweisen (1a).\n");
				continue;
			}

			LoadDepot(PlayerData, House->DepotNr, TempDepot);
			int DepotMoney = CountMoney(GetFirstContainerObject(TempDepot));
			if(DepotMoney < TransferPrice){
				Log("houses", "Käufer hat nicht genügend Geld.\n");

				char WarningMessage[500];
				snprintf(WarningMessage, sizeof(WarningMessage),
						"Warning!\n"
						"\n"
						"You have not enough gold to pay\n"
						"the price for house \"%s\".\n"
						"Therefore the transfer is cancelled.",
						House->Name);
				Object WarningLetter = SetObject(TempDepot, GetSpecialObject(LETTER_STAMPED), 0);
				ChangeObject(WarningLetter, TEXTSTRING, AddDynamicString(WarningMessage));
				SaveDepot(PlayerData, House->DepotNr, TempDepot);
				DeleteContainerObjects(TempDepot);
				ReleasePlayerPoolSlot(PlayerData);

				Log("houses", "bei Reset: UPDATE HouseOwners SET Termination=null,NewOwner=null,Accepted=0,Price=null WHERE World=\'%s\' AND HouseID=%d;\n",
						HelpWorld, HouseID);
				QueryManagerConnection->cancelHouseTransfer(HouseID);
				continue;
			}

			DeleteMoney(TempDepot, TransferPrice);
			SaveDepot(PlayerData, House->DepotNr, TempDepot);
			DeleteContainerObjects(TempDepot);
			ReleasePlayerPoolSlot(PlayerData);

			PlayerData = AssignPlayerPoolSlot(House->OwnerID, false);
			if(PlayerData == NULL){
				error("CollectRents: Kann keinen Slot für Spielerdaten zuweisen (1b).\n");
				continue;
			}

			LoadDepot(PlayerData, House->DepotNr, TempDepot);
			CreateMoney(TempDepot, TransferPrice);
			SaveDepot(PlayerData, House->DepotNr, TempDepot);
			DeleteContainerObjects(TempDepot);
			ReleasePlayerPoolSlot(PlayerData);
		}

		// TODO(fusion): The old owner needs to manually move out when transfering
		// to a new owner?
		Log("houses", "Räume Haus %d von %u.\n", HouseID, House->OwnerID);
		if(NewOwnerID == 0){
			CleanHouse(House, NULL);
		}

		ClearHouse(House);
		House->Help = 1;

		if(NewOwnerID != 0){
			Log("houses", "Übertrage Haus %d an %u.\n", HouseID, NewOwnerID);
			House->OwnerID = NewOwnerID;
			strcpy(House->OwnerName, NewOwnerName);
			House->LastTransition = TimeStamp;
			Log("houses", "bei Reset: UPDATE HouseOwners SET Termination=%d,NewOwner=%u,Accepted=1,Price=%d WHERE World='%s' AND HouseID=%d;\n",
					TimeStamp, NewOwnerID, TransferPrice, HelpWorld, HouseID);
		}else{
			Log("houses", "bei Reset: UPDATE HouseOwners SET Termination=%d,NewOwner=null,Accepted=0,Price=0 WHERE World=\'%s\' AND HouseID=%d;\n",
					TimeStamp, HelpWorld, HouseID);
		}
	}

	DeleteObject(TempDepot);
	return true;
}

bool EvictFreeAccounts(void){
	int NumberOfEvictions = Houses;
	uint16 *HouseIDs      = (uint16*)alloca(NumberOfEvictions * sizeof(uint16));
	uint32 *OwnerIDs      = (uint32*)alloca(NumberOfEvictions * sizeof(uint32));
	int Ret = QueryManagerConnection->evictFreeAccounts(&NumberOfEvictions, HouseIDs, OwnerIDs);
	if(Ret != 0){
		error("CollectRents: Kann Zahlungsdaten nicht ermitteln.\n");
		return false;
	}

	for(int EvictionNr = 0; EvictionNr < NumberOfEvictions; EvictionNr += 1){
		uint16 HouseID = HouseIDs[EvictionNr];
		uint32 OwnerID = OwnerIDs[EvictionNr];

		THouse *House = GetHouse(HouseID);
		if(House == NULL){
			error("CollectRents: Haus mit Nummer %d existiert nicht.\n", HouseID);
			continue;
		}

		Log("houses", "Account von Haus %d ist nicht mehr bezahlt.\n", HouseID);
		if(OwnerID != House->OwnerID){
			Log("houses", "... aber Haus wurde eben noch übertragen.\n");
			continue;
		}

		CleanHouse(House, NULL);
		ClearHouse(House);
		House->Help = 1;
	}

	return true;
}

bool EvictDeletedCharacters(void){
	int NumberOfEvictions = Houses;
	uint16 *HouseIDs      = (uint16*)alloca(NumberOfEvictions * sizeof(uint16));
	int Ret = QueryManagerConnection->evictDeletedCharacters(&NumberOfEvictions, HouseIDs);
	if(Ret != 0){
		error("CollectRents: Kann gelöschte Charaktere nicht ermitteln.\n");
		return false;
	}

	for(int EvictionNr = 0; EvictionNr < NumberOfEvictions; EvictionNr += 1){
		uint16 HouseID = HouseIDs[EvictionNr];
		THouse *House = GetHouse(HouseID);
		if(House == NULL){
			error("CollectRents: Haus mit Nummer %d existiert nicht.\n", HouseID);
			continue;
		}

		Log("houses", "Besitzer von Haus %d ist gelöscht.\n", HouseID);
		CleanHouse(House, NULL);
		ClearHouse(House);
		House->Help = 1;
	}

	return true;
}

bool EvictExGuildLeaders(void){
	int NumberOfGuildHouses = 0;
	uint16 *HouseIDs        = (uint16*)alloca(Houses * sizeof(uint16));
	uint32 *GuildLeaders    = (uint32*)alloca(Houses * sizeof(uint32));
	for(int HouseNr = 0; HouseNr < Houses; HouseNr += 1){
		THouse *H = House.at(HouseNr);
		if(H->GuildHouse && H->OwnerID != 0){
			HouseIDs[NumberOfGuildHouses] = H->ID;
			GuildLeaders[NumberOfGuildHouses] = H->OwnerID;
			NumberOfGuildHouses += 1;
		}
	}

	if(NumberOfGuildHouses > 0){
		int NumberOfEvictions;
		int Ret = QueryManagerConnection->evictExGuildleaders(
				NumberOfGuildHouses, &NumberOfEvictions, HouseIDs, GuildLeaders);
		if(Ret != 0){
			error("CollectRents: Kann Gildenränge nicht ermitteln.\n");
			return false;
		}

		for(int EvictionNr = 0; EvictionNr < NumberOfEvictions; EvictionNr += 1){
			uint16 HouseID = HouseIDs[EvictionNr];
			THouse *House = GetHouse(HouseID);
			if(House == NULL){
				error("CollectRents: Haus mit Nummer %d existiert nicht.\n", HouseID);
				continue;
			}

			Log("houses", "Mieter von Gildenhaus %d ist kein Gildenführer mehr.\n", HouseID);
			CleanHouse(House, NULL);
			ClearHouse(House);
			House->Help = 1;
		}
	}

	return true;
}

void CollectRent(void){
	Log("houses", "Treibe Mieten ein...\n");
	int TimeStamp = (int)time(NULL);
	Object TempDepot = CreateTempDepot();
	for(int HouseNr = 0; HouseNr < Houses; HouseNr += 1){
		THouse *H = House.at(HouseNr);
		if(H->OwnerID == 0 || H->PaidUntil > TimeStamp){
			continue;
		}

		Log("houses", "Treibe Miete für Haus %d von %u ein.\n", H->ID, H->OwnerID);

		int Deadline = H->PaidUntil + (7 * 24 * 60 * 60); // 1 week notice
		// TODO(fusion): How is `PaymentExtension` set? This doesn't make a lot of sense.
		if(Deadline < PaymentExtension){
			Deadline = PaymentExtension;
		}

		TPlayerData *PlayerData = AssignPlayerPoolSlot(H->OwnerID, false);
		if(PlayerData == NULL){
			error("CollectRents: Kann keinen Slot für Spielerdaten zuweisen (2).\n");
			continue;
		}

		LoadDepot(PlayerData, H->DepotNr, TempDepot);
		int DepotMoney = CountMoney(GetFirstContainerObject(TempDepot));
		if(DepotMoney < H->Rent){
			if(TimeStamp < Deadline){
				Log("houses", "Mieter erhält Mahnung.\n");

				char WarningMessage[500];
				int DaysLeft = 1 + ((Deadline - TimeStamp - 3600) / 86400);
				snprintf(WarningMessage, sizeof(WarningMessage),
						"Warning!\n"
						"\n"
						"The monthly rent of %d gold\n"
						"for your house \"%s\"\n"
						"is payable. Have it available\n"
						"within %d day%s, or you will\n"
						"lose this house.",
						H->Rent, H->Name, DaysLeft, (DaysLeft != 1 ? "s" : ""));
				Object WarningLetter = SetObject(TempDepot, GetSpecialObject(LETTER_STAMPED), 0);
				ChangeObject(WarningLetter, TEXTSTRING, AddDynamicString(WarningMessage));
				SaveDepot(PlayerData, H->DepotNr, TempDepot);
			}else{
				Log("houses", "Mieter wird hinausgeworfen.\n");

				if(QueryManagerConnection->excludeFromAuctions(H->OwnerID, false) != 0){
					error("CollectRents: Kann Mieter nicht verbannen.\n");
				}

				CleanHouse(H, PlayerData);
				ClearHouse(H);
			}
		}else{
			Log("houses", "Mieter hat genügend Geld.\n");
			DeleteMoney(TempDepot, H->Rent);
			SaveDepot(PlayerData, H->DepotNr, TempDepot);
			H->PaidUntil += 30 * 24 * 60 * 60; // one month
		}

		DeleteContainerObjects(TempDepot);
		ReleasePlayerPoolSlot(PlayerData);
	}
	DeleteObject(TempDepot);
}

void ProcessRent(void){
	for(int HouseNr = 0; HouseNr < Houses; HouseNr += 1){
		House.at(HouseNr)->Help = 0;
	}

	// TODO(fusion): These functions were all inlined here but each one did some
	// stack allocation and it was a complete mess. I'd assume they were using
	// variable length arrays instead of `alloca` which makes sense but it's not
	// widely supported in C++.
	if(!TransferHouses()
			|| !EvictFreeAccounts()
			|| !EvictDeletedCharacters()
			|| !EvictExGuildLeaders()){
		return;
	}

	for(int HouseNr = 0; HouseNr < Houses; HouseNr += 1){
		THouse *H = House.at(HouseNr);
		if(H->Help == 1){
			if(QueryManagerConnection->deleteHouseOwner(H->ID) != 0){
				error("CollectRents: Kann Haus %d nicht aus HouseOwners austragen.\n", H->ID);
			}
		}
	}

	CollectRent();
}

bool StartAuctions(void){
	Log("houses", "Starte neue Versteigerungen...\n");

	int NumberOfAuctions = Houses;
	uint16 *HouseIDs     = (uint16*)alloca(NumberOfAuctions * sizeof(uint16));
	int Ret = QueryManagerConnection->getAuctions(&NumberOfAuctions, HouseIDs);
	if(Ret != 0){
		error("StartAuctions: Kann laufende Versteigerungen nicht ermitteln.\n");
		return false;
	}

	for(int HouseNr = 0; HouseNr < Houses; HouseNr += 1){
		House.at(HouseNr)->Help = 0;
	}

	for(int AuctionNr = 0; AuctionNr < NumberOfAuctions; AuctionNr += 1){
		THouse *House = GetHouse(HouseIDs[AuctionNr]);
		if(House == NULL){
			error("StartAuctions: Haus mit Nummer %d existiert nicht (1).\n", HouseIDs[AuctionNr]);
			continue;
		}

		House->Help = 1;
	}

	char HelpWorld[60];
	AddSlashes(HelpWorld, WorldName);
	for(int HouseNr = 0; HouseNr < Houses; HouseNr += 1){
		THouse *H = House.at(HouseNr);
		if(H->OwnerID == 0 && H->Help == 0 && !H->NoAuction){
			Log("houses", "Trage Haus %d zur Versteigerung ein.\n", H->ID);
			Log("houses", "bei Reset: DELETE FROM Auctions WHERE World=\'%s\' AND HouseID=%d;\n",
					HelpWorld, H->ID);
			if(QueryManagerConnection->startAuction(H->ID) != 0){
				error("StartAuctions: Kann Haus %d nicht zur Versteigerung eintragen.\n", H->ID);
			}
		}
	}

	return true;
}

bool UpdateHouseOwners(void){
	int NumberOfOwners     = Houses;
	uint16 *HouseIDs       = (uint16*)alloca(NumberOfOwners * sizeof(uint16));
	uint32 *OwnerIDs       = (uint32*)alloca(NumberOfOwners * sizeof(uint32));
	char (*OwnerNames)[30] = (char(*)[30])alloca(NumberOfOwners * 30);
	int *PaidUntils        = (int*)alloca(NumberOfOwners * sizeof(int));
	int Ret = QueryManagerConnection->getHouseOwners(&NumberOfOwners,
						HouseIDs, OwnerIDs, OwnerNames, PaidUntils);
	if(Ret != 0){
		error("StartAuctions: Kann eingetragene vermietete Häuser nicht ermitteln.\n");
		return false;
	}

	for(int HouseNr = 0; HouseNr < Houses; HouseNr += 1){
		House.at(HouseNr)->Help = 0;
	}

	for(int OwnerNr = 0; OwnerNr < NumberOfOwners; OwnerNr += 1){
		THouse *House = GetHouse(HouseIDs[OwnerNr]);
		if(House == NULL){
			error("StartAuctions: Haus mit Nummer %d existiert nicht (2).\n", HouseIDs[OwnerNr]);
			continue;
		}

		if(House->OwnerID == OwnerIDs[OwnerNr] && House->PaidUntil == PaidUntils[OwnerNr]){
			House->Help = 1;
		}else{
			House->Help = 2;
		}
	}

	Log("houses", "Aktualisiere Liste der Mieter...\n");
	for(int HouseNr = 0; HouseNr < Houses; HouseNr += 1){
		THouse *H = House.at(HouseNr);

		if(H->OwnerID == 0 && H->Help != 0){
			Log("houses", "Trage nicht mehr vermietetes Haus %d aus.\n", H->ID);
			if(QueryManagerConnection->deleteHouseOwner(H->ID) != 0){
				error("StartAuctions: Kann nicht mehr vermietetes Haus %d nicht austragen.\n", H->ID);
			}
		}

		if(H->OwnerID != 0 && H->Help == 0){
			Log("houses", "Trage vermietetes Haus %d ein.\n", H->ID);
			if(QueryManagerConnection->insertHouseOwner(H->ID, H->OwnerID, H->PaidUntil) != 0){
				error("StartAuctions: Kann vermietetes Haus %d nicht eintragen.\n", H->ID);
			}
		}

		if(H->Help == 2){
			Log("houses", "Aktualisiere vermietetes Haus %d.\n", H->ID);
			if(QueryManagerConnection->updateHouseOwner(H->ID, H->OwnerID, H->PaidUntil) != 0){
				error("StartAuctions: Kann Daten des vermieteten Hauses %d nicht aktualisieren.\n", H->ID);
			}
		}
	}

	return true;
}

void PrepareHouseCleanup(void){
	HelpDepots = 0;
}

void FinishHouseCleanup(void){
	for(int HelpDepotNr = 0; HelpDepotNr < HelpDepots; HelpDepotNr += 1){
		THelpDepot *Help = HelpDepot.at(HelpDepotNr);
		TPlayerData *PlayerData = AssignPlayerPoolSlot(Help->CharacterID, false);
		if(PlayerData == NULL){
			error("FinishHouseCleanup: Kann keinen Slot für Spielerdaten zuweisen.\n");
			continue;
		}

		SaveDepot(PlayerData, Help->DepotNr, Help->Box);
		DeleteObject(Help->Box);
		ReleasePlayerPoolSlot(PlayerData);
	}

	HelpDepots = 0;
}

void CleanHouseField(int x, int y, int z){
	uint16 HouseID = GetHouseID(x, y, z);

	if(HouseID == 0 && CoordinateFlag(x, y, z, HOOKSOUTH)){
		HouseID = GetHouseID(x, y + 1, z);

		if(HouseID == 0){
			HouseID = GetHouseID(x - 1, y + 1, z);
		}

		if(HouseID == 0){
			HouseID = GetHouseID(x + 1, y + 1, z);
		}
	}

	if(HouseID == 0 && CoordinateFlag(x, y, z, HOOKEAST)){
		HouseID = GetHouseID(x + 1, y, z);

		if(HouseID == 0){
			HouseID = GetHouseID(x + 1, y - 1, z);
		}

		if(HouseID == 0){
			HouseID = GetHouseID(x + 1, y + 1, z);
		}
	}

	if(HouseID == 0){
		error("CleanHouseField: Kein Haus zu Feld [%d,%d,%d] gefunden.\n", x, y, z);
		return;
	}

	THouse *House = GetHouse(HouseID);
	if(House == NULL || House->OwnerID == 0){
		return;
	}

	int HelpDepotNr = 0;
	while(HelpDepotNr < HelpDepots){
		THelpDepot *Help = HelpDepot.at(HelpDepotNr);
		if(Help->CharacterID == House->OwnerID && Help->DepotNr == House->DepotNr){
			break;
		}
		HelpDepotNr += 1;
	}

	if(HelpDepotNr >= HelpDepots){
		TPlayerData *PlayerData = AssignPlayerPoolSlot(House->OwnerID, false);
		if(PlayerData == NULL){
			error("CleanHouseField: Kann keinen Slot für Spielerdaten zuweisen.\n");
			return;
		}

		HelpDepotNr = HelpDepots;
		HelpDepots += 1;

		THelpDepot *Help = HelpDepot.at(HelpDepotNr);
		Help->CharacterID = House->OwnerID;
		Help->DepotNr = House->DepotNr;
		Help->Box = CreateTempDepot();
		LoadDepot(PlayerData, House->DepotNr, Help->Box);
		ReleasePlayerPoolSlot(PlayerData);
	}

	CleanField(x, y, z, HelpDepot.at(HelpDepotNr)->Box);
}

void LoadHouseAreas(void){
	Log("houses", "Lade Daten der Häusergebiete...\n");

	char FileName[4096];
	snprintf(FileName, sizeof(FileName), "%s/houseareas.dat", DATAPATH);
	if(!FileExists(FileName)){
		Log("houses", "Keine Daten für Häusergebiete gefunden.\n");
		return;
	}

	TReadScriptFile Script;
	Script.open(FileName);
	while(true){
		Script.nextToken();
		if(Script.Token == ENDOFFILE){
			Script.close();
			break;
		}

		// TODO(fusion): Any identifier? Seems that `Area` is used so we should
		// probably check that?.
		//if(strcmp(Script.getIdentifier(), "area") != 0){
		//	Script.error("area expected");
		//}
		Script.getIdentifier();

		THouseArea *Area = HouseArea.at(HouseAreas);
		Script.readSymbol('=');
		Script.readSymbol('(');
		Area->ID = (uint16)Script.readNumber();
		Script.readSymbol(',');
		Script.readString(); // area name
		Script.readSymbol(',');
		Area->SQMPrice = Script.readNumber();
		Script.readSymbol(',');
		Area->DepotNr = Script.readNumber();
		Script.readSymbol(')');
		HouseAreas += 1;
	}
}

void LoadHouses(void){
	Log("houses", "Lade Häuserdaten...\n");

	Houses = 0;
	MaxHouseX = 0;
	MaxHouseY = 0;

	char FileName[4096];
	snprintf(FileName, sizeof(FileName), "%s/houses.dat", DATAPATH);
	if(!FileExists(FileName)){
		Log("houses", "Keine Häuserdaten gefunden.\n");
		return;
	}

	TReadScriptFile Script;
	Script.open(FileName);
	while(true){
		Script.nextToken();
		if(Script.Token == ENDOFFILE){
			Script.close();
			break;
		}

		// TODO(fusion): We expect house fields to be in an exact order and the
		// identifiers don't matter at all. For whatever reason these feel precarious
		// when compared to other loaders.

		Script.getIdentifier(); // "id"
		Script.readSymbol('=');
		uint16 HouseID = (uint16)Script.readNumber();
		if(HouseID == 0){
			error("LoadHouses: Ungültige ID %d.\n", HouseID);
			throw "cannot load houses";
		}

		if(Houses > 0 && HouseID <= House.at(Houses - 1)->ID){
			error("LoadHouses: IDs nicht aufsteigend sortiert (ID=%d).\n", HouseID);
			throw "cannot load houses";
		}

		THouse *H = House.at(Houses);
		H->ID = HouseID;

		Script.readIdentifier(); // "name"
		Script.readSymbol('=');
		strcpy(H->Name, Script.readString());

		Script.readIdentifier(); // "description"
		Script.readSymbol('=');
		strcpy(H->Description, Script.readString());

		Script.readIdentifier(); // "rentoffset"
		Script.readSymbol('=');
		int RentOffset = Script.readNumber();

		Script.readIdentifier(); // "area"
		Script.readSymbol('=');
		uint16 AreaID = (uint16)Script.readNumber();
		THouseArea *Area = GetHouseArea(AreaID);
		if(Area == NULL){
			error("LoadHouses: Gebiet für Haus %d existiert nicht.\n", HouseID);
			throw "cannot load houses";
		}
		H->DepotNr = Area->DepotNr;

		Script.readIdentifier(); // "guildhouse"
		Script.readSymbol('=');
		H->GuildHouse = (strcmp(Script.readIdentifier(), "true") == 0);

		H->NoAuction = false;

		Script.readIdentifier(); // "exit"
		Script.readSymbol('=');
		Script.readCoordinate(&H->ExitX, &H->ExitY, &H->ExitZ);
		if(H->ExitX == 0){
			error("LoadHouses: Ausgang für Haus %d nicht gesetzt.\n", HouseID);
		}

		Script.readIdentifier(); // "center"
		Script.readSymbol('=');
		Script.readCoordinate(&H->CenterX, &H->CenterY, &H->CenterZ);
		if(H->CenterX == 0){
			// TODO(fusion): Same error message as above?
			error("LoadHouses: Ausgang für Haus %d nicht gesetzt.\n", HouseID);
		}

		Script.readIdentifier(); // "fields"
		Script.readSymbol('=');
		Script.readSymbol('{');
		while(true){
			Script.nextToken();
			if(Script.Token == SPECIAL){
				if(Script.getSpecial() == '}'){
					break;
				}else if(Script.getSpecial() == ','){
					continue;
				}
			}

			int FieldX, FieldY, FieldZ;
			Script.getCoordinate(&FieldX, &FieldY, &FieldZ);
			SetHouseID(FieldX, FieldY, FieldZ, HouseID);
			H->Size += 1;

			int RadiusX = std::abs(FieldX - H->CenterX);
			int RadiusY = std::abs(FieldY - H->CenterY);

			if(RadiusX > MaxHouseX){
				MaxHouseX = RadiusX;
			}

			if(RadiusY > MaxHouseY){
				MaxHouseY = RadiusY;
			}
		}

		H->Rent = Area->SQMPrice * H->Size + RentOffset;
		H->OwnerID = 0;
		H->OwnerName[0] = 0;
		H->LastTransition = 0;
		H->PaidUntil = 0;
		H->Subowners = 0;
		H->Guests = 0;

		Houses += 1;
	}

	uint16 *HouseIDs          = (uint16*)alloca(Houses * sizeof(uint16));
	const char **Names        = (const char**)alloca(Houses * sizeof(const char*));
	int *Rents                = (int*)alloca(Houses * sizeof(int));
	const char **Descriptions = (const char**)alloca(Houses * sizeof(const char*));
	int *Sizes                = (int*)alloca(Houses * sizeof(int));
	int *PositionsX           = (int*)alloca(Houses * sizeof(int));
	int *PositionsY           = (int*)alloca(Houses * sizeof(int));
	int *PositionsZ           = (int*)alloca(Houses * sizeof(int));
	char (*Towns)[30]         = (char(*)[30])alloca(Houses * 30);
	bool *Guildhouses         = (bool*)alloca(Houses * sizeof(bool));
	for(int HouseNr = 0; HouseNr < Houses; HouseNr += 1){
		THouse *H = House.at(HouseNr);
		HouseIDs[HouseNr] = H->ID;
		Names[HouseNr] = H->Name;
		Rents[HouseNr] = H->Rent;
		Descriptions[HouseNr] = H->Description;
		Sizes[HouseNr] = H->Size;
		PositionsX[HouseNr] = H->CenterX;
		PositionsY[HouseNr] = H->CenterY;
		PositionsZ[HouseNr] = H->CenterZ;
		strcpy(Towns[HouseNr], GetDepotName(H->DepotNr));
		Guildhouses[HouseNr] = H->GuildHouse;
	}

	int QueryBufferSize = Houses * 600;
	if(QueryBufferSize < (int)KB(16)){
		QueryBufferSize = (int)KB(16);
	}

	QueryManagerConnection = new TQueryManagerConnection(QueryBufferSize);
	int Ret = QueryManagerConnection->insertHouses(Houses, HouseIDs, Names, Rents,
			Descriptions, Sizes, PositionsX, PositionsY, PositionsZ, Towns, Guildhouses);
	if(Ret != 0){
		error("LoadHouses: Kann Stammdaten der Häuser nicht eintragen.\n");
	}
}

void LoadOwners(void){
	Log("houses", "Lade Mieterdaten...\n");

	PaymentExtension = 0;
	bool ClearGuests = false;
	bool ClearBeds = false;

	char FileName[4096];
	snprintf(FileName, sizeof(FileName), "%s/owners.dat", DATAPATH);
	if(!FileExists(FileName)){
		Log("houses", "Keine Mieterdaten gefunden.\n");
		return;
	}

	TReadScriptFile Script;
	Script.open(FileName);
	while(true){
		Script.nextToken();
		if(Script.Token == ENDOFFILE){
			Script.close();
			break;
		}

		const char *Identifier = Script.getIdentifier();
		if(strcmp(Identifier, "extension") == 0){
			Script.readSymbol('=');
			PaymentExtension = Script.readNumber();
		}else if(strcmp(Identifier, "clearguests") == 0){
			ClearGuests = true;
		}else if(strcmp(Identifier, "clearbeds") == 0){
			ClearBeds = true;
		}else if(strcmp(Identifier, "id") == 0){
			Script.readSymbol('=');
			uint16 HouseID = (uint16)Script.readNumber();
			THouse *House = GetHouse(HouseID);
			if(House == NULL){
				error("LoadOwners: Haus zu ID %d existiert nicht.", HouseID);
				throw "Cannot load owners";
			}

			Script.readIdentifier(); // "owner"
			Script.readSymbol('=');
			House->OwnerID = (uint32)Script.readNumber();

			Script.readIdentifier(); // "lasttransition"
			Script.readSymbol('=');
			House->LastTransition = Script.readNumber();

			Script.readIdentifier(); // "paiduntil"
			Script.readSymbol('=');
			House->PaidUntil = Script.readNumber();

			Script.readIdentifier(); // "guests"
			Script.readSymbol('=');
			Script.readSymbol('{');
			while(true){
				Script.nextToken();
				if(Script.Token == SPECIAL){
					if(Script.getSpecial() == '}'){
						break;
					}else if(Script.getSpecial() == ','){
						continue;
					}
				}

				strcpy(House->Guest.at(House->Guests)->Name, Script.getString());
				House->Guests += 1;
			}

			Script.readIdentifier(); // "subowners"
			Script.readSymbol('=');
			Script.readSymbol('{');
			while(true){
				Script.nextToken();
				if(Script.Token == SPECIAL){
					if(Script.getSpecial() == '}'){
						break;
					}else if(Script.getSpecial() == ','){
						continue;
					}
				}

				strcpy(House->Subowner.at(House->Subowners)->Name, Script.getString());
				House->Subowners += 1;
			}

			if(ClearGuests){
				House->Guests = 0;
			}
		}else{
			Script.error("Unknown identifier");
		}
	}

	int NumberOfOwners     = Houses;
	uint16 *HouseIDs       = (uint16*)alloca(NumberOfOwners * sizeof(uint16));
	uint32 *OwnerIDs       = (uint32*)alloca(NumberOfOwners * sizeof(uint32));
	char (*OwnerNames)[30] = (char(*)[30])alloca(NumberOfOwners * 30);
	int *PaidUntils        = (int*)alloca(NumberOfOwners * sizeof(int));
	int Ret = QueryManagerConnection->getHouseOwners(&NumberOfOwners,
						HouseIDs, OwnerIDs, OwnerNames, PaidUntils);
	if(Ret != 0){
		error("LoadOwners: Kann Namen der Mieter nicht ermitteln.\n");
		throw "Cannot load owners";
	}

	for(int OwnerNr = 0; OwnerNr < NumberOfOwners; OwnerNr += 1){
		THouse *House = GetHouse(HouseIDs[OwnerNr]);
		if(House == NULL){
			error("LoadOwners: Haus %d existiert nicht.\n", HouseIDs[OwnerNr]);
			continue;
		}

		if(House->OwnerID != 0){
			strcpy(House->OwnerName, OwnerNames[OwnerNr]);
		}
	}

	if(ClearBeds){
		print(1, "Räume alle Betten...\n");

		for(int HouseNr = 0; HouseNr < Houses; HouseNr += 1){
			THouse *H = House.at(HouseNr);
			int MinX = H->CenterX - MaxHouseX;
			int MaxX = H->CenterX + MaxHouseX;
			int MinY = H->CenterY - MaxHouseY;
			int MaxY = H->CenterY + MaxHouseY;
			int MinZ = SectorZMin;
			int MaxZ = SectorZMax;
			for(int FieldZ = MinZ; FieldZ <= MaxZ; FieldZ += 1)
			for(int FieldY = MinY; FieldY <= MaxY; FieldY += 1)
			for(int FieldX = MinX; FieldX <= MaxX; FieldX += 1){
				Object Bed = GetFirstObject(FieldX, FieldY, FieldZ);
				while(Bed != NONE){
					if(Bed.getObjectType().getFlag(BED)){
						break;
					}
					Bed = Bed.getNextObject();
				}

				if(Bed != NONE && Bed.getAttribute(TEXTSTRING) != 0){
					try{
						UseObjects(0, Bed, Bed);
					}catch(RESULT r){
						error("LoadOwners: Exception %d beim Aufräumen eines Bettes.\n", r);
					}
				}
			}
		}
	}
}

void SaveOwners(void){
	char FileName[4096];
	snprintf(FileName, sizeof(FileName), "%s/owners.dat", DATAPATH);

	TWriteScriptFile Script;
	Script.open(FileName);

	if(PaymentExtension > (int)time(NULL)){
		Script.writeText("Extension = ");
		Script.writeNumber(PaymentExtension);
		Script.writeLn();
		Script.writeLn();
	}

	for(int HouseNr = 0; HouseNr < Houses; HouseNr += 1){
		THouse *H = House.at(HouseNr);
		if(H->OwnerID == 0){
			continue;
		}

		Script.writeText("ID = ");
		Script.writeNumber((int)H->ID);
		Script.writeLn();

		Script.writeText("Owner = ");
		Script.writeNumber((int)H->OwnerID);
		Script.writeLn();

		Script.writeText("LastTransition = ");
		Script.writeNumber((int)H->LastTransition);
		Script.writeLn();

		Script.writeText("PaidUntil = ");
		Script.writeNumber((int)H->PaidUntil);
		Script.writeLn();

		Script.writeText("Guests = {");
		for(int i = 0; i < H->Guests; i += 1){
			if(i > 0){
				Script.writeText(",");
			}
			Script.writeString(H->Guest.at(i)->Name);
		}
		Script.writeText("}");
		Script.writeLn();

		Script.writeText("Subowners = {");
		for(int i = 0; i < H->Subowners; i += 1){
			if(i > 0){
				Script.writeText(",");
			}
			Script.writeString(H->Subowner.at(i)->Name);
		}
		Script.writeText("}");
		Script.writeLn();

		Script.writeLn();
	}

	Script.close();
}

void ProcessHouses(void){
	FinishAuctions();
	ProcessRent();
	StartAuctions();
	UpdateHouseOwners();
}

void InitHouses(void){
	// TODO(fusion): Connect to query manager HERE then pass the connection around
	// house initialization and processing functions. It is currently connecting in
	// `LoadHouses`, used by other functions, then deleted at the end of this function.
	//TQueryManagerConnection QueryManagerConnection(QueryBufferSize);

	InitLog("houses");
	LoadHouseAreas();
	LoadHouses();
	LoadOwners();
	ProcessHouses();
	delete QueryManagerConnection;
}

void ExitHouses(void){
	SaveOwners();
}
