#include "operate.hh"
#include "config.hh"
#include "houses.hh"
#include "info.hh"
#include "magic.hh"
#include "moveuse.hh"
#include "reader.hh"

#include <dirent.h>

static fifo<TStatement> Statements(1024);
static fifo<TListener> Listeners(1024);

static vector<TChannel> Channel(0, 18, 10);
static int Channels = PUBLIC_CHANNELS;
static int CurrentChannelID;
static int CurrentSubscriberNumber;

static vector<TParty> Party(0, 100, 50);
static int Parties;

// World Operations
// =============================================================================

// TODO(fusion): The radii parameters for `TFindCreatures` are commonly around
// 16 and 14 for x and y respectively so there is probably some constant involved.
//	Also, since we're talking about radii, these values are quite large when you
// consider the regular client's viewport dimensions of 15x11 visible fields or
// 18x14 total fields.

void AnnounceMovingCreature(uint32 CreatureID, Object Con){
	TCreature *Creature = GetCreature(CreatureID);
	if(Creature == NULL){
		error(Translate("AnnounceMovingCreature: Kreatur %d existiert nicht.\n",
						"AnnounceMovingCreature: Creature %d does not exist.\n"), CreatureID);
		return;
	}

	int ConX, ConY, ConZ;
	GetObjectCoordinates(Con, &ConX, &ConY, &ConZ);

	int SearchRadiusX = 16 + (std::abs(Creature->posx - ConX) / 2) + 1;
	int SearchRadiusY = 14 + (std::abs(Creature->posy - ConY) / 2) + 1;
	int SearchCenterX = (Creature->posx + ConX) / 2;
	int SearchCenterY = (Creature->posy + ConY) / 2;
	TFindCreatures Search(SearchRadiusX, SearchRadiusY, SearchCenterX, SearchCenterY, FIND_PLAYERS);
	while(true){
		uint32 CharacterID = Search.getNext();
		if(CharacterID == 0){
			break;
		}

		TPlayer *Player = GetPlayer(CharacterID);
		if(Player == NULL || Player->Connection == NULL){
			continue;
		}

		SendMoveCreature(Player->Connection, CreatureID, ConX, ConY, ConZ);
	}
}

void AnnounceChangedCreature(uint32 CreatureID, int Type){
	TCreature *Creature = GetCreature(CreatureID);
	if(Creature == NULL){
		error(Translate("AnnounceChangedCreature: Kreatur %d existiert nicht (Type=%d).\n",
						"AnnounceChangedCreature: Creature %d does not exist (Type=%d).\n"), CreatureID, Type);
		return;
	}

	for(TKnownCreature *KnownCreature = Creature->FirstKnowingConnection;
			KnownCreature != NULL;
			KnownCreature = KnownCreature->Next){
		TConnection *KnowingConnection = KnownCreature->Connection;
		if(KnowingConnection->State != CONNECTION_GAME
				|| KnownCreature->State == KNOWNCREATURE_OUTDATED){
			continue;
		}

		if(KnowingConnection->IsVisible(Creature->posx, Creature->posy, Creature->posz)){
			switch(Type){
				case CREATURE_HEALTH_CHANGED: SendCreatureHealth(KnowingConnection, CreatureID); break;
				case CREATURE_LIGHT_CHANGED:  SendCreatureLight(KnowingConnection, CreatureID); break;
				case CREATURE_OUTFIT_CHANGED: SendCreatureOutfit(KnowingConnection, CreatureID); break;
				case CREATURE_SPEED_CHANGED:  SendCreatureSpeed(KnowingConnection, CreatureID); break;
				case CREATURE_SKULL_CHANGED:  SendCreatureSkull(KnowingConnection, CreatureID); break;
				case CREATURE_PARTY_CHANGED:  SendCreatureParty(KnowingConnection, CreatureID); break;
			}
		}else{
			KnownCreature->State = KNOWNCREATURE_OUTDATED;
		}
	}
}

void AnnounceChangedField(Object Obj, int Type){
	if(!Obj.exists()){
		error(Translate("AnnounceChangedField: Übergebenes Objekt existiert nicht.\n",
						"AnnounceChangedField: Passed object does not exist.\n"));
		return;
	}

	int ObjX, ObjY, ObjZ;
	GetObjectCoordinates(Obj, &ObjX, &ObjY, &ObjZ);
	TFindCreatures Search(16, 14, ObjX, ObjY, FIND_PLAYERS);
	while(true){
		uint32 CharacterID = Search.getNext();
		if(CharacterID == 0){
			break;
		}

		TPlayer *Player = GetPlayer(CharacterID);
		if(Player == NULL || Player->Connection == NULL){
			continue;
		}

		if(!Player->Connection->IsVisible(ObjX, ObjY, ObjZ)){
			continue;
		}

		switch(Type){
			case OBJECT_DELETED: SendDeleteField(Player->Connection, ObjX, ObjY, ObjZ, Obj); break;
			case OBJECT_CREATED: SendAddField(Player->Connection,    ObjX, ObjY, ObjZ, Obj); break;
			case OBJECT_CHANGED: SendChangeField(Player->Connection, ObjX, ObjY, ObjZ, Obj); break;
			default:{
				error(Translate("AnnounceChangedField: Ungültiger Typ %d.\n",
								"AnnounceChangedField: Invalid type %d.\n"), Type);
				return;
			}
		}
	}
}

void AnnounceChangedContainer(Object Obj, int Type){
	if(!Obj.exists()){
		error(Translate("AnnounceChangedContainer: Übergebenes Objekt existiert nicht.\n",
						"AnnounceChangedContainer: Passed object does not exist.\n"));
		return;
	}

	Object Con = Obj.getContainer();
	TFindCreatures Search(1, 1, Obj, FIND_PLAYERS);
	while(true){
		uint32 CharacterID = Search.getNext();
		if(CharacterID == 0){
			break;
		}

		TPlayer *Player = GetPlayer(CharacterID);
		if(Player == NULL || Player->Connection == NULL){
			continue;
		}

		for(int ContainerNr = 0;
				ContainerNr < NARRAY(Player->OpenContainer);
				ContainerNr += 1){
			if(Player->GetOpenContainer(ContainerNr) != Con){
				continue;
			}

			switch(Type){
				case OBJECT_DELETED: SendDeleteInContainer(Player->Connection, ContainerNr, Obj); break;
				case OBJECT_CREATED: SendCreateInContainer(Player->Connection, ContainerNr, Obj); break;
				case OBJECT_CHANGED: SendChangeInContainer(Player->Connection, ContainerNr, Obj); break;
				default:{
					error(Translate("AnnounceChangedContainer: Ungültiger Typ %d.\n",
									"AnnounceChangedContainer: Invalid type %d.\n"), Type);
					return;
				}
			}
		}
	}
}

void AnnounceChangedInventory(Object Obj, int Type){
	if(!Obj.exists()){
		error(Translate("AnnounceChangedInventory: Übergebenes Objekt existiert nicht.\n",
						"AnnounceChangedInventory: Passed object does not exist.\n"));
		return;
	}

	uint32 OwnerID = GetObjectCreatureID(Obj);
	TCreature *Creature = GetCreature(OwnerID);
	if(Creature == NULL){
		error(Translate("AnnounceChangedInventory: Objekt liegt in keinem Inventory.\n",
						"AnnounceChangedInventory: Object is not in any inventory.\n"));
		return;
	}

	if(Creature->Type != PLAYER){
		return;
	}

	int Position = GetObjectBodyPosition(Obj);
	switch(Type){
		case OBJECT_DELETED: SendDeleteInventory(Creature->Connection, Position); break;
		case OBJECT_CREATED: SendSetInventory(Creature->Connection, Position, Obj); break;
		case OBJECT_CHANGED: SendSetInventory(Creature->Connection, Position, Obj); break;
		default:{
			error(Translate("AnnounceChangedInventory: Ungültiger Typ %d.\n",
							"AnnounceChangedInventory: Invalid type %d.\n"), Type);
			return;
		}
	}
}

void AnnounceChangedObject(Object Obj, int Type){
	if(!Obj.exists()){
		error(Translate("AnnounceChangedObject: Übergebenes Objekt existiert nicht.\n",
						"AnnounceChangedObject: Passed object does not exist.\n"));
		return;
	}

	Object Con = Obj.getContainer();
	ObjectType ConType = Con.getObjectType();
	if(ConType.isMapContainer()){
		AnnounceChangedField(Obj, Type);
	}else if(ConType.isBodyContainer()){
		AnnounceChangedInventory(Obj, Type);
	}else{
		AnnounceChangedContainer(Obj, Type);
	}
}

void AnnounceGraphicalEffect(int x, int y, int z, int Type){
	if(!IsOnMap(x, y, z)){
		return;
	}

	TFindCreatures Search(16, 14, x, y, FIND_PLAYERS);
	while(true){
		uint32 CharacterID = Search.getNext();
		if(CharacterID == 0){
			break;
		}

		TPlayer *Player = GetPlayer(CharacterID);
		if(Player == NULL || Player->Connection == NULL){
			continue;
		}

		if(!Player->Connection->IsVisible(x, y, z)){
			continue;
		}

		SendGraphicalEffect(Player->Connection, x, y, z, Type);
	}
}

void AnnounceTextualEffect(int x, int y, int z, int Color, const char *Text){
	if(!IsOnMap(x, y, z)){
		return;
	}

	TFindCreatures Search(16, 14, x, y, FIND_PLAYERS);
	while(true){
		uint32 CharacterID = Search.getNext();
		if(CharacterID == 0){
			break;
		}

		TPlayer *Player = GetPlayer(CharacterID);
		if(Player == NULL || Player->Connection == NULL){
			continue;
		}

		if(!Player->Connection->IsVisible(x, y, z)){
			continue;
		}

		SendTextualEffect(Player->Connection, x, y, z, Color, Text);
	}
}

void AnnounceMissile(int OrigX, int OrigY, int OrigZ,
		int DestX, int DestY, int DestZ, int Type){
	if(!IsOnMap(OrigX, OrigY, OrigZ) || !IsOnMap(DestX, DestY, DestZ)){
		return;
	}

	int SearchRadiusX = 16 + (std::abs(OrigX - DestX) / 2) + 1;
	int SearchRadiusY = 14 + (std::abs(OrigY - DestY) / 2) + 1;
	int SearchCenterX = (OrigX + DestX) / 2;
	int SearchCenterY = (OrigY + DestY) / 2;
	TFindCreatures Search(SearchRadiusX, SearchRadiusY, SearchCenterX, SearchCenterY, FIND_PLAYERS);
	while(true){
		uint32 CharacterID = Search.getNext();
		if(CharacterID == 0){
			break;
		}

		TPlayer *Player = GetPlayer(CharacterID);
		if(Player == NULL || Player->Connection == NULL){
			continue;
		}

		if(!Player->Connection->IsVisible(OrigX, OrigY, OrigZ)
		&& !Player->Connection->IsVisible(DestX, DestY, DestZ)){
			continue;
		}

		SendMissileEffect(Player->Connection,
				OrigX, OrigY, OrigZ,
				DestX, DestY, DestZ, Type);
	}
}

void CheckTopMoveObject(uint32 CreatureID, Object Obj, Object Ignore){
	if(!Obj.exists()){
		error(Translate("CheckTopMoveObject: Objekt existiert nicht.\n",
						"CheckTopMoveObject: Object does not exist.\n"));
		throw ERROR;
	}

	if(CreatureID == 0 || !IsCreaturePlayer(CreatureID)){
		return;
	}

	Object Con = Obj.getContainer();
	ObjectType ConType = Con.getObjectType();
	if(!ConType.isMapContainer()){
		return;
	}

	ObjectType ObjType = Obj.getObjectType();
	if(ObjType.isCreatureContainer() && Obj.getCreatureID() == CreatureID){
		return;
	}

	Object Best = NONE;
	bool BestIsCreature = false;
	Object Help = GetFirstContainerObject(Con);
	while(Help != NONE){
		if(Help != Ignore){
			ObjectType HelpType = Help.getObjectType();

			// NOTE(fusion): We're looking for the top move object. The creature
			// check here makes sure only the first creature found is kept as the
			// best candidate, since creatures on a map container are in a sequence.
			if(Best == NONE || (!HelpType.getFlag(UNMOVE) && (!BestIsCreature || !HelpType.isCreatureContainer()))){
				Best = Help;
				BestIsCreature = HelpType.isCreatureContainer();
			}

			if(GetObjectPriority(Help) == PRIORITY_LOW){
				break;
			}
		}
		Help = Help.getNextObject();
	}

	if(Obj != Best){
		throw NOTACCESSIBLE;
	}
}

void CheckTopUseObject(uint32 CreatureID, Object Obj){
	if(!Obj.exists()){
		error(Translate("CheckTopUseObject: Objekt existiert nicht.\n",
						"CheckTopUseObject: Object does not exist.\n"));
		throw ERROR;
	}

	if(CreatureID == 0 || !IsCreaturePlayer(CreatureID)){
		return;
	}

	Object Con = Obj.getContainer();
	ObjectType ConType = Con.getObjectType();
	if(!ConType.isMapContainer()){
		return;
	}

	Object Best = NONE;
	Object Help = GetFirstContainerObject(Con);
	while(Help != NONE){
		ObjectType HelpType = Help.getObjectType();
		if(Best == NONE || (!HelpType.isCreatureContainer() && !HelpType.getFlag(LIQUIDPOOL))){
			Best = Help;
		}

		if(HelpType.getFlag(FORCEUSE) || GetObjectPriority(Help) == PRIORITY_LOW){
			break;
		}

		Help = Help.getNextObject();
	}

	if(Obj != Best){
		throw NOTACCESSIBLE;
	}
}

void CheckTopMultiuseObject(uint32 CreatureID, Object Obj){
	if(!Obj.exists()){
		error(Translate("CheckTopMultiuseObject: Objekt existiert nicht.\n",
						"CheckTopMultiuseObject: Object does not exist.\n"));
		throw ERROR;
	}

	if(CreatureID == 0 || !IsCreaturePlayer(CreatureID)){
		return;
	}

	Object Con = Obj.getContainer();
	ObjectType ConType = Con.getObjectType();
	if(!ConType.isMapContainer()){
		return;
	}

	Object Best = NONE;
	Object Help = GetFirstContainerObject(Con);
	while(Help != NONE){
		ObjectType HelpType = Help.getObjectType();
		if(Best == NONE || !HelpType.getFlag(LIQUIDPOOL)){
			Best = Help;
		}

		if(HelpType.getFlag(FORCEUSE)
				|| GetObjectPriority(Help) == PRIORITY_CREATURE
				|| GetObjectPriority(Help) == PRIORITY_LOW){
			break;
		}

		Help = Help.getNextObject();
	}

	if(Obj != Best){
		throw NOTACCESSIBLE;
	}
}

void CheckMoveObject(uint32 CreatureID, Object Obj, bool Take){
	if(CreatureID == 0){
		return;
	}

	if(!ObjectAccessible(CreatureID, Obj, 1)){
		throw NOTACCESSIBLE;
	}

	ObjectType ObjType = Obj.getObjectType();
	if(ObjType.getFlag(UNMOVE)){
		throw NOTMOVABLE;
	}

	if(ObjType.isCreatureContainer() && Obj.getCreatureID() != CreatureID){
		TCreature *MovingCreature = GetCreature(Obj);
		if(MovingCreature == NULL){
			error(Translate("CheckMoveObject: Kreatur existiert nicht.\n",
							"CheckMoveObject: Creature does not exist.\n"));
			throw ERROR;
		}

		if(GetRaceUnpushable(MovingCreature->Race) && (WorldType != NON_PVP || !MovingCreature->IsPeaceful())){
			throw NOTMOVABLE;
		}
	}

	if(Take && !ObjType.getFlag(TAKE)){
		throw NOTTAKABLE;
	}
}

// NOTE(fusion): This is a helper function for `CheckMapDestination` and `CheckMapPlace`
// and improves the readability of otherwise two convoluted functions.
static bool IsMapBlocked(int DestX, int DestY, int DestZ, ObjectType Type){
	bool HasBank = CoordinateFlag(DestX, DestY, DestZ, BANK);
	if(HasBank && !CoordinateFlag(DestX, DestY, DestZ, UNPASS)){
		return false;
	}

	if(!Type.getFlag(UNPASS)){
		if(HasBank && !CoordinateFlag(DestX, DestY, DestZ, UNLAY)){
			return false;
		}

		if(Type.getFlag(HANG)){
			bool HasHook = CoordinateFlag(DestX, DestY, DestZ, HOOKSOUTH)
						|| CoordinateFlag(DestX, DestY, DestZ, HOOKEAST);
			if(HasHook && !CoordinateFlag(DestX, DestY, DestZ, HANG)){
				return false;
			}
		}
	}

	return true;
}

void CheckMapDestination(uint32 CreatureID, Object Obj, Object MapCon){
	if(CreatureID == 0){
		return;
	}

	int OrigX, OrigY, OrigZ;
	int DestX, DestY, DestZ;
	ObjectType ObjType = Obj.getObjectType();
	GetObjectCoordinates(Obj, &OrigX, &OrigY, &OrigZ);
	GetObjectCoordinates(MapCon, &DestX, &DestY, &DestZ);
	if(!ObjType.isCreatureContainer()){
		if(IsMapBlocked(DestX, DestY, DestZ, ObjType)){
			throw NOROOM;
		}

		if(!ObjType.getFlag(TAKE) && !ObjectInRange(CreatureID, MapCon, 2)){
			throw OUTOFRANGE;
		}
	}else{
		if(std::abs(OrigX - DestX) > 1
				|| std::abs(OrigY - DestY) > 1
				|| std::abs(OrigZ - DestZ) > 1){
			throw OUTOFRANGE;
		}

		if(DestZ == (OrigZ - 1)){
			if(GetHeight(OrigX, OrigY, OrigZ) < 24){ // JUMP_HEIGHT ?
				throw NOROOM;
			}
		}else if(DestZ == (OrigZ + 1)){
			if(GetHeight(DestX, DestY, DestZ) < 24){ // JUMP_HEIGHT ?
				throw NOWAY;
			}
		}

		TCreature *MovingCreature = GetCreature(Obj);
		if(MovingCreature == NULL){
			error("CheckMapDestination: Moving creature is NULL.");
			throw ERROR;
		}

		if(OrigZ == DestZ || MovingCreature->Type != MONSTER){
			if(!MovingCreature->MovePossible(DestX, DestY, DestZ, true, OrigZ != DestZ)){
				if(CreatureID == MovingCreature->ID){
					throw MOVENOTPOSSIBLE;
				}else{
					throw NOROOM;
				}
			}

			if(CreatureID != MovingCreature->ID){
				if(CoordinateFlag(DestX, DestY, DestZ, AVOID)){
					throw NOROOM;
				}

				if(IsProtectionZone(OrigX, OrigY, OrigZ) && !IsProtectionZone(DestX, DestY, DestZ)){
					throw PROTECTIONZONE;
				}
			}
		}
	}

	// TODO(fusion): This looks awfully similar to `ObjectAccessible` outside
	// from the exceptions and floor check.
	if(ObjType.getFlag(HANG)){
		bool HookSouth = CoordinateFlag(DestX, DestY, DestZ, HOOKSOUTH);
		bool HookEast = CoordinateFlag(DestX, DestY, DestZ, HOOKEAST);
		if(HookSouth || HookEast){
			TCreature *Creature = GetCreature(CreatureID);
			if(Creature == NULL){
				error(Translate("CheckMapDestination: Ausführende Kreatur existiert nicht.\n",
								"CheckMapDestination: Executing creature does not exist.\n"));
				throw ERROR;
			}

			if(Creature->posz > DestZ){
				throw UPSTAIRS;
			}else if(Creature->posz < DestZ){
				throw DOWNSTAIRS;
			}

			if(HookSouth){
				if(Creature->posy < DestY
						|| Creature->posy > (DestY + 1)
						|| Creature->posx < (DestX - 1)
						|| Creature->posx > (DestX + 1)){
					throw OUTOFRANGE;
				}
			}
			
			if(HookEast){
				if(Creature->posx < DestX
						|| Creature->posx > (DestX + 1)
						|| Creature->posy < (DestY - 1)
						|| Creature->posy > (DestY + 1)){
					throw OUTOFRANGE;
				}
			}

			return;
		}
	}

	if(!ThrowPossible(OrigX, OrigY, OrigZ, DestX, DestY, DestZ, 1)){
		throw CANNOTTHROW;
	}
}

