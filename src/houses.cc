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
		error(Translate("GetHouseArea: Gebiet mit ID %d nicht gefunden.\n",
						"GetHouseArea: Territory with ID %d not found.\n"), ID);
	}

	return Result;
}

int CheckAccessRight(const char *Rule, TPlayer *Player){
	if(Rule == NULL){
		error(Translate("CheckAccessRight: Regel ist NULL.\n",
						"CheckAccessRight: Rule is NULL.\n"));
		return 0; // NOT_APPLICABLE ?
	}

	if(Player == NULL){
		error(Translate("CheckAccessRight: pl ist NULL.\n",
						"CheckAccessRight: Player is NULL.\n"));
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
		error(Translate("GetHouse: Haus mit ID %d nicht gefunden.\n",
						"GetHouse: House with ID %d not found.\n"), ID);
	}

	return Result;
}

bool IsOwner(uint16 HouseID, TPlayer *Player){
	THouse *House = GetHouse(HouseID);
	if(House == NULL){
		error(Translate("IsOwner: Haus mit ID %d existiert nicht.\n",
						"IsOwner: House with ID %d does not exist.\n"), HouseID);
		return false;
	}

	if(Player == NULL){
		error(Translate("IsOwner: pl ist NULL.\n",
						"IsOwner: Player is NULL.\n"));
		return false;
	}

	return House->OwnerID == Player->ID;
}

bool IsSubowner(uint16 HouseID, TPlayer *Player, int TimeStamp){
	THouse *House = GetHouse(HouseID);
	if(House == NULL){
		error(Translate("IsSubowner: Haus mit ID %d existiert nicht.\n",
						"IsSubowner: House with ID %d does not exist.\n"), HouseID);
		return false;
	}

	if(Player == NULL){
		error(Translate("IsSubowner: pl ist NULL.\n",
						"IsSubowner: Player is NULL.\n"));
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
		error(Translate("IsGuest: Haus mit ID %d existiert nicht.\n",
						"IsGuest: House with ID %d does not exist.\n"), HouseID);
		return false;
	}

	if(Player == NULL){
		error(Translate("IsGuest: pl ist NULL.\n",
						"IsGuest: Player is NULL.\n"));
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
		error(Translate("IsInvited: pl ist NULL.\n",
						"IsInvited: Player is NULL.\n"));
		return false;
	}

	return IsOwner(HouseID, Player)
		|| IsSubowner(HouseID, Player, TimeStamp)
		|| IsGuest(HouseID, Player, TimeStamp);
}

const char *GetHouseName(uint16 HouseID){
	THouse *House = GetHouse(HouseID);
	if(House == NULL){
		error(Translate("GetHouseName: Haus mit ID %d existiert nicht.\n",
						"GetHouseName: House with ID %d does not exist.\n"), HouseID);
		return NULL;
	}

	return House->Name;
}

const char *GetHouseOwner(uint16 HouseID){
	THouse *House = GetHouse(HouseID);
	if(House == NULL){
		error(Translate("GetHouseOwner: Haus mit ID %d existiert nicht.\n",
						"GetHouseOwner: House with ID %d does not exist.\n"), HouseID);
		return NULL;
	}

	return House->OwnerName;
}

// TODO(fusion): This function is unsafe like `strcpy`.
void ShowSubownerList(uint16 HouseID, TPlayer *Player, char *Buffer){
	THouse *House = GetHouse(HouseID);
	if(House == NULL){
		error(Translate("ShowSubownerList: Haus mit ID %d existiert nicht.\n",
						"ShowSubownerList: House with ID %d does not exist.\n"), HouseID);
		throw ERROR;
	}

	if(Player == NULL){
		error(Translate("ShowSubownerList: pl ist NULL.\n",
						"ShowSubownerList: Player is NULL.\n"));
		throw ERROR;
	}

	if(Buffer == NULL){
		error(Translate("ShowSubownerList: Buffer ist NULL.\n",
						"ShowSubownerList: Buffer is NULL.\n"));
		throw ERROR;
	}

	if(!IsOwner(HouseID, Player)){
		throw NOTACCESSIBLE;
	}

	print(3, Translate("Editiere Untermieterliste von Haus %d.\n",
					   "Editing subtenant list of house %d.\n"), HouseID);
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
		error(Translate("ShowGuestList: Haus mit ID %d existiert nicht.\n",
						"ShowGuestList: House with ID %d does not exist.\n"), HouseID);
		throw ERROR;
	}

	if(Player == NULL){
		error(Translate("ShowGuestList: pl ist NULL.\n",
						"ShowGuestList: Player is NULL.\n"));
		throw ERROR;
	}

	if(Buffer == NULL){
		error(Translate("ShowGuestList: Buffer ist NULL.\n",
						"ShowGuestList: Buffer is NULL.\n"));
		throw ERROR;
	}

	if(!IsOwner(HouseID, Player) && (!IsSubowner(HouseID, Player, INT_MAX)
									|| !CheckRight(Player->ID, PREMIUM_ACCOUNT))){
		throw NOTACCESSIBLE;
	}

	print(3, Translate("Editiere Gästeliste von Haus %d.\n",
					   "Editing guest list of house %d.\n"), HouseID);
	sprintf(Buffer, "# Guests of %s\n", House->Name);
	for(int i = 0; i < House->Guests; i += 1){
		strcat(Buffer, House->Guest.at(i)->Name);
		strcat(Buffer, "\n");
	}
}