void CheckMapPlace(uint32 CreatureID, ObjectType Type, Object MapCon){
	int DestX, DestY, DestZ;
	GetObjectCoordinates(MapCon, &DestX, &DestY, &DestZ);

	if(!Type.getFlag(UNMOVE) && !CoordinateFlag(DestX, DestY, DestZ, BANK)){
		if(!Type.getFlag(HANG)){
			throw NOROOM;
		}

		if(!CoordinateFlag(DestX, DestY, DestZ, HOOKSOUTH)
		&& !CoordinateFlag(DestX, DestY, DestZ, HOOKEAST)){
			throw NOROOM;
		}
	}

	// TODO(fusion): The original function used a slightly modified version of
	// `IsMapBlocked` which wouldn't check if there was already a HANG item at
	// the destination but since this function seems to only be called from
	// `Create` which sets `CreatureID` to zero, I assume this was probably an
	// oversight?
	if(CreatureID != 0 && IsMapBlocked(DestX, DestY, DestZ, Type)){
		throw NOROOM;
	}
}

void CheckContainerDestination(Object Obj, Object Con){
	if(IsHeldByContainer(Con, Obj)){
		throw CROSSREFERENCE;
	}

	ObjectType ConType = Con.getObjectType();
	if(!ConType.getFlag(CHEST)){
		int ConObjects = CountObjectsInContainer(Con);
		int ConCapacity = (int)ConType.getAttribute(CAPACITY);
		if(ConObjects >= ConCapacity){
			throw CONTAINERFULL;
		}
	}
}

void CheckContainerPlace(ObjectType Type, Object Con, Object OldObj){
	if(Type == GetSpecialObject(DEPOT_CHEST)){
		return;
	}

	ObjectType ConType = Con.getObjectType();
	if(!ConType.getFlag(CHEST)){
		if(Type.getFlag(UNMOVE)){
			throw NOTMOVABLE;
		}

		if(!Type.getFlag(TAKE)){
			throw NOTTAKABLE;
		}

		if(OldObj == NONE){
			int ConObjects = CountObjectsInContainer(Con);
			int ConCapacity = (int)ConType.getAttribute(CAPACITY);
			if(ConObjects >= ConCapacity){
				throw CONTAINERFULL;
			}
		}
	}
}

void CheckDepotSpace(uint32 CreatureID, Object Source, Object Destination, int Count){
	if(CreatureID == 0){
		return;
	}

	TCreature *Creature = GetCreature(CreatureID);
	if(Creature == NULL){
		error(Translate("CheckDepotSpace: Kreatur %u existiert nicht.\n",
						"CheckDepotSpace: Creature %u does not exist.\n"), CreatureID);
		throw ERROR;
	}

	if(Destination == NONE){
		error(Translate("CheckDepotSpace: Ziel existiert nicht.\n",
						"CheckDepotSpace: Target does not exist.\n"));
		throw ERROR;
	}

	if(Creature->Type == PLAYER){
		TPlayer *Player = (TPlayer*)Creature;
		if(Player->Depot == NONE || Count <= Player->DepotSpace){
			return;
		}

		if(!IsHeldByContainer(Source, Player->Depot)
		&& IsHeldByContainer(Destination, Player->Depot)){
			throw CONTAINERFULL;
		}
	}
}

void CheckInventoryDestination(Object Obj, Object Con, bool Split){
	ObjectType ObjType = Obj.getObjectType();
	int ConPosition = GetObjectBodyPosition(Con);
	bool HandContainer = ConPosition == INVENTORY_RIGHTHAND
					|| ConPosition == INVENTORY_LEFTHAND;

	if(!HandContainer && ConPosition != INVENTORY_AMMO){
		if(!ObjType.getFlag(CLOTHES)){
			throw WRONGPOSITION;
		}

		int ObjPosition = (int)ObjType.getAttribute(BODYPOSITION);
		if(ObjPosition == 0){
			throw WRONGPOSITION2;
		}else if(ObjPosition != ConPosition){
			throw WRONGCLOTHES;
		}
	}

	uint32 CreatureID = GetObjectCreatureID(Con);
	if(HandContainer && ObjType.isTwoHanded()){
		Object RightHand = GetBodyObject(CreatureID, INVENTORY_RIGHTHAND);
		if(RightHand != NONE && RightHand != Obj){
			throw HANDSNOTFREE;
		}

		Object LeftHand = GetBodyObject(CreatureID, INVENTORY_LEFTHAND);
		if(LeftHand != NONE && LeftHand != Obj){
			throw HANDSNOTFREE;
		}

		return;
	}

	if(GetBodyObject(CreatureID, ConPosition) != NONE){
		throw NOROOM;
	}

	if(HandContainer){
		for(int Position = INVENTORY_HAND_FIRST;
				Position <= INVENTORY_HAND_LAST;
				Position += 1){
			Object Other = GetBodyObject(CreatureID, Position);
			if(Other != NONE){
				ObjectType OtherType = Other.getObjectType();
				if(OtherType.isTwoHanded()){
					throw HANDBLOCKED;
				}

				if(Split || Other != Obj){
					if(OtherType.isWeapon() && ObjType.isWeapon()){
						throw ONEWEAPONONLY;
					}
				}
			}
		}
	}
}

void CheckInventoryPlace(ObjectType Type, Object Con, Object OldObj){
	// TODO(fusion): This is awfully similar to `CheckInventoryDestination` with
	// a few subtle differences. Perhaps they use the same underlying function?

	int ConPosition = GetObjectBodyPosition(Con);
	bool HandContainer = ConPosition == INVENTORY_RIGHTHAND
					|| ConPosition == INVENTORY_LEFTHAND;

	if(!HandContainer && ConPosition != INVENTORY_AMMO){
		if(!Type.getFlag(CLOTHES)){
			throw WRONGPOSITION;
		}

		int TypePosition = (int)Type.getAttribute(BODYPOSITION);
		if(TypePosition == 0){
			throw WRONGPOSITION2;
		}else if(TypePosition != ConPosition){
			throw WRONGCLOTHES;
		}
	}

	uint32 CreatureID = GetObjectCreatureID(Con);
	if(HandContainer && Type.isTwoHanded()){
		Object RightHand = GetBodyObject(CreatureID, INVENTORY_RIGHTHAND);
		if(RightHand != NONE && RightHand != OldObj){
			throw HANDSNOTFREE;
		}

		Object LeftHand = GetBodyObject(CreatureID, INVENTORY_LEFTHAND);
		if(LeftHand != NONE && LeftHand != OldObj){
			throw HANDSNOTFREE;
		}

		return;
	}

	if(GetBodyObject(CreatureID, ConPosition) != NONE
	&& GetBodyObject(CreatureID, ConPosition) != OldObj){
		throw NOROOM;
	}

	if(HandContainer){
		for(int Position = INVENTORY_HAND_FIRST;
				Position <= INVENTORY_HAND_LAST;
				Position += 1){
			Object Other = GetBodyObject(CreatureID, Position);
			if(Other != NONE && Other != OldObj){
				ObjectType OtherType = Other.getObjectType();
				if(OtherType.isTwoHanded()){
					throw HANDBLOCKED;
				}

				if(OtherType.isWeapon() && Type.isWeapon()){
					throw ONEWEAPONONLY;
				}
			}
		}
	}
}

void CheckWeight(uint32 CreatureID, Object Obj, int Count){
	if(GetObjectCreatureID(Obj) == CreatureID){
		return;
	}

	TCreature *Creature = GetCreature(CreatureID);
	if(Creature == NULL){
		error(Translate("CheckWeight: Kreatur %d existiert nicht.\n",
						"CheckWeight: Creature %d does not exist.\n"), CreatureID);
		throw ERROR;
	}

	ObjectType ObjType = Obj.getObjectType();
	if(!ObjType.getFlag(TAKE)){
		throw TOOHEAVY;
	}

	if(Creature->Type == PLAYER){
		if(CheckRight(CreatureID, UNLIMITED_CAPACITY)){
			return;
		}

		if(CheckRight(CreatureID, ZERO_CAPACITY)){
			throw TOOHEAVY;
		}
	}

	TSkill *CarryStrength = Creature->Skills[SKILL_CARRY_STRENGTH];
	if(CarryStrength == NULL){
		error(Translate("CheckWeight: Skill CARRYSTRENGTH existiert nicht.\n",
						"CheckWeight: Skill CARRYSTRENGTH does not exist.\n"));
		throw ERROR;
	}

	int ObjWeight;
	if(ObjType.getFlag(CUMULATIVE)){
		ObjWeight = GetWeight(Obj, Count);
	}else{
		ObjWeight = GetCompleteWeight(Obj);
	}

	int InventoryWeight = GetInventoryWeight(CreatureID);
	int MaxWeight = CarryStrength->Get() * 100;
	if((InventoryWeight + ObjWeight) > MaxWeight){
		throw TOOHEAVY;
	}
}

void CheckWeight(uint32 CreatureID, ObjectType Type, uint32 Value, int OldWeight){
	TCreature *Creature = GetCreature(CreatureID);
	if(Creature == NULL){
		error(Translate("CheckWeight: Kreatur %d existiert nicht.\n",
						"CheckWeight: Creature %d does not exist.\n"), CreatureID);
		throw ERROR;
	}

	if(!Type.getFlag(TAKE)){
		throw TOOHEAVY;
	}

	if(Creature->Type == PLAYER){
		if(CheckRight(CreatureID, UNLIMITED_CAPACITY)){
			return;
		}

		if(CheckRight(CreatureID, ZERO_CAPACITY)){
			throw TOOHEAVY;
		}
	}

	TSkill *CarryStrength = Creature->Skills[SKILL_CARRY_STRENGTH];
	if(CarryStrength == NULL){
		error(Translate("CheckWeight: Skill CARRYSTRENGTH existiert nicht.\n",
						"CheckWeight: Skill CARRYSTRENGTH does not exist.\n"));
		throw ERROR;
	}

	int TypeWeight = (int)Type.getAttribute(WEIGHT);
	if(Type.getFlag(CUMULATIVE) && (int)Value > 1){
		TypeWeight *= (int)Value;
	}

	if(TypeWeight > OldWeight){
		int InventoryWeight = GetInventoryWeight(CreatureID);
		int MaxWeight = CarryStrength->Get() * 100;
		if((InventoryWeight + TypeWeight) > MaxWeight){
			throw TOOHEAVY;
		}
	}
}

void NotifyCreature(uint32 CreatureID, Object Obj, bool Inventory){
	if(CreatureID == 0){
		return;
	}

	ObjectType ObjType = Obj.getObjectType();
	if(!ObjType.isCreatureContainer()){
		TCreature *Creature = GetCreature(CreatureID);
		if(Creature == NULL){
			error(Translate("NotifyCreature: Kreatur existiert nicht.\n",
							"NotifyCreature: Creature does not exist.\n"));
			return;
		}

		if(Inventory && ObjType.getFlag(LIGHT)){
			AnnounceChangedCreature(CreatureID, CREATURE_LIGHT_CHANGED);
		}

		Creature->NotifyChangeInventory();
	}
}

void NotifyCreature(uint32 CreatureID, ObjectType Type, bool Inventory){
	if(CreatureID == 0){
		return;
	}

	// TODO(fusion): Shouldn't we check if `Type` is creature container too?

	TCreature *Creature = GetCreature(CreatureID);
	if(Creature == NULL){
		error(Translate("NotifyCreature: Kreatur existiert nicht.\n",
						"NotifyCreature: Creature does not exist.\n"));
		return;
	}

	if(Inventory && Type.getFlag(LIGHT)){
		AnnounceChangedCreature(CreatureID, CREATURE_LIGHT_CHANGED);
	}

	Creature->NotifyChangeInventory();
}

void NotifyAllCreatures(Object Obj, int Type, Object OldCon){
	if(!Obj.exists()){
		error(Translate("NotifyAllCreatures: Übergebenes Objekt existiert nicht.\n",
						"NotifyAllCreatures: Passed object does not exist.\n"));
		return;
	}

	ObjectType ObjType = Obj.getObjectType();
	if(!ObjType.isCreatureContainer()){
		return;
	}

	int ObjX, ObjY, ObjZ;
	uint32 CreatureID = Obj.getCreatureID();
	GetObjectCoordinates(Obj, &ObjX, &ObjY, &ObjZ);

	constexpr int StimulusRadius = 10;
	int SearchRadiusX = StimulusRadius;
	int SearchRadiusY = StimulusRadius;
	int SearchCenterX = ObjX;
	int SearchCenterY = ObjY;
	if(Type == OBJECT_MOVED){
		if(!OldCon.exists()){
			error(Translate("NotifyAllCreatures: Übergebener alter Container existiert nicht.\n",
							"NotifyAllCreatures: Old container handed over does not exist.\n"));
			return;
		}

		// NOTE(fusion): Report stimulus as `OBJECT_CHANGED`.
		Type = OBJECT_CHANGED;

		// NOTE(fusion): Perform a secondary search if the distances are beyond
		// the stimulus diameter or, extend the primary search otherwise.
		int OldX, OldY, OldZ;
		GetObjectCoordinates(OldCon, &OldX, &OldY, &OldZ);
		int DistanceX = std::abs(ObjX - OldX);
		int DistanceY = std::abs(ObjY - OldY);
		if(DistanceX > (StimulusRadius * 2) || DistanceY > (StimulusRadius * 2)){
			TFindCreatures Search(StimulusRadius, StimulusRadius, OldX, OldY, FIND_ALL);
			while(true){
				uint32 SpectatorID = Search.getNext();
				if(SpectatorID == 0){
					break;
				}

				// TODO(fusion): Check if the spectator is NULL?
				TCreature *Spectator = GetCreature(SpectatorID);
				Spectator->CreatureMoveStimulus(CreatureID, Type);
			}
		}else{
			SearchRadiusX = StimulusRadius + (DistanceX + 1) / 2;
			SearchRadiusY = StimulusRadius + (DistanceY + 1) / 2;
			SearchCenterX = (ObjX + OldX) / 2;
			SearchCenterY = (ObjY + OldY) / 2;
		}
	}

	TFindCreatures Search(SearchRadiusX, SearchRadiusY, SearchCenterX, SearchCenterY, FIND_ALL);
	while(true){
		uint32 SpectatorID = Search.getNext();
		if(SpectatorID == 0){
			break;
		}

		// TODO(fusion): Check if the spectator is NULL?
		TCreature *Spectator = GetCreature(SpectatorID);
		Spectator->CreatureMoveStimulus(CreatureID, Type);
	}
}

void NotifyTrades(Object Obj){
	if(!Obj.exists()){
		error(Translate("NotifyTrades: Übergebenes Objekt existiert nicht.\n",
						"NotifyTrades: Passed object does not exist.\n"));
		return;
	}

	ObjectType ObjType = Obj.getObjectType();
	if(ObjType.isCreatureContainer()){
		return;
	}

	// TODO(fusion): These radii seem to be correct but I'm skeptical because
	// you're required to be close to the trade object to even start the trade.
	// Maybe you can walk around after a trade is started?
	TFindCreatures Search(12, 10, Obj, FIND_PLAYERS);
	while(true){
		uint32 CharacterID = Search.getNext();
		if(CharacterID == 0){
			break;
		}

		TPlayer *Player = GetPlayer(CharacterID);
		if(Player == NULL){
			error(Translate("NotifyTrade: Spieler existiert nicht.\n",
							"NotifyTrade: Player does not exist.\n"));
			continue;
		}

		Object TradeObject = Player->InspectTrade(true, 0);
		if(TradeObject != NONE){
			if(IsHeldByContainer(Obj, TradeObject) || IsHeldByContainer(TradeObject, Obj)){
				SendCloseTrade(Player->Connection);
				SendMessage(Player->Connection, TALK_FAILURE_MESSAGE, "Trade cancelled.");
				Player->RejectTrade();
			}
		}
	}
}

void NotifyDepot(uint32 CreatureID, Object Obj, int Count){
	if(CreatureID == 0){
		return;
	}

	TCreature *Creature = GetCreature(CreatureID);
	if(Creature == NULL){
		error(Translate("NotifyDepot: Kreatur %u existiert nicht.\n",
						"NotifyDepot: Creature %u does not exist.\n"), CreatureID);
		// TODO(fusion): We should probably also throw here.
		//throw ERROR;
		return;
	}

	if(!Obj.exists()){
		error(Translate("NotifyDepot: Objekt existiert nicht.\n",
						"NotifyDepot: Object does not exist.\n"));
		throw ERROR;
	}

	if(Creature->Type == PLAYER){
		TPlayer *Player = (TPlayer*)Creature;
		if(Player->Depot == NONE){
			return;
		}

		if(IsHeldByContainer(Obj, Player->Depot)){
			Player->DepotSpace += Count;
			print(3, Translate("Neuer Depot-Freiraum für %s: %d\n",
							   "New depot free space for %s: %d\n"),
					Player->Name, Player->DepotSpace);
		}
	}
}

void CloseContainer(Object Con, bool Force){
	if(!Con.exists()){
		error(Translate("CloseContainer: Übergebener Container existiert nicht.\n",
						"CloseContainer: Transferred container does not exist.\n"));
		return;
	}

	ObjectType ConType = Con.getObjectType();
	if(ConType.isCreatureContainer()){
		return;
	}

	// TODO(fusion): Similar to `NotifyTrades`.
	TFindCreatures Search(12, 10, Con, FIND_PLAYERS);
	while(true){
		uint32 CharacterID = Search.getNext();
		if(CharacterID == 0){
			break;
		}

		TPlayer *Player = GetPlayer(CharacterID);
		if(Player == NULL){
			error(Translate("CloseContainer: Spieler existiert nicht.\n",
							"CloseContainer: Player does not exist.\n"));
			continue;
		}

		for(int ContainerNr = 0;
				ContainerNr < NARRAY(Player->OpenContainer);
				ContainerNr += 1){
			Object OpenCon = Player->GetOpenContainer(ContainerNr);
			if(IsHeldByContainer(OpenCon, Con)){
				if(Force || !ObjectAccessible(Player->ID, Con, 1)){
					Player->SetOpenContainer(ContainerNr, NONE);
					SendCloseContainer(Player->Connection, ContainerNr);
				}else{
					// TODO(fusion): Why are we doing this? Is the container being refreshed?
					SendContainer(Player->Connection, ContainerNr);
				}
			}
		}
	}
}

Object Create(Object Con, ObjectType Type, uint32 Value){
	if(!Con.exists()){
		error(Translate("Create: Zielobjekt existiert nicht.\n",
						"Create: Target object does not exist.\n"));
		throw ERROR;
	}

	ObjectType ConType = Con.getObjectType();
	if(!ConType.getFlag(CONTAINER) && !ConType.getFlag(CHEST)){
		error(Translate("Create: Zielobjekt ist kein Container.\n",
						"Create: Target object is not a Container.\n"));
		throw ERROR;
	}

	// TODO(fusion): The map container does indeed use type id zero but it seems
	// it is also considered a "no type" id so we might also want to add an alias
	// to it as `isValid()`, `isVoid()`, `isNone()`, or `isNull()` perhaps.
	if(Type.isMapContainer()){
		error(Translate("Create: Objekttyp existiert nicht.\n",
						"Create: Object type does not exist.\n"));
		throw ERROR;
	}

	if(ConType.isMapContainer()){
		CheckMapPlace(0, Type, Con);
	}else if(ConType.isBodyContainer()){
		CheckInventoryPlace(Type, Con, NONE);
	}else{
		CheckContainerPlace(Type, Con, NONE);
	}

	uint32 ConOwnerID = GetObjectCreatureID(Con);
	if(ConOwnerID != 0){
		CheckWeight(ConOwnerID, Type, Value, 0);
	}

	// NOTE(fusion): Attempt to merge with top object.
	if(ConType.isMapContainer() && Type.getFlag(CUMULATIVE)){
		int ConX, ConY, ConZ;
		GetObjectCoordinates(Con, &ConX, &ConY, &ConZ);
		Object Top = GetTopObject(ConX, ConY, ConZ, true);
		if(Top != NONE && Top.getObjectType() == Type){
			uint32 Count = Top.getAttribute(AMOUNT);
			if(Value != 0){
				Count += Value;
			}else{
				Count += 1;
			}

			if(Count <= 100){
				Change(Top, AMOUNT, Count);
				return Top;
			}
		}
	}

	// TODO(fusion): I feel these usages of `Value` should be exclusive?

	Object Obj = SetObject(Con, Type, (Type.isCreatureContainer() ? Value : 0));

	if(Type.getFlag(CUMULATIVE)){
		ChangeObject(Obj, AMOUNT, Value);
	}

	if(Type.getFlag(MAGICFIELD)){
		ChangeObject(Obj, RESPONSIBLE, Value);
	}

	if(Type.getFlag(LIQUIDPOOL)){
		ChangeObject(Obj, POOLLIQUIDTYPE, Value);
	}

	if(Type.getFlag(LIQUIDCONTAINER)){
		ChangeObject(Obj, CONTAINERLIQUIDTYPE, Value);
	}

	if(Type.getFlag(KEY)){
		ChangeObject(Obj, KEYNUMBER, Value);
	}

	if(Type.getFlag(RUNE)){
		ChangeObject(Obj, CHARGES, Value);
	}

	if(Type.isCreatureContainer()){
		// BUG(fusion): We should just check this before creating the object.
		// Also, using `Delete` instead of `DeleteObject` here is problematic
		// because the object's placement wasn't yet broadcasted.
		TCreature *Creature = GetCreature(Value);
		if(Creature == NULL){
			error(Translate("Create: Ungültige Kreatur-ID %u\n",
							"Create: Invalid creature ID %u\n"), Value);
			Delete(Obj, -1);
			throw ERROR;
		}

		// IMPORTANT(fusion): Creature body containers are created here and their
		// type ids match inventory slots exactly. We're looping in reverse because
		// `SetObject` will insert objects at the front of the object chain.
		for(int Position = INVENTORY_LAST;
				Position >= INVENTORY_FIRST;
				Position -= 1){
			SetObject(Obj, ObjectType(Position), 0);
		}

		Creature->CrObject = Obj;
		Creature->NotifyCreate();
	}

	AnnounceChangedObject(Obj, OBJECT_CREATED);
	NotifyTrades(Obj);
	NotifyCreature(ConOwnerID, Obj, ConType.isBodyContainer());
	MovementEvent(Obj, Con, Con); // TODO(fusion): This one doesn't make a lot of sense.
	CollisionEvent(Obj, Con);
	NotifyAllCreatures(Obj, OBJECT_CREATED, NONE);
	return Obj;
}

Object Copy(Object Con, Object Source){
	if(!Con.exists()){
		error(Translate("Copy: Ziel existiert nicht.\n",
						"Copy: Target does not exist.\n"));
		throw ERROR;
	}

	ObjectType ConType = Con.getObjectType();
	if(!ConType.getFlag(CONTAINER) && !ConType.getFlag(CHEST)){
		error(Translate("Copy: Ziel ist kein Container.\n",
						"Copy: Target is not a Container.\n"));
		throw ERROR;
	}

	if(!Source.exists()){
		error(Translate("Copy: Quelle existiert nicht.\n",
						"Copy: Source does not exist.\n"));
		throw ERROR;
	}

	ObjectType SourceType = Source.getObjectType();
	if(SourceType.isCreatureContainer()){
		error(Translate("Copy: Quelle ist eine Kreatur.\n",
						"Copy: Source is a creature.\n"));
		throw ERROR;
	}

	if(ConType.isMapContainer()){
		CheckMapDestination(0, Source, Con);
	}else if(ConType.isBodyContainer()){
		// TODO(fusion): Not sure about this third param.
		CheckInventoryDestination(Source, Con, true);
	}else{
		CheckContainerDestination(Source, Con);
	}

	uint32 ConOwnerID = GetObjectCreatureID(Con);
	if(ConOwnerID != 0){
		CheckWeight(ConOwnerID, Source, -1);
	}

	// TODO(fusion): Using `Copy` recursively here should work because the object
	// is placed when `CopyObject` is called. The thing is, announce and notify
	// functions will do meaningless work there (I think, check what happens when
	// we're actually running).
	Object Obj = CopyObject(Con, Source);
	if(SourceType.getFlag(CONTAINER)){
		Object Help = GetFirstContainerObject(Source);
		while(Help != NONE){
			Copy(Obj, Help);
			Help = Help.getNextObject();
		}
	}

	AnnounceChangedObject(Obj, OBJECT_CREATED);
	NotifyTrades(Obj);
	NotifyCreature(ConOwnerID, Obj, ConType.isBodyContainer());
	MovementEvent(Obj, Con, Con); // TODO(fusion): Same as `Create`.
	CollisionEvent(Obj, Con);
	NotifyAllCreatures(Obj, OBJECT_CREATED, NONE);
	return Obj;
}

void Move(uint32 CreatureID, Object Obj, Object Con, int Count, bool NoMerge, Object Ignore){
	if(!Obj.exists()){
		error(Translate("Move: Übergebenes Objekt existiert nicht.\n",
						"Move: Passed object does not exist.\n"));
		throw ERROR;
	}

	if(!Con.exists()){
		int ObjX, ObjY, ObjZ;
		ObjectType ObjType = Obj.getObjectType();
		GetObjectCoordinates(Obj, &ObjX, &ObjY, &ObjZ);
		error(Translate("Move: Zielobjekt existiert nicht.\n",
						"Move: Target object does not exist.\n"));
		error(Translate("# Objekt %d an [%d,%d,%d]\n",
						"# Object %d at [%d,%d,%d]\n"), ObjType.TypeID, ObjX, ObjY, ObjZ);
		throw ERROR;
	}

	ObjectType ConType = Con.getObjectType();
	if(!ConType.getFlag(CONTAINER)){
		error(Translate("Move: Zielobjekt ist kein Container.\n",
						"Move: Target object is not a Container.\n"));
		throw ERROR;
	}

	ObjectType ObjType = Obj.getObjectType();
	if(ObjType.getFlag(CUMULATIVE) && (Count == 0 || Count > (int)Obj.getAttribute(AMOUNT))){
		error(Translate("Move: Ungültige Anzahl %d von %d an Teilen bei kumulierbarem Objekt.\n",
						"Move: Invalid number %d of %d parts for cumulative object.\n"),
				Count, (int)Obj.getAttribute(AMOUNT));
		throw ERROR;
	}

	Object OldCon = Obj.getContainer();
	if(!NoMerge && ConType.isMapContainer()
			&& ObjType.getFlag(CUMULATIVE)
			&& OldCon != Con){
		int ConX, ConY, ConZ;
		GetObjectCoordinates(Con, &ConX, &ConY, &ConZ);
		Object Top = GetTopObject(ConX, ConY, ConZ, true);
		if(Top != NONE){
			try{
				Merge(CreatureID, Obj, Top, Count, Ignore);
				return; // <-- EARLY RETURN IF `Merge` SUCCEEDS.
			}catch(RESULT r){
				if(r == DESTROYED){
					throw;
				}
			}
		}
	}

	// TODO(fusion): The only event inside `Merge` that could modify the object
	// and still throw some non `DESTROYED` error is the separation event.
	//	From what I've seen, it is used exclusively with non CUMULATIVE objects
	// like depot switches to perform "step out" events but using them with
	// CUMULATIVE objects could be problematic as the event is triggered on the
	// whole object which includes both the remainder and moved parts, meaning
	// that transforming the object then would already make the merge "invalid".

	if(!Obj.exists()){
		error(Translate("Move: Übergebenes Objekt existiert nicht mehr.\n",
						"Move: Passed object no longer exists.\n"));
	}

	// NOTE(fusion): Refresh object type just in case a merge event modified it.
	ObjType = Obj.getObjectType();

	int ObjCount = 1;
	if(ObjType.getFlag(CUMULATIVE)){
		ObjCount = (int)Obj.getAttribute(AMOUNT);
	}

	if(Count == -1){
		Count = ObjCount;
	}

	bool Split = Count < ObjCount;
	uint32 ObjOwnerID = GetObjectCreatureID(Obj);
	uint32 ConOwnerID = GetObjectCreatureID(Con);
	CheckTopMoveObject(CreatureID, Obj, Ignore);
	if(ConType.isMapContainer()){
		CheckMoveObject(CreatureID, Obj, false);
		CheckMapDestination(CreatureID, Obj, Con);
	}else{
		CheckMoveObject(CreatureID, Obj, true);
		if(ConType.isBodyContainer()){
			CheckInventoryDestination(Obj, Con, Split);
		}else{
			CheckContainerDestination(Obj, Con);
		}

		if(Split){
			CheckDepotSpace(CreatureID, NONE, Con, 1);
		}else{
			CheckDepotSpace(CreatureID, Obj, Con, CountObjects(Obj));
		}

		if(ConOwnerID != 0){
			CheckWeight(ConOwnerID, Obj, Count);
		}
	}

	// NOTE(fusion): `Obj` becomes the split part while `Remainder`, the remaining part.
	Object Remainder = NONE;
	if(Split){
		Remainder = Obj;
		Obj = SplitObject(Obj, Count);
	}

	if(OldCon != Con){
		SeparationEvent(Obj, OldCon);
	}

	// TODO(fusion): There is surely a way to reduce these creature checks?
	// perhaps split them into two linear paths, with a few duplications but
	// easier to read.

	if(!ObjType.isCreatureContainer() && Remainder == NONE){
		AnnounceChangedObject(Obj, OBJECT_DELETED);
		NotifyTrades(Obj);
		NotifyDepot(CreatureID, Obj, CountObjects(Obj));
	}

	// TODO(fusion): This could probably be moved to the end of the function
	// along with the other `CloseContainer`.
	//	Probably the other way around, since events could modify the object
	// and I don't think it makes a difference if we close it before or after
	// performing the move.
	//	Except it does because the object's position will be different lol.
	if(ObjType.getFlag(CONTAINER) && CreatureID == 0){
		CloseContainer(Obj, true);
	}

	if(ObjType.isCreatureContainer()){
		uint32 MovingCreatureID = Obj.getCreatureID();
		TCreature *Creature = GetCreature(MovingCreatureID);
		if(Creature != NULL){
			Creature->NotifyTurn(Con);
		}else{
			error(Translate("Move: Kreatur mit ungültiger ID %d.\n",
							"Move: Creature with invalid ID %d.\n"), MovingCreatureID);
		}
		AnnounceMovingCreature(MovingCreatureID, Con);
	}

	MoveObject(Obj, Con);

	if(!ObjType.isCreatureContainer()){
		AnnounceChangedObject(Obj, OBJECT_CREATED);
		NotifyTrades(Obj);
		NotifyDepot(CreatureID, Obj, -CountObjects(Obj));
	}

	if(Remainder != NONE){
		AnnounceChangedObject(Remainder, OBJECT_CHANGED);
		NotifyTrades(Remainder);
	}

	if(ObjType.isCreatureContainer()){
		uint32 MovingCreatureID = Obj.getCreatureID();
		TCreature *Creature = GetCreature(MovingCreatureID);
		if(Creature != NULL){
			Creature->NotifyGo();
		}else{
			error(Translate("Move: Kreatur mit ungültiger ID %d.\n",
							"Move: Creature with invalid ID %d.\n"), MovingCreatureID);
		}
	}

	NotifyCreature(ObjOwnerID, Obj, OldCon.getObjectType().isBodyContainer());
	NotifyCreature(ConOwnerID, Obj, Con.getObjectType().isBodyContainer());

	if(ObjType.getFlag(CONTAINER)){
		CloseContainer(Obj, false);
	}

	MovementEvent(Obj, OldCon, Con);
	CollisionEvent(Obj, Con);
	NotifyAllCreatures(Obj, OBJECT_MOVED, OldCon);
}

void Merge(uint32 CreatureID, Object Obj, Object Dest, int Count, Object Ignore){
	if(!Obj.exists()){
		error(Translate("Merge: Übergebenes Objekt existiert nicht.\n",
						"Merge: Passed object does not exist.\n"));
		throw ERROR;
	}

	if(!Dest.exists()){
		error(Translate("Merge: Zielobjekt existiert nicht.\n",
						"Merge: Target object does not exist.\n"));
		throw ERROR;
	}

	ObjectType ObjType = Obj.getObjectType();
	if(ObjType.getFlag(CUMULATIVE) && (Count == 0 || Count > (int)Obj.getAttribute(AMOUNT))){
		error(Translate("Merge: Ungültige Anzahl %d an Teilen bei kumulierbarem Objekt.\n",
						"Merge: Invalid number %d of parts for cumulative object.\n"), Count);
		throw ERROR;
	}

	if(Obj == Dest){
		return;
	}

	if(ObjType != Dest.getObjectType()){
		throw NOMATCH;
	}

	if(!ObjType.getFlag(CUMULATIVE)){
		throw NOTCUMULABLE;
	}

	int ObjCount = Obj.getAttribute(AMOUNT);
	int DestCount = Dest.getAttribute(AMOUNT);
	if(Count == -1){
		Count = ObjCount;
	}

	if((Count + DestCount) > 100){
		throw TOOMANYPARTS;
	}

	Object DestCon = Dest.getContainer();
	uint32 DestOwnerID = GetObjectCreatureID(Dest);
	CheckTopMoveObject(CreatureID, Obj, Ignore);
	if(DestCon.getObjectType().isMapContainer()){
		CheckMoveObject(CreatureID, Obj, false);
		CheckMapDestination(CreatureID, Obj, DestCon);
	}else{
		CheckMoveObject(CreatureID, Obj, true);
		CheckDepotSpace(CreatureID, Obj, DestCon, 0);
		if(DestOwnerID != 0){
			CheckWeight(DestOwnerID, Obj, Count);
		}
	}

	Object ObjCon = Obj.getContainer();
	uint32 ObjOwnerID = GetObjectCreatureID(Obj);

	// TODO(fusion): What is even going on here?
	if(ObjType.getFlag(MOVEMENTEVENT)){
		error(Translate("Merge: Movement-Event für kumulierbares Objekt %d wird nicht ausgelöst.\n",
						"Merge: Movement event for cumulative object %d is not triggered.\n"), ObjType.TypeID);
	}

	if(ObjCon != DestCon){
		SeparationEvent(Obj, ObjCon);
	}

	if(Count < ObjCount){
		ChangeObject(Obj, AMOUNT, ObjCount - Count);
		AnnounceChangedObject(Obj, OBJECT_CHANGED);
		NotifyTrades(Obj);
		ChangeObject(Dest, AMOUNT, DestCount + Count);
	}else{
		AnnounceChangedObject(Obj, OBJECT_DELETED);
		NotifyTrades(Obj);
		NotifyDepot(CreatureID, Obj, 1);
		MergeObjects(Obj, Dest);
	}

	AnnounceChangedObject(Dest, OBJECT_CHANGED);
	NotifyTrades(Dest);
	NotifyCreature(ObjOwnerID, Dest, ObjCon.getObjectType().isBodyContainer());
	NotifyCreature(DestOwnerID, Dest, DestCon.getObjectType().isBodyContainer());
	CollisionEvent(Dest, DestCon);
	NotifyAllCreatures(Dest, OBJECT_CHANGED, NONE);
}

void Change(Object Obj, ObjectType NewType, uint32 Value){
	if(!Obj.exists()){
		error(Translate("Change: Übergebenes Objekt existiert nicht.\n",
						"Change: Passed object does not exist.\n"));
		throw ERROR;
	}

	ObjectType OldType = Obj.getObjectType();
	if(OldType.isCreatureContainer() || NewType.isCreatureContainer()){
		error(Translate("Change: Kann keine Kreaturen verändern.\n",
						"Change: Cannot modify creatures.\n"));
		throw ERROR;
	}

	// TODO(fusion): Same thing as `Create`. The zero type id is also used as
	// a "no type" id.
	if(NewType.isMapContainer()){
		Delete(Obj, -1);
		return;
	}

	if(OldType.getFlag(CONTAINER)){
		if(!NewType.getFlag(CONTAINER) && GetFirstContainerObject(Obj) != NONE){
			error(Translate("Change: Zielobjekt %d für %d ist kein Container mehr.\n",
							"Change: Target object %d for %d is no longer a container.\n"),
					NewType.TypeID, OldType.TypeID);
			throw EMPTYCONTAINER;
		}

		if(NewType.getFlag(CONTAINER)){
			int ObjectCount = CountObjectsInContainer(Obj);
			int NewCapacity = (int)NewType.getAttribute(CAPACITY);
			if(ObjectCount > NewCapacity){
				error(Translate("Change: Zielobjekt %d für %d ist kleinerer Container.\n",
								"Change: Target object %d for %d is smaller container.\n"),
						NewType.TypeID, OldType.TypeID);
				throw EMPTYCONTAINER;
			}
		}
	}

	if(OldType.getFlag(CHEST) && !NewType.getFlag(CHEST)){
		error(Translate("Change: Schatztruhe muß Schatztruhe bleiben.\n",
						"Change: Chest must remain a chest.\n"));
		throw ERROR;
	}

	if(OldType.getFlag(CUMULATIVE)
			&& OldType != NewType
			&& Obj.getAttribute(AMOUNT) > 1){
		throw SPLITOBJECT;
	}

	Object Con = Obj.getContainer();
	ObjectType ConType = Con.getObjectType();
	uint32 ObjOwnerID = GetObjectCreatureID(Obj);
	if(ObjOwnerID != 0 && OldType != NewType){
		if(ConType.isBodyContainer()){
			CheckInventoryPlace(NewType, Con, Obj);
		}else{
			CheckContainerPlace(NewType, Con, Obj);
		}

		uint32 Count = 1;
		if(OldType.getFlag(CUMULATIVE)){
			Count = Obj.getAttribute(AMOUNT);
		}

		int OldWeight = GetWeight(Obj, -1);
		CheckWeight(ObjOwnerID, NewType, Count, OldWeight);
	}

	if(OldType.getFlag(CONTAINER) && !NewType.getFlag(CONTAINER)){
		CloseContainer(Obj, true);
	}

	ChangeObject(Obj, NewType);

	// TODO(fusion): Same thing as `Create`. I feel these usages of `Value`
	// should be exclusive.

	if(NewType.getFlag(CUMULATIVE) && !OldType.getFlag(CUMULATIVE)){
		ChangeObject(Obj, AMOUNT, Value);
	}

	if(NewType.getFlag(MAGICFIELD) && !OldType.getFlag(MAGICFIELD)){
		ChangeObject(Obj, RESPONSIBLE, Value);
	}

	if(NewType.getFlag(LIQUIDPOOL) && !OldType.getFlag(LIQUIDPOOL)){
		ChangeObject(Obj, POOLLIQUIDTYPE, Value);
	}

	if(NewType.getFlag(LIQUIDCONTAINER) && !OldType.getFlag(LIQUIDCONTAINER)){
		ChangeObject(Obj, CONTAINERLIQUIDTYPE, Value);
	}

	if(NewType.getFlag(KEY) && !OldType.getFlag(KEY)){
		ChangeObject(Obj, KEYNUMBER, Value);
	}

	if(NewType.getFlag(RUNE) && !OldType.getFlag(RUNE)){
		ChangeObject(Obj, CHARGES, Value);
	}

	AnnounceChangedObject(Obj, OBJECT_CHANGED);
	NotifyTrades(Obj);
	NotifyCreature(ObjOwnerID, Obj, ConType.isBodyContainer());
	NotifyAllCreatures(Obj, OBJECT_CHANGED, NONE);
}