void ChangeSubowners(uint16 HouseID, TPlayer *Player, const char *Buffer){
	THouse *House = GetHouse(HouseID);
	if(House == NULL){
		error(Translate("ChangeSubowners: Haus mit ID %d existiert nicht.\n",
						"ChangeSubowners: House with ID %d does not exist.\n"), HouseID);
		throw ERROR;
	}

	if(Player == NULL){
		error(Translate("ChangeSubowners: pl ist NULL.\n",
						"ChangeSubowners: Player is NULL.\n"));
		throw ERROR;
	}

	if(Buffer == NULL){
		error(Translate("ChangeSubowners: Buffer ist NULL.\n",
						"ChangeSubowners: Buffer is NULL.\n"));
		throw ERROR;
	}

	if(!IsOwner(HouseID, Player)){
		throw NOTACCESSIBLE;
	}

	Log("houses", Translate("%s ändert Liste der Untermieter von Haus %d.\n",
							"%s changes list of subtenants of house %d.\n"), Player->Name, HouseID);

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
		error(Translate("ChangeGuests: Haus mit ID %d existiert nicht.\n",
						"ChangeGuests: House with ID %d does not exist.\n"), HouseID);
		throw ERROR;
	}

	if(Player == NULL){
		error(Translate("ChangeGuests: pl ist NULL.\n",
						"ChangeGuests: Player is NULL.\n"));
		throw ERROR;
	}

	if(Buffer == NULL){
		error(Translate("ChangeGuests: Buffer ist NULL.\n",
						"ChangeGuests: Buffer is NULL.\n"));
		throw ERROR;
	}

	if(!IsOwner(HouseID, Player) && (!IsSubowner(HouseID, Player, INT_MAX)
									|| !CheckRight(Player->ID, PREMIUM_ACCOUNT))){
		throw NOTACCESSIBLE;
	}

	Log("houses", Translate("%s ändert Liste der Gäste von Haus %d.\n",
							"%s is changing the list of guests of house %d.\n"), Player->Name, HouseID);

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
		error(Translate("GetExitPosition: Haus mit ID %d existiert nicht.\n",
						"GetExitPosition: House with ID %d does not exist.\n"), HouseID);
		return;
	}

	*x = House->ExitX;
	*y = House->ExitY;
	*z = House->ExitZ;
}

void KickGuest(uint16 HouseID, TPlayer *Guest){
	THouse *House = GetHouse(HouseID);
	if(House == NULL){
		error(Translate("KickGuest(houses 1): Haus mit ID %d existiert nicht.\n",
						"KickGuest(houses 1): House with ID %d does not exist.\n"), HouseID);
		throw ERROR;
	}

	if(Guest == NULL){
		error(Translate("KickGuest(houses 1): Guest ist NULL.\n",
						"KickGuest(houses 1): Guest is NULL.\n"));
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
		print(3, Translate("Spieler steht auf einem Bett, während er aus dem Haus gekickt wird.\n",
							"Player stands on a bed while being kicked out of the house.\n"));
		try{
			UseObjects(0, Bed, Bed);
		}catch(RESULT r){
			error(Translate("KickGuest: Exception %d beim Aufräumen des Bettes.\n",
							"KickGuest: Exception %d while making the bed.\n"), r);
		}
	}

	Object Exit = GetMapContainer(House->ExitX, House->ExitY, House->ExitZ);
	Move(0, Guest->CrObject, Exit, -1, false, NONE);
	GraphicalEffect(Guest->CrObject, EFFECT_ENERGY);
}