void Change(Object Obj, INSTANCEATTRIBUTE Attribute, uint32 Value){
	if(!Obj.exists()){
		error(Translate("Change: Übergebenes Objekt existiert nicht.\n",
						"Change: Passed object does not exist.\n"));
		throw ERROR;
	}

	if(Attribute == CONTENT
			|| Attribute == CHESTQUESTNUMBER
			|| Attribute == DOORLEVEL
			|| Attribute == DOORQUESTNUMBER
			|| Attribute == DOORQUESTVALUE
			|| Attribute == ABSTELEPORTDESTINATION
			|| Attribute == REMAININGEXPIRETIME){
		error(Translate("Change: Kann Attribut %d nicht ändern.\n",
						"Change: Cannot change attribute %d.\n"), Attribute);
		throw ERROR;
	}

	uint32 ObjOwnerID = GetObjectCreatureID(Obj);
	if(ObjOwnerID != 0 && Attribute == AMOUNT && Value > Obj.getAttribute(AMOUNT)){
		int OldWeight = GetWeight(Obj, -1);
		CheckWeight(ObjOwnerID, Obj.getObjectType(), Value, OldWeight);
	}

	ChangeObject(Obj, Attribute, Value);
	if(Attribute == AMOUNT
			|| Attribute == CONTAINERLIQUIDTYPE
			|| Attribute == POOLLIQUIDTYPE){
		AnnounceChangedObject(Obj, OBJECT_CHANGED);
	}

	NotifyTrades(Obj);
	NotifyCreature(ObjOwnerID, Obj, Obj.getContainer().getObjectType().isBodyContainer());
}

void Delete(Object Obj, int Count){
	if(!Obj.exists()){
		error(Translate("Delete: Übergebenes Objekt existiert nicht.\n",
						"Delete: Passed object does not exist.\n"));
		throw ERROR;
	}

	ObjectType ObjType = Obj.getObjectType();
	if(ObjType.getFlag(CUMULATIVE) && Count > (int)Obj.getAttribute(AMOUNT)){
		error(Translate("Delete: Ungültige Anzahl %d an Teilen bei kumulierbarem Objekt.\n",
						"Delete: Invalid number %d of parts for cumulative object.\n"), Count);
		throw ERROR;
	}

	Object Remainder = NONE;
	if(ObjType.getFlag(CUMULATIVE) && Count != -1
			&& Count < (int)Obj.getAttribute(AMOUNT)){
		Remainder = Obj;
		Obj = SplitObject(Obj, Count);
	}else{
		NotifyTrades(Obj);
	}

	if(ObjType.getFlag(CONTAINER) || ObjType.getFlag(CHEST)){
		if(ObjType.getFlag(CONTAINER)){
			CloseContainer(Obj, true);
		}

		Object Help = GetFirstContainerObject(Obj);
		while(Help != NONE){
			Object Next = Help.getNextObject();
			DeleteObject(Help);
			Help = Next;
		}
	}

	Object Con = Obj.getContainer();
	uint32 ObjOwnerID = GetObjectCreatureID(Obj);
	SeparationEvent(Obj, Con);

	// BUG(fusion): Object may have been destroyed by the separation event? This
	// looks suspicious because even if the object is properly destroyed, its
	// remainder could be left unannounced.
	//	If it's a container, its objects don't get a separation event trigger,
	// and the separation event trigger on the container itself is only triggered
	// after it is empty.
	if(Obj.exists()){
		if(Remainder == NONE){
			AnnounceChangedObject(Obj, OBJECT_DELETED);
		}

		NotifyAllCreatures(Obj, OBJECT_DELETED, NONE);
		if(ObjType.isCreatureContainer()){
			TCreature *Creature = GetCreature(ObjOwnerID);
			if(Creature != NULL){
				Creature->NotifyDelete();
			}else{
				error(Translate("Delete: Kreatur mit ungültiger ID %d.\n",
								"Delete: Creature with invalid ID %d.\n"), ObjOwnerID);
			}
		}

		DeleteObject(Obj);

		if(Remainder != NONE){
			AnnounceChangedObject(Remainder, OBJECT_CHANGED);
			NotifyTrades(Remainder);
		}

		NotifyCreature(ObjOwnerID, ObjType, Con.getObjectType().isBodyContainer());
	}
}

void Empty(Object Con, int Remainder){
	if(!Con.exists()){
		error(Translate("Empty: Übergebener Container existiert nicht.\n",
						"Empty: Transferred container does not exist.\n"));
		throw ERROR;
	}

	if(Remainder < 0){
		error(Translate("Empty: Ungültiger Rest %d.\n",
						"Empty: Invalid Remainder %d.\n"), Remainder);
		throw ERROR;
	}

	ObjectType ConType = Con.getObjectType();
	if(!ConType.getFlag(CONTAINER)){
		error(Translate("Empty: Übergebenes Objekt ist kein Container.\n",
						"Empty: Passed object is not a container.\n"));
		throw ERROR;
	}

	CloseContainer(Con, true);

	bool IsCorpse = ConType.getFlag(CORPSE);
	int ObjectCount = CountObjectsInContainer(Con);
	Object Help = GetFirstContainerObject(Con);
	while(Help != NONE && ObjectCount > Remainder){
		Object Next = Help.getNextObject();
		if(IsCorpse){
			Delete(Help, -1);
		}else{
			Move(0, Help, Con.getContainer(), -1, false, NONE);
		}
		Help = Next;
		ObjectCount -= 1;
	}
}

void GraphicalEffect(int x, int y, int z, int Type){
	AnnounceGraphicalEffect(x, y, z, Type);
}

void GraphicalEffect(Object Obj, int Type){
	if(Obj.exists()){
		int ObjX, ObjY, ObjZ;
		GetObjectCoordinates(Obj, &ObjX, &ObjY, &ObjZ);
		AnnounceGraphicalEffect(ObjX, ObjY, ObjZ, Type);
	}
}

void TextualEffect(Object Obj, int Color, const char *Format, ...){
	if(!Obj.exists()){
		error(Translate("TextualEffect: Übergebener Mapcontainer existiert nicht.\n",
						"TextualEffect: Passed map container does not exist.\n"));
		return;
	}

	char Buffer[10];
	va_list ap;
	va_start(ap, Format);
	vsnprintf(Buffer, sizeof(Buffer), Format, ap);
	va_end(ap);

	int ObjX, ObjY, ObjZ;
	GetObjectCoordinates(Obj, &ObjX, &ObjY, &ObjZ);
	AnnounceTextualEffect(ObjX, ObjY, ObjZ, Color, Buffer);
}

void Missile(Object Start, Object Dest, int Type){
	if(!Start.exists()){
		error(Translate("Missile: Übergebener Startpunkt existiert nicht.\n",
						"Missile: Passed starting point does not exist.\n"));
		return;
	}

	if(!Dest.exists()){
		error(Translate("Missile: Übergebener Zielpunkt existiert nicht.\n",
						"Missile: Passed destination point does not exist.\n"));
		return;
	}

	int StartX, StartY, StartZ;
	int DestX, DestY, DestZ;
	GetObjectCoordinates(Start, &StartX, &StartY, &StartZ);
	GetObjectCoordinates(Dest, &DestX, &DestY, &DestZ);
	AnnounceMissile(StartX, StartY, StartZ, DestX, DestY, DestZ, Type);
}

void Look(uint32 CreatureID, Object Obj){
	// TODO(fusion): Honestly one of the first things we should focus on, after
	// the server is running, is to implement a small stack buffer writer/formatter
	// to retire `strcpy`, `strcat`, and `sprintf`.

	TPlayer *Player = GetPlayer(CreatureID);
	if(Player == NULL){
		error(Translate("Look: Kreatur existiert nicht.\n",
						"Look: Creature does not exist.\n"));
		throw ERROR;
	}

	if(!Obj.exists()){
		error(Translate("Look: Objekt existiert nicht.\n",
						"Look: Object does not exist.\n"));
		throw ERROR;
	}

	ObjectType ObjType = Obj.getObjectType();
	if(ObjType.isCreatureContainer()){
		TCreature *Target = GetCreature(Obj);
		if(Target == NULL){
			error(Translate("Look: Object %d hat keine Kreatur!\n",
							"Look: Object %d has no creature!\n"), ObjType.TypeID);
			return;
		}

		if(Target->Type == PLAYER){
			TPlayer *TargetPlayer = (TPlayer*)Target;
			bool Yourself = (CreatureID == TargetPlayer->ID);
			char Pronoun[4] = {};
			char Name[50] = {};
			char Vocation[50] = {};

			if(TargetPlayer->Sex == 1){
				strcpy(Pronoun, "He");
			}else{
				strcpy(Pronoun, "She");
			}

			uint8 Profession = TargetPlayer->GetActiveProfession();
			if(Yourself){
				strcpy(Name, "yourself");

				if(Profession != PROFESSION_NONE){
					char Help[30];
					GetProfessionName(Help, Profession, true, false);
					snprintf(Vocation, sizeof(Vocation), "You are %s", Help);
				}else{
					strcpy(Vocation, "You have no vocation");
				}
			}else{
				int Level = 1;
				if(TargetPlayer->Skills[SKILL_LEVEL] != NULL){
					Level = TargetPlayer->Skills[SKILL_LEVEL]->Get();
				}
				snprintf(Name, sizeof(Name), "%s (Level %d)", TargetPlayer->Name, Level);

				if(Profession != PROFESSION_NONE){
					char Help[30];
					GetProfessionName(Help, Profession, true, false);
					snprintf(Vocation, sizeof(Vocation), "%s is %s", Pronoun, Help);
				}else{
					snprintf(Vocation, sizeof(Vocation), "%s has no vocation", Pronoun);
				}
			}

			if(TargetPlayer->Guild[0] != 0){
				char Membership[200] = {};
				if(Yourself){
					strcpy(Membership, "You are ");
				}else{
					snprintf(Membership, sizeof(Membership), "%s is ", Pronoun);
				}

				if(TargetPlayer->Rank[0] != 0){
					strcat(Membership, TargetPlayer->Rank);
				}else{
					strcat(Membership, "a member");
				}

				strcat(Membership, " of the ");
				strcat(Membership, TargetPlayer->Guild);
				if(TargetPlayer->Title[0] != 0){
					strcat(Membership, " (");
					strcat(Membership, TargetPlayer->Title);
					strcat(Membership, ")");
				}

				SendMessage(Player->Connection, TALK_INFO_MESSAGE,
						"You see %s. %s. %s.", Name, Vocation, Membership);
			}else{
				SendMessage(Player->Connection, TALK_INFO_MESSAGE,
						"You see %s. %s.", Name, Vocation);
			}
		}else{
			SendMessage(Player->Connection, TALK_INFO_MESSAGE, "You see %s.", Target->Name);
		}
	}else{
		char Description[500] = {};
		char Help[500] = {};

		snprintf(Description, sizeof(Description), "You see %s", GetName(Obj));

		if(ObjType.getFlag(LEVELDOOR)){
			snprintf(Help, sizeof(Help), " for level %u",
					Obj.getAttribute(DOORLEVEL));
			strcat(Description, Help);
		}

		if(ObjType.getFlag(CONTAINER)){
			snprintf(Help, sizeof(Help), " (Vol:%u)",
					ObjType.getAttribute(CAPACITY));
			strcat(Description, Help);
		}

		if(ObjType.getFlag(WEAPON)){
			snprintf(Help, sizeof(Help), " (Atk:%u Def:%u)",
					ObjType.getAttribute(WEAPONATTACKVALUE),
					ObjType.getAttribute(WEAPONDEFENDVALUE));
			strcat(Description, Help);
		}

		if(ObjType.getFlag(THROW)){
			snprintf(Help, sizeof(Help), " (Atk:%u Def:%u)",
					ObjType.getAttribute(THROWATTACKVALUE),
					ObjType.getAttribute(THROWDEFENDVALUE));
			strcat(Description, Help);
		}

		if(ObjType.getFlag(SHIELD)){
			snprintf(Help, sizeof(Help), " (Def:%u)",
					ObjType.getAttribute(SHIELDDEFENDVALUE));
			strcat(Description, Help);
		}

		if(ObjType.getFlag(ARMOR)){
			snprintf(Help, sizeof(Help), " (Arm:%u)",
					ObjType.getAttribute(ARMORVALUE));
			strcat(Description, Help);
		}

		if(ObjType.getFlag(KEY)){
			snprintf(Help, sizeof(Help), " (Key:%04u)",
					Obj.getAttribute(KEYNUMBER));
			strcat(Description, Help);
		}

		if(ObjType.getFlag(KEYDOOR) && CheckRight(Player->ID, SHOW_KEYHOLE_NUMBERS)){
			snprintf(Help, sizeof(Help), " (Door:%04u)",
					Obj.getAttribute(KEYHOLENUMBER));
			strcat(Description, Help);
		}

		if(ObjType.getFlag(SHOWDETAIL)){
			if(ObjType.getFlag(WEAROUT) && ObjType.getAttribute(TOTALUSES) > 1){
				uint32 RemainingUses = Obj.getAttribute(REMAININGUSES);
				snprintf(Help, sizeof(Help), " that has %u charge%s left",
						RemainingUses, (RemainingUses != 1 ? "s" : ""));
				strcat(Description, Help);
			}else if(ObjType.getFlag(EXPIRE) || ObjType.getFlag(EXPIRESTOP)){
				uint32 SecondsLeft;
				if(ObjType.getFlag(EXPIRE)){
					SecondsLeft = CronInfo(Obj, false);
				}else{
					SecondsLeft = Obj.getAttribute(SAVEDEXPIRETIME);
				}

				uint32 MinutesLeft = (SecondsLeft + 59) / 60;
				if(MinutesLeft == 0){
					snprintf(Help, sizeof(Help), " that is brand-new");
				}else{
					snprintf(Help, sizeof(Help), " that has energy for %u minutes%s left",
							MinutesLeft, (MinutesLeft != 1 ? "s" : ""));
				}
				strcat(Description, Help);
			}else{
				error(Translate("Look: Objekt %d hat Flag SHOWDETAIL, aber weder WEAROUT noch EXPIRE.\n",
								"Look: Object %d has flag SHOWDETAIL, but neither WEAROUT nor EXPIRE.\n"), ObjType.TypeID);
			}
		}

		if(ObjType.getFlag(RESTRICTLEVEL) || ObjType.getFlag(RESTRICTPROFESSION)){
			strcat(Description, ".\nIt can only be wielded by ");
			if(ObjType.getFlag(RESTRICTPROFESSION)){
				int ProfessionCount = 0;
				uint32 ProfessionMask = ObjType.getAttribute(PROFESSIONS);

				if(ProfessionMask & 1){
					ProfessionCount += 1;
					strcpy(Help, "players without vocation");
				}else{
					strcpy(Help, "");
				}

				// NOTE(fusion): We're iterating professions in reverse because
				// the string is built in reverse to avoid extra processing for
				// adding commas and the final "and ...".
				for(int Profession = 4; Profession >= 1; Profession -= 1){
					if((ProfessionMask & (1 << Profession)) == 0){
						continue;
					}

					char Temp[200] = {};
					if(ProfessionCount == 1){
						snprintf(Temp, sizeof(Temp), " and %s", Help);
					}else if(ProfessionCount > 1){
						snprintf(Temp, sizeof(Temp), ", %s", Help);
					}

					switch(Profession){
						case PROFESSION_KNIGHT:		strcpy(Help, "knights"); break;
						case PROFESSION_PALADIN:	strcpy(Help, "paladins"); break;
						case PROFESSION_SORCERER:	strcpy(Help, "sorcerers"); break;
						case PROFESSION_DRUID:		strcpy(Help, "druids"); break;
					}

					strcat(Help, Temp);
				}
			}else{
				strcpy(Help, "players");
			}

			if(ObjType.getFlag(RESTRICTLEVEL)){
				char Temp[200] = {};
				snprintf(Temp, sizeof(Temp), " of level %u or higher",
						ObjType.getAttribute(MINIMUMLEVEL));
				strcat(Help, Temp);
			}

			strcat(Description, Help);
		}

		if(ObjType.getFlag(RUNE)){
			int MagicLevel;
			GetMagicItemDescription(Obj, Help, &MagicLevel);
			if(MagicLevel != 0){
				char Temp[200] = {};
				snprintf(Temp, sizeof(Temp), " for magic level %d", MagicLevel);
				strcat(Description, Temp);
			}

			strcat(Description, ". It's an \"");
			strcat(Description, Help);
			strcat(Description, "\"-spell");

			snprintf(Help, sizeof(Help), " (%ux)", Obj.getAttribute(CHARGES));
			strcat(Description, Help);
		}

		if(ObjType.getFlag(BED)){
			uint32 Sleeper = Obj.getAttribute(TEXTSTRING);
			if(Sleeper != 0){
				snprintf(Help, sizeof(Help), ". %s is sleeping there",
						GetDynamicString(Sleeper));
				strcat(Description, Help);
			}
		}

		if(ObjType.getFlag(NAMEDOOR)){
			int ObjX, ObjY, ObjZ;
			GetObjectCoordinates(Obj, &ObjX, &ObjY, &ObjZ);

			uint16 HouseID = GetHouseID(ObjX, ObjY, ObjZ);
			if(HouseID != 0){
				const char *HouseOwner = GetHouseOwner(HouseID);
				if(HouseOwner == NULL || HouseOwner[0] == 0){
					HouseOwner = "Nobody";
				}

				snprintf(Help, sizeof(Help),
						". It belongs to house '%s'. %s owns this house",
						GetHouseName(HouseID), HouseOwner);
				strcat(Description, Help);
			}else{
				error(Translate("Look: NameDoor auf [%d,%d,%d] gehört zu keinem Haus.\n",
								"Look: NameDoor on [%d,%d,%d] does not belong to any house.\n"), ObjX, ObjY, ObjZ);
			}
		}

		strcat(Description, ".");

		if((ObjType.getFlag(LIQUIDCONTAINER) && Obj.getAttribute(CONTAINERLIQUIDTYPE) == LIQUID_NONE)
		|| (ObjType.getFlag(LIQUIDPOOL)      && Obj.getAttribute(POOLLIQUIDTYPE)      == LIQUID_NONE)){
			strcat(Description, " It is empty.");
		}

		// TODO(fusion): This looks like an inlined function because we'd have
		// already returned at the beginning if `CreatureID` was zero.
		if(CreatureID == 0 || ObjectInRange(CreatureID, Obj, 1)){
			if(ObjType.getFlag(TAKE) && GetWeight(Obj, -1) > 0){
				int ObjWeight = GetCompleteWeight(Obj);
				bool Multiple = ObjType.getFlag(CUMULATIVE)
						&& Obj.getAttribute(AMOUNT) > 1
						&& IsCountable(ObjType.getName(1));
				snprintf(Help, sizeof(Help), "\n%s %d.%02d oz.",
						(Multiple ? "They weigh" : "It weighs"),
						(ObjWeight / 100), (ObjWeight % 100));
				strcat(Description, Help);
			}

			const char *ObjInfo = GetInfo(Obj);
			if(ObjInfo != NULL){
				snprintf(Help, sizeof(Help), "\n%s.", ObjInfo);
				strcat(Description, Help);
			}
		}

		if(ObjType.getFlag(TEXT)){
			int FontSize = (int)ObjType.getAttribute(FONTSIZE);
			if(FontSize == 0){
				uint32 Text = Obj.getAttribute(TEXTSTRING);
				if(Text != 0){
					snprintf(Help, sizeof(Help), "\n%s.", GetDynamicString(Text));
					strcat(Description, Help);
				}
			}else if(FontSize >= 2){
				uint32 Text = Obj.getAttribute(TEXTSTRING);
				if(Text != 0){
					// TODO(fusion): Again, `CreatureID` can't be zero here.
					if(CreatureID == 0 || ObjectInRange(CreatureID, Obj, FontSize)){
						uint32 Editor = Obj.getAttribute(EDITOR);
						if(Editor != 0){
							snprintf(Help, sizeof(Help), "\n%s has written: %s",
									GetDynamicString(Editor),
									GetDynamicString(Text));
						}else{
							snprintf(Help, sizeof(Help), "\nYou read: %s",
									GetDynamicString(Text));
						}
						strcat(Description, Help);
					}else{
						strcat(Description, "\nYou are too far away to read it.");
					}
				}else{
					strcat(Description, "\nNothing is written on it.");
				}
			}
		}

		SendMessage(Player->Connection, TALK_INFO_MESSAGE, "%s", Description);
	}
}

void Talk(uint32 CreatureID, int Mode, const char *Addressee, const char *Text, bool CheckSpamming){
	// TODO(fusion): `Text` was originally `char*`, but I wanted to improve
	// string handling overall and modifying string parameters is a bad idea,
	// specially when you don't know their capacity.
	//	It is only modified when a player uses `TALK_YELL` to make it upper
	// case, which shouldn't be a problem, but if we plan to do any cleanup
	// afterwards, we need to get this right from the beginning.
	char YellBuffer[256];

	TCreature *Creature = GetCreature(CreatureID);
	if(Creature == NULL){
		error(Translate("Talk: Übergebene Kreatur %d existiert nicht.\n",
						"Talk: Surrendered creature %d does not exist.\n"), CreatureID);
		throw ERROR;
	}

	if(Text == NULL){
		error(Translate("Talk: Text ist NULL (Sprecher: %s, Modus: %d).\n",
						"Talk: Text is NULL (Speaker: %s, Mode: %d).\n"), Creature->Name, Mode);
		throw ERROR;
	}

	int TextLen = (int)strlen(Text);
	if(TextLen >= 256){
		error(Translate("Talk: Text ist zu lang (Sprecher: %s, Länge: %d).\n",
						"Talk: Text is too long (Speaker: %s, Length: %d).\n"), Creature->Name, TextLen);
		throw ERROR;
	}

	int Channel = 0;
	if(Mode == TALK_CHANNEL_CALL
			|| Mode == TALK_GAMEMASTER_CHANNELCALL
			|| Mode == TALK_HIGHLIGHT_CHANNELCALL
			|| Mode == TALK_ANONYMOUS_CHANNELCALL){
		// TODO(fusion): Check if `Addressee` is NULL?
		Channel = atoi(Addressee);
		if(Channel < 0 || Channel >= Channels){
			error(Translate("Talk: Ungültiger Kanal %d.\n",
							"Talk: Invalid channel %d.\n"), Channel);
			return;
		}
	}

	print(2, "%s: \"%s\"\n", Creature->Name, Text);

	if(Creature->Type == PLAYER){
		TPlayer *Player = (TPlayer*)Creature;

		// TODO(fusion): Some `IsTalkMuteable` inlined function?
		bool TalkMuteable = Mode == TALK_SAY
				|| Mode == TALK_WHISPER
				|| Mode == TALK_YELL
				|| Mode == TALK_PRIVATE_MESSAGE
				|| ((Mode == TALK_CHANNEL_CALL
					|| Mode == TALK_HIGHLIGHT_CHANNELCALL)
						&& Channel != CHANNEL_GUILD
						&& Channel < CHANNEL_PRIVATE);

		int Muting = 0;
		if(CheckSpamming){
			Muting = Player->CheckForMuting();
			if(Muting > 0 && TalkMuteable){
				SendMessage(Player->Connection, TALK_FAILURE_MESSAGE,
						"You are still muted for %d second%s.",
						Muting, (Muting != 1 ? "s" : ""));
				return;
			}
		}

		int SpellType = 0;
		if(Muting == 0){
			SpellType = CheckForSpell(CreatureID, Text);
			if(SpellType < 0 || SpellType == 1 || SpellType == 5){
				return;
			}

			if(SpellType != 0){
				Mode = TALK_SAY;
			}
		}

		if(Mode == TALK_YELL){
			if(Player->EarliestYellRound > RoundNr){
				throw EXHAUSTED;
			}

			Player->EarliestYellRound = RoundNr + 30;
			strcpy(YellBuffer, Text);
			strUpper(YellBuffer);
			Text = YellBuffer;
		}

		if(Mode == TALK_CHANNEL_CALL && Channel == 5){
			if(Player->EarliestTradeChannelRound > RoundNr){
				SendMessage(Player->Connection, TALK_FAILURE_MESSAGE,
						"You may only place one offer in two minutes.");
				return;
			}

			Player->EarliestTradeChannelRound = RoundNr + 120;
		}

		if(CheckSpamming && SpellType == 0 && TalkMuteable
				&& !CheckRight(Player->ID, NO_BANISHMENT)){
			Muting = Player->RecordTalk();
			if(Muting > 0){
				SendMessage(Player->Connection, TALK_FAILURE_MESSAGE,
						"You are muted for %d second%s.",
						Muting, (Muting != 1 ? "s" : ""));
				return;
			}
		}
	}

	if(Mode == TALK_PRIVATE_MESSAGE
			|| Mode == TALK_GAMEMASTER_ANSWER
			|| Mode == TALK_PLAYER_ANSWER
			|| Mode == TALK_GAMEMASTER_MESSAGE
			|| Mode == TALK_GAMEMASTER_MESSAGE
			|| Mode == TALK_ANONYMOUS_MESSAGE){
		// TODO(fusion): These assumed the creature was a player but it wasn't checked.
		if(Creature->Type != PLAYER){
			error("Talk: Expected player creature for message talk modes.");
			throw ERROR;
		}

		if(Addressee == NULL){
			error(Translate("Talk: Adressat für Botschaft nicht angegeben.\n",
							"Talk: Addressee for message not specified.\n"));
			throw ERROR;
		}

		TPlayer *Player = (TPlayer*)Creature;
		TPlayer *Receiver;
		bool IgnoreGamemasters = !CheckRight(Player->ID, READ_GAMEMASTER_CHANNEL);
		switch(IdentifyPlayer(Addressee, false, IgnoreGamemasters, &Receiver)){
			case  0:	break; // PLAYERFOUND ?
			case -1:	throw PLAYERNOTONLINE;
			case -2:	throw NAMEAMBIGUOUS;
			default:{
				error(Translate("Talk: Ungültiger Rückgabewert von IdentifyPlayer.\n",
								"Talk: Invalid return value from IdentifyPlayer.\n"));
				throw ERROR;
			}
		}

		if(Mode == TALK_PRIVATE_MESSAGE){
			int Muting = Player->RecordMessage(Receiver->ID);
			if(Muting > 0){
				SendMessage(Player->Connection, TALK_FAILURE_MESSAGE,
						"You have addressed too many players. You are muted for %d second%s.",
						Muting, (Muting != 1 ? "s" : ""));
				return;
			}
		}

		if(Mode != TALK_ANONYMOUS_MESSAGE){
			uint32 StatementID = LogCommunication(CreatureID, Mode, 0, Text);
			LogListener(StatementID, Player);
			SendTalk(Receiver->Connection, LogListener(StatementID, Receiver),
					(Mode == TALK_GAMEMASTER_ANSWER ? "Gamemaster" : Player->Name),
					Mode, Text, 0);
		}else{
			SendMessage(Receiver->Connection, TALK_ADMIN_MESSAGE, "%s", Text);
		}

		SendMessage(Player->Connection, TALK_FAILURE_MESSAGE, "Message sent to %s.",
				(Mode == TALK_PLAYER_ANSWER ? "Gamemaster" : Receiver->Name));
		return;
	}else if(Mode == TALK_SAY
			|| Mode == TALK_WHISPER
			|| Mode == TALK_YELL
			|| Mode == TALK_ANIMAL_LOW
			|| Mode == TALK_ANIMAL_LOUD){
		uint32 StatementID;
		if(Creature->Type == PLAYER){
			StatementID = LogCommunication(CreatureID, Mode, 0, Text);
		}else{
			StatementID = 0;
		}

		int Radius;
		if(Mode == TALK_YELL || Mode == TALK_ANIMAL_LOUD){
			Radius = 30; // RANGE_YELL ?
		}else{
			Radius = 7; // RANGE_SAY ?
		}

		TFindCreatures Search(Radius, Radius, Creature->posx, Creature->posy, FIND_PLAYERS);
		while(true){
			uint32 SpectatorID = Search.getNext();
			if(SpectatorID == 0){
				break;
			}

			TPlayer *Spectator = GetPlayer(SpectatorID);
			if(Spectator == NULL || Spectator->Connection == NULL){
				continue;
			}

			int DistanceX = std::abs(Spectator->posx - Creature->posx);
			int DistanceY = std::abs(Spectator->posy - Creature->posy);
			int DistanceZ = std::abs(Spectator->posz - Creature->posz);
			if(Mode == TALK_SAY || Mode == TALK_ANIMAL_LOW){
				if(DistanceX > 7 || DistanceY > 5 || DistanceZ > 0){
					continue;
				}
			}else if(Mode == TALK_WHISPER){
				if(DistanceX > 7 || DistanceY > 5 || DistanceZ > 0){
					continue;
				}

				if(DistanceX > 1 && DistanceY > 1){
					SendTalk(Spectator->Connection, 0, Creature->Name, Mode,
							Creature->posx, Creature->posy, Creature->posz,
							"pspsps");
					continue;
				}
			}else if(Mode == TALK_YELL || Mode == TALK_ANIMAL_LOUD){
				if(DistanceX > 30 || DistanceY > 30){
					continue;
				}

				// TODO(fusion): This seems to be correct. Underground yells
				// aren't multi floor.
				if(DistanceZ > 0 && (Spectator->posz > 7 || Creature->posz > 7)){
					continue;
				}
			}

			SendTalk(Spectator->Connection,
					LogListener(StatementID, Spectator),
					Creature->Name, Mode,
					Creature->posx, Creature->posy, Creature->posz, Text);
		}
	}else if(Mode == TALK_GAMEMASTER_BROADCAST){
		uint32 StatementID = LogCommunication(CreatureID, Mode, 0, Text);
		TConnection *Connection = GetFirstConnection();
		while(Connection != NULL){
			if(Connection->Live()){
				SendTalk(Connection,
						LogListener(StatementID, Connection->GetPlayer()),
						Creature->Name, Mode, Text, 0);
			}

			Connection = GetNextConnection();
		}
	}else if(Mode == TALK_ANONYMOUS_BROADCAST){
		TConnection *Connection = GetFirstConnection();
		while(Connection != NULL){
			if(Connection->Live()){
				SendMessage(Connection, TALK_ADMIN_MESSAGE, Text);
			}

			Connection = GetNextConnection();
		}
	}else if(Mode == TALK_CHANNEL_CALL
			|| Mode == TALK_GAMEMASTER_CHANNELCALL
			|| Mode == TALK_HIGHLIGHT_CHANNELCALL
			|| Mode == TALK_ANONYMOUS_CHANNELCALL){
		uint32 StatementID = 0;
		if(Mode != TALK_ANONYMOUS_CHANNELCALL){
			StatementID = LogCommunication(CreatureID, Mode, Channel, Text);
		}

		// TODO(fusion): We should probably cleanup these global iterators.
		for(uint32 SubscriberID = GetFirstSubscriber(Channel);
				SubscriberID != 0;
				SubscriberID = GetNextSubscriber()){
			TPlayer *Subscriber = GetPlayer(SubscriberID);
			if(Subscriber == NULL){
				continue;
			}

			// TODO(fusion): We should probably review this. You'd assume creature
			// should be a player when talking to any channel.
			if(Channel == CHANNEL_GUILD && (Creature->Type != PLAYER
					|| strcmp(((TPlayer*)Creature)->Guild, Subscriber->Guild) != 0)){
				continue;
			}

			// TODO(fusion): There was some weird logic here to select the statement
			// id and it didn't seem to make a difference since `LogListener` was
			// always called, regardless.
			SendTalk(Subscriber->Connection,
					LogListener(StatementID, Subscriber),
					Creature->Name, Mode, Channel, Text);
		}
	}else{
		// TODO(fusion): Put private messages into this if-else chain.
		error(Translate("Talk: Ungültiger Sprechmodus %d.\n",
						"Talk: Invalid speaking mode %d.\n"), Mode);
	}

	if(Mode == TALK_SAY && Creature->Type == PLAYER){
		TFindCreatures Search(3, 3, CreatureID, FIND_NPCS);
		while(true){
			uint32 SpectatorID = Search.getNext();
			if(SpectatorID == 0){
				break;
			}

			TCreature *Spectator = GetCreature(SpectatorID);
			if(Spectator == NULL){
				error(Translate("Talk: Kreatur existiert nicht.\n",
								"Talk: Creature does not exist.\n"));
				continue;
			}

			if(Spectator->posz == Creature->posz){
				Spectator->TalkStimulus(CreatureID, Text);
			}
		}
	}
}

void Use(uint32 CreatureID, Object Obj1, Object Obj2, uint8 Info){
	if(!Obj1.exists()){
		error(Translate("Use: Übergebenes Objekt Obj 1 existiert nicht.\n",
						"Use: Passed object Obj 1 does not exist.\n"));
		throw ERROR;
	}

	if(Obj2 != NONE && !Obj2.exists()){
		error(Translate("Use: Übergebenes Objekt Obj 2 existiert nicht.\n",
						"Use: Passed object Obj 2 does not exist.\n"));
		throw ERROR;
	}

	CheckTopUseObject(CreatureID, Obj1);
	if(!ObjectAccessible(CreatureID, Obj1, 1)){
		throw NOTACCESSIBLE;
	}

	ObjectType ObjType1 = Obj1.getObjectType();
	if(Obj2 != NONE){
		// TODO(fusion): Is this correct?
		if(!ObjType1.getFlag(DISTUSE)){
			CheckTopMultiuseObject(CreatureID, Obj2);
		}

		if(!ObjectAccessible(CreatureID, Obj2, 1)){
			if(!ObjType1.getFlag(DISTUSE)){
				throw NOTACCESSIBLE;
			}

			TCreature *Creature = GetCreature(CreatureID);
			if(Creature == NULL){
				error(Translate("Use: Verursachende Kreatur existiert nicht.\n",
								"Use: Causing creature does not exist.\n"));
				throw ERROR;
			}

			int ObjX2, ObjY2, ObjZ2;
			GetObjectCoordinates(Obj2, &ObjX2, &ObjY2, &ObjZ2);
			if(Creature->posz > ObjZ2){
				throw UPSTAIRS;
			}else if(Creature->posz < ObjZ2){
				throw DOWNSTAIRS;
			}

			if(!ThrowPossible(Creature->posx, Creature->posy, Creature->posz, ObjX2, ObjY2, ObjZ2, 0)){
				throw CANNOTTHROW;
			}
		}
	}

	if(ObjType1.getFlag(CONTAINER)){
		UseContainer(CreatureID, Obj1, Info);
	}else if(ObjType1.getFlag(CHEST)){
		UseChest(CreatureID, Obj1);
	}else if(ObjType1.getFlag(LIQUIDCONTAINER)){
		UseLiquidContainer(CreatureID, Obj1, Obj2);
	}else if(ObjType1.getFlag(FOOD)){
		UseFood(CreatureID, Obj1);
	}else if(ObjType1.getFlag(WRITE) || ObjType1.getFlag(WRITEONCE)){
		UseTextObject(CreatureID, Obj1);
	}else if(ObjType1.getFlag(INFORMATION)){
		UseAnnouncer(CreatureID, Obj1);
	}else if(ObjType1.getFlag(RUNE)){
		UseMagicItem(CreatureID, Obj1, Obj2);
	}else if(ObjType1.getFlag(KEY)){
		UseKeyDoor(CreatureID, Obj1, Obj2);
	}else if(ObjType1.getFlag(NAMEDOOR)){
		UseNameDoor(CreatureID, Obj1);
	}else if(ObjType1.getFlag(LEVELDOOR)){
		UseLevelDoor(CreatureID, Obj1);
	}else if(ObjType1.getFlag(QUESTDOOR)){
		UseQuestDoor(CreatureID, Obj1);
	}else if(ObjType1.isCloseWeapon()
			&& Obj2 != NONE
			&& Obj2.getObjectType().getFlag(DESTROY)){
		UseWeapon(CreatureID, Obj1, Obj2);
	}else if(ObjType1.getFlag(CHANGEUSE)){
		UseChangeObject(CreatureID, Obj1);
	}else if(ObjType1.getFlag(USEEVENT)){
		if(Obj2 != NONE){
			UseObjects(CreatureID, Obj1, Obj2);
		}else{
			UseObject(CreatureID, Obj1);
		}
	}else if(ObjType1.getFlag(TEXT)
			&& ObjType1.getAttribute(FONTSIZE) == 1){
		UseTextObject(CreatureID, Obj1);
	}else{
		throw NOTUSABLE;
	}
}

void Turn(uint32 CreatureID, Object Obj){
	if(!Obj.exists()){
		error(Translate("Turn: Übergebenes Objekt existiert nicht.\n",
						"Turn: Passed object does not exist.\n"));
		throw ERROR;
	}

	if(!ObjectAccessible(CreatureID, Obj, 1)){
		throw NOTACCESSIBLE;
	}

	ObjectType ObjType = Obj.getObjectType();
	if(!ObjType.getFlag(ROTATE)){
		throw NOTTURNABLE;
	}

	ObjectType RotateTarget = (int)ObjType.getAttribute(ROTATETARGET);
	if(RotateTarget == ObjType){
		error(Translate("Turn: Objekt %d wird durch Drehen zerstört.\n",
						"Turn: Object %d is destroyed by rotating.\n"), ObjType.TypeID);
	}

	Change(Obj, RotateTarget, 0);
}

void CreatePool(Object Con, ObjectType Type, uint32 Value){
	if(!Con.exists()){
		error(Translate("CreatePool: Übergebener MapContainer existiert nicht.\n",
						"CreatePool: Passed MapContainer does not exist.\n"));
		throw ERROR;
	}

	ObjectType ConType = Con.getObjectType();
	if(!ConType.isMapContainer()){
		error(Translate("CreatePool: Übergebenes Objekt ist kein MapContainer.\n",
						"CreatePool: Passed object is not a MapContainer.\n"));
		throw ERROR;
	}

	// NOTE(fusion): There can be no liquid pools on fields with other BOTTOM
	// objects, with the exception of other liquid pools, in which case the new
	// liquid pool would replace the old one.
	Object Help = GetFirstContainerObject(Con);
	while(Help != NONE){
		Object Next = Help.getNextObject();
		ObjectType HelpType = Help.getObjectType();
		if(HelpType.getFlag(BOTTOM)){
			if(!HelpType.getFlag(LIQUIDPOOL)){
				throw NOROOM;
			}

			try{
				Delete(Help, -1);
			}catch(RESULT r){
				error(Translate("CreatePool: Exception %d beim Löschen des alten Pools.\n",
								"CreatePool: Exception %d while deleting the old pool.\n"), r);
			}
		}
		Help = Next;
	}

	Create(Con, Type, Value);
}