void KickGuest(uint16 HouseID, TPlayer *Host, TPlayer *Guest){
	THouse *House = GetHouse(HouseID);
	if(House == NULL){
		error(Translate("KickGuest(houses 2): Haus mit ID %d existiert nicht.\n",
						"KickGuest(houses 2): House with ID %d does not exist.\n"), HouseID);
		throw ERROR;
	}

	if(Host == NULL){
		error(Translate("KickGuest(houses 2): Host ist NULL.\n",
						"KickGuest(houses 2): Host is NULL.\n"));
		throw ERROR;
	}

	if(Guest == NULL){
		error(Translate("KickGuest(houses 2): Guest ist NULL.\n",
						"KickGuest(houses 2): Guest is NULL.\n"));
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
		error(Translate("KickGuests: Haus mit ID %d existiert nicht.\n",
						"KickGuests: House with ID %d does not exist.\n"), HouseID);
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
		error(Translate("MayOpenDoor: Tür existiert nicht.\n",
						"MayOpenDoor: Door does not exist.\n"));
		return false;
	}

	if(Player == NULL){
		error(Translate("MayOpenDoor: pl ist NULL.\n",
						"MayOpenDoor: Player is NULL.\n"));
		return false;
	}

	ObjectType DoorType = Door.getObjectType();
	if(!DoorType.getFlag(NAMEDOOR) || !DoorType.getFlag(TEXT)){
		error(Translate("MayOpenDoor: Tür ist keine NameDoor.\n",
						"MayOpenDoor: Door is not a NameDoor.\n"));
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
		error(Translate("ShowNameDoor: Tür existiert nicht.\n",
						"ShowNameDoor: Door does not exist.\n"));
		throw ERROR;
	}

	if(Player == NULL){
		error(Translate("ShowNameDoor: pl ist NULL.\n",
						"ShowNameDoor: Player is NULL.\n"));
		throw ERROR;
	}

	if(Buffer == NULL){
		error(Translate("ShowNameDoor: Buffer ist NULL.\n",
						"ShowNameDoor: Buffer is NULL.\n"));
		throw ERROR;
	}

	int DoorX, DoorY, DoorZ;
	GetObjectCoordinates(Door, &DoorX, &DoorY, &DoorZ);
	uint16 HouseID = GetHouseID(DoorX, DoorY, DoorZ);
	if(HouseID == 0){
		error(Translate("ShowNameDoor: Tür auf Koordinate [%d,%d,%d] gehört zu keinem Haus.\n",
						"ShowNameDoor: Door at coordinate [%d,%d,%d] does not belong to any house.\n"),
				DoorX, DoorY, DoorZ);
		throw ERROR;
	}

	if(!IsOwner(HouseID, Player)){
		print(3, Translate("Spieler %s ist nicht Mieter des Hauses %d.\n",
							"Player %s is not a tenant of the house %d.\n"),
				Player->Name, HouseID);
		throw NOTACCESSIBLE;
	}

	print(3, Translate("Editiere NameDoor.\n",
					   "Edit NameDoor.\n"));

	// TODO(fusion): Check for `NAMEDOOR` flag as well?
	if(Door.getObjectType().getFlag(TEXT)){
		error(Translate("ShowNameDoor: Tür auf Koordinate [%d,%d,%d] enthält keinen Text.\n",
						"ShowNameDoor: Door at coordinate [%d,%d,%d] contains no text.\n"),
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
		error(Translate("ChangeNameDoor: Tür existiert nicht.\n",
						"ChangeNameDoor: Door does not exist.\n"));
		throw ERROR;
	}

	if(Player == NULL){
		error(Translate("ChangeNameDoor: pl ist NULL.\n",
						"ChangeNameDoor: Player is NULL.\n"));
		throw ERROR;
	}

	if(Buffer == NULL){
		error(Translate("ChangeNameDoor: Buffer ist NULL.\n",
						"ChangeNameDoor: Buffer is NULL.\n"));
		throw ERROR;
	}

	int DoorX, DoorY, DoorZ;
	GetObjectCoordinates(Door, &DoorX, &DoorY, &DoorZ);
	uint16 HouseID = GetHouseID(DoorX, DoorY, DoorZ);
	if(HouseID == 0){
		error(Translate("ChangeNameDoor: Tür auf Koordinate [%d,%d,%d] gehört zu keinem Haus.\n",
						"ChangeNameDoor: Door at coordinate [%d,%d,%d] does not belong to any house.\n"),
				DoorX, DoorY, DoorZ);
		throw ERROR;
	}

	if(!IsOwner(HouseID, Player)){
		throw NOTACCESSIBLE;
	}

	// TODO(fusion): Check for `NAMEDOOR` flag as well?
	if(!Door.getObjectType().getFlag(TEXT)){
		error(Translate("ChangeNameDoor: Tür auf Koordinate [%d,%d,%d] enthält keinen Text.\n",
						"ChangeNameDoor: Door at coordinate [%d,%d,%d] contains no text.\n"),
				DoorX, DoorY, DoorZ);
		throw ERROR;
	}

	Log("houses", Translate("%s ändert NameDoor von Haus %d auf Koordinate [%d,%d,%d].\n",
							"%s changes NameDoor of house %d at coordinate [%d,%d,%d].\n"),
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

// NOTE(fusion): This is used inside house processing functions to empty the depot
// container after processing a player's depot.
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
		error(Translate("CleanField: Depot-Box existiert nicht.\n",
						"CleanField: Depot-Box does not exist.\n"));
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
		error(Translate("CleanHouse: house ist NULL.\n",
						"CleanHouse: house is NULL.\n"));
		return;
	}

	if(House->OwnerID == 0){
		error(Translate("CleanHouse: Haus %d hat keinen Besitzer.\n",
						"CleanHouse: House %d has no owner.\n"), House->ID);
		return;
	}

	bool PlayerDataAssigned = false;
	if(PlayerData == NULL){
		PlayerData = AssignPlayerPoolSlot(House->OwnerID, false);
		if(PlayerData == NULL){
			error(Translate("CleanHouse: Kann keinen Slot für Spielerdaten zuweisen.\n",
							"CleanHouse: Cannot assign a slot for player data.\n"));
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
	Log("houses", Translate("Bearbeite beendete Auktionen...\n",
							"Edit completed auctions...\n"));

	// TODO(fusion): It could be worse.
	int NumberOfAuctions       = Houses;
	uint16 *HouseIDs           = (uint16*)alloca(NumberOfAuctions * sizeof(uint16));
	uint32 *CharacterIDs       = (uint32*)alloca(NumberOfAuctions * sizeof(uint32));
	char (*CharacterNames)[30] = (char(*)[30])alloca(NumberOfAuctions * 30);
	int *Bids                  = (int*)alloca(NumberOfAuctions * sizeof(int));
	int Ret = QueryManagerConnection->finishAuctions(&NumberOfAuctions,
							HouseIDs, CharacterIDs, CharacterNames, Bids);
	if(Ret != 0){
		error(Translate("FinishAuctions: Kann Versteigerungen nicht ermitteln.\n",
						"FinishAuctions: Cannot identify auctions.\n"));
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

		Log("houses", Translate("Auktion für Haus %d beendet: Ersteigert von %s für %d Gold.\n",
								"Auction for house %d finished: Won by %s for %d gold.\n"),
				HouseID, CharacterName, Bid);
		Log("houses", Translate("bei Reset: INSERT INTO HouseAssignments VALUES ('%s',%d,%d,%d);\n",
								"On reset: INSERT INTO HouseAssignments VALUES ('%s',%d,%d,%d);\n"),
				HelpWorld, HouseID, CharacterID, Bid);

		THouse *House = GetHouse(HouseID);
		if(House == NULL){
			error(Translate("FinishAuctions: Haus mit Nummer %d existiert nicht.\n",
							"House with number %d does not exist.\n"),
					HouseID);
			continue;
		}

		if(House->OwnerID != 0){
			error(Translate("FinishAuctions: Haus mit Nummer %d gehört schon Charakter %u.\n",
							"FinishAuctions: House with number %d already belongs to character %u.\n"),
					HouseID, House->OwnerID);
			continue;
		}

		TPlayerData *PlayerData = AssignPlayerPoolSlot(CharacterID, false);
		if(PlayerData == NULL){
			error(Translate("FinishAuctions: Kann keinen Slot für Spielerdaten zuweisen.\n",
							"FinishAuctions: Cannot assign a slot for player data.\n"));
			continue;
		}

		LoadDepot(PlayerData, House->DepotNr, TempDepot);
		int DepotMoney = CountMoney(GetFirstContainerObject(TempDepot));
		if(DepotMoney < (House->Rent + Bid)){
			Log("houses", Translate("Ersteigerer hat nicht genügend Geld.\n",
									"Bidder does not have enough money.\n"));
			House->OwnerID = CharacterID;
			strcpy(House->OwnerName, CharacterName);
			House->PaidUntil = 0;
		}else{
			Log("houses", Translate("Ersteigerer hat genügend Geld.\n",
									"Bidder has enough money.\n"));
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
				error(Translate("FinishAuctions: Kann Ersteigerer nicht verbannen.\n",
								"FinishAuctions: Cannot ban bidders.\n"));
			}

			ClearHouse(H);
		}
	}

	return true;
}

bool TransferHouses(void){
	Log("houses", Translate("Bearbeite freiwillige Kündigungen...\n",
							"Process voluntary terminations...\n"));
	int NumberOfTransfers     = Houses;
	uint16 *HouseIDs          = (uint16*)alloca(NumberOfTransfers * sizeof(uint16));
	uint32 *NewOwnerIDs       = (uint32*)alloca(NumberOfTransfers * sizeof(uint32));
	char (*NewOwnerNames)[30] = (char(*)[30])alloca(NumberOfTransfers * 30);
	int *Prices               = (int*)alloca(NumberOfTransfers * sizeof(int));
	int Ret = QueryManagerConnection->transferHouses(&NumberOfTransfers,
							HouseIDs, NewOwnerIDs, NewOwnerNames, Prices);
	if(Ret != 0){
		error(Translate("CollectRents: Kann Kündigungen nicht ermitteln.\n",
						"CollectRents: Unable to detect cancellations.\n"));
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
			error(Translate("CollectRents: Haus mit Nummer %d existiert nicht.\n",
							"CollectRents: House with number %d does not exist.\n"), HouseID);
			continue;
		}

		if(NewOwnerID != 0 && TransferPrice > 0){
			Log("houses", Translate("Verkaufe Haus %d an %u für %d Gold.\n",
									"Selling house %d to %u for %d gold.\n"),
					HouseID, NewOwnerID, TransferPrice);
			TPlayerData *PlayerData = AssignPlayerPoolSlot(NewOwnerID, false);
			if(PlayerData == NULL){
				error(Translate("CollectRents: Kann keinen Slot für Spielerdaten zuweisen (1a).\n",
								"CollectRents: Cannot assign a slot for player data (1a).\n"));
				continue;
			}

			LoadDepot(PlayerData, House->DepotNr, TempDepot);
			int DepotMoney = CountMoney(GetFirstContainerObject(TempDepot));
			if(DepotMoney < TransferPrice){
				Log("houses", Translate("Käufer hat nicht genügend Geld.\n",
										"Buyer does not have enough money.\n"));

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

				Log("houses", Translate("bei Reset: UPDATE HouseOwners SET Termination=null,NewOwner=null,Accepted=0,Price=null WHERE World=\'%s\' AND HouseID=%d;\n",
										"On reset: UPDATE HouseOwners SET Termination=null,NewOwner=null,Accepted=0,Price=null WHERE World=\'%s\' AND HouseID=%d;\n"),
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
				error(Translate("CollectRents: Kann keinen Slot für Spielerdaten zuweisen (1b).\n",
								"CollectRents: Cannot assign a slot for player data (1b).\n"));
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
		Log("houses", Translate("Räume Haus %d von %u.\n",
								"Vacating house %d from %u.\n"), HouseID, House->OwnerID);
		if(NewOwnerID == 0){
			CleanHouse(House, NULL);
		}

		ClearHouse(House);
		House->Help = 1;

		if(NewOwnerID != 0){
			Log("houses", Translate("Übertrage Haus %d an %u.\n",
									"Transferring house %d to %u.\n"), HouseID, NewOwnerID);
			House->OwnerID = NewOwnerID;
			strcpy(House->OwnerName, NewOwnerName);
			House->LastTransition = TimeStamp;
			Log("houses", Translate("bei Reset: UPDATE HouseOwners SET Termination=%d,NewOwner=%u,Accepted=1,Price=%d WHERE World='%s' AND HouseID=%d;\n",
									"On reset: UPDATE HouseOwners SET Termination=%d,NewOwner=%u,Accepted=1,Price=%d WHERE World='%s' AND HouseID=%d;\n"),
					TimeStamp, NewOwnerID, TransferPrice, HelpWorld, HouseID);
		}else{
			Log("houses", Translate("bei Reset: UPDATE HouseOwners SET Termination=%d,NewOwner=null,Accepted=0,Price=0 WHERE World=\'%s\' AND HouseID=%d;\n",
									"On reset: UPDATE HouseOwners SET Termination=%d,NewOwner=null,Accepted=0,Price=0 WHERE World=\'%s\' AND HouseID=%d;\n"),
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
		error(Translate("CollectRents: Kann Zahlungsdaten nicht ermitteln.\n",
						"CollectRents: Cannot determine payment details.\n"));
		return false;
	}

	for(int EvictionNr = 0; EvictionNr < NumberOfEvictions; EvictionNr += 1){
		uint16 HouseID = HouseIDs[EvictionNr];
		uint32 OwnerID = OwnerIDs[EvictionNr];

		THouse *House = GetHouse(HouseID);
		if(House == NULL){
			error(Translate("CollectRents: Haus mit Nummer %d existiert nicht.\n",
							"CollectRents: House with number %d does not exist.\n"), HouseID);
			continue;
		}

		Log("houses", Translate("Account von Haus %d ist nicht mehr bezahlt.\n",
								"Account of house %d is no longer paid.\n"), HouseID);
		if(OwnerID != House->OwnerID){
			Log("houses", Translate("... aber Haus wurde eben noch übertragen.\n",
									"... but the house was just transferred.\n"));
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
		error(Translate("CollectRents: Kann gelöschte Charaktere nicht ermitteln.\n",
						"CollectRents: Cannot detect deleted characters.\n"));
		return false;
	}

	for(int EvictionNr = 0; EvictionNr < NumberOfEvictions; EvictionNr += 1){
		uint16 HouseID = HouseIDs[EvictionNr];
		THouse *House = GetHouse(HouseID);
		if(House == NULL){
			error(Translate("CollectRents: Haus mit Nummer %d existiert nicht.\n",
							"CollectRents: House with number %d does not exist.\n"), HouseID);
			continue;
		}

		Log("houses", Translate("Besitzer von Haus %d ist gelöscht.\n",
								"Owner of house %d is deleted.\n"), HouseID);
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
			error(Translate("CollectRents: Kann Gildenränge nicht ermitteln.\n",
							"CollectRents: Cannot determine guild ranks.\n"));
			return false;
		}

		for(int EvictionNr = 0; EvictionNr < NumberOfEvictions; EvictionNr += 1){
			uint16 HouseID = HouseIDs[EvictionNr];
			THouse *House = GetHouse(HouseID);
			if(House == NULL){
				error(Translate("CollectRents: Haus mit Nummer %d existiert nicht.\n",
								"CollectRents: House with number %d does not exist.\n"), HouseID);
				continue;
			}

			Log("houses", Translate("Mieter von Gildenhaus %d ist kein Gildenführer mehr.\n",
									"Tenant of guild house %d is no longer a guild leader.\n"), HouseID);
			CleanHouse(House, NULL);
			ClearHouse(House);
			House->Help = 1;
		}
	}

	return true;
}

void CollectRent(void){
	Log("houses", Translate("Treibe Mieten ein...\n",
							"Collect rent...\n"));
	int TimeStamp = (int)time(NULL);
	Object TempDepot = CreateTempDepot();
	for(int HouseNr = 0; HouseNr < Houses; HouseNr += 1){
		THouse *H = House.at(HouseNr);
		if(H->OwnerID == 0 || H->PaidUntil > TimeStamp){
			continue;
		}

		Log("houses", Translate("Treibe Miete für Haus %d von %u ein.\n",
								"Collect rent for house %d from %u.\n"), H->ID, H->OwnerID);

		int Deadline = H->PaidUntil + (7 * 24 * 60 * 60); // 1 week notice
		// TODO(fusion): How is `PaymentExtension` set? This doesn't make a lot of sense.
		if(Deadline < PaymentExtension){
			Deadline = PaymentExtension;
		}

		TPlayerData *PlayerData = AssignPlayerPoolSlot(H->OwnerID, false);
		if(PlayerData == NULL){
			error(Translate("CollectRents: Kann keinen Slot für Spielerdaten zuweisen (2).\n",
							"CollectRents: Cannot assign a slot for player data (2).\n"));
			continue;
		}

		LoadDepot(PlayerData, H->DepotNr, TempDepot);
		int DepotMoney = CountMoney(GetFirstContainerObject(TempDepot));
		if(DepotMoney < H->Rent){
			if(TimeStamp < Deadline){
				Log("houses", Translate("Mieter erhält Mahnung.\n",
										"Tenant receives reminder.\n"));

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
				Log("houses", Translate("Mieter wird hinausgeworfen.\n",
										"Tenant is evicted.\n"));

				if(QueryManagerConnection->excludeFromAuctions(H->OwnerID, false) != 0){
					error(Translate("CollectRents: Kann Mieter nicht verbannen.\n",
									"CollectRents: Cannot ban tenants.\n"));
				}

				CleanHouse(H, PlayerData);
				ClearHouse(H);
			}
		}else{
			Log("houses", Translate("Mieter hat genügend Geld.\n",
									"Tenant has enough money.\n"));
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
				error(Translate("CollectRents: Kann Haus %d nicht aus HouseOwners austragen.\n",
								"CollectRents: Cannot remove house %d from HouseOwners.\n"), H->ID);
			}
		}
	}

	CollectRent();
}

bool StartAuctions(void){
	Log("houses", Translate("Starte neue Versteigerungen...\n",
							"Start new auctions...\n"));

	int NumberOfAuctions = Houses;
	uint16 *HouseIDs     = (uint16*)alloca(NumberOfAuctions * sizeof(uint16));
	int Ret = QueryManagerConnection->getAuctions(&NumberOfAuctions, HouseIDs);
	if(Ret != 0){
		error(Translate("StartAuctions: Kann laufende Versteigerungen nicht ermitteln.\n",
						"StartAuctions: Cannot find ongoing auctions.\n"));
		return false;
	}

	for(int HouseNr = 0; HouseNr < Houses; HouseNr += 1){
		House.at(HouseNr)->Help = 0;
	}

	for(int AuctionNr = 0; AuctionNr < NumberOfAuctions; AuctionNr += 1){
		THouse *House = GetHouse(HouseIDs[AuctionNr]);
		if(House == NULL){
			error(Translate("StartAuctions: Haus mit Nummer %d existiert nicht (1).\n",
							"StartAuctions: House with number %d does not exist (1).\n"), HouseIDs[AuctionNr]);
			continue;
		}

		House->Help = 1;
	}

	char HelpWorld[60];
	AddSlashes(HelpWorld, WorldName);
	for(int HouseNr = 0; HouseNr < Houses; HouseNr += 1){
		THouse *H = House.at(HouseNr);
		if(H->OwnerID == 0 && H->Help == 0 && !H->NoAuction){
			Log("houses", Translate("Trage Haus %d zur Versteigerung ein.\n",
									"Enter house %d for auction.\n"), H->ID);
			Log("houses", Translate("bei Reset: DELETE FROM Auctions WHERE World=\'%s\' AND HouseID=%d;\n",
									"On reset: DELETE FROM Auctions WHERE World=\'%s\' AND HouseID=%d;\n"),
					HelpWorld, H->ID);
			if(QueryManagerConnection->startAuction(H->ID) != 0){
				error(Translate("StartAuctions: Kann Haus %d nicht zur Versteigerung eintragen.\n",
								"StartAuctions: Cannot list house %d for auction.\n"), H->ID);
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
		error(Translate("StartAuctions: Kann eingetragene vermietete Häuser nicht ermitteln.\n",
						"StartAuctions: Cannot identify registered rented houses.\n"));
		return false;
	}

	for(int HouseNr = 0; HouseNr < Houses; HouseNr += 1){
		House.at(HouseNr)->Help = 0;
	}

	for(int OwnerNr = 0; OwnerNr < NumberOfOwners; OwnerNr += 1){
		THouse *House = GetHouse(HouseIDs[OwnerNr]);
		if(House == NULL){
			error(Translate("StartAuctions: Haus mit Nummer %d existiert nicht (2).\n",
							"StartAuctions: House with number %d does not exist (2).\n"), HouseIDs[OwnerNr]);
			continue;
		}

		if(House->OwnerID == OwnerIDs[OwnerNr] && House->PaidUntil == PaidUntils[OwnerNr]){
			House->Help = 1;
		}else{
			House->Help = 2;
		}
	}

	Log("houses", Translate("Aktualisiere Liste der Mieter...\n",
							"Update list of tenants...\n"));
	for(int HouseNr = 0; HouseNr < Houses; HouseNr += 1){
		THouse *H = House.at(HouseNr);

		if(H->OwnerID == 0 && H->Help != 0){
			Log("houses", Translate("Trage nicht mehr vermietetes Haus %d aus.\n",
									"Remove house %d that is no longer rented.\n"), H->ID);
			if(QueryManagerConnection->deleteHouseOwner(H->ID) != 0){
				error(Translate("StartAuctions: Kann nicht mehr vermietetes Haus %d nicht austragen.\n",
								"StartAuctions: Cannot remove house %d that is no longer rented.\n"), H->ID);
			}
		}

		if(H->OwnerID != 0 && H->Help == 0){
			Log("houses", Translate("Trage vermietetes Haus %d ein.\n",
									"Enter rented house %d.\n"), H->ID);
			if(QueryManagerConnection->insertHouseOwner(H->ID, H->OwnerID, H->PaidUntil) != 0){
				error(Translate("StartAuctions: Kann vermietetes Haus %d nicht eintragen.\n",
								"StartAuctions: Cannot enter rented house %d.\n"), H->ID);
			}
		}

		if(H->Help == 2){
			Log("houses", Translate("Aktualisiere vermietetes Haus %d.\n",
									"Updating rented house %d.\n"), H->ID);
			if(QueryManagerConnection->updateHouseOwner(H->ID, H->OwnerID, H->PaidUntil) != 0){
				error(Translate("StartAuctions: Kann Daten des vermieteten Hauses %d nicht aktualisieren.\n",
								"StartAuctions: Cannot update data of rented house %d.\n"), H->ID);
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
			error(Translate("FinishHouseCleanup: Kann keinen Slot für Spielerdaten zuweisen.\n",
							"FinishHouseCleanup: Cannot assign a slot for player data.\n"));
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
		error(Translate("CleanHouseField: Kein Haus zu Feld [%d,%d,%d] gefunden.\n",
						"CleanHouseField: No house found for field [%d,%d,%d].\n"), x, y, z);
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
			error(Translate("CleanHouseField: Kann keinen Slot für Spielerdaten zuweisen.\n",
							"CleanHouseField: Cannot assign a slot for player data.\n"));
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
	Log("houses", Translate("Lade Daten der Häusergebiete...\n",
							"Loading data of housing areas...\n"));

	char FileName[4096];
	snprintf(FileName, sizeof(FileName), "%s/houseareas.dat", DATAPATH);
	if(!FileExists(FileName)){
		Log("houses", Translate("Keine Daten für Häusergebiete gefunden.\n",
								"No data found for residential areas.\n"));
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
	Log("houses", Translate("Lade Häuserdaten...\n",
							"Loading house data...\n"));

	Houses = 0;
	MaxHouseX = 0;
	MaxHouseY = 0;

	char FileName[4096];
	snprintf(FileName, sizeof(FileName), "%s/houses.dat", DATAPATH);
	if(!FileExists(FileName)){
		Log("houses", Translate("Keine Häuserdaten gefunden.\n",
								"No house data found.\n"));
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
			error(Translate("LoadHouses: Ungültige ID %d.\n",
							"LoadHouses: Invalid ID %d.\n"), HouseID);
			throw "cannot load houses";
		}

		if(Houses > 0 && HouseID <= House.at(Houses - 1)->ID){
			error(Translate("LoadHouses: IDs nicht aufsteigend sortiert (ID=%d).\n",
							"LoadHouses: IDs not sorted in ascending order (ID=%d).\n"), HouseID);
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
			error(Translate("LoadHouses: Gebiet für Haus %d existiert nicht.\n",
							"LoadHouses: Territory for house %d does not exist.\n"), HouseID);
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
			error(Translate("LoadHouses: Ausgang für Haus %d nicht gesetzt.\n",
							"LoadHouses: Exit for house %d not set.\n"), HouseID);
		}

		Script.readIdentifier(); // "center"
		Script.readSymbol('=');
		Script.readCoordinate(&H->CenterX, &H->CenterY, &H->CenterZ);
		if(H->CenterX == 0){
			// TODO(fusion): Same error message as above?
			error(Translate("LoadHouses: Ausgang für Haus %d nicht gesetzt.\n",
							"LoadHouses: Exit for house %d not set.\n"), HouseID);
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
		error(Translate("LoadHouses: Kann Stammdaten der Häuser nicht eintragen.\n",
						"LoadHouses: Cannot enter master data of the houses.\n"));
	}
}

void LoadOwners(void){
	Log("houses", Translate("Lade Mieterdaten...\n",
							"Loading tenant data...\n"));

	PaymentExtension = 0;
	bool ClearGuests = false;
	bool ClearBeds = false;

	char FileName[4096];
	snprintf(FileName, sizeof(FileName), "%s/owners.dat", DATAPATH);
	if(!FileExists(FileName)){
		Log("houses", Translate("Keine Mieterdaten gefunden.\n",
								"No tenant data found.\n"));
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
				error(Translate("LoadOwners: Haus zu ID %d existiert nicht.\n",
								"LoadOwners: House for ID %d does not exist.\n"), HouseID);
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
		error(Translate("LoadOwners: Kann Namen der Mieter nicht ermitteln.\n",
						"LoadOwners: Cannot determine tenant names.\n"));
		throw "Cannot load owners";
	}

	for(int OwnerNr = 0; OwnerNr < NumberOfOwners; OwnerNr += 1){
		THouse *House = GetHouse(HouseIDs[OwnerNr]);
		if(House == NULL){
			error(Translate("LoadOwners: Haus %d existiert nicht.\n",
							"LoadOwners: House %d does not exist.\n"), HouseIDs[OwnerNr]);
			continue;
		}

		if(House->OwnerID != 0){
			strcpy(House->OwnerName, OwnerNames[OwnerNr]);
		}
	}

	if(ClearBeds){
		print(1, Translate("Räume alle Betten...\n",
							"Clear all beds...\n"));

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
						error(Translate("LoadOwners: Exception %d beim Aufräumen eines Bettes.\n",
										"LoadOwners: Exception %d while tidying a bed.\n"), r);
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