void EditText(uint32 CreatureID, Object Obj, const char *Text){
	if(!Obj.exists()){
		error(Translate("EditText: Übergebenes Objekt existiert nicht.\n",
						"EditText: Passed object does not exist.\n"));
		throw ERROR;
	}

	ObjectType ObjType = Obj.getObjectType();
	if(!ObjType.getFlag(WRITE) && (!ObjType.getFlag(WRITEONCE) || Obj.getAttribute(TEXTSTRING) != 0)){
		error(Translate("EditText: Objekt ist nicht beschreibbar.\n",
						"EditText: Object is not writable.\n"));
		throw ERROR;
	}

	TCreature *Creature = GetCreature(CreatureID);
	if(Creature == NULL){
		error(Translate("EditText: Kreatur existiert nicht.\n",
						"EditText: Creature does not exist.\n"));
		throw ERROR;
	}

	int TextLength = (int)strlen(Text);
	int MaxLength = (ObjType.getFlag(WRITE)
			? (int)ObjType.getAttribute(MAXLENGTH)
			: (int)ObjType.getAttribute(MAXLENGTHONCE));
	if(TextLength >= MaxLength){
		throw TOOLONG;
	}

	// TODO(fusion): Similar to maybe inlined function in `Look`.
	// TODO(fusion): Same as in `Look`. `CreatureID` can't be zero here or we'd
	// have already returned so its probably some inlined function.
	if(CreatureID != 0 && !ObjectAccessible(CreatureID, Obj, 1)){
		throw NOTACCESSIBLE;
	}

	uint32 ObjText = Obj.getAttribute(TEXTSTRING);
	if((ObjText != 0 && strcmp(GetDynamicString(ObjText), Text) == 0)
			|| (ObjText == 0 && Text[0] == 0)){
		print(3, Translate("Text hat sich nicht geändert.\n",
							"Text has not changed.\n"));
		return;
	}

	DeleteDynamicString(ObjText);
	Change(Obj, TEXTSTRING, AddDynamicString(Text));

	DeleteDynamicString(Obj.getAttribute(EDITOR));
	Change(Obj, EDITOR, AddDynamicString(Creature->Name));
}

Object CreateAtCreature(uint32 CreatureID, ObjectType Type, uint32 Value){
	// TODO(fusion): Bruhh...

	TCreature *Creature = GetCreature(CreatureID);
	if(Creature == NULL){
		// TODO(fusion): Different function name?
		error(Translate("GiveObjectToCreature: Kann Kreatur nicht finden.\n",
						"GiveObjectToCreature: Cannot find creature.\n"));
		throw ERROR;
	}

	try{
		CheckWeight(CreatureID, Type, Value, 0);
	}catch(RESULT r){
		return Create(GetMapContainer(Creature->CrObject), Type, Value);
	}

	Object Obj = NONE;
	bool CheckContainers = Type.getFlag(MOVEMENTEVENT);
	for(int i = 0; i < 2; i += 1){
		bool TooHeavy = false;
		for(int Position = INVENTORY_FIRST;
				Position <= INVENTORY_LAST;
				Position += 1){
			try{
				Object BodyObj = GetBodyObject(CreatureID, Position);
				if(CheckContainers){
					if(BodyObj != NONE && BodyObj.getObjectType().getFlag(CONTAINER)){
						Obj = Create(BodyObj, Type, Value);
						break;
					}
				}else{
					if(BodyObj == NONE){
						Obj = Create(GetBodyContainer(CreatureID, Position), Type, Value);
						break;
					}
				}
			}catch(RESULT r){
				// TODO(fusion): Is this even possible if we're checking the
				// weight before hand?
				if(r == TOOHEAVY){
					TooHeavy = true;
					break;
				}
			}
		}

		if(Obj != NONE || TooHeavy){
			break;
		}

		CheckContainers = !CheckContainers;
	}

	if(Obj == NONE){
		Obj = Create(GetMapContainer(Creature->CrObject), Type, Value);
	}

	return Obj;
}

void DeleteAtCreature(uint32 CreatureID, ObjectType Type, int Amount, uint32 Value){
	while(Amount > 0){
		Object Obj = GetInventoryObject(CreatureID, Type, Value);
		if(Obj == NONE){
			error(Translate("DeleteAtCreature: Kein Objekt gefunden.\n",
							"DeleteAtCreature: No object found.\n"));
			throw ERROR;
		}

		if(Type.getFlag(CUMULATIVE)){
			int ObjAmount = (int)Obj.getAttribute(AMOUNT);
			if(ObjAmount <= Amount){
				Delete(Obj, -1);
				Amount -= ObjAmount;
			}else{
				Change(Obj, AMOUNT, (ObjAmount - Amount));
				Amount = 0;
			}
		}else{
			Delete(Obj, -1);
			Amount -= 1;
		}
	}
}

void ProcessCronSystem(void){
	while(true){
		Object Obj = CronCheck();
		if(Obj == NONE){
			break;
		}

		try{
			ObjectType ObjType = Obj.getObjectType();
			ObjectType ExpireTarget = (int)ObjType.getAttribute(EXPIRETARGET);
			if(ObjType.getFlag(CONTAINER)){
				int Remainder = 0;
				if(!ExpireTarget.isMapContainer() && ExpireTarget.getFlag(CONTAINER)){
					Remainder = (int)ExpireTarget.getAttribute(CAPACITY);
				}

				Empty(Obj, Remainder);
			}

			Change(Obj, ExpireTarget, 0);
		}catch(RESULT r){
			error(Translate("ProcessCronSystem: Exception %d beim Verwesen von %d.\n",
							"ProcessCronSystem: Exception %d while decaying %d.\n"),
					r, Obj.getObjectType().TypeID);

			try{
				Delete(Obj, -1);
			}catch(RESULT r){
				error(Translate("ProcessCronSystem: Kann Objekt nicht löschen - Exception %d.\n",
								"ProcessCronSystem: Cannot delete object - Exception %d.\n"), r);
			}
		}
	}
}

bool SectorRefreshable(int SectorX, int SectorY, int SectorZ){
	// TODO(fusion): Have the sector size defined as a constant in `map.hh`?
	//constexpr int SectorSize = 32;
	int SearchRadiusX = 32 - 1;
	int SearchRadiusY = 32 - 1;
	int SearchCenterX = SectorX * 32 + (32 / 2);
	int SearchCenterY = SectorY * 32 + (32 / 2);
	TFindCreatures Search(SearchRadiusX, SearchRadiusY, SearchCenterX, SearchCenterY, FIND_PLAYERS);
	while(true){
		uint32 CharacterID = Search.getNext();
		if(CharacterID == 0){
			break;
		}

		TPlayer *Player = GetPlayer(CharacterID);
		if(Player == NULL){
			error(Translate("SectorRefreshable: Kreatur existiert nicht.\n",
							"SectorRefreshable: Creature does not exist.\n"));
			continue;
		}

		if(Player->CanSeeFloor(SectorZ)){
			return false;
		}
	}

	return true;
}

void RefreshSector(int SectorX, int SectorY, int SectorZ, const uint8 *Data, int Count){
	if(!SectorRefreshable(SectorX, SectorY, SectorZ)){
		return;
	}

	TReadBuffer Buffer(Data, Count);
	RefreshSector(SectorX, SectorY, SectorZ, &Buffer);

	// TODO(fusion): This function is very similar to `ApplyPatch`.
	int SearchRadiusX = 32 / 2;
	int SearchRadiusY = 32 / 2;
	int SearchCenterX = SectorX * 32 + (32 / 2);
	int SearchCenterY = SectorY * 32 + (32 / 2);
	TFindCreatures Search(SearchRadiusX, SearchRadiusY, SearchCenterX, SearchCenterY, FIND_ALL);
	while(true){
		uint32 CreatureID = Search.getNext();
		if(CreatureID == 0){
			break;
		}

		TCreature *Creature = GetCreature(CreatureID);
		if(Creature == NULL){
			error(Translate("RefreshSector: Kreatur existiert nicht.\n",
							"RefreshSector: Creature does not exist.\n"));
			continue;
		}

		if(Creature->posz != SectorZ){
			continue;
		}

		bool FieldBlocked = false;
		int FieldX = Creature->posx;
		int FieldY = Creature->posy;
		int FieldZ = Creature->posz;
		Object Obj = GetFirstObject(FieldX, FieldY, FieldZ);
		while(Obj != NONE){
			ObjectType ObjType = Obj.getObjectType();
			if(!ObjType.isCreatureContainer() && ObjType.getFlag(UNPASS)){
				FieldBlocked = true;
				break;
			}
			Obj = Obj.getNextObject();
		}

		if(FieldBlocked){
			try{
				if(!SearchFreeField(&FieldX, &FieldY, &FieldZ, 1, 0, false)){
					if(Creature->Type == NPC){
						print(2, Translate("NPC auf [%d,%d,%d] muß auf seinen Start zurückgesetzt werden.\n",
										   "NPC on [%d,%d,%d] needs to be reset to its start.\n"), FieldX, FieldY, FieldZ);
						FieldX = Creature->startx;
						FieldY = Creature->starty;
						FieldZ = Creature->startz;
					}else if(Creature->Type == MONSTER){
						print(2, Translate("Monster auf [%d,%d,%d] muß gelöscht werden.\n",
										   "Monster on [%d,%d,%d] must be deleted.\n"), FieldX, FieldY, FieldZ);
						delete Creature;
						continue;
					}else{
						error(Translate("Spieler auf [%d,%d,%d] ist von Refresh betroffen.\n",
										"Player on [%d,%d,%d] is affected by refresh.\n"), FieldX, FieldY, FieldZ);
						continue;
					}
				}

				Object MapCon = GetMapContainer(FieldX, FieldY, FieldZ);
				Move(0, Creature->CrObject, MapCon, -1, false, NONE);
			}catch(RESULT r){
				error(Translate("RefreshSector: Exception %d beim Neusetzen des Monsters.\n",
								"RefreshSector: Exception %d when resetting the monster.\n"), r);
			}
		}
	}
}

void RefreshMap(void){
	TDynamicWriteBuffer HelpBuffer(KB(64));
	for(int SectorZ = SectorZMin; SectorZ <= SectorZMax; SectorZ += 1)
	for(int SectorY = SectorYMin; SectorY <= SectorYMax; SectorY += 1)
	for(int SectorX = SectorXMin; SectorX <= SectorXMax; SectorX += 1){
		if(!SectorRefreshable(SectorX, SectorY, SectorZ)){
			continue;
		}

		char FileName[4096];
		snprintf(FileName, sizeof(FileName), "%s/%04d-%04d-%02d.sec",
				ORIGMAPPATH, SectorX, SectorY, SectorZ);
		if(!FileExists(FileName)){
			continue;
		}

		int OffsetX = -1;
		int OffsetY = -1;
		bool Refreshable = false;
		HelpBuffer.Position = 0;
		try{
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
					uint8 *SectorOffset = Script.getBytesequence();
					OffsetX = (int)SectorOffset[0];
					OffsetY = (int)SectorOffset[1];
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

			if(HelpBuffer.Position > 0){
				TReadBuffer ReadBuffer(HelpBuffer.Data, HelpBuffer.Position);
				RefreshSector(SectorX, SectorY, SectorZ, &ReadBuffer);
			}
		}catch(const char *str){
			error(Translate("RefreshMap: Fehler beim Bearbeiten der Datei (%s).\n",
							"RefreshMap: Error editing file (%s).\n"), str);
		}
	}
}

void RefreshCylinders(void){
	// TODO(fusion): `RefreshedCylinders` is the number of cylinders we attempt to
	// refresh in this function, which is called every minute or so by `AdvanceGame`.
	// We should probably rename it to something more clear.

	// TODO(fusion): This might be on purpose but `RefreshX` and `RefreshY` will be
	// way below `SectorXMin` and `SectorYMin` the first N times this function is
	// called and depending on the value of `RefreshedCylinders`, could take a few
	// hours before we even start refreshing the map.
	//	If the intent is to delay refreshing cylinders until some time after startup,
	// we should change how this function is called by `AdvanceGame` instead.

	static int RefreshX = -1;
	static int RefreshY = -1;
	for(int i = 0; i < RefreshedCylinders; i += 1){
		if(RefreshX < SectorXMax){
			RefreshX += 1;
		}else{
			RefreshX = SectorXMin;
			if(RefreshY < SectorYMax){
				RefreshY += 1;
			}else{
				RefreshY = SectorYMin;
			}
		}

		for(int RefreshZ = SectorZMin;
				RefreshZ <= SectorZMax;
				RefreshZ += 1){
			if(SectorRefreshable(RefreshX, RefreshY, RefreshZ)){
				LoadSectorOrder(RefreshX, RefreshY, RefreshZ);
			}
		}
	}
}

void ApplyPatch(int SectorX, int SectorY, int SectorZ,
		bool FullSector, TReadScriptFile *Script, bool SaveHouses){
	if(SectorX < SectorXMin || SectorX > SectorXMax
	|| SectorY < SectorYMin || SectorY > SectorYMax
	|| SectorZ < SectorZMin || SectorZ > SectorZMax){
		return;
	}

	PrepareHouseCleanup();
	PatchSector(SectorX, SectorY, SectorZ, FullSector, Script, SaveHouses);
	FinishHouseCleanup();

	// TODO(fusion): Similar to `SectorRefreshable` but with a reduced radius.
	int SearchRadiusX = 32 / 2;
	int SearchRadiusY = 32 / 2;
	int SearchCenterX = SectorX * 32 + (32 / 2);
	int SearchCenterY = SectorY * 32 + (32 / 2);
	TFindCreatures Search(SearchRadiusX, SearchRadiusY, SearchCenterX, SearchCenterY, FIND_ALL);
	while(true){
		uint32 CreatureID = Search.getNext();
		if(CreatureID == 0){
			break;
		}

		TCreature *Creature = GetCreature(CreatureID);
		if(Creature == NULL){
			error(Translate("ApplyPatch: Kreatur existiert nicht.\n",
							"ApplyPatch: Creature does not exist.\n"));
			continue;
		}

		if(Creature->posz != SectorZ){
			continue;
		}

		bool FieldBlocked = false;
		int FieldX = Creature->posx;
		int FieldY = Creature->posy;
		int FieldZ = Creature->posz;
		Object Obj = GetFirstObject(FieldX, FieldY, FieldZ);
		while(Obj != NONE){
			ObjectType ObjType = Obj.getObjectType();
			if(!ObjType.isCreatureContainer() && ObjType.getFlag(UNPASS)){
				FieldBlocked = true;
				break;
			}
			Obj = Obj.getNextObject();
		}

		if(!FieldBlocked){
			// TODO(fusion): Not sure why we're moving the creature to the same
			// field, specially with `MoveObject`. Does `PatchSector` leave
			// creatures on invalid indices?
			Object MapCon = GetMapContainer(FieldX, FieldY, FieldZ);
			MoveObject(Creature->CrObject, MapCon);
		}else{
			try{
				if(!SearchFreeField(&FieldX, &FieldY, &FieldZ, 1, 0, false)){
					if(Creature->Type == NPC){
						print(2, Translate("NPC auf [%d,%d,%d] muß auf seinen Start zurückgesetzt werden.\n",
										   "NPC on [%d,%d,%d] needs to be reset to its start.\n"), FieldX, FieldY, FieldZ);
						FieldX = Creature->startx;
						FieldY = Creature->starty;
						FieldZ = Creature->startz;
					}else if(Creature->Type == MONSTER){
						print(2, Translate("Monster auf [%d,%d,%d] muß gelöscht werden.\n",
										   "Monster on [%d,%d,%d] must be deleted.\n"), FieldX, FieldY, FieldZ);
						Delete(Creature->CrObject, -1);
						Creature->StartLogout(false, false);
						continue;
					}else{
						error(Translate("Spieler auf [%d,%d,%d] ist von Patch betroffen.\n",
										"Player on [%d,%d,%d] is affected by patch.\n"), FieldX, FieldY, FieldZ);
						continue;
					}
				}

				Object MapCon = GetMapContainer(FieldX, FieldY, FieldZ);
				Move(0, Creature->CrObject, MapCon, -1, false, NONE);
			}catch(RESULT r){
				error(Translate("ApplyPatch: Exception %d beim Neusetzen des Monsters.\n",
								"ApplyPatch: Exception %d when resetting the monster.\n"), r);
			}
		}
	}
}

void ApplyPatches(void){
	// TODO(fusion): We don't handle any script exceptions in here which could
	// lead to `PatchDir` being leaked. It may not be a problem overall because
	// this function is only called at startup by `InitAll`.

	char FileName[4096];
	bool SaveHouses = false;
	snprintf(FileName, sizeof(FileName), "%s/save-houses", SAVEPATH);
	if(FileExists(FileName)){
		SaveHouses = true;
		print(2, Translate("Häuser werden nicht gepatcht.\n",
						   "Houses are not patched.\n"));
		unlink(FileName);
	}

	DIR *PatchDir = opendir(SAVEPATH);
	if(PatchDir == NULL){
		error(Translate("ApplyPatches: Unterverzeichnis %s nicht gefunden.\n",
						"ApplyPatches: Subdirectory %s not found.\n"), SAVEPATH);
		return;
	}

	int MaxPatch = -1;
	while(dirent *DirEntry = readdir(PatchDir)){
		if(DirEntry->d_type != DT_REG){
			continue;
		}

		const char *FileExt = findLast(DirEntry->d_name, '.');
		if(FileExt == NULL){
			continue;
		}

		if(strcmp(FileExt, ".pat") == 0){
			int Patch;
			if(sscanf(DirEntry->d_name, "%d.pat", &Patch) == 1){
				if(MaxPatch < Patch){
					MaxPatch = Patch;
				}
			}
		}else if(strcmp(FileExt, ".sec") == 0){
			int SectorX, SectorY, SectorZ;
			if(sscanf(DirEntry->d_name, "%d-%d-%d.sec", &SectorX, &SectorY, &SectorZ) == 3){
				print(2,Translate("Patche kompletten Sektor %d/%d/%d ...\n",
								  "Patch entire sector %d/%d/%d ...\n"), SectorX, SectorY, SectorZ);
				snprintf(FileName, sizeof(FileName), "%s/%s", SAVEPATH, DirEntry->d_name);

				TReadScriptFile Script;
				Script.open(FileName);
				ApplyPatch(SectorX, SectorY, SectorZ, true, &Script, SaveHouses);
				Script.close();
				unlink(FileName);
			}
		}
	}

	closedir(PatchDir);

	for(int Patch = 0; Patch <= MaxPatch; Patch += 1){
		snprintf(FileName, sizeof(FileName), "%s/%03d.pat", SAVEPATH, Patch);
		if(FileExists(FileName)){
			TReadScriptFile Script;
			Script.open(FileName);
			if(strcmp(Script.readIdentifier(), "sector") != 0){
				Script.error("Sector expected");
			}

			int SectorX = Script.readNumber();
			Script.readSymbol(',');
			int SectorY = Script.readNumber();
			Script.readSymbol(',');
			int SectorZ = Script.readNumber();

			print(2, Translate("Patche Teile von Sektor %d/%d/%d (Patch %d) ...\n",
							   "Patch parts of sector %d/%d/%d (Patch %d) ...\n"),
					SectorX, SectorY, SectorZ, Patch);
			ApplyPatch(SectorX, SectorY, SectorZ, false, &Script, false);
			Script.close();
			unlink(FileName);
		}
	}
}

// Communication Logging
// =============================================================================
uint32 LogCommunication(uint32 CreatureID, int Mode, int Channel, const char *Text){
	static uint32 StatementID = 0;

	if(Text == NULL){
		error(Translate("LogCommunication: Text ist NULL.\n",
						"LogCommunication: Text is NULL.\n"));
		return 0;
	}

	if(CreatureID == 0){
		error(Translate("LogCommunication: CharacterID ist Null.\n",
						"LogCommunication: CharacterID is Null.\n"));
		return 0;
	}

	StatementID += 1;

	TStatement *Statement = Statements.append();
	Statement->StatementID = StatementID;
	Statement->TimeStamp = (uint32)time(NULL);
	Statement->CharacterID = CreatureID;
	Statement->Mode = Mode;
	Statement->Channel = Channel;
	Statement->Text = AddDynamicString(Text);
	Statement->Reported = false;
	return StatementID;
}

uint32 LogListener(uint32 StatementID, TPlayer *Player){
	if(StatementID == 0 || Player == NULL
			|| !CheckRight(Player->ID, LOG_COMMUNICATION)){
		return 0;
	}

	TListener *Listener = Listeners.append();
	Listener->StatementID = StatementID;
	Listener->CharacterID = Player->ID;
	return StatementID;
}

void ProcessCommunicationControl(void){
	// NOTE(fusion): Remove statements older than 30 minutes.
	int Now = (int)time(NULL);
	uint32 Limit = 0;
	while(true){
		TStatement *Statement = Statements.next();
		if(Statement == NULL || Now <= (Statement->TimeStamp + 1800)){
			if(Statement != NULL){
				Limit = Statement->StatementID;
			}
			break;
		}

		DeleteDynamicString(Statement->Text);
		Statements.remove();
	}

	// TODO(fusion): If there are no statements left we'll end up with `Limit`
	// equal to zero which will prevent any listeners entry from being removed.
	while(true){
		TListener *Listener = Listeners.next();
		if(Listener == NULL || Listener->StatementID >= Limit){
			break;
		}

		Listeners.remove();
	}
}

int GetCommunicationContext(uint32 CharacterID, uint32 StatementID,
		int *NumberOfStatements, vector<TReportedStatement> **ReportedStatements){
	TStatement *Statement = NULL;
	int StatementsIter = Statements.iterFirst();
	while(true){
		Statement = Statements.iterNext(&StatementsIter);
		if(Statement == NULL || Statement->StatementID == StatementID){
			break;
		}
	}

	if(Statement == NULL){
		return 1; // STATEMENT_UNKNOWN ?
	}

	if(CharacterID == 0){
		bool ChannelMode = Statement->Mode == TALK_CHANNEL_CALL
				|| Statement->Mode == TALK_GAMEMASTER_CHANNELCALL
				|| Statement->Mode == TALK_HIGHLIGHT_CHANNELCALL;

		bool ReportableChannel = Statement->Channel == CHANNEL_TUTOR
				|| Statement->Channel == CHANNEL_GAMECHAT
				|| Statement->Channel == CHANNEL_TRADE
				|| Statement->Channel == CHANNEL_RLCHAT
				|| Statement->Channel == CHANNEL_HELP;

		if(!ChannelMode || !ReportableChannel){
			error(Translate("GetCommunicationContext: Äußerung dürfte nicht gemeldet werden.\n",
							"GetCommunicationContext: Statement should not be reported.\n"));
			return 1; // STATEMENT_UNKNOWN ?
		}
	}

	if(Statement->Reported){
		return 2; // STATEMENT_ALREADY_REPORTED ?
	}

	// TODO(fusion): `FreeSpace` limits the amount of data that is gathered. It
	// seems to grow/shrink with the size of the text plus an extra 46 bytes for
	// each statement entry. This flat memory overhead of 46 bytes could be some
	// compilation/decompilation artifact so I've left it out for now.
	//	The end point of this data is `ProcessPunishmentOrder` which may record
	// statements into the database so it may be related to that.

	bool StatementContained = false;
	int FreeSpace = KB(16);
	*NumberOfStatements = 0;
	*ReportedStatements = new vector<TReportedStatement>(0, 100, 100);
	StatementsIter = Statements.iterLast();
	while(true){
		TStatement *Current = Statements.iterPrev(&StatementsIter);
		if(Current == NULL){
			break;
		}

		if(Current->TimeStamp < (Statement->TimeStamp - 180)
		|| Current->TimeStamp > (Statement->TimeStamp + 60)){
			continue;
		}

		if(FreeSpace <= 0 && Current->TimeStamp > Statement->TimeStamp){
			error(Translate("GetCommunicationContext: Kontext wird zu groß. Schneide Ende ab.\n",
							"GetCommunicationContext: Context is too large. Cutting end.\n"));
			break;
		}

		if(CharacterID == 0){
			bool ChannelMode = Current->Mode == TALK_CHANNEL_CALL
					|| Current->Mode == TALK_GAMEMASTER_CHANNELCALL
					|| Current->Mode == TALK_HIGHLIGHT_CHANNELCALL;
			if(!ChannelMode || Current->Channel != Statement->Channel){
				continue;
			}
		}else{
			// NOTE(fusion): Check if the character listened to the current statement.
			TListener *Listener = NULL;
			int ListenersIter = Listeners.iterLast();
			while(true){
				Listener = Listeners.iterPrev(&ListenersIter);
				if(Listener == NULL
						|| Listener->StatementID > Current->StatementID
						|| (Listener->StatementID == Current->StatementID
								&& Listener->CharacterID == CharacterID)){
					break;
				}
			}

			if(Listener == NULL || Listener->StatementID != Current->StatementID){
				continue;
			}
		}

		TReportedStatement *Entry = (*ReportedStatements)->at(*NumberOfStatements);
		Entry->StatementID = Current->StatementID;
		Entry->TimeStamp = Current->TimeStamp;
		Entry->CharacterID = Current->CharacterID;
		Entry->Mode = Current->Mode;
		Entry->Channel = Current->Channel;
		if(Current->Text != 0){
			// TODO(fusion): OOF SIZE: Large.
			strcpy(Entry->Text, GetDynamicString(Current->Text));
		}else{
			Entry->Text[0] = 0;
		}
		*NumberOfStatements += 1;
		FreeSpace -= (int)strlen(Entry->Text);

		if(Current->StatementID == StatementID){
			StatementContained = true;
		}
	}

	if(FreeSpace < 0){
		error(Translate("GetCommunicationContext: Kontext wird um %d Bytes zu groß. Lösche Anfang.\n",
						"GetCommunicationContext: Context is %d bytes too large. Deleting beginning.\n"), -FreeSpace);

		for(int i = 0; i < *NumberOfStatements && FreeSpace < 0; i += 1){
			TReportedStatement *Entry = (*ReportedStatements)->at(i);
			if(Entry->StatementID != StatementID){
				Entry->StatementID = 0;
				FreeSpace += (int)strlen(Entry->Text);
			}
		}

		if(FreeSpace < 0){
			error(Translate("GetCommunicationContext: FreeSpace ist immer noch negativ (%d).\n",
							"GetCommunicationContext: FreeSpace is still negative (%d).\n"), FreeSpace);
		}
	}

	if(!StatementContained){
		error(Translate("GetCommunicationContext: Statement ist nicht im Kontext enthalten.\n",
						"GetCommunicationContext: Statement is not included in the context.\n"));
		delete *ReportedStatements;
		*NumberOfStatements = 0;
		*ReportedStatements = NULL;
		return 1; // STATEMENT_UNKNOWN ?
	}

	Statement->Reported = true;
	return 0; // STATEMENT_REPORTED ?
}

// Channel
// =============================================================================
TChannel::TChannel(void) : Subscriber(0, 10, 10), InvitedPlayer(0, 10, 10) {
	this->Moderator = 0;
	this->ModeratorName[0] = 0;
	this->Subscribers = 0;
	this->InvitedPlayers = 0;
}

TChannel::TChannel(const TChannel &Other) : TChannel() {
	this->operator=(Other);
}

void TChannel::operator=(const TChannel &Other){
	this->Moderator = Other.Moderator;
	memcpy(this->ModeratorName, Other.ModeratorName, sizeof(this->ModeratorName));

	this->Subscribers = Other.Subscribers;
	for(int i = 0; i < Other.Subscribers; i += 1){
		*this->Subscriber.at(i) = Other.Subscriber.copyAt(i);
	}

	this->InvitedPlayers = Other.InvitedPlayers;
	for(int i = 0; i < Other.InvitedPlayers; i += 1){
		*this->InvitedPlayer.at(i) = Other.InvitedPlayer.copyAt(i);
	}
}

int GetNumberOfChannels(void){
    return Channels;
}

bool ChannelActive(int ChannelID){
	if(ChannelID < 0 || ChannelID >= Channels){
		error(Translate("ChannelActive: Ungültige Kanalnummer %d.\n",
						"ChannelActive: Invalid channel number %d.\n"), ChannelID);
		return false;
	}

	return ChannelID < CHANNEL_PRIVATE || Channel.at(ChannelID)->Moderator != 0;
}

bool ChannelAvailable(int ChannelID, uint32 CharacterID){
	if(!ChannelActive(ChannelID)){
		return false;
	}

	TPlayer *Player = GetPlayer(CharacterID);
	if(Player == NULL){
		error(Translate("ChannelAvailable: Spieler existiert nicht.\n",
						"ChannelAvailable: Player does not exist.\n"));
		return false;
	}

	switch(ChannelID){
		case CHANNEL_GUILD:          return Player->Guild[0] != 0;
		case CHANNEL_GAMEMASTER:     return CheckRight(Player->ID, READ_GAMEMASTER_CHANNEL);
		case CHANNEL_TUTOR:          return CheckRight(Player->ID, READ_TUTOR_CHANNEL);
		case CHANNEL_RULEVIOLATIONS: return CheckRight(Player->ID, READ_GAMEMASTER_CHANNEL);
		case CHANNEL_GAMECHAT:       return true;
		case CHANNEL_TRADE:          return true;
		case CHANNEL_RLCHAT:         return true;
		case CHANNEL_HELP:           return true;
		default:{
			if(ChannelID >= CHANNEL_PRIVATE){
				TChannel *Chan = Channel.at(ChannelID);
				if(Chan->Moderator == CharacterID){
					return true;
				}

				for(int i = 0; i < Chan->InvitedPlayers; i += 1){
					if(*Chan->InvitedPlayer.at(i) == CharacterID){
						return true;
					}
				}
			}
			return false;
		}
	}
}

const char *GetChannelName(int ChannelID, uint32 CharacterID){
	static char ChannelName[40];

	if(!ChannelActive(ChannelID)){
		return "Unknown";
	}

	TPlayer *Player = GetPlayer(CharacterID);
	if(Player == NULL){
		error(Translate("GetChannelName: Spieler existiert nicht.\n",
						"GetChannelName: Player does not exist.\n"));
		return "Unknown";
	}

	switch(ChannelID){
		case CHANNEL_GUILD:          return Player->Guild;
		case CHANNEL_GAMEMASTER:     return "Gamemaster";
		case CHANNEL_TUTOR:          return "Tutor";
		case CHANNEL_RULEVIOLATIONS: return "Rule Violations";
		case CHANNEL_GAMECHAT:       return "Game-Chat";
		case CHANNEL_TRADE:          return "Trade";
		case CHANNEL_RLCHAT:         return "RL-Chat";
		case CHANNEL_HELP:           return "Help";
		default:{
			if(ChannelID >= CHANNEL_PRIVATE){
				snprintf(ChannelName, sizeof(ChannelName), "%s's Channel",
						Channel.at(ChannelID)->ModeratorName);
				return ChannelName;
			}else{
				error(Translate("GetChannelName: Unbenutzter Kanal %d.\n",
								"GetChannelName: Unused channel %d.\n"), ChannelID);
				return "Unknown";
			}
		}
	}
}

bool ChannelSubscribed(int ChannelID, uint32 CharacterID){
	if(!ChannelActive(ChannelID)){
		return false;
	}

	TChannel *Chan = Channel.at(ChannelID);
	for(int i = 0; i < Chan->Subscribers; i += 1){
		if(*Chan->Subscriber.at(i) == CharacterID){
			return true;
		}
	}

	return false;
}

// TODO(fusion): This could have been a simple for loop?
uint32 GetFirstSubscriber(int ChannelID){
	CurrentChannelID = ChannelID;
	CurrentSubscriberNumber = 0;
	return GetNextSubscriber();
}

uint32 GetNextSubscriber(void){
	if(!ChannelActive(CurrentChannelID)){
		return 0;
	}

	uint32 Subscriber = 0;
	TChannel *Chan = Channel.at(CurrentChannelID);
	if(CurrentSubscriberNumber < Chan->Subscribers){
		Subscriber = *Chan->Subscriber.at(CurrentSubscriberNumber);
		CurrentSubscriberNumber += 1;
	}

	return Subscriber;
}

bool MayOpenChannel(uint32 CharacterID){
	if(Channels >= MAX_CHANNELS){
		return false;
	}

	if(!CheckRight(CharacterID, PREMIUM_ACCOUNT)){
		return false;
	}

	// NOTE(fusion): Check if character is already moderator of any non-public channel.
	for(int ChannelID = CHANNEL_PRIVATE; ChannelID < Channels; ChannelID += 1){
		if(Channel.at(ChannelID)->Moderator == CharacterID){
			return false;
		}
	}

	return true;
}

void OpenChannel(uint32 CharacterID){
	TPlayer *Player = GetPlayer(CharacterID);
	if(Player == NULL){
		error(Translate("OpenChannel: Kreatur %u existiert nicht.\n",
						"OpenChannel: Creature %u does not exist.\n"), CharacterID);
		throw ERROR;
	}

	// TODO(fusion): Shouldn't we scan for a free channel before returning here?
	if(Channels >= MAX_CHANNELS){
		error(Translate("OpenChannel: Zu viele Kanäle.\n",
						"OpenChannel: Too many channels.\n"));
		throw ERROR;
	}

	if(!CheckRight(CharacterID, PREMIUM_ACCOUNT)){
		throw NOPREMIUMACCOUNT;
	}

	// NOTE(fusion): Check if character already has an open channel.
	for(int ChannelID = CHANNEL_PRIVATE; ChannelID < Channels; ChannelID += 1){
		if(Channel.at(ChannelID)->Moderator == CharacterID){
			SendOpenOwnChannel(Player->Connection, ChannelID);
			return;
		}
	}

	// NOTE(fusion): Assign free channel to character.
	int ChannelID = CHANNEL_PRIVATE;
	while(ChannelID < Channels){
		if(Channel.at(ChannelID)->Moderator == 0){
			break;
		}
		ChannelID += 1;
	}

	if(ChannelID == Channels){
		Channels += 1;
	}

	TChannel *Chan = Channel.at(ChannelID);
	Chan->Moderator = CharacterID;
	strcpy(Chan->ModeratorName, Player->Name);
	*Chan->Subscriber.at(0) = CharacterID;
	Chan->Subscribers = 1;
	Chan->InvitedPlayers = 0;
	SendOpenOwnChannel(Player->Connection, ChannelID);
}

void CloseChannel(int ChannelID){
	if(ChannelID < CHANNEL_PRIVATE || ChannelID >= Channels){
		error(Translate("CloseChannel: Ungültige ChannelID %d.\n",
						"CloseChannel: Invalid ChannelID %d.\n"), ChannelID);
		return;
	}

	TChannel *Chan = Channel.at(ChannelID);
	if(Chan->Moderator == 0){
		error(Translate("CloseChannel: Kanal ist schon geschlossen.\n",
						"CloseChannel: Channel is already closed.\n"));
		return;
	}

	Chan->Moderator = 0;
}

void InviteToChannel(uint32 CharacterID, const char *Name){
	TPlayer *Player = GetPlayer(CharacterID);
	if(Player == NULL){
		error(Translate("InviteToChannel: Kreatur %u existiert nicht.\n",
						"InviteToChannel: Creature %u does not exist.\n"), CharacterID);
		return;
	}

	int ChannelID = CHANNEL_PRIVATE;
	while(ChannelID < Channels){
		if(Channel.at(ChannelID)->Moderator == CharacterID){
			break;
		}
		ChannelID += 1;
	}

	if(ChannelID >= Channels){
		return;
	}

	TPlayer *Other = NULL;
	uint32 OtherID = 0;
	bool IgnoreGamemasters = !CheckRight(Player->ID, READ_GAMEMASTER_CHANNEL);
	switch(IdentifyPlayer(Name, false, IgnoreGamemasters, &Other)){
		case 0:{ // PLAYERFOUND ?
			OtherID = Other->ID;
			Name = Other->Name;
			break;
		}

		case -1:{ // PLAYERNOTONLINE ?
			OtherID = GetCharacterID(Name);
			if(OtherID == 0){
				throw PLAYERNOTEXISTING;
			}

			Name = GetCharacterName(Name);
			break;
		}

		case -2:{ // NAMEAMBIGUOUS ?
			OtherID = GetCharacterID(Name);
			if(OtherID == 0){
				throw NAMEAMBIGUOUS;
			}

			Name = GetCharacterName(Name);
			break;
		}

		default:{
			// TODO(fusion): This wasn't here but I don't think `IdentifyPlayer`
			// can return anything else than 0, -1, and -2 anyways.
			error(Translate("InviteToChannel: Ungültiger Rückgabewert von IdentifyPlayer.\n",
							"InviteToChannel: Invalid return value from IdentifyPlayer.\n"));
			throw ERROR;
		}
	}

	print(3, Translate("Lade Spieler %s (%u) in Private Channel ein.\n",
					   "Invite player %s (%u) to private channel.\n"), Name, OtherID);
	if(CharacterID != OtherID){
		TChannel *Chan = Channel.at(ChannelID);
		for(int i = 0; i < Chan->InvitedPlayers; i += 1){
			if(*Chan->InvitedPlayer.at(i) == OtherID){
				SendMessage(Player->Connection, TALK_INFO_MESSAGE,
						"%s has already been invited.", Name);
				return;
			}
		}

		*Chan->InvitedPlayer.at(Chan->InvitedPlayers) = OtherID;
		Chan->InvitedPlayers += 1;

		SendMessage(Player->Connection, TALK_INFO_MESSAGE,
				"%s has been invited.", Name);

		if(Other != NULL){
			SendMessage(Other->Connection, TALK_INFO_MESSAGE,
					"%s invites you to %s private chat channel.",
					Player->Name, (Player->Sex == 1 ? "his" : "her"));
		}
	}
}

void ExcludeFromChannel(uint32 CharacterID, const char *Name){
	TPlayer *Player = GetPlayer(CharacterID);
	if(Player == NULL){
		error(Translate("ExcludeFromChannel: Kreatur %u existiert nicht.\n",
						"ExcludeFromChannel: Creature %u does not exist.\n"), CharacterID);
		return;
	}

	int ChannelID = CHANNEL_PRIVATE;
	while(ChannelID < Channels){
		if(Channel.at(ChannelID)->Moderator == CharacterID){
			break;
		}
		ChannelID += 1;
	}

	if(ChannelID >= Channels){
		return;
	}

	// TODO(fusion): Why don't we call `GetCharacterName` as we do in
	// `InviteToChannel`, when the player is not found?
	TPlayer *Other = NULL;
	uint32 OtherID = 0;
	bool IgnoreGamemasters = !CheckRight(Player->ID, READ_GAMEMASTER_CHANNEL);
	switch(IdentifyPlayer(Name, false, IgnoreGamemasters, &Other)){
		case 0:{ // PLAYERFOUND ?
			OtherID = Other->ID;
			Name = Other->Name;
			break;
		}

		case -1:{ // PLAYERNOTONLINE ?
			OtherID = GetCharacterID(Name);
			if(OtherID == 0){
				throw PLAYERNOTEXISTING;
			}
			break;
		}

		case -2:{ // NAMEAMBIGUOUS ?
			OtherID = GetCharacterID(Name);
			if(OtherID == 0){
				throw NAMEAMBIGUOUS;
			}
			break;
		}

		default:{
			// TODO(fusion): Same as `InviteToChannel`.
			error(Translate("ExcludeFromChannel: Ungültiger Rückgabewert von IdentifyPlayer.\n",
							"ExcludeFromChannel: Invalid return value from IdentifyPlayer.\n"));
			throw ERROR;
		}
	}

	print(3, Translate("Schließe Spieler %s (%u) aus Private Channel aus.\n",
						"Exclude player %s (%u) from private channel.\n"), Name, OtherID);
	if(CharacterID != OtherID){
		bool Removed = false;
		TChannel *Chan = Channel.at(ChannelID);
		for(int i = 0; i < Chan->InvitedPlayers; i += 1){
			if(*Chan->InvitedPlayer.at(i) == OtherID){
				// NOTE(fusion): A little swap and pop action.
				Chan->InvitedPlayers -= 1;
				*Chan->InvitedPlayer.at(i) = *Chan->InvitedPlayer.at(Chan->InvitedPlayers);
				Removed = true;
				break;
			}
		}

		if(!Removed){
			SendMessage(Player->Connection, TALK_INFO_MESSAGE,
					"%s has not been invited.", Name);
		}else{
			SendMessage(Player->Connection, TALK_INFO_MESSAGE,
					"%s has been excluded.", Name);
			if(ChannelSubscribed(ChannelID, OtherID)){
				LeaveChannel(ChannelID, OtherID, false);

				// TODO(fusion): We didn't check if `Other` was NULL here. It
				// seems that the player destructor will call `LeaveAllChannels`
				// to make sure a player is only subscribed if it is still valid.
				//	Nevertheless, this check should become invisible to the branch
				// predictor and it's always best to be safe.
				if(Other != NULL){
					SendCloseChannel(Other->Connection, ChannelID);
				}
			}
		}
	}
}

bool JoinChannel(int ChannelID, uint32 CharacterID){
	if(!ChannelActive(ChannelID)){
		throw ERROR;
	}

	if(!ChannelAvailable(ChannelID, CharacterID)){
		throw NOTACCESSIBLE;
	}

	TChannel *Chan = Channel.at(ChannelID);
	if(!ChannelSubscribed(ChannelID, CharacterID)){
		*Chan->Subscriber.at(Chan->Subscribers) = CharacterID;
		Chan->Subscribers += 1;
	}

	return CharacterID == Chan->Moderator;
}

void LeaveChannel(int ChannelID, uint32 CharacterID, bool Close){
	if(!ChannelActive(ChannelID)){
		return;
	}

	TChannel *Chan = Channel.at(ChannelID);
	for(int i = 0; i < Chan->Subscribers; i += 1){
		if(*Chan->Subscriber.at(i) == CharacterID){
			// NOTE(fusion): A little swap and pop action.
			Chan->Subscribers -= 1;
			*Chan->Subscriber.at(i) = *Chan->Subscriber.at(Chan->Subscribers);
			break;
		}
	}

	if(Close && CharacterID == Chan->Moderator){
		for(int i = 0; i < Chan->Subscribers; i += 1){
			uint32 SubscriberID = *Chan->Subscriber.at(i);
			TPlayer *Subscriber = GetPlayer(SubscriberID);
			if(Subscriber != NULL){
				SendCloseChannel(Subscriber->Connection, ChannelID);
			}
		}
		Chan->Subscribers = 0;
	}

	if(ChannelID >= CHANNEL_PRIVATE && Chan->Subscribers == 0){
		CloseChannel(ChannelID);
	}
}

void LeaveAllChannels(uint32 CharacterID){
	for(int ChannelID = 0; ChannelID < Channels; ChannelID += 1){
		if(ChannelActive(ChannelID)){
			LeaveChannel(ChannelID, CharacterID, false);
		}
	}
}

// Party
// =============================================================================
TParty::TParty(void) : Member(0, 10, 10), InvitedPlayer(0, 10, 10) {
	this->Leader = 0;
	this->Members = 0;
	this->InvitedPlayers = 0;
}

TParty::TParty(const TParty &Other) : TParty() {
	this->operator=(Other);
}

void TParty::operator=(const TParty &Other){
	this->Leader = Other.Leader;

	this->Members = Other.Members;
	for(int i = 0; i < Other.Members; i += 1){
		*this->Member.at(i) = Other.Member.copyAt(i);
	}

	this->InvitedPlayers = Other.InvitedPlayers;
	for(int i = 0; i < Other.InvitedPlayers; i += 1){
		*this->InvitedPlayer.at(i) = Other.InvitedPlayer.copyAt(i);
	}
}

TParty *GetParty(uint32 LeaderID){
	TParty *Result = NULL;
	for(int i = 0; i < Parties; i += 1){
		if(Party.at(i)->Leader == LeaderID){
			Result = Party.at(i);
			break;
		}
	}
	return Result;
}

bool IsInvitedToParty(uint32 GuestID, uint32 HostID){
	bool Result = false;
	TParty *P = GetParty(HostID);
	if(P != NULL){
		for(int i = 0; i < P->InvitedPlayers; i += 1){
			if(*P->InvitedPlayer.at(i) == GuestID){
				Result = true;
				break;
			}
		}
	}
	return Result;
}

void DisbandParty(uint32 LeaderID){
	TParty *P = GetParty(LeaderID);
	if(P == NULL){
		error(Translate("DisbandParty: Party von Anführer %u nicht gefunden.\n",
						"DisbandParty: Party of leader %und not found.\n"), LeaderID);
		return;
	}

	// NOTE(fusion): We need to iterate over members twice because `SendCreatureParty`
	// and `SendCreatureSkull` use party information, which is annoying.

	for(int i = 0; i < P->Members; i += 1){
		uint32 MemberID = *P->Member.at(i);
		TPlayer *Member = GetPlayer(MemberID);
		if(Member != NULL){
			Member->LeaveParty();
		}
	}

	P->Leader = 0;

	for(int i = 0; i < P->Members; i += 1){
		uint32 MemberID = *P->Member.at(i);
		TPlayer *Member = GetPlayer(MemberID);
		if(Member != NULL && Member->Connection != NULL){
			SendMessage(Member->Connection, TALK_INFO_MESSAGE,
					"Your party has been disbanded.");
			for(int j = 0; j < P->Members; j += 1){
				uint32 OtherID = *P->Member.at(j);
				SendCreatureParty(Member->Connection, OtherID);
				SendCreatureSkull(Member->Connection, OtherID);
			}
		}
	}

	TPlayer *Leader = GetPlayer(LeaderID);
	if(Leader != NULL){
		for(int i = 0; i < P->InvitedPlayers; i += 1){
			uint32 GuestID = *P->InvitedPlayer.at(i);
			TPlayer *Guest = GetPlayer(GuestID);
			if(Guest != NULL){
				SendCreatureParty(Guest->Connection, LeaderID);
				SendCreatureParty(Leader->Connection, GuestID);
			}
		}
	}
}

void InviteToParty(uint32 HostID, uint32 GuestID){
	if(HostID == GuestID){
		return;
	}

	TPlayer *Host = GetPlayer(HostID);
	if(Host == NULL){
		error(Translate("InviteToParty: Einladender Spieler %u existiert nicht.\n",
						"InviteToParty: Inviting player %u does not exist.\n"), HostID);
		return;
	}

	TPlayer *Guest = GetPlayer(GuestID);
	if(Guest == NULL){
		SendResult(Host->Connection, PLAYERNOTONLINE);
		return;
	}

	if(Host->GetPartyLeader(true) == 0){
		if(Guest->GetPartyLeader(true) != 0){
			SendMessage(Host->Connection, TALK_INFO_MESSAGE,
					"%s is already member of a party.", Guest->Name);
			return;
		}

		TParty *P = GetParty(0);
		if(P == NULL){
			P = Party.at(Parties);
			Parties += 1;
		}

		P->Leader = HostID;
		*P->Member.at(0) = HostID;
		P->Members = 1;
		*P->InvitedPlayer.at(0) = GuestID;
		P->InvitedPlayers = 1;

		Host->JoinParty(HostID);
		SendMessage(Host->Connection, TALK_INFO_MESSAGE,
				"%s has been invited.", Guest->Name);
		SendMessage(Guest->Connection, TALK_INFO_MESSAGE,
				"%s invites you to %s party.", Host->Name,
				(Host->Sex == 1 ? "his" : "her"));
		SendCreatureParty(Host->Connection, HostID);
		SendCreatureSkull(Host->Connection, HostID);
		SendCreatureParty(Host->Connection, GuestID);
		SendCreatureParty(Guest->Connection, HostID);
	}else if(Host->GetPartyLeader(false) == HostID){
		if(Guest->GetPartyLeader(true) != 0){
			SendMessage(Host->Connection, TALK_INFO_MESSAGE,
					"%s is already member of %s party.", Guest->Name,
					(Guest->GetPartyLeader(true) == HostID ? "your" : "a"));
			return;
		}

		TParty *P = GetParty(HostID);
		if(P == NULL){
			error(Translate("InviteToParty: Party von Anführer %u nicht gefunden.\n",
							"InviteToParty: Party Leader %u not found.\n"), HostID);
			return;
		}

		for(int i = 0; i < P->InvitedPlayers; i += 1){
			if(*P->InvitedPlayer.at(i) == GuestID){
				SendMessage(Host->Connection, TALK_INFO_MESSAGE,
						"%s has already been invited.", Guest->Name);
				return;
			}
		}

		*P->InvitedPlayer.at(P->InvitedPlayers) = GuestID;
		P->InvitedPlayers += 1;

		SendMessage(Host->Connection, TALK_INFO_MESSAGE,
				"%s has been invited.", Guest->Name);
		SendMessage(Guest->Connection, TALK_INFO_MESSAGE,
				"%s invites you to %s party.", Host->Name,
				(Host->Sex == 1 ? "his" : "her"));
		SendCreatureParty(Host->Connection, GuestID);
		SendCreatureParty(Guest->Connection, HostID);
	}else{
		SendMessage(Host->Connection, TALK_INFO_MESSAGE,
				"You may not invite players.");
	}
}

void RevokeInvitation(uint32 HostID, uint32 GuestID){
	TPlayer *Host = GetPlayer(HostID);
	if(Host == NULL){
		error(Translate("RevokeInvitation: Einladender Spieler %u existiert nicht.\n",
						"RevokeInvitation: Inviting player %u does not exist.\n"), HostID);
		return;
	}

	if(Host->GetPartyLeader(false) != HostID){
		SendMessage(Host->Connection, TALK_INFO_MESSAGE,
				"You may not invite players.");
		return;
	}

	TParty *P = GetParty(HostID);
	if(P == NULL){
		error(Translate("RevokeInvitation: Party von Anführer %u nicht gefunden.\n",
						"RevokeInvitation: Party Leader %u not found.\n"), HostID);
		return;
	}

	int InviteIndex = 0;
	while(InviteIndex < P->InvitedPlayers){
		if(*P->InvitedPlayer.at(InviteIndex) == GuestID){
			break;
		}
		InviteIndex += 1;
	}

	TPlayer *Guest = GetPlayer(GuestID);
	if(InviteIndex >= P->InvitedPlayers){
		if(Guest != NULL){
			SendMessage(Host->Connection, TALK_INFO_MESSAGE,
					"%s has not been invited.", Guest->Name);
		}else{
			SendMessage(Host->Connection, TALK_INFO_MESSAGE,
					"This player has not been invited.");
		}
		return;
	}

	// TODO(fusion): This ordered removal isn't relevant at all. It could be a
	// swap and pop just like in `JoinParty`, unlike when removing members.
	P->InvitedPlayers -= 1;
	for(int i = InviteIndex; i < P->InvitedPlayers; i += 1){
		*P->InvitedPlayer.at(i) = *P->InvitedPlayer.at(i + 1);
	}

	if(Guest != NULL){
		SendMessage(Host->Connection, TALK_INFO_MESSAGE,
				"Invitation for %s has been revoked.", Guest->Name);
		SendMessage(Guest->Connection, TALK_INFO_MESSAGE,
				"%s has revoked %s invitation.", Host->Name,
				(Host->Sex == 1 ? "his" : "her"));
		SendCreatureParty(Host->Connection, GuestID);
		SendCreatureParty(Guest->Connection, HostID);
	}else{
		SendMessage(Host->Connection, TALK_INFO_MESSAGE,
				"Invitation has been revoked.");
	}

	if(P->Members == 1 && P->InvitedPlayers == 0){
		DisbandParty(HostID);
	}
}

void JoinParty(uint32 GuestID, uint32 HostID){
	if(GuestID == HostID){
		return;
	}

	TPlayer *Guest = GetPlayer(GuestID);
	if(Guest == NULL){
		error(Translate("JoinParty: Eingeladener Spieler %u existiert nicht.\n",
						"JoinParty: Invited player %u does not exist.\n"), GuestID);
		return;
	}

	TPlayer *Host = GetPlayer(HostID);
	if(Host == NULL){
		SendResult(Guest->Connection, PLAYERNOTONLINE);
		return;
	}

	if(Guest->GetPartyLeader(true) != 0){
		SendMessage(Guest->Connection, TALK_INFO_MESSAGE,
				"You are already member of %s party.",
				(Guest->InPartyWith(Host, true) ? "this" : "a"));
		return;
	}

	TParty *P = GetParty(HostID);
	if(P == NULL){
		SendMessage(Guest->Connection, TALK_INFO_MESSAGE,
				"%s has not invited you.", Host->Name);
		return;
	}

	int InviteIndex = 0;
	while(InviteIndex < P->InvitedPlayers){
		if(*P->InvitedPlayer.at(InviteIndex) == GuestID){
			break;
		}
		InviteIndex += 1;
	}

	if(InviteIndex >= P->InvitedPlayers){
		SendMessage(Guest->Connection, TALK_INFO_MESSAGE,
				"%s has not invited you.", Host->Name);
		return;
	}

	// NOTE(fusion): A little swap and pop action.
	P->InvitedPlayers -= 1;
	*P->InvitedPlayer.at(InviteIndex) = *P->InvitedPlayer.at(P->InvitedPlayers);

	*P->Member.at(P->Members) = GuestID;
	P->Members += 1;

	Guest->JoinParty(HostID);
	SendMessage(Guest->Connection, TALK_INFO_MESSAGE,
			"You have joined %s's party.", Host->Name);
	for(int i = 0; i < P->Members; i += 1){
		uint32 MemberID = *P->Member.at(i);
		SendCreatureParty(Guest->Connection, MemberID);
		SendCreatureSkull(Guest->Connection, MemberID);
		if(MemberID != GuestID){
			TPlayer *Member = GetPlayer(MemberID);
			if(Member != NULL){
				SendMessage(Member->Connection, TALK_INFO_MESSAGE,
						"%s has joined the party.", Guest->Name);
				SendCreatureParty(Member->Connection, GuestID);
				SendCreatureSkull(Member->Connection, GuestID);
			}
		}
	}
}

void PassLeadership(uint32 OldLeaderID, uint32 NewLeaderID){
	if(OldLeaderID == NewLeaderID){
		return;
	}

	TPlayer *OldLeader = GetPlayer(OldLeaderID);
	if(OldLeader == NULL){
		error(Translate("PassLeadership: Alter Anführer %u existiert nicht.\n",
						"PassLeadership: Old Leader %u does not exist.\n"), OldLeaderID);
		return;
	}

	TPlayer *NewLeader = GetPlayer(NewLeaderID);
	if(NewLeader == NULL){
		SendResult(OldLeader->Connection, PLAYERNOTONLINE);
		return;
	}

	if(OldLeader->GetPartyLeader(false) != OldLeaderID){
		SendMessage(OldLeader->Connection, TALK_INFO_MESSAGE,
				"You are not leader of a party.");
		return;
	}

	if(NewLeader->GetPartyLeader(false) != OldLeaderID){
		SendMessage(OldLeader->Connection, TALK_INFO_MESSAGE,
				"%s is not member of your party.", NewLeader->Name);
		return;
	}

	TParty *P = GetParty(OldLeaderID);
	if(P == NULL){
		error(Translate("PassLeadership: Party von Anführer %u nicht gefunden.\n",
						"PassLeadership: Party Leader %u not found.\n"), OldLeaderID);
		return;
	}

	P->Leader = NewLeaderID;

	// NOTE(fusion): Same as `DisbandParty`.
	for(int i = 0; i < P->Members; i += 1){
		uint32 MemberID = *P->Member.at(i);
		TPlayer *Member = GetPlayer(MemberID);
		if(Member != NULL){
			Member->JoinParty(NewLeaderID);
		}
	}

	for(int i = 0; i < P->Members; i += 1){
		uint32 MemberID = *P->Member.at(i);
		TPlayer *Member = GetPlayer(MemberID);
		if(Member != NULL){
			if(MemberID == NewLeaderID){
				SendMessage(Member->Connection, TALK_INFO_MESSAGE,
						"You are now leader of your party.");
			}else{
				SendMessage(Member->Connection, TALK_INFO_MESSAGE,
						"%s is now leader of your party.", NewLeader->Name);
			}
			SendCreatureParty(Member->Connection, OldLeaderID);
			SendCreatureParty(Member->Connection, NewLeaderID);
		}
	}

	// NOTE(fusion): The list of invited players is cleared here.
	int InvitedPlayers = P->InvitedPlayers;
	P->InvitedPlayers = 0;

	for(int i = 0; i < InvitedPlayers; i += 1){
		uint32 GuestID = *P->InvitedPlayer.at(i);
		TPlayer *Guest = GetPlayer(GuestID);
		if(Guest != NULL){
			SendCreatureParty(Guest->Connection, OldLeaderID);
			SendCreatureParty(OldLeader->Connection, GuestID);
		}
	}
}

void LeaveParty(uint32 MemberID, bool Forced){
	TPlayer *Member = GetPlayer(MemberID);
	if(Member == NULL){
		error(Translate("LeaveParty: Mitglied existiert nicht.\n",
						"LeaveParty: Member does not exist.\n"));
		return;
	}

	uint32 LeaderID = Member->GetPartyLeader(false);
	if(LeaderID == 0){
		error(Translate("LeaveParty: Spieler ist kein Mitglied einer Jagdgruppe.\n",
						"LeaveParty: Player is not a member of a hunting group.\n"));
		return;
	}

	if(!Forced && Member->EarliestLogoutRound > RoundNr){
		SendMessage(Member->Connection, TALK_INFO_MESSAGE,
				"You may not leave your party during or immediately after a fight!");
		return;
	}

	TParty *P = GetParty(LeaderID);
	if(P == NULL){
		error(Translate("LeaveParty: Party von Anführer %u nicht gefunden.\n",
						"LeaveParty: Party Leader %u not found.\n"), LeaderID);
		return;
	}

	if(P->Members == 1 || (P->Members == 2 && P->InvitedPlayers == 0)){
		DisbandParty(LeaderID);
		return;
	}

	if(LeaderID == MemberID){
		LeaderID = *P->Member.at(0);
		if(LeaderID == MemberID){
			LeaderID = *P->Member.at(1);
		}
		PassLeadership(MemberID, LeaderID);
	}

	int MemberIndex = 0;
	while(MemberIndex < P->Members){
		if(*P->Member.at(MemberIndex) == MemberID){
			break;
		}
		MemberIndex += 1;
	}

	if(MemberIndex >= P->Members){
		error(Translate("LeaveParty: Mitglied nicht gefunden.\n",
						"LeaveParty: Member not found.\n"));
		return;
	}

	// NOTE(fusion): This ordered removal is important because we want the oldest
	// members to get leadership when the leader leaves.
	P->Members -= 1;
	for(int i = MemberIndex; i < P->Members; i += 1){
		*P->Member.at(i) = *P->Member.at(i + 1);
	}

	Member->LeaveParty();
	if(!Forced){
		SendMessage(Member->Connection, TALK_INFO_MESSAGE,
				"You have left the party.");
	}

	SendCreatureParty(Member->Connection, MemberID);
	SendCreatureSkull(Member->Connection, MemberID);

	for(int i = 0; i < P->Members; i += 1){
		uint32 OtherID = *P->Member.at(i);
		SendCreatureParty(Member->Connection, OtherID);
		SendCreatureSkull(Member->Connection, OtherID);

		TPlayer *Other = GetPlayer(OtherID);
		if(Other != NULL){
			SendMessage(Other->Connection, TALK_INFO_MESSAGE,
					"%s has left the party.", Member->Name);
			SendCreatureParty(Other->Connection, MemberID);
			SendCreatureSkull(Other->Connection, MemberID);
		}
	}
}
