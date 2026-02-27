#include "moveuse.hh"
#include "config.hh"
#include "houses.hh"
#include "info.hh"
#include "magic.hh"
#include "operate.hh"
#include "reader.hh"
#include "writer.hh"

static vector<TMoveUseCondition> MoveUseConditions(1, 1000, 1000);
static int NumberOfMoveUseConditions;

static vector<TMoveUseAction> MoveUseActions(1, 1000, 1000);
static int NumberOfMoveUseActions;

static TMoveUseDatabase MoveUseDatabases[5];

static vector<TDelayedMail> DelayedMail(0, 10, 10);
static int DelayedMails;

// Coordinate Packing
// =============================================================================
int PackAbsoluteCoordinate(int x, int y, int z){
	// DOMAIN: [24576, 40959] x [24576, 40959] x [0, 15]
	int Packed = (((x - 24576) & 0x00003FFF) << 18)
				| (((y - 24576) & 0x00003FFF) << 4)
				| (z & 0x0000000F);
	return Packed;
}

void UnpackAbsoluteCoordinate(int Packed, int *x, int *y, int *z){
	*x = ((Packed >> 18) & 0x00003FFF) + 24576;
	*y = ((Packed >>  4) & 0x00003FFF) + 24576;
	*z = ((Packed >>  0) & 0x0000000F);
}

int PackRelativeCoordinate(int x, int y, int z){
	// DOMAIN: [-8192, 8191] x [-8192, 8191] x [-8, 7]
	int Packed = (((x + 8192) & 0x00003FFF) << 18)
				| (((y + 8192) & 0x00003FFF) << 4)
				| ((z + 8) & 0x0000000F);
	return Packed;
}

void UnpackRelativeCoordinate(int Packed, int *x, int *y, int *z){
	*x = ((Packed >> 18) & 0x00003FFF) - 8192;
	*y = ((Packed >>  4) & 0x00003FFF) - 8192;
	*z = ((Packed >>  0) & 0x0000000F) - 8;
}

// Event Execution
// =============================================================================
Object GetEventObject(int Nr, Object User, Object Obj1, Object Obj2, Object Temp){
	Object Obj = NONE;
	switch(Nr){
		case 0: Obj = NONE; break;
		case 1: Obj = Obj1; break;
		case 2: Obj = Obj2; break;
		case 3: Obj = User; break;
		case 4: Obj = Temp; break;
		default:{
			error("GetEventObject: Unbekannte Nummer %d.\n", Nr);
			break;
		}
	}
	return Obj;
}

bool Compare(int Value1, int Operator, int Value2){
	bool Result = false;
	switch(Operator){
		case '<': Result = (Value1 <  Value2); break;
		case '>': Result = (Value1 >  Value2); break;
		case '=': Result = (Value1 == Value2); break;
		case 'N': Result = (Value1 != Value2); break;
		case 'L': Result = (Value1 <= Value2); break;
		case 'G': Result = (Value1 >= Value2); break;
		default:{
			error("Compare: Ungültiger Operator %d.\n", Operator);
			break;
		}
	}
	return Result;
}

bool CheckCondition(MoveUseEventType EventType, TMoveUseCondition *Condition,
		Object User, Object Obj1, Object Obj2, Object *Temp){
	bool Result = false;
	switch(Condition->Condition){
		case MOVEUSE_CONDITION_ISPOSITION:{
			int ObjX, ObjY, ObjZ, CoordX, CoordY, CoordZ;
			Object Obj = GetEventObject(Condition->Parameters[0], User, Obj1, Obj2, *Temp);
			UnpackAbsoluteCoordinate(Condition->Parameters[1], &CoordX, &CoordY, &CoordZ);
			GetObjectCoordinates(Obj, &ObjX, &ObjY, &ObjZ);
			Result = (ObjX == CoordX && ObjY == CoordY && ObjZ == CoordZ);
			break;
		}

		case MOVEUSE_CONDITION_ISTYPE:{
			Object Obj = GetEventObject(Condition->Parameters[0], User, Obj1, Obj2, *Temp);
			Result = (Obj.getObjectType().TypeID == Condition->Parameters[1]);
			break;
		}

		case MOVEUSE_CONDITION_ISCREATURE:{
			Object Obj = GetEventObject(Condition->Parameters[0], User, Obj1, Obj2, *Temp);
			Result = Obj.getObjectType().isCreatureContainer();
			break;
		}

		case MOVEUSE_CONDITION_ISPLAYER:{
			Object Obj = GetEventObject(Condition->Parameters[0], User, Obj1, Obj2, *Temp);
			if(Obj.getObjectType().isCreatureContainer()){
				TCreature *Creature = GetCreature(Obj);
				Result = (Creature && Creature->Type == PLAYER);
			}
			break;
		}

		case MOVEUSE_CONDITION_HASFLAG:{
			Object Obj = GetEventObject(Condition->Parameters[0], User, Obj1, Obj2, *Temp);
			Result = Obj.getObjectType().getFlag((FLAG)Condition->Parameters[1]);
			break;
		}

		case MOVEUSE_CONDITION_HASTYPEATTRIBUTE:{
			Object Obj = GetEventObject(Condition->Parameters[0], User, Obj1, Obj2, *Temp);
			int Attribute = (int)Obj.getObjectType().getAttribute((TYPEATTRIBUTE)Condition->Parameters[1]);
			int Operator = Condition->Parameters[2];
			int Value = Condition->Parameters[3];
			Result = Compare(Attribute, Operator, Value);
			break;
		}

		case MOVEUSE_CONDITION_HASINSTANCEATTRIBUTE:{
			Object Obj = GetEventObject(Condition->Parameters[0], User, Obj1, Obj2, *Temp);
			int Attribute = (int)Obj.getAttribute((INSTANCEATTRIBUTE)Condition->Parameters[1]);
			int Operator = Condition->Parameters[2];
			int Value = Condition->Parameters[3];
			Result = Compare(Attribute, Operator, Value);
			break;
		}

		case MOVEUSE_CONDITION_HASTEXT:{
			Object Obj = GetEventObject(Condition->Parameters[0], User, Obj1, Obj2, *Temp);
			const char *Attribute = GetDynamicString(Obj.getAttribute(TEXTSTRING));
			const char *Text = GetDynamicString(Condition->Parameters[1]);
			Result = (strcmp(Attribute, Text) == 0);
			break;
		}

		case MOVEUSE_CONDITION_ISPEACEFUL:{
			Object Obj = GetEventObject(Condition->Parameters[0], User, Obj1, Obj2, *Temp);
			if(Obj.getObjectType().isCreatureContainer()){
				TCreature *Creature = GetCreature(Obj);
				Result = (Creature && Creature->IsPeaceful());
			}
			break;
		}

		case MOVEUSE_CONDITION_MAYLOGOUT:{
			Object Obj = GetEventObject(Condition->Parameters[0], User, Obj1, Obj2, *Temp);
			if(Obj.getObjectType().isCreatureContainer()){
				TCreature *Creature = GetCreature(Obj);
				Result = (Creature != NULL && Creature->Type == PLAYER
					&& Creature->LogoutPossible() == 0);
			}
			break;
		}

		case MOVEUSE_CONDITION_HASPROFESSION:{
			Object Obj = GetEventObject(Condition->Parameters[0], User, Obj1, Obj2, *Temp);
			int Profession = Condition->Parameters[1];
			if(Obj.getObjectType().isCreatureContainer()){
				TCreature *Creature = GetCreature(Obj);
				Result = (Creature != NULL && Creature->Type == PLAYER
					&& ((TPlayer*)Creature)->GetEffectiveProfession() == Profession);
			}
			break;
		}

		case MOVEUSE_CONDITION_HASLEVEL:{
			Object Obj = GetEventObject(Condition->Parameters[0], User, Obj1, Obj2, *Temp);
			int Operator = Condition->Parameters[1];
			int Value = Condition->Parameters[2];
			if(Obj.getObjectType().isCreatureContainer()){
				TCreature *Creature = GetCreature(Obj);
				if(Creature != NULL){
					int Level = Creature->Skills[SKILL_LEVEL]->Get();
					Result = Compare(Level, Operator, Value);
				}
			}
			break;
		}

		case MOVEUSE_CONDITION_HASRIGHT:{
			Object Obj = GetEventObject(Condition->Parameters[0], User, Obj1, Obj2, *Temp);
			RIGHT Right = (RIGHT)Condition->Parameters[1];
			if(Obj.getObjectType().isCreatureContainer()){
				TCreature *Creature = GetCreature(Obj);
				Result = (Creature != NULL && Creature->Type == PLAYER
					&& CheckRight(Creature->ID, Right));
			}
			break;
		}

		case MOVEUSE_CONDITION_HASQUESTVALUE:{
			Object Obj = GetEventObject(Condition->Parameters[0], User, Obj1, Obj2, *Temp);
			int QuestNr = Condition->Parameters[1];
			int Operator = Condition->Parameters[2];
			int Value = Condition->Parameters[3];
			if(Obj.getObjectType().isCreatureContainer()){
				TCreature *Creature = GetCreature(Obj);
				if(Creature != NULL && Creature->Type == PLAYER){
					int QuestValue = ((TPlayer*)Creature)->GetQuestValue(QuestNr);
					Result = Compare(QuestValue, Operator, Value);
				}
			}
			break;
		}

		case MOVEUSE_CONDITION_TESTSKILL:{
			Object Obj = GetEventObject(Condition->Parameters[0], User, Obj1, Obj2, *Temp);
			int SkillNr = Condition->Parameters[1];
			int Difficulty = Condition->Parameters[2];
			int Probability = Condition->Parameters[3];
			if(Obj.getObjectType().isCreatureContainer()){
				TCreature *Creature = GetCreature(Obj);
				if(Creature != NULL){
					if(SkillNr >= 0 && SkillNr < NARRAY(Creature->Skills)){
						Result = Creature->Skills[SkillNr]->Probe(Difficulty, Probability, true);
					}else{
						error("CheckCondition (TestSkill): Ungültige Skillnummer %d.\n", SkillNr);
					}
				}
			}
			break;
		}

		// TODO(fusion): This also counts objects at the object's map location?
		case MOVEUSE_CONDITION_COUNTOBJECTS:{
			Object Obj = GetEventObject(Condition->Parameters[0], User, Obj1, Obj2, *Temp);
			int Operator = Condition->Parameters[1];
			int Value = Condition->Parameters[2];
			Object MapCon = GetMapContainer(Obj);
			int ObjCount = CountObjectsInContainer(MapCon);
			Result = Compare(ObjCount, Operator, Value);
			break;
		}

		case MOVEUSE_CONDITION_COUNTOBJECTSONMAP:{
			int CoordX, CoordY, CoordZ;
			UnpackAbsoluteCoordinate(Condition->Parameters[0], &CoordX, &CoordY, &CoordZ);
			int Operator = Condition->Parameters[1];
			int Value = Condition->Parameters[2];
			Object MapCon = GetMapContainer(CoordX, CoordY, CoordZ);
			int ObjCount = CountObjectsInContainer(MapCon);
			Result = Compare(ObjCount, Operator, Value);
			break;
		}

		case MOVEUSE_CONDITION_ISOBJECTTHERE:{
			int CoordX, CoordY, CoordZ;
			UnpackAbsoluteCoordinate(Condition->Parameters[0], &CoordX, &CoordY, &CoordZ);
			ObjectType Type = Condition->Parameters[1];
			*Temp = GetFirstSpecObject(CoordX, CoordY, CoordZ, Type);
			Result = (*Temp != NONE);
			break;
		}

		case MOVEUSE_CONDITION_ISCREATURETHERE:{
			int CoordX, CoordY, CoordZ;
			UnpackAbsoluteCoordinate(Condition->Parameters[0], &CoordX, &CoordY, &CoordZ);
			ObjectType Type = TYPEID_CREATURE_CONTAINER;
			*Temp = GetFirstSpecObject(CoordX, CoordY, CoordZ, Type);
			Result = (*Temp != NONE);
			break;
		}

		// TODO(fusion): I feel this one should iterate the field because there
		// could be more than one creature.
		case MOVEUSE_CONDITION_ISPLAYERTHERE:{
			int CoordX, CoordY, CoordZ;
			UnpackAbsoluteCoordinate(Condition->Parameters[0], &CoordX, &CoordY, &CoordZ);
			ObjectType Type = TYPEID_CREATURE_CONTAINER;
			*Temp = GetFirstSpecObject(CoordX, CoordY, CoordZ, Type);
			if(*Temp != NONE){
				TCreature *Creature = GetCreature(*Temp);
				Result = Creature && Creature->Type == PLAYER;
			}
			break;
		}

		case MOVEUSE_CONDITION_ISOBJECTININVENTORY:{
			Object Obj = GetEventObject(Condition->Parameters[0], User, Obj1, Obj2, *Temp);
			ObjectType Type = Condition->Parameters[1];
			int Count = Condition->Parameters[2];
			if(Obj.getObjectType().isCreatureContainer()){
				*Temp = GetInventoryObject(Obj.getCreatureID(), Type, Count);
				Result = (*Temp != NONE);
			}
			break;
		}

		case MOVEUSE_CONDITION_ISPROTECTIONZONE:{
			int ObjX, ObjY, ObjZ;
			Object Obj = GetEventObject(Condition->Parameters[0], User, Obj1, Obj2, *Temp);
			GetObjectCoordinates(Obj, &ObjX, &ObjY, &ObjZ);
			Result = IsProtectionZone(ObjX, ObjY, ObjZ);
			break;
		}

		case MOVEUSE_CONDITION_ISHOUSE:{
			int ObjX, ObjY, ObjZ;
			Object Obj = GetEventObject(Condition->Parameters[0], User, Obj1, Obj2, *Temp);
			GetObjectCoordinates(Obj, &ObjX, &ObjY, &ObjZ);
			Result = IsHouse(ObjX, ObjY, ObjZ);
			break;
		}

		case MOVEUSE_CONDITION_ISHOUSEOWNER:{
			Object Obj = GetEventObject(Condition->Parameters[0], User, Obj1, Obj2, *Temp);
			Object Cr = GetEventObject(Condition->Parameters[1], User, Obj1, Obj2, *Temp);
			if(Cr.getObjectType().isCreatureContainer()){
				TCreature *Creature = GetCreature(Cr);
				if(Creature != NULL && Creature->Type == PLAYER){
					int ObjX, ObjY, ObjZ;
					GetObjectCoordinates(Obj, &ObjX, &ObjY, &ObjZ);
					uint16 HouseID = GetHouseID(ObjX, ObjY, ObjZ);
					Result = IsOwner(HouseID, (TPlayer*)Creature);
				}
			}
			break;
		}

		case MOVEUSE_CONDITION_ISDRESSED:{
			Object Obj = GetEventObject(Condition->Parameters[0], User, Obj1, Obj2, *Temp);
			ObjectType ObjType = Obj.getObjectType();
			if(ObjType.getFlag(CLOTHES)){
				int CurPosition = GetObjectBodyPosition(Obj);
				int ObjPosition = (int)ObjType.getAttribute(BODYPOSITION);
				Result = (CurPosition == ObjPosition);
			}else{
				error("CheckCondition (IsDressed): Objekt ist kein Kleidungsstück.\n");
			}
			break;
		}

		case MOVEUSE_CONDITION_RANDOM:{
			Result = (random(1, 100) <= Condition->Parameters[0]);
			break;
		}

		default:{
			error("CheckCondition: Unbekannte Bedingung %d.\n", Condition->Condition);
			return false;
		}
	}

	if(Condition->Modifier == MOVEUSE_MODIFIER_NORMAL){
		// no-op
	}else if(Condition->Modifier == MOVEUSE_MODIFIER_INVERT){
		Result = !Result;
	}else if(Condition->Modifier == MOVEUSE_MODIFIER_TRUE){
		Result = true;
	}else{
		// TODO(fusion): Error?
		Result = false;
	}

	return Result;
}

Object CreateObject(Object Con, ObjectType Type, uint32 Value){
	for(int Attempt = 0; true; Attempt += 1){
		try{
			Object Obj = NONE;
			ObjectType ConType = Con.getObjectType();
			if(ConType.isBodyContainer()){
				uint32 CreatureID = GetObjectCreatureID(Con);
				Obj = CreateAtCreature(CreatureID, Type, Value);
			}else{
				Obj = Create(Con, Type, Value);
			}
			return Obj;
		}catch(RESULT r){
			if(Attempt == 0 && (r == NOTTAKABLE || r == TOOHEAVY || r == CONTAINERFULL)){
				// NOTE(fusion): Try to create object on the map, ONCE.
				Con = GetMapContainer(Con);
			}else{
				int ConX, ConY, ConZ;
				GetObjectCoordinates(Con, &ConX, &ConY, &ConZ);
				error("moveuse::CreateObject: Exception %d, Objekt %d, Position [%d,%d,%d].\n",
						r, Type.TypeID, ConX, ConY, ConZ);
				return NONE;
			}
		}
	}
}

void ChangeObject(Object Obj, ObjectType NewType, uint32 Value){
	if(!Obj.exists()){
		error("ChangeObject: Übergebenes Objekt existiert nicht (1, NewType=%d).\n", NewType.TypeID);
		return;
	}

	// NOTE(fusion): `Change` requires CUMULATIVE objects to have AMOUNT
	// equal to ONE or it fails with `SPLITOBJECT`. This means might need
	// to split the object and the split destination may change depending
	// on how things go.
	Object SplitDest = Obj.getContainer();
	for(int Attempt = 0; true; Attempt += 1){
		if(!Obj.exists()){
			error("ChangeObject: Übergebenes Objekt existiert nicht (2, NewType=%d).\n", NewType.TypeID);
			return;
		}

		try{
			ObjectType ObjType = Obj.getObjectType();
			if(ObjType.getFlag(CUMULATIVE)){
				uint32 Amount = Obj.getAttribute(AMOUNT);
				if(Amount > 1){
					Move(0, Obj, SplitDest, Amount - 1, true, NONE);
				}
			}

			Change(Obj, NewType, Value);
			return;
		}catch(RESULT r){
			if(!Obj.exists()){
				error("ChangeObject: Übergebenes Objekt existiert nicht (3, NewType=%d).\n", NewType.TypeID);
				return;
			}

			// TODO(fusion): I feel a couple of attempts should be enough?
			if(Attempt >= 2){
				throw;
			}

			if(r == CONTAINERFULL){
				// NOTE(fusion): Set `SplitDest` to map.
				SplitDest = GetMapContainer(Obj);
			}else if(r == TOOHEAVY){
				// NOTE(fusion): Move object to map, keep `SplitDest`.
				Object MapCon = GetMapContainer(Obj);
				Move(0, Obj, MapCon, -1, true, NONE);
			}else if(r == NOROOM
					&& Obj.getObjectType().getFlag(CUMULATIVE)
					&& Obj.getAttribute(AMOUNT) > 1){
				// NOTE(fusion): Split to map directly. Similar to setting `SplitDest`
				// to map, although failing here will throw us out of the function.
				uint32 Amount = Obj.getAttribute(AMOUNT);
				Object MapCon = GetMapContainer(SplitDest);
				Move(0, Obj, MapCon, Amount - 1, true, NONE);
			}else{
				throw;
			}
		}
	}
}

void MoveOneObject(Object Obj, Object Con){
	if(!Obj.exists()){
		error("MoveOneObject: Übergebenes Objekt existiert nicht.\n");
		return;
	}

	if(!Con.exists()){
		error("MoveOneObject: Übergebener Container existiert nicht.\n");
		return;
	}

	ObjectType ConType = Con.getObjectType();
	if(!ConType.getFlag(CONTAINER)){
		error("MoveOneObject: \"Con\" ist kein Container.\n");
		return;
	}

	ObjectType ObjType = Obj.getObjectType();
	if(ObjType.isCreatureContainer() && !ConType.isMapContainer()){
		error("MoveOneObject: \"Con\" ist kein MapContainer.\n");
		return;
	}

	try{
		if(ObjType.getFlag(LIQUIDPOOL) || ObjType.getFlag(MAGICFIELD)){
			Delete(Obj, -1);
		}else{
			Move(0, Obj, Con, -1, true, NONE);
		}
	}catch(RESULT r){
		if(r != DESTROYED){
			int ConX, ConY, ConZ;
			GetObjectCoordinates(Con, &ConX, &ConY, &ConZ);
			error("MoveOneObject: Exception %d; Koordinate [%d,%d,%d].\n",
					r, ConX, ConY, ConZ);
		}

		if(r == CONTAINERFULL){
			Delete(Obj, -1);
		}
	}
}

void MoveAllObjects(Object Obj, Object Dest, Object Exclude, bool MoveUnmovable){
	if(Obj == NONE){
		return;
	}

	if(!Dest.getObjectType().isMapContainer()){
		error("MoveAllObjects: \"Dest\" ist kein Mapcontainer.\n");
		return;
	}

	// TODO(fusion): We probably use recursion to move objects in reverse order?
	MoveAllObjects(Obj.getNextObject(), Dest, Exclude, MoveUnmovable);

	if(Obj != Exclude){
		ObjectType ObjType = Obj.getObjectType();
		try{
			if(ObjType.getFlag(LIQUIDPOOL) || ObjType.getFlag(MAGICFIELD)){
				Delete(Obj, -1);
			}else if(MoveUnmovable || !ObjType.getFlag(UNMOVE)){
				Move(0, Obj, Dest, -1, true, NONE);
			}
		}catch(RESULT r){
			if(r != DESTROYED){
				int DestX, DestY, DestZ;
				GetObjectCoordinates(Dest, &DestX, &DestY, &DestZ);
				error("MoveAllObjects: Exception %d; Objekt %d, Zielkoordinate [%d,%d,%d].\n",
						r, ObjType.TypeID, DestX, DestY, DestZ);
			}
		}
	}
}

void DeleteAllObjects(Object Obj, Object Exclude, bool DeleteUnmovable){
	if(Obj == NONE){
		return;
	}

	// TODO(fusion): Same as `MoveAllObjects`.
	DeleteAllObjects(Obj.getNextObject(), Exclude, DeleteUnmovable);

	if(Obj != Exclude){
		ObjectType ObjType = Obj.getObjectType();
		if(ObjType.isCreatureContainer()){
			int ObjX, ObjY, ObjZ;
			GetObjectCoordinates(Obj, &ObjX, &ObjY, &ObjZ);
			error("DeleteAllObjects: An Position [%d,%d,%d] soll eine Kreatur gelöscht werden.\n",
					ObjX, ObjY, ObjZ);
			return;
		}

		if(!DeleteUnmovable && ObjType.getFlag(UNMOVE)
				&& !ObjType.getFlag(LIQUIDPOOL)
				&& !ObjType.getFlag(MAGICFIELD)){
			return;
		}

		try{
			Delete(Obj, -1);
		}catch(RESULT r){
			error("DeleteAllObjects: Exception %d.\n", r);
		}
	}
}

void ClearField(Object Obj, Object Exclude){
	// IMPORTANT(fusion): This function seems to be used when items transform
	// into `UNPASS` versions of themselves, like doors, which is why we skip
	// the actual object when calling `MoveAllObjects` at the end.
	//	The order we scan available fields may also be important which is why
	// I've kept the same ordering used by the original function.

	if(!Obj.exists()){
		error("ClearField: Objekt existiert nicht.\n");
		return;
	}

	int ObjX, ObjY, ObjZ;
	GetObjectCoordinates(Obj, &ObjX, &ObjY, &ObjZ);

	bool DestFound = false;
	int DestX, DestY, DestZ;
	int OffsetX[4] = { 1,  0, -1,  0};
	int OffsetY[4] = { 0,  1,  0, -1};

	// NOTE(fusion): First look for a field that's not blocked.
	for(int i = 0; i < 4; i += 1){
		DestX = ObjX + OffsetX[i];
		DestY = ObjY + OffsetY[i];
		DestZ = ObjZ;
		if(CoordinateFlag(DestX, DestY, DestZ, BANK)
		&& !CoordinateFlag(DestX, DestY, DestZ, UNPASS)){
			DestFound = true;
			break;
		}
	}

	// NOTE(fusion): Then look for a field where jumping is possible.
	if(!DestFound){
		for(int i = 0; i < 4; i += 1){
			DestX = ObjX + OffsetX[i];
			DestY = ObjY + OffsetY[i];
			DestZ = ObjZ;
			if(JumpPossible(DestX, DestY, DestZ, false)){
				DestFound = true;
				break;
			}
		}
	}

	if(DestFound){
		Object Dest = GetMapContainer(DestX, DestY, DestZ);
		MoveAllObjects(Obj.getNextObject(), Dest, Exclude, true);
	}
}

void LoadDepotBox(uint32 CreatureID, int Nr, Object Con){
	TPlayer *Player = GetPlayer(CreatureID);
	if(Player == NULL){
		error("moveuse::LoadDepotBox: Kreatur nicht gefunden.\n");
		return;
	}

	if(!Con.exists()){
		error("moveuse::LoadDepotBox: Übergebener Container existiert nicht.\n");
		return;
	}

	if(!Con.getObjectType().getFlag(CONTAINER)){
		error("moveuse::LoadDepotBox: Übergebenes Objekt ist kein Container.\n");
		return;
	}

	// NOTE(fusion): Holy fuck. Objects are actually loaded into the depot box.
	LoadDepot(Player->PlayerData, Nr, Con);

	int DepotObjects = CountObjects(Con) - 1;
	int DepotCapacity = GetDepotSize(Nr, CheckRight(Player->ID, PREMIUM_ACCOUNT));
	int DepotSpace = DepotCapacity - DepotObjects;
	Player->Depot = Con;
	Player->DepotNr = Nr;
	Player->DepotSpace = DepotSpace;

	print(3, "Depot von %s hat %d freie Plätze.\n", Player->Name, DepotSpace);
	SendMessage(Player->Connection, TALK_STATUS_MESSAGE,
			"Your depot contains %d item%s.",
			DepotObjects, (DepotObjects != 1 ? "s" : ""));

	if(DepotSpace <= 0){
		SendMessage(Player->Connection, TALK_INFO_MESSAGE,
				"Your depot is full. Remove surplus items before storing new ones.");
	}
}

void SaveDepotBox(uint32 CreatureID, int Nr, Object Con){
	TPlayer *Player = GetPlayer(CreatureID);
	if(Player == NULL){
		error("moveuse::SaveDepotBox: Kreatur nicht gefunden.\n");
		return;
	}

	if(!Con.exists()){
		error("moveuse::SaveDepotBox: Übergebener Container existiert nicht.\n");
		return;
	}

	if(!Con.getObjectType().getFlag(CONTAINER)){
		error("moveuse::SaveDepotBox: Übergebenes Objekt ist kein Container.\n");
		return;
	}

	int DepotObjects = CountObjects(Con) - 1;
	Log("game", "Speichere Depot %d von %s ... Depotgröße: %d.\n",
			Nr, Player->Name, DepotObjects);
	print(3, "Depot von %s: berechnete freie Plätze %d, tatsächliche Objekte %d.\n",
			Player->Name, Player->DepotSpace, DepotObjects);

	SaveDepot(Player->PlayerData, Nr, Con);
	Player->Depot = NONE;

	Object Obj = GetFirstContainerObject(Con);
	while(Obj != NONE){
		Object Next = Obj.getNextObject();
		Delete(Obj, -1);
		Obj = Next;
	}
}

// TODO(fusion): Maybe move this to `strings.cc` or `utils.cc`?
static int ReadLine(char *Dest, int DestCapacity, const char *Text, int ReadPos){
	ASSERT(DestCapacity > 0);
	int WritePos = 0;
	while(Text[ReadPos] != 0 && Text[ReadPos] != '\n'){
		if(WritePos < (DestCapacity - 1)){
			Dest[WritePos] = Text[ReadPos];
			WritePos += 1;
		}
		ReadPos += 1;
	}

	if(Text[ReadPos] == '\n'){
		ReadPos += 1;
	}

	Dest[WritePos] = 0;
	return ReadPos;
}

void SendMail(Object Obj){
	if(!Obj.exists()){
		error("SendMail: Übergebenes Objekt existiert nicht.\n");
		return;
	}

	ObjectType ObjType = Obj.getObjectType();
	if(ObjType != GetSpecialObject(PARCEL_NEW)
	&& ObjType != GetSpecialObject(LETTER_NEW)){
		return;
	}

	print(3, "Versende Post...\n");

	uint32 TextAttr = 0;
	if(ObjType == GetSpecialObject(LETTER_NEW)){
		TextAttr = Obj.getAttribute(TEXTSTRING);
	}else{
		ObjectType LabelType = GetSpecialObject(PARCEL_LABEL);
		Object Label = GetFirstContainerObject(Obj);
		while(Label != NONE){
			if(Label.getObjectType() == LabelType){
				break;
			}
			Label = Label.getNextObject();
		}

		if(Label == NONE){
			return;
		}

		TextAttr = Label.getAttribute(TEXTSTRING);
	}

	const char *Text = GetDynamicString(TextAttr);
	if(Text == NULL || Text[0] == 0){
		return;
	}

	char Addressee[256];
	char Town[256];
	{
		int ReadPos = 0;
		ReadPos = ReadLine(Addressee, sizeof(Addressee), Text, ReadPos);
		ReadPos = ReadLine(Town,      sizeof(Town),      Text, ReadPos);
		Trim(Addressee);
		Trim(Town);
	}

	if(Addressee[0] == 0 || Town[0] == 0){
		return;
	}

	// TODO(fusion): This constant for the size of creature names showing up again.
	// Maybe `MAX_NAME_LENGTH`?
	if(strlen(Addressee) >= 30){
		return;
	}

	Log("game", "Post an %s in %s.\n", Addressee, Town);
	int DepotNr = GetDepotNumber(Town);
	if(DepotNr == -1){
		return;
	}

	TPlayer *Player;
	bool PlayerOnline;
	uint32 CharacterID;
	if(IdentifyPlayer(Addressee, true, true, &Player) == 0){
		PlayerOnline = true;
		CharacterID = Player->ID;
	}else{
		PlayerOnline = false;
		CharacterID = GetCharacterID(Addressee);
		if(CharacterID == 0){
			return;
		}
	}

	if(PlayerOnline && Player->Depot != NONE && Player->DepotNr == DepotNr){
		print(3, "Adressat ist eingeloggt und hat Depot offen.\n");
		try{
			Move(0, Obj, Player->Depot, -1, true, NONE);
			if(ObjType == GetSpecialObject(LETTER_NEW)){
				Change(Obj, GetSpecialObject(LETTER_STAMPED), 0);
			}else{
				Change(Obj, GetSpecialObject(PARCEL_STAMPED), 0);
			}
			Log("game", "Post an %s in %s versandt.\n", Addressee, Town);
			Player->DepotSpace -= CountObjects(Obj);
			print(3, "Depot von %s hat jetzt %d freie Plätze.\n", Player->Name, Player->DepotSpace);
			SendMessage(Player->Connection, TALK_INFO_MESSAGE, "New mail has arrived.");
			print(3, "Post erfolgreich versandt.\n");
		}catch(RESULT r){
			if(r != CONTAINERFULL){
				error("SendMail (Depot offen): Exception %d.\n", r);
			}

			print(3, "Exception %d beim Postversand.\n", r);
		}
	}else{
		print(3, "Adressat ist nicht eingeloggt oder hat Depot geschlossen.\n");

		// TODO(fusion): The scope of this try block was unclear but I suppose
		// this is correct due to how `Change` is also inside the try block in
		// the try block above, when the player is online.
		try{
			if(ObjType == GetSpecialObject(LETTER_NEW)){
				Change(Obj, GetSpecialObject(LETTER_STAMPED), 0);
			}else{
				Change(Obj, GetSpecialObject(PARCEL_STAMPED), 0);
			}

			TDynamicWriteBuffer WriteBuffer(1024);
			SaveObjects(Obj, &WriteBuffer, true);
			Delete(Obj, -1);

			TDelayedMail *Mail = DelayedMail.at(DelayedMails);
			Mail->CharacterID = CharacterID;
			Mail->DepotNumber = DepotNr;
			Mail->PacketSize = WriteBuffer.Position;
			// TODO(fusion): Check if `WriteBuffer.Position` is not zero?
			Mail->Packet = new uint8[WriteBuffer.Position];
			memcpy(Mail->Packet, WriteBuffer.Data, WriteBuffer.Position);
			DelayedMails += 1;

			Log("game", "Post an %s in %s zurückgestellt.\n", Addressee, Town);
			print(3, "Post zurückgestellt.\n");
			if(PlayerOnline){
				SendMails(Player->PlayerData);
			}else{
				LoadCharacterOrder(CharacterID);
			}
		}catch(RESULT r){
			error("SendMail: Kann Paket nicht löschen (%d).\n", r);
		}catch(const char *str){
			error("SendMail: Kann Paket nicht schreiben (%s).\n", str);
		}
	}
}

void SendMails(TPlayerData *PlayerData){
	if(PlayerData == NULL){
		error("SendMails: PlayerData ist NULL.\n");
		return;
	}

	if(PlayerData->CharacterID == 0){
		error("SendMails: Slot ist nicht belegt.\n");
		return;
	}

	if(PlayerData->Locked != gettid()){
		error("SendMails: Slot ist nicht vom GameThread gesperrt.\n");
		return;
	}

	for(int i = 0; i < DelayedMails; i += 1){
		TDelayedMail *Mail = DelayedMail.at(i);
		if(Mail->CharacterID != PlayerData->CharacterID){
			continue;
		}

		int DepotNr = Mail->DepotNumber;
		if(DepotNr < 0 || DepotNr >= MAX_DEPOTS){
			error("SendMails: Ungültige Depotnummer %d.\n", DepotNr);
			DepotNr = 0;
		}

		int NewDepotSize = 0;
		uint8 *NewDepot = NULL;
		if(PlayerData->Depot[DepotNr] != NULL){
			NewDepotSize = PlayerData->DepotSize[DepotNr] + Mail->PacketSize;
			NewDepot = new uint8[NewDepotSize];
			memcpy(&NewDepot[0], Mail->Packet, Mail->PacketSize);
			memcpy(&NewDepot[Mail->PacketSize],
					PlayerData->Depot[DepotNr],
					PlayerData->DepotSize[DepotNr]);
			delete[] PlayerData->Depot[DepotNr];
		}else{
			NewDepotSize = Mail->PacketSize + 5;
			NewDepot = new uint8[NewDepotSize];
			memcpy(&NewDepot[0], Mail->Packet, Mail->PacketSize);
			TWriteBuffer WriteBuffer(&NewDepot[Mail->PacketSize], 5);
			WriteBuffer.writeWord((uint16)GetSpecialObject(DEPOT_CHEST).TypeID);
			WriteBuffer.writeByte(0xFF);
			WriteBuffer.writeWord(0xFFFF);
		}

		PlayerData->Depot[DepotNr] = NewDepot;
		PlayerData->DepotSize[DepotNr] = NewDepotSize;
		PlayerData->Dirty = true;

		Log("game", "Post an %s zugestellt.\n", PlayerData->Name);

		if(Mail->Packet != NULL){
			delete[] Mail->Packet;
		}

		// NOTE(fusion): A little swap and pop action.
		DelayedMails -= 1;
		*DelayedMail.at(i) = *DelayedMail.at(DelayedMails);
		*DelayedMail.at(DelayedMails) = TDelayedMail{};

		// NOTE(fusion): Check current index again after removal.
		i -= 1;
	}
}

void TextEffect(const char *Text, int x, int y, int z, int Radius){
	if(Text == NULL){
		error("TextEffect: Text existiert nicht.\n");
		return;
	}

	TFindCreatures Search(Radius, Radius, x, y, FIND_PLAYERS);
	while(true){
		uint32 CharacterID = Search.getNext();
		if(CharacterID == 0){
			break;
		}

		TPlayer *Player = GetPlayer(CharacterID);
		if(Player == NULL || Player->Connection == NULL){
			continue;
		}

		int PlayerZ = Player->posz;
		if(PlayerZ == z || Radius > 30 || (Radius > 7 && PlayerZ <= 7 && z <= 7)){
			SendTalk(Player->Connection, 0, "", TALK_ANIMAL_LOW, x, y, z, Text);
		}
	}
}

void ExecuteAction(MoveUseEventType EventType, TMoveUseAction *Action,
		Object User, Object Obj1, Object Obj2, Object *Temp){
	try{
		switch(Action->Action){
			case MOVEUSE_ACTION_CREATEONMAP:{
				int CoordX, CoordY, CoordZ;
				UnpackAbsoluteCoordinate(Action->Parameters[0], &CoordX, &CoordY, &CoordZ);
				ObjectType Type = Action->Parameters[1];
				int Value = Action->Parameters[2];
				Object MapCon = GetMapContainer(CoordX, CoordY, CoordZ);
				*Temp = CreateObject(MapCon, Type, Value);
				break;
			}

			case MOVEUSE_ACTION_CREATE:{
				Object Obj = GetEventObject(Action->Parameters[0], User, Obj1, Obj2, *Temp);
				ObjectType Type = Action->Parameters[1];
				int Value = Action->Parameters[2];
				*Temp = CreateObject(Obj.getContainer(), Type, Value);
				break;
			}

			case MOVEUSE_ACTION_MONSTERONMAP:{
				int CoordX, CoordY, CoordZ;
				UnpackAbsoluteCoordinate(Action->Parameters[0], &CoordX, &CoordY, &CoordZ);
				int Race = Action->Parameters[1];
				CreateMonster(Race, CoordX, CoordY, CoordZ, 0, 0, true);
				break;
			}

			case MOVEUSE_ACTION_MONSTER:{
				int ObjX, ObjY, ObjZ;
				Object Obj = GetEventObject(Action->Parameters[0], User, Obj1, Obj2, *Temp);
				int Race = Action->Parameters[1];
				GetObjectCoordinates(Obj, &ObjX, &ObjY, &ObjZ);
				CreateMonster(Race, ObjX, ObjY, ObjZ, 0, 0, true);
				break;
			}

			case MOVEUSE_ACTION_EFFECTONMAP:{
				int CoordX, CoordY, CoordZ;
				UnpackAbsoluteCoordinate(Action->Parameters[0], &CoordX, &CoordY, &CoordZ);
				int Effect = Action->Parameters[1];
				GraphicalEffect(CoordX, CoordY, CoordZ, Effect);
				break;
			}

			case MOVEUSE_ACTION_EFFECT:{
				int ObjX, ObjY, ObjZ;
				Object Obj = GetEventObject(Action->Parameters[0], User, Obj1, Obj2, *Temp);
				int Effect = Action->Parameters[1];
				GetObjectCoordinates(Obj, &ObjX, &ObjY, &ObjZ);
				GraphicalEffect(ObjX, ObjY, ObjZ, Effect);
				break;
			}

			case MOVEUSE_ACTION_TEXTONMAP:{
				int CoordX, CoordY, CoordZ;
				UnpackAbsoluteCoordinate(Action->Parameters[0], &CoordX, &CoordY, &CoordZ);
				const char *Text = GetDynamicString(Action->Parameters[1]);
				int Radius = Action->Parameters[2];
				TextEffect(Text, CoordX, CoordY, CoordZ, Radius);
				break;
			}

			case MOVEUSE_ACTION_TEXT:{
				int ObjX, ObjY, ObjZ;
				Object Obj = GetEventObject(Action->Parameters[0], User, Obj1, Obj2, *Temp);
				const char *Text = GetDynamicString(Action->Parameters[1]);
				int Radius = Action->Parameters[2];
				GetObjectCoordinates(Obj, &ObjX, &ObjY, &ObjZ);
				TextEffect(Text, ObjX, ObjY, ObjZ, Radius);
				break;
			}

			case MOVEUSE_ACTION_CHANGEONMAP:{
				int CoordX, CoordY, CoordZ;
				UnpackAbsoluteCoordinate(Action->Parameters[0], &CoordX, &CoordY, &CoordZ);
				ObjectType OldType = Action->Parameters[1];
				ObjectType NewType = Action->Parameters[2];
				int NewValue = Action->Parameters[3];
				*Temp = GetFirstSpecObject(CoordX, CoordY, CoordZ, OldType);
				if(*Temp != NONE){
					ChangeObject(*Temp, NewType, NewValue);
				}else{
					error("ExecuteAction (CHANGEONMAP): Kein Objekt %d auf [%d,%d,%d].\n",
							OldType.TypeID, CoordX, CoordY, CoordZ);
				}
				break;
			}

			case MOVEUSE_ACTION_CHANGE:{
				Object Obj = GetEventObject(Action->Parameters[0], User, Obj1, Obj2, *Temp);
				ObjectType NewType = Action->Parameters[1];
				int NewValue = Action->Parameters[2];
				ChangeObject(Obj, NewType, NewValue);
				break;
			}

			case MOVEUSE_ACTION_CHANGEREL:{
				int ObjX, ObjY, ObjZ, RelX, RelY, RelZ;
				Object Obj = GetEventObject(Action->Parameters[0], User, Obj1, Obj2, *Temp);
				UnpackRelativeCoordinate(Action->Parameters[1], &RelX, &RelY, &RelZ);
				ObjectType OldType = Action->Parameters[2];
				ObjectType NewType = Action->Parameters[3];
				int NewValue = Action->Parameters[4];
				GetObjectCoordinates(Obj, &ObjX, &ObjY, &ObjZ);
				*Temp = GetFirstSpecObject((ObjX + RelX), (ObjY + RelY), (ObjZ + RelZ), OldType);
				if(*Temp != NONE){
					ChangeObject(*Temp, NewType, NewValue);
				}else{
					error("ExecuteAction (CHANGEREL): Kein Objekt %d auf [%d,%d,%d].\n",
							OldType.TypeID, (ObjX + RelX), (ObjY + RelY), (ObjZ + RelZ));
				}
				break;
			}

			case MOVEUSE_ACTION_SETATTRIBUTE:{
				Object Obj = GetEventObject(Action->Parameters[0], User, Obj1, Obj2, *Temp);
				INSTANCEATTRIBUTE Attribute = (INSTANCEATTRIBUTE)Action->Parameters[1];
				int Value = Action->Parameters[2];
				Change(Obj, Attribute, Value);
				break;
			}

			case MOVEUSE_ACTION_CHANGEATTRIBUTE:{
				Object Obj = GetEventObject(Action->Parameters[0], User, Obj1, Obj2, *Temp);
				INSTANCEATTRIBUTE Attribute = (INSTANCEATTRIBUTE)Action->Parameters[1];
				int Amount = Action->Parameters[2];
				// TODO(fusion): This is weird.
				int OldValue = (int)Obj.getAttribute(Attribute);
				int NewValue = OldValue + Amount;
				if(NewValue <= 0){
					if(OldValue == 0){
						NewValue = 0;
					}else{
						NewValue = 1;
					}
				}
				Change(Obj, Attribute, NewValue);
				break;
			}

			case MOVEUSE_ACTION_SETQUESTVALUE:{
				Object Obj = GetEventObject(Action->Parameters[0], User, Obj1, Obj2, *Temp);
				int QuestNr = Action->Parameters[1];
				int QuestValue = Action->Parameters[2];
				ObjectType ObjType = Obj.getObjectType();
				if(ObjType.isCreatureContainer()){
					TCreature *Creature = GetCreature(Obj);
					if(Creature != NULL && Creature->Type == PLAYER){
						((TPlayer*)Creature)->SetQuestValue(QuestNr, QuestValue);
					}else{
						error("ExecuteAction (SETQUESTVALUE): Kreatur existiert nicht oder ist kein Spieler.\n");
					}
				}else{
					error("ExecuteAction (SETQUESTVALUE): Objekt ist keine Kreatur, sondern Typ %d.\n", ObjType.TypeID);
				}
				break;
			}

			case MOVEUSE_ACTION_DAMAGE:{
				Object AttackerObj = GetEventObject(Action->Parameters[0], User, Obj1, Obj2, *Temp);
				Object VictimObj = GetEventObject(Action->Parameters[1], User, Obj1, Obj2, *Temp);
				int DamageType = Action->Parameters[2];
				int Damage = Action->Parameters[3];

				TCreature *Attacker = NULL;
				if(AttackerObj != NONE){
					ObjectType AttackerType = AttackerObj.getObjectType();
					if(AttackerType.isCreatureContainer()){
						Attacker = GetCreature(AttackerObj);
					}else if(AttackerType.getFlag(MAGICFIELD)
							&& AttackerObj.getAttribute(RESPONSIBLE) != 0){
						int TotalExpireTime = (int)AttackerType.getAttribute(TOTALEXPIRETIME);
						int CurExpireTime = (int)CronInfo(AttackerObj, false);
						if((TotalExpireTime - CurExpireTime) < 5){
							Attacker = GetCreature(AttackerObj.getAttribute(RESPONSIBLE));
						}
					}
				}

				ObjectType VictimType = VictimObj.getObjectType();
				if(VictimType.isCreatureContainer()){
					TCreature *Victim = GetCreature(VictimObj);
					if(Victim != NULL){
						Victim->Damage(Attacker, Damage, DamageType);
					}else{
						error("ExecuteAction (DAMAGE): Kreatur existiert nicht.\n");
					}
				}else{
					error("ExecuteAction (DAMAGE): Objekt ist keine Kreatur, sondern Typ %d.\n", VictimType.TypeID);
				}
				break;
			}

			case MOVEUSE_ACTION_SETSTART:{
				int StartX, StartY, StartZ;
				Object Obj = GetEventObject(Action->Parameters[0], User, Obj1, Obj2, *Temp);
				UnpackAbsoluteCoordinate(Action->Parameters[1], &StartX, &StartY, &StartZ);

				if(!Obj.getObjectType().isCreatureContainer()){
					throw ERROR;
				}

				TCreature *Creature = GetCreature(Obj);
				if(Creature == NULL || Creature->Type != PLAYER){
					throw ERROR;
				}

				Creature->startx = StartX;
				Creature->starty = StartY;
				Creature->startz = StartZ;
				((TPlayer*)Creature)->SaveData();
				break;
			}

			case MOVEUSE_ACTION_WRITENAME:{
				Object WriterObj = GetEventObject(Action->Parameters[0], User, Obj1, Obj2, *Temp);
				const char *Format = GetDynamicString(Action->Parameters[1]);
				Object TargetObj = GetEventObject(Action->Parameters[2], User, Obj1, Obj2, *Temp);

				if(WriterObj == NONE || !WriterObj.getObjectType().isCreatureContainer()
				|| TargetObj == NONE || !TargetObj.getObjectType().getFlag(TEXT)){
					throw ERROR;
				}

				TCreature *Writer = GetCreature(WriterObj);
				if(Writer == NULL){
					throw ERROR;
				}

				// TODO(fusion): Some helper function to format text?
				char Text[256] = {};
				{
					int ReadPos = 0;
					int WritePos = 0;
					int TextSize = (int)sizeof(Text);
					while(Format[ReadPos] != 0){
						if(Format[ReadPos] == '%' && Format[ReadPos + 1] == 'N'){
							// NOTE(fusion): Make sure we don't overflow the buffer.
							int NameLen = (int)strlen(Writer->Name);
							if(NameLen > 0){
								if((WritePos + NameLen) > TextSize){
									NameLen = TextSize - WritePos;
								}

								if(NameLen > 0){
									memcpy(&Text[WritePos], Writer->Name, NameLen);
									WritePos += NameLen;
								}
							}

							ReadPos += 2;
						}else{
							// NOTE(fusion): Make sure we don't overflow the buffer.
							if(WritePos < TextSize){
								Text[WritePos] = Format[ReadPos];
								WritePos += 1;
							}

							ReadPos += 1;
						}
					}

					if(WritePos >= TextSize){
						throw ERROR;
					}
				}

				DeleteDynamicString(TargetObj.getAttribute(TEXTSTRING));
				DeleteDynamicString(TargetObj.getAttribute(EDITOR));
				Change(TargetObj, TEXTSTRING, AddDynamicString(Text));
				Change(TargetObj, EDITOR, 0);
				break;
			}

			case MOVEUSE_ACTION_WRITETEXT:{
				const char *Text = GetDynamicString(Action->Parameters[0]);
				Object Obj = GetEventObject(Action->Parameters[1], User, Obj1, Obj2, *Temp);

				if(Obj == NONE || !Obj.getObjectType().getFlag(TEXT)){
					throw ERROR;
				}

				DeleteDynamicString(Obj.getAttribute(TEXTSTRING));
				DeleteDynamicString(Obj.getAttribute(EDITOR));
				Change(Obj, TEXTSTRING, AddDynamicString(Text));
				Change(Obj, EDITOR, 0);
				break;
			}

			case MOVEUSE_ACTION_LOGOUT:{
				Object Obj = GetEventObject(Action->Parameters[0], User, Obj1, Obj2, *Temp);

				if(Obj == NONE || !Obj.getObjectType().isCreatureContainer()){
					throw ERROR;
				}

				TCreature *Creature = GetCreature(Obj);
				if(Creature == NULL){
					throw ERROR;
				}

				Creature->StartLogout(true, true);
				break;
			}

			case MOVEUSE_ACTION_MOVEALLONMAP:{
				int OrigX, OrigY, OrigZ, DestX, DestY, DestZ;
				UnpackAbsoluteCoordinate(Action->Parameters[0], &OrigX, &OrigY, &OrigZ);
				UnpackAbsoluteCoordinate(Action->Parameters[1], &DestX, &DestY, &DestZ);
				Object First = GetFirstObject(OrigX, OrigY, OrigZ);
				Object Dest = GetMapContainer(DestX, DestY, DestZ);
				MoveAllObjects(First, Dest, NONE, false);
				break;
			}

			case MOVEUSE_ACTION_MOVEALL:{
				int DestX, DestY, DestZ;
				Object Obj = GetEventObject(Action->Parameters[0], User, Obj1, Obj2, *Temp);
				UnpackAbsoluteCoordinate(Action->Parameters[1], &DestX, &DestY, &DestZ);
				Object First = GetFirstContainerObject(Obj.getContainer());
				Object Dest = GetMapContainer(DestX, DestY, DestZ);
				// TODO(fusion): Why do we set the `Exclude` parameter to `First`?
				// Maybe we just don't want to check if it is `NONE` can call
				// `getNextObject()` like MOVETOPONMAP?
				MoveAllObjects(First, Dest, First, false);
				break;
			}

			case MOVEUSE_ACTION_MOVEALLREL:{
				int ObjX, ObjY, ObjZ, RelX, RelY, RelZ;
				Object Obj = GetEventObject(Action->Parameters[0], User, Obj1, Obj2, *Temp);
				UnpackRelativeCoordinate(Action->Parameters[1], &RelX, &RelY, &RelZ);
				GetObjectCoordinates(Obj, &ObjX, &ObjY, &ObjZ);
				Object First = GetFirstContainerObject(Obj.getContainer());
				Object Dest = GetMapContainer((ObjX + RelX), (ObjY + RelY), (ObjZ + RelZ));
				// TODO(fusion): Same as above?
				MoveAllObjects(First, Dest, First, false);
				break;
			}

			case MOVEUSE_ACTION_MOVETOPONMAP:{
				int OrigX, OrigY, OrigZ, DestX, DestY, DestZ;
				UnpackAbsoluteCoordinate(Action->Parameters[0], &OrigX, &OrigY, &OrigZ);
				ObjectType Type = Action->Parameters[1];
				UnpackAbsoluteCoordinate(Action->Parameters[2], &DestX, &DestY, &DestZ);
				Object Obj = GetFirstSpecObject(OrigX, OrigY, OrigZ, Type);
				if(Obj != NONE){
					Object Dest = GetMapContainer(DestX, DestY, DestZ);
					MoveAllObjects(Obj.getNextObject(), Dest, NONE, true);
				}else{
					error("ExecuteAction (MOVETOPONMAP): Kein Objekt %d auf [%d,%d,%d].\n",
							Type.TypeID, OrigX, OrigY, OrigZ);
				}
				break;
			}

			case MOVEUSE_ACTION_MOVETOP:{
				int DestX, DestY, DestZ;
				Object Obj = GetEventObject(Action->Parameters[0], User, Obj1, Obj2, *Temp);
				UnpackAbsoluteCoordinate(Action->Parameters[1], &DestX, &DestY, &DestZ);
				Object Dest = GetMapContainer(DestX, DestY, DestZ);
				MoveAllObjects(Obj.getNextObject(), Dest, NONE, true);
				break;
			}

			case MOVEUSE_ACTION_MOVETOPREL:{
				int ObjX, ObjY, ObjZ, RelX, RelY, RelZ;
				Object Obj = GetEventObject(Action->Parameters[0], User, Obj1, Obj2, *Temp);
				UnpackRelativeCoordinate(Action->Parameters[1], &RelX, &RelY, &RelZ);
				GetObjectCoordinates(Obj, &ObjX, &ObjY, &ObjZ);
				Object Dest = GetMapContainer((ObjX + RelX), (ObjY + RelY), (ObjZ + RelZ));
				MoveAllObjects(Obj.getNextObject(), Dest, NONE, true);
				break;
			}

			case MOVEUSE_ACTION_MOVE:{
				int DestX, DestY, DestZ;
				Object Obj = GetEventObject(Action->Parameters[0], User, Obj1, Obj2, *Temp);
				UnpackAbsoluteCoordinate(Action->Parameters[1], &DestX, &DestY, &DestZ);
				Object Dest = GetMapContainer(DestX, DestY, DestZ);
				MoveOneObject(Obj, Dest);
				break;
			}

			case MOVEUSE_ACTION_MOVEREL:{
				int RefX, RefY, RefZ, RelX, RelY, RelZ;
				Object MovObj = GetEventObject(Action->Parameters[0], User, Obj1, Obj2, *Temp);
				Object RefObj = GetEventObject(Action->Parameters[1], User, Obj1, Obj2, *Temp);
				UnpackRelativeCoordinate(Action->Parameters[2], &RelX, &RelY, &RelZ);
				GetObjectCoordinates(RefObj, &RefX, &RefY, &RefZ);
				Object Dest = GetMapContainer((RefX + RelX), (RefY + RelY), (RefZ + RelZ));
				MoveOneObject(MovObj, Dest);
				break;
			}

			case MOVEUSE_ACTION_RETRIEVE:{
				int ObjX, ObjY, ObjZ, OrigRelX, OrigRelY, OrigRelZ, DestRelX, DestRelY, DestRelZ;
				Object Obj = GetEventObject(Action->Parameters[0], User, Obj1, Obj2, *Temp);
				UnpackRelativeCoordinate(Action->Parameters[1], &OrigRelX, &OrigRelY, &OrigRelZ);
				UnpackRelativeCoordinate(Action->Parameters[2], &DestRelX, &DestRelY, &DestRelZ);
				GetObjectCoordinates(Obj, &ObjX, &ObjY, &ObjZ);
				Object Top = GetTopObject((ObjX + OrigRelX), (ObjY + OrigRelY), (ObjZ + OrigRelZ), false);
				if(Top != NONE && !Top.getObjectType().getFlag(UNMOVE)){
					Object Dest = GetMapContainer((ObjX + DestRelX), (ObjY + DestRelY), (ObjZ + DestRelZ));
					MoveOneObject(Top, Dest);
				}
				break;
			}

			case MOVEUSE_ACTION_DELETEALLONMAP:{
				int CoordX, CoordY, CoordZ;
				UnpackAbsoluteCoordinate(Action->Parameters[0], &CoordX, &CoordY, &CoordZ);
				Object First = GetFirstObject(CoordX, CoordY, CoordZ);
				// TODO(fusion): Same as `MOVEALL`.
				DeleteAllObjects(First, First, false);
				break;
			}

			case MOVEUSE_ACTION_DELETETOPONMAP:{
				int CoordX, CoordY, CoordZ;
				UnpackAbsoluteCoordinate(Action->Parameters[0], &CoordX, &CoordY, &CoordZ);
				ObjectType Type = Action->Parameters[1];
				Object First = GetFirstSpecObject(CoordX, CoordY, CoordZ, Type);
				// TODO(fusion): Sometimes we throw, sometimes we print an error,
				// sometimes we ignore it, what a mess.
				if(First == NONE){
					throw ERROR;
				}
				DeleteAllObjects(First, First, true);
				break;
			}

			case MOVEUSE_ACTION_DELETEONMAP:{
				int CoordX, CoordY, CoordZ;
				UnpackAbsoluteCoordinate(Action->Parameters[0], &CoordX, &CoordY, &CoordZ);
				ObjectType Type = Action->Parameters[1];
				Object Obj = GetFirstSpecObject(CoordX, CoordY, CoordZ, Type);
				if(Obj != NONE){
					bool IsUseEvent = (EventType == MOVEUSE_EVENT_USE
							|| EventType == MOVEUSE_EVENT_MULTIUSE);
					Delete(Obj, (IsUseEvent ? 1 : -1));
				}else{
					error("ExecuteAction (DELETEONMAP): Kein Objekt %d auf [%d,%d,%d].\n",
							Type.TypeID, CoordX, CoordY, CoordZ);
				}
				break;
			}

			case MOVEUSE_ACTION_DELETE:{
				Object Obj = GetEventObject(Action->Parameters[0], User, Obj1, Obj2, *Temp);
				if(Obj == NONE){
					throw ERROR;
				}
				bool IsUseEvent = (EventType == MOVEUSE_EVENT_USE
						|| EventType == MOVEUSE_EVENT_MULTIUSE);
				Delete(Obj, (IsUseEvent ? 1 : -1));
				break;
			}

			case MOVEUSE_ACTION_DELETEININVENTORY:{
				Object Obj = GetEventObject(Action->Parameters[0], User, Obj1, Obj2, *Temp);
				ObjectType Type = Action->Parameters[1];
				int Value = Action->Parameters[2];
				if(Obj.getObjectType().isCreatureContainer()){
					DeleteAtCreature(Obj.getCreatureID(), Type, 1, Value);
				}else{
					throw ERROR;
				}
				break;
			}

			case MOVEUSE_ACTION_DESCRIPTION:{
				Object Obj = GetEventObject(Action->Parameters[0], User, Obj1, Obj2, *Temp);
				Object Cr = GetEventObject(Action->Parameters[1], User, Obj1, Obj2, *Temp);
				if(Cr.getObjectType().isCreatureContainer()){
					TCreature *Creature = GetCreature(Cr);
					if(Creature != NULL && Creature->Type == PLAYER){
						SendMessage(Creature->Connection, TALK_INFO_MESSAGE,
								"%s.", GetInfo(Obj));
					}
				}
				break;
			}

			case MOVEUSE_ACTION_LOADDEPOT:{
				int CoordX, CoordY, CoordZ;
				UnpackAbsoluteCoordinate(Action->Parameters[0], &CoordX, &CoordY, &CoordZ);
				ObjectType Type = Action->Parameters[1];
				Object Cr = GetEventObject(Action->Parameters[2], User, Obj1, Obj2, *Temp);
				int DepotNr = Action->Parameters[3];

				if(!Cr.getObjectType().isCreatureContainer()){
					throw ERROR;
				}

				Object Depot = GetFirstSpecObject(CoordX, CoordY, CoordZ, Type);
				if(Depot == NONE){
					throw ERROR;
				}

				LoadDepotBox(Cr.getCreatureID(), DepotNr, Depot);
				break;
			}

			case MOVEUSE_ACTION_SAVEDEPOT:{
				int CoordX, CoordY, CoordZ;
				UnpackAbsoluteCoordinate(Action->Parameters[0], &CoordX, &CoordY, &CoordZ);
				ObjectType Type = Action->Parameters[1];
				Object Cr = GetEventObject(Action->Parameters[2], User, Obj1, Obj2, *Temp);
				int DepotNr = Action->Parameters[3];

				if(!Cr.getObjectType().isCreatureContainer()){
					throw ERROR;
				}

				Object Depot = GetFirstSpecObject(CoordX, CoordY, CoordZ, Type);
				if(Depot == NONE){
					throw ERROR;
				}

				SaveDepotBox(Cr.getCreatureID(), DepotNr, Depot);
				break;
			}

			case MOVEUSE_ACTION_SENDMAIL:{
				Object Obj = GetEventObject(Action->Parameters[0], User, Obj1, Obj2, *Temp);
				SendMail(Obj);
				break;
			}

			case MOVEUSE_ACTION_NOP:{
				// no-op
				break;
			}

			default:{
				error("ExecuteAction: Unbekannte Aktion %d.\n", Action->Action);
				break;
			}
		}
	}catch(RESULT r){
		error("ExecuteAction: Exception %d (Aktion %d).\n", r, Action->Action);
	}
}

bool HandleEvent(MoveUseEventType EventType, Object User, Object Obj1, Object Obj2){
	static int RecursionDepth = 0;

	if(RecursionDepth > 10){
		error("HandleEvent: Endlosschleife im Move/Use-System vermutet.\n");
		return false;
	}

	bool Result = false;
	RecursionDepth += 1;
	TMoveUseDatabase *DB = &MoveUseDatabases[EventType];
	for(int RuleNr = 1; RuleNr <= DB->NumberOfRules; RuleNr += 1){
		bool Execute = true;
		Object Temp = NONE;
		TMoveUseRule *Rule = DB->Rules.at(RuleNr);
		for(int ConditionNr = Rule->FirstCondition;
				ConditionNr <= Rule->LastCondition;
				ConditionNr += 1){
			TMoveUseCondition *Condition = MoveUseConditions.at(ConditionNr);
			if(!CheckCondition(EventType, Condition, User, Obj1, Obj2, &Temp)){
				Execute = false;
				break;
			}
		}

		if(Execute){
			for(int ActionNr = Rule->FirstAction;
					ActionNr <= Rule->LastAction;
					ActionNr += 1){
				TMoveUseAction *Action = MoveUseActions.at(ActionNr);
				ExecuteAction(EventType, Action, User, Obj1, Obj2, &Temp);
			}
			Result = true;
			break;
		}
	}
	RecursionDepth -= 1;
	return Result;
}

// Event Dispatching
// =============================================================================
void UseContainer(uint32 CreatureID, Object Con, int NextContainerNr){
	if(!Con.exists()){
		error("UseContainer: Übergebenes Objekt existiert nicht.\n");
		throw ERROR;
	}

	ObjectType ConType = Con.getObjectType();
	if(!ConType.getFlag(CONTAINER)){
		error("UseContainer: Übergebenes Objekt ist kein Container.\n");
		throw ERROR;
	}

	if(NextContainerNr < 0 || NextContainerNr >= NARRAY(TPlayer::OpenContainer)){
		error("UseContainer: Ungültige Fensternummer %d.\n", NextContainerNr);
		throw ERROR;
	}

	if(ConType.isCreatureContainer()){
		throw NOTUSABLE;
	}

	TPlayer *Player = GetPlayer(CreatureID);
	if(Player == NULL){
		error("UseContainer: Spieler %d existiert nicht.\n", CreatureID);
		throw ERROR;
	}

	bool ContainerClosed = false;
	for(int ContainerNr = 0;
			ContainerNr < NARRAY(TPlayer::OpenContainer);
			ContainerNr += 1){
		if(Player->GetOpenContainer(ContainerNr) == Con){
			Player->SetOpenContainer(ContainerNr, NONE);
			SendCloseContainer(Player->Connection, ContainerNr);
			ContainerClosed = true;
			break;
		}
	}

	if(!ContainerClosed){
		Player->SetOpenContainer(NextContainerNr, Con);
		SendContainer(Player->Connection, NextContainerNr);
	}
}

void UseChest(uint32 CreatureID, Object Chest){
	if(!Chest.exists()){
		error("UseChest: Übergebenes Objekt existiert nicht.\n");
		throw ERROR;
	}

	ObjectType ChestType = Chest.getObjectType();
	if(!ChestType.getFlag(CHEST)){
		error("UseChest: Übergebenes Objekt ist keine Schatztruhe.\n");
		throw ERROR;
	}

	int ChestX, ChestY, ChestZ;
	GetObjectCoordinates(Chest, &ChestX, &ChestY, &ChestZ);

	int QuestNr = (int)Chest.getAttribute(CHESTQUESTNUMBER);
	if(QuestNr < 0 || QuestNr >= NARRAY(TPlayer::QuestValues)){
		error("UseChest: Ungültige Nummer %d auf Schatztruhe an Position [%d,%d,%d].\n",
				QuestNr, ChestX, ChestY, ChestZ);
		throw ERROR;
	}

	if(CountObjectsInContainer(Chest) != 1){
		error("UseChest: Schatztruhe auf Position [%d,%d,%d] enthält nicht genau ein Objekt.\n",
				ChestX, ChestY, ChestZ);
		throw ERROR;
	}

	Object Treasure = GetFirstContainerObject(Chest);
	ObjectType TreasureType = Treasure.getObjectType();
	if(TreasureType.getFlag(UNMOVE) || !TreasureType.getFlag(TAKE)){
		error("UseChest: Schatz auf Position [%d,%d,%d] ist nicht nehmbar.\n",
				ChestX, ChestY, ChestZ);
		throw ERROR;
	}

	TPlayer *Player = GetPlayer(CreatureID);
	if(Player == NULL){
		error("UseChest: Spieler %u existiert nicht.\n", CreatureID);
		throw ERROR;
	}

	if(Player->GetQuestValue(QuestNr) != 0){
		print(3, "Schatztruhe ist schon bekannt.\n");
		SendMessage(Player->Connection, TALK_INFO_MESSAGE,
				"The %s is empty.", ChestType.getName(-1));
		return;
	}

	if(!CheckRight(CreatureID, UNLIMITED_CAPACITY)){
		int TreasureWeight = GetCompleteWeight(Treasure);
		int InventoryWeight = GetInventoryWeight(CreatureID);
		int MaxWeight = Player->Skills[SKILL_CARRY_STRENGTH]->Get() * 100;
		if((InventoryWeight + TreasureWeight) > MaxWeight
				|| CheckRight(CreatureID, ZERO_CAPACITY)){
			bool Multiple = TreasureType.getFlag(CUMULATIVE)
					&& Treasure.getAttribute(AMOUNT) > 1;
			SendMessage(Player->Connection, TALK_INFO_MESSAGE,
					"You have found %s. Weighing %d.%02d oz %s too heavy.",
					GetName(Treasure), (TreasureWeight / 100), (TreasureWeight % 100),
					(Multiple ? "they are" : "it is"));
			return;
		}
	}

	// NOTE(fusion): Copy treasure object and attempt to move into the player's
	// inventory. The original version was somewhat confusing but this should
	// achieve the same end result.
	Treasure = Copy(Chest, Treasure);
	bool CheckContainers = TreasureType.getFlag(MOVEMENTEVENT);
	bool TreasureMoved = false;
	for(int i = 0; i < 2 && !TreasureMoved; i += 1){
		for(int Position = INVENTORY_FIRST;
				Position <= INVENTORY_LAST && !TreasureMoved;
				Position += 1){
			try{
				Object BodyObj = GetBodyObject(CreatureID, Position);
				if(CheckContainers){
					if(BodyObj != NONE && BodyObj.getObjectType().getFlag(CONTAINER)){
						Move(CreatureID, Treasure, BodyObj, -1, true, NONE);
						TreasureMoved = true;
					}
				}else{
					if(BodyObj == NONE){
						Object BodyCon = GetBodyContainer(CreatureID, Position);
						Move(CreatureID, Treasure, BodyCon, -1, true, NONE);
						TreasureMoved = true;
					}
				}
			}catch(RESULT r){
				// no-op
			}
		}
		CheckContainers = !CheckContainers;
	}

	if(!TreasureMoved){
		bool Multiple = TreasureType.getFlag(CUMULATIVE)
				&& Treasure.getAttribute(AMOUNT) > 1;
		SendMessage(Player->Connection, TALK_INFO_MESSAGE,
				"You have found %s, but you have no room to take %s.\n",
				GetName(Treasure), (Multiple ? "them" : "it"));
		Delete(Treasure, -1);
		return;
	}

	SendMessage(Player->Connection, TALK_INFO_MESSAGE,
			"You have found %s.\n", GetName(Treasure));
	Player->SetQuestValue(QuestNr, 1);
}

void UseLiquidContainer(uint32 CreatureID, Object Obj, Object Dest){
	if(!Obj.exists()){
		error("UseLiquidContainer: Übergebenes Objekt Obj existiert nicht.\n");
		throw ERROR;
	}

	ObjectType ObjType = Obj.getObjectType();
	if(!Dest.exists()){
		error("UseLiquidContainer: Übergebenes Objekt Dest existiert nicht (Obj %d).\n",
				ObjType.TypeID);
		throw ERROR;
	}

	if(!ObjType.getFlag(LIQUIDCONTAINER)){
		error("UseLiquidContainer: Übergebenes Objekt ist kein Flüssigkeitscontainer.\n");
		throw ERROR;
	}

	ObjectType DestType = Dest.getObjectType();
	int LiquidType = Obj.getAttribute(CONTAINERLIQUIDTYPE);

	// NOTE(fusion): Fill liquid container.
	if(DestType.getFlag(LIQUIDSOURCE) && LiquidType == LIQUID_NONE){
		int DestLiquidType = (int)DestType.getAttribute(SOURCELIQUIDTYPE);
		Change(Obj, CONTAINERLIQUIDTYPE, DestLiquidType);
		return;
	}

	// NOTE(fusion): Transfer liquid to another container.
	if(DestType.getFlag(LIQUIDCONTAINER) && LiquidType != LIQUID_NONE
			&& Dest.getAttribute(CONTAINERLIQUIDTYPE) == LIQUID_NONE){
		Change(Dest, CONTAINERLIQUIDTYPE, LiquidType);
		Change(Obj, CONTAINERLIQUIDTYPE, LIQUID_NONE);
		return;
	}

	// NOTE(fusion): This is similar to the target picking logic for runes except
	// we want to prioritize the user drinking the liquid, instead of otherwise
	// throwing it on the ground.
	if(!DestType.isCreatureContainer()){
		Object Help = GetFirstContainerObject(Dest.getContainer());
		while(Help != NONE){
			ObjectType HelpType = Help.getObjectType();
			if(HelpType.isCreatureContainer() && Help.getCreatureID() == CreatureID){
				Dest = Help;
				DestType = HelpType;
				break;
			}
			Help = Help.getNextObject();
		}
	}

	// NOTE(fusion): Spill liquid.
	if(!DestType.isCreatureContainer() || Dest.getCreatureID() != CreatureID){
		if(LiquidType == LIQUID_NONE){
			throw NOTUSABLE;
		}

		int DestX, DestY, DestZ;
		GetObjectCoordinates(Dest, &DestX, &DestY, &DestZ);
		if(!CoordinateFlag(DestX, DestY, DestZ, BANK)
		|| CoordinateFlag(DestX, DestY, DestZ, UNLAY)){
			throw NOROOM;
		}

		Change(Obj, CONTAINERLIQUIDTYPE, LIQUID_NONE);
		CreatePool(GetMapContainer(DestX, DestY, DestZ),
				GetSpecialObject(BLOOD_POOL), LiquidType);
		return;
	}

	// NOTE(fusion): Drink liquid.
	TPlayer *Player = GetPlayer(CreatureID);
	if(Player == NULL){
		error("UseLiquidContainer: Spieler %d existiert nicht.\n", CreatureID);
		throw ERROR;
	}

	switch(LiquidType){
		case LIQUID_NONE:{
			// no-op
			break;
		}

		case LIQUID_WINE:
		case LIQUID_BEER:{
			Change(Obj, CONTAINERLIQUIDTYPE, LIQUID_NONE);
			int DrunkLevel = Player->Skills[SKILL_DRUNKEN]->TimerValue();
			if(DrunkLevel < 5){
				Player->SetTimer(SKILL_DRUNKEN, (DrunkLevel + 1), 120, 120, -1);
			}
			Talk(CreatureID, TALK_SAY, NULL, "Aah...", false);
			break;
		}

		case LIQUID_SLIME:{
			Change(Obj, CONTAINERLIQUIDTYPE, LIQUID_NONE);
			Player->Damage(NULL, 200, DAMAGE_POISON_PERIODIC);
			Talk(CreatureID, TALK_SAY, NULL, "Urgh!", false);
			break;
		}

		case LIQUID_URINE:{
			Change(Obj, CONTAINERLIQUIDTYPE, LIQUID_NONE);
			Talk(CreatureID, TALK_SAY, NULL, "Urgh!", false);
			break;
		}

		case LIQUID_MANA:
		case LIQUID_LIFE:{
			DrinkPotion(CreatureID, Obj);
			Talk(CreatureID, TALK_SAY, NULL, "Aaaah...", false);
			break;
		}

		case LIQUID_LEMONADE:{
			Change(Obj, CONTAINERLIQUIDTYPE, LIQUID_NONE);
			Talk(CreatureID, TALK_SAY, NULL, "Mmmh.", false);
			break;
		}

		default:{
			Change(Obj, CONTAINERLIQUIDTYPE, LIQUID_NONE);
			Talk(CreatureID, TALK_SAY, NULL, "Gulp.", false);
			break;
		}
	}
}

void UseFood(uint32 CreatureID, Object Obj){
	if(!Obj.exists()){
		error("UseFood: Übergebenes Objekt existiert nicht.\n");
		throw ERROR;
	}

	ObjectType ObjType = Obj.getObjectType();
	if(!ObjType.getFlag(FOOD)){
		error("UseFood: Übergebenes Objekt ist kein Nahrungsmittel.\n");
		throw ERROR;
	}

	TPlayer *Player = GetPlayer(CreatureID);
	if(Player == NULL){
		error("UseFood: Kreatur %u existiert nicht.\n", CreatureID);
		throw ERROR;
	}

	int ObjFoodTime = (int)ObjType.getAttribute(NUTRITION) * 12;
	int CurFoodTime = Player->Skills[SKILL_FED]->TimerValue();
	int MaxFoodTime = Player->Skills[SKILL_FED]->Max;
	if((CurFoodTime + ObjFoodTime) > MaxFoodTime){
		throw FEDUP;
	}

	Player->SetTimer(SKILL_FED, (CurFoodTime + ObjFoodTime), 0, 0, -1);
	Delete(Obj, 1);
}

void UseTextObject(uint32 CreatureID, Object Obj){
	if(!Obj.exists()){
		error("UseTextObject: Übergebenes Objekt existiert nicht.\n");
		throw ERROR;
	}

	ObjectType ObjType = Obj.getObjectType();
	if(!ObjType.getFlag(TEXT)){
		error("UseTextObject: Übergebenes Objekt ist kein Schriftstück.\n");
		throw ERROR;
	}

	TPlayer *Player = GetPlayer(CreatureID);
	if(Player == NULL){
		error("UseTextObject: Kreatur %u existiert nicht.\n", CreatureID);
		throw ERROR;
	}

	SendEditText(Player->Connection, Obj);
}

void UseAnnouncer(uint32 CreatureID, Object Obj){
	if(!Obj.exists()){
		error("UseAnnouncer: Übergebenes Objekt existiert nicht.\n");
		throw ERROR;
	}

	ObjectType ObjType = Obj.getObjectType();
	if(!ObjType.getFlag(INFORMATION)){
		error("UseAnnouncer: Übergebenes Objekt liefert keine Informationen.\n");
		throw ERROR;
	}

	TPlayer *Player = GetPlayer(CreatureID);
	if(Player == NULL){
		error("UseAnnouncer: Kreatur %d existiert nicht.\n", CreatureID);
		throw ERROR;
	}

	int Information = (int)ObjType.getAttribute(INFORMATIONTYPE);
	switch(Information){
		case 1:{ // INFORMATION_DATETIME
			int Year, Cycle, Day, Hour, Minute;
			GetDate(&Year, &Cycle, &Day);
			GetTime(&Hour, &Minute);
			SendMessage(Player->Connection, TALK_INFO_MESSAGE,
					"It is the %dth day of the %dth cycle in the year %d. The time is %d:%.2d.",
					Day, Cycle, Year, Hour, Minute);
			break;
		}

		case 2:{ // INFORMATION_TIME
			int Hour, Minute;
			GetTime(&Hour, &Minute);
			SendMessage(Player->Connection, TALK_INFO_MESSAGE,
					"The time is %d:%.2d.", Hour, Minute);
			break;
		}

		case 3:{ // INFORMATION_BLESSINGS
			int NumBlessings = 0;
			char Help[256] = {};
			strcpy(Help, "Received blessings:");

			if(Player->GetQuestValue(101) != 0){
				strcat(Help, "\nWisdom of Solitude");
				NumBlessings += 1;
			}

			if(Player->GetQuestValue(102) != 0){
				strcat(Help, "\nSpark of the Phoenix");
				NumBlessings += 1;
			}

			if(Player->GetQuestValue(103) != 0){
				strcat(Help, "\nFire of the Suns");
				NumBlessings += 1;
			}

			if(Player->GetQuestValue(104) != 0){
				strcat(Help, "\nSpiritual Shielding");
				NumBlessings += 1;
			}

			if(Player->GetQuestValue(105) != 0){
				strcat(Help, "\nEmbrace of Tibia");
				NumBlessings += 1;
			}

			if(NumBlessings == 0){
				strcpy(Help, "No blessings received.");
			}

			SendMessage(Player->Connection, TALK_INFO_MESSAGE, "%s", Help);
			break;
		}

		case 4:{ // INFORMATION_SPELLBOOK
			SendEditText(Player->Connection, Obj);
			break;
		}

		default:{
			error("UseAnnouncer: Ungültiger Informationstyp %d.\n", Information);
			break;
		}
	}
}

void UseKeyDoor(uint32 CreatureID, Object Key, Object Door){
	if(!Key.exists()){
		error("UseKeyDoor: Übergebenes Objekt Key existiert nicht.\n");
		throw ERROR;
	}

	if(!Door.exists()){
		error("UseKeyDoor: Übergebenes Objekt Door existiert nicht.\n");
		throw ERROR;
	}

	ObjectType KeyType = Key.getObjectType();
	if(!KeyType.getFlag(KEY)){
		error("UseKeyDoor: Übergebenes Objekt ist kein Schlüssel.\n");
		throw ERROR;
	}

	ObjectType DoorType = Door.getObjectType();
	if(!DoorType.getFlag(KEYDOOR)){
		throw NOTUSABLE;
	}

	int KeyNumber = (int)Key.getAttribute(KEYNUMBER);
	int KeyHoleNumber = (int)Door.getAttribute(KEYHOLENUMBER);
	if(KeyNumber == 0 || KeyHoleNumber == 0 || KeyNumber != KeyHoleNumber){
		throw NOKEYMATCH;
	}

	ObjectType KeyDoorTarget = (int)DoorType.getAttribute(KEYDOORTARGET);
	if(KeyDoorTarget.isMapContainer()){
		error("UseKeyDoor: Zieltür für Tür %d nicht spezifiziert.\n",
				DoorType.TypeID);
		throw ERROR;
	}

	if(KeyDoorTarget.getFlag(UNPASS)){
		ClearField(Door, NONE);
	}

	Change(Door, KeyDoorTarget, 0);
}

void UseNameDoor(uint32 CreatureID, Object Door){
	if(!Door.exists()){
		error("UseNameDoor: Übergebenes Objekt Door existiert nicht.\n");
		throw ERROR;
	}

	ObjectType DoorType = Door.getObjectType();
	if(!DoorType.getFlag(NAMEDOOR)){
		error("UseNameDoor: Übergebenes Objekt Door ist keine beschriftete Tür.\n");
		throw ERROR;
	}

	if(!DoorType.getFlag(TEXT)){
		error("UseNameDoor: Übergebenes Objekt Door trägt keinen Text.\n");
		throw ERROR;
	}

	TPlayer *Player = GetPlayer(CreatureID);
	if(Player == NULL){
		error("UseNameDoor: Spieler existiert nicht.\n");
		throw ERROR;
	}

	int DoorX, DoorY, DoorZ;
	GetObjectCoordinates(Door, &DoorX, &DoorY, &DoorZ);

	uint16 HouseID = GetHouseID(DoorX, DoorY, DoorZ);
	if(HouseID == 0){
		error("UseNameDoor: Koordinate [%d,%d,%d] gehört zu keinem Haus.\n",
				DoorX, DoorY, DoorZ);
		throw ERROR;
	}

	if(!IsOwner(HouseID, Player)
			&& !CheckRight(CreatureID, OPEN_NAMEDOORS)
			&& !MayOpenDoor(Door, Player)){
		throw NOTACCESSIBLE;
	}

	ObjectType NameDoorTarget = (int)DoorType.getAttribute(NAMEDOORTARGET);
	if(NameDoorTarget.isMapContainer()){
		error("UseNameDoor: Zieltür für Tür %d nicht spezifiziert.\n",
				DoorType.TypeID);
		throw ERROR;
	}

	if(NameDoorTarget.getFlag(UNPASS)){
		ClearField(Door, NONE);
	}

	Change(Door, NameDoorTarget, 0);
}

void UseLevelDoor(uint32 CreatureID, Object Door){
	if(!Door.exists() || !Door.getObjectType().getFlag(LEVELDOOR)
			|| !Door.getContainer().getObjectType().isMapContainer()){
		error("UseLevelDoor: Übergebenes Objekt Door ist keine Level-Tür.\n");
		throw ERROR;
	}

	TPlayer *Player = GetPlayer(CreatureID);
	if(Player == NULL){
		error("UseLevelDoor: Spieler existiert nicht.\n");
		throw ERROR;
	}

	ObjectType DoorType = Door.getObjectType();
	if(!DoorType.getFlag(UNPASS)){
		throw NOTUSABLE;
	}

	int DoorLevel = (int)Door.getAttribute(DOORLEVEL);
	int PlayerLevel = Player->Skills[SKILL_LEVEL]->Get();
	if(PlayerLevel < DoorLevel){
		SendMessage(Player->Connection, TALK_INFO_MESSAGE, "%s.", GetInfo(Door));
		return;
	}

	ObjectType LevelDoorTarget = (int)DoorType.getAttribute(LEVELDOORTARGET);
	if(LevelDoorTarget.isMapContainer() || LevelDoorTarget.getFlag(UNPASS)){
		error("UseLevelDoor: Zieltür für Tür %d nicht spezifiziert oder nicht passierbar.\n",
				DoorType.TypeID);
		throw ERROR;
	}

	Change(Door, LevelDoorTarget, 0);
	Move(0, Player->CrObject, Door.getContainer(), -1, false, NONE);
}

void UseQuestDoor(uint32 CreatureID, Object Door){
	if(!Door.exists() || !Door.getObjectType().getFlag(QUESTDOOR)
			|| !Door.getContainer().getObjectType().isMapContainer()){
		error("UseQuestDoor: Übergebenes Objekt Door ist keine Quest-Tür.\n");
		throw ERROR;
	}

	TPlayer *Player = GetPlayer(CreatureID);
	if(Player == NULL){
		error("UseQuestDoor: Spieler existiert nicht.\n");
		throw ERROR;
	}

	ObjectType DoorType = Door.getObjectType();
	if(!DoorType.getFlag(UNPASS)){
		throw NOTUSABLE;
	}

	int QuestNr = Door.getAttribute(DOORQUESTNUMBER);
	int QuestValue = Door.getAttribute(DOORQUESTVALUE);
	if(Player->GetQuestValue(QuestNr) != QuestValue){
		SendMessage(Player->Connection, TALK_INFO_MESSAGE, "%s.", GetInfo(Door));
		return;
	}

	ObjectType QuestDoorTarget = (int)DoorType.getAttribute(QUESTDOORTARGET);
	if(QuestDoorTarget.isMapContainer() || QuestDoorTarget.getFlag(UNPASS)){
		error("UseQuestDoor: Zieltür für Tür %d nicht spezifiziert oder nicht passierbar.\n",
				DoorType.TypeID);
		throw ERROR;
	}

	Change(Door, QuestDoorTarget, 0);
	Move(0, Player->CrObject, Door.getContainer(), -1, false, NONE);
}

void UseWeapon(uint32 CreatureID, Object Weapon, Object Target){
	if(!Weapon.exists() || !Weapon.getObjectType().isCloseWeapon()){
		error("UseWeapon: Übergebene Waffe existiert nicht oder ist keine Waffe.\n");
		throw ERROR;
	}

	if(!Target.exists() || !Target.getObjectType().getFlag(DESTROY)){
		error("UseWeapon: Übergebenes Ziel existiert nicht oder ist nicht zerstörbar.\n");
		throw ERROR;
	}

	TPlayer *Player = GetPlayer(CreatureID);
	if(Player == NULL){
		error("UseWeapon: Spieler %d existiert nicht.\n", CreatureID);
		throw ERROR;
	}

	if(!Target.getContainer().getObjectType().isMapContainer()){
		throw NOTACCESSIBLE;
	}

	GraphicalEffect(Target, EFFECT_POFF);
	if(random(1, 3) == 1){
		// TODO(fusion): This is very similar to what is done in `ProcessCronSystem`
		// and it is probably some inlined helper function to transform items that
		// might be containers.
		ObjectType TargetType = Target.getObjectType();
		ObjectType DestroyTarget = (int)TargetType.getAttribute(DESTROYTARGET);
		if(TargetType.getFlag(CONTAINER)){
			int Remainder = 0;
			if(!DestroyTarget.isMapContainer() && DestroyTarget.getFlag(CONTAINER)){
				Remainder = (int)DestroyTarget.getAttribute(CAPACITY);
			}

			Empty(Target, Remainder);
		}
		Change(Target, DestroyTarget, 0);
	}
}

void UseChangeObject(uint32 CreatureID, Object Obj){
	if(!Obj.exists() || !Obj.getObjectType().getFlag(CHANGEUSE)){
		error("UseChangeObject: Objekt existiert nicht oder ist kein CHANGEUSE-Objekt.\n");
		throw ERROR;
	}

	TPlayer *Player = GetPlayer(CreatureID);
	if(Player == NULL){
		error("UseChangeObject: Spieler %d existiert nicht.\n", CreatureID);
		throw ERROR;
	}

	ObjectType ObjType = Obj.getObjectType();
	ObjectType ChangeTarget = (int)ObjType.getAttribute(CHANGETARGET);
	if(!ObjType.getFlag(UNPASS)
			&& !ChangeTarget.isMapContainer()
			&& ChangeTarget.getFlag(UNPASS)){
		ClearField(Obj, NONE);
	}else if(!ObjType.getFlag(UNLAY)
			&& !ChangeTarget.isMapContainer()
			&& ChangeTarget.getFlag(UNLAY)){
		// TODO(fusion): This is similar to `ClearField` except we look for an
		// field without `UNLAY`. It is probably an inlined function.
		int ObjX, ObjY, ObjZ;
		GetObjectCoordinates(Obj, &ObjX, &ObjY, &ObjZ);

		int OffsetX[4] = { 1,  0, -1,  0};
		int OffsetY[4] = { 0,  1,  0, -1};
		for(int i = 0; i < 4; i += 1){
			int DestX = ObjX + OffsetX[i];
			int DestY = ObjY + OffsetY[i];
			int DestZ = ObjZ;
			if(CoordinateFlag(DestX, DestY, DestZ, BANK)
			&& !CoordinateFlag(DestX, DestY, DestZ, UNPASS)){
				Object Dest = GetMapContainer(DestX, DestY, DestZ);
				MoveAllObjects(Obj.getNextObject(), Dest, NONE, true);
				break;
			}
		}
	}

	Change(Obj, ChangeTarget, 0);
}

void UseObject(uint32 CreatureID, Object Obj){
	if(!Obj.exists()){
		error("UseObjects: Übergebenes Objekt existiert nicht.\n");
		throw ERROR;
	}

	if(!Obj.getObjectType().getFlag(USEEVENT)){
		error("UseObjects: Übergebenes Objekt ist nicht benutzbar.\n");
		throw ERROR;
	}

	Object User = NONE;
	if(CreatureID != 0){
		TCreature *Creature = GetCreature(CreatureID);
		if(Creature != NULL){
			User = Creature->CrObject;
		}
	}

	if(!HandleEvent(MOVEUSE_EVENT_USE, User, Obj, NONE)){
		throw NOTUSABLE;
	}
}

void UseObjects(uint32 CreatureID, Object Obj1, Object Obj2){
	if(!Obj1.exists()){
		error("UseObjects: Übergebenes Objekt Obj1 existiert nicht.\n");
		throw ERROR;
	}

	if(!Obj2.exists()){
		error("UseObjects: Übergebenes Objekt Obj2 existiert nicht.\n");
		throw ERROR;
	}

	if(!Obj1.getObjectType().getFlag(USEEVENT)){
		error("UseObjects: Übergebenes Objekt Obj1 ist nicht benutzbar.\n");
		throw ERROR;
	}

	Object User = NONE;
	if(CreatureID != 0){
		TCreature *Creature = GetCreature(CreatureID);
		if(Creature != NULL){
			User = Creature->CrObject;
		}
	}

	if(!HandleEvent(MOVEUSE_EVENT_MULTIUSE, User, Obj1, Obj2)){
		throw NOTUSABLE;
	}
}

void MovementEvent(Object Obj, Object Start, Object Dest){
	if(!Obj.exists()){
		error("MovementEvent: Übergebenes Objekt existiert nicht.\n");
		throw ERROR;
	}

	ObjectType StartType = Start.getObjectType();
	if(!Start.exists() || (!StartType.getFlag(CONTAINER) && !StartType.getFlag(CHEST))){
		error("MovementEvent: \"Start\" ist kein Container.\n");
		throw ERROR;
	}

	ObjectType DestType = Dest.getObjectType();
	if(!Dest.exists() || (!DestType.getFlag(CONTAINER) && !DestType.getFlag(CHEST))){
		error("MovementEvent: \"Dest\" ist kein Container.\n");
		throw ERROR;
	}

	if(Obj.getObjectType().getFlag(MOVEMENTEVENT)){
		HandleEvent(MOVEUSE_EVENT_MOVEMENT, NONE, Obj, NONE);
		if(!Obj.exists()){
			throw DESTROYED;
		}
	}
}

void SeparationEvent(Object Obj, Object Start){
	if(!Obj.exists()){
		error("SeparationEvent: Übergebenes Objekt existiert nicht.\n");
		throw ERROR;
	}

	if(!Start.exists()){
		error("SeparationEvent: Übergebener Container existiert nicht.\n");
		throw ERROR;
	}

	ObjectType StartType = Start.getObjectType();
	if(!StartType.isMapContainer()){
		return;
	}

	// NOTE(fusion): Separation event for `Obj` against objects at `Start`.
	ObjectType ObjType = Obj.getObjectType();
	if(ObjType.getFlag(SEPARATIONEVENT)){
		Object Help = GetFirstContainerObject(Start);
		while(Help != NONE){
			Object Next = Help.getNextObject();
			if(Help != Obj){
				HandleEvent(MOVEUSE_EVENT_SEPARATION, NONE, Obj, Help);
				if(!Obj.exists()){
					throw DESTROYED;
				}
			}
			Help = Next;
		}
	}

	// NOTE(fusion): Separation event for objects at `Start` against `Obj`.
	Object Help = GetFirstContainerObject(Start);
	while(Help != NONE){
		Object Next = Help.getNextObject();
		if(Help != Obj){
			ObjectType HelpType = Help.getObjectType();
			if(ObjType.isCreatureContainer()
					&& (HelpType.getFlag(LEVELDOOR) || HelpType.getFlag(QUESTDOOR))){
				ObjectType DoorTarget = HelpType.getFlag(LEVELDOOR)
						? (int)HelpType.getAttribute(LEVELDOORTARGET)
						: (int)HelpType.getAttribute(QUESTDOORTARGET);
				if(DoorTarget.isMapContainer() || !DoorTarget.getFlag(UNPASS)){
					error("SeparationEvent: Zieltür für Tür %d nicht spezifiziert oder passierbar.\n",
							HelpType.TypeID);
					throw ERROR;
				}

				ClearField(Help, Obj);
				Change(Help, DoorTarget, 0);
				break;
			}

			if(HelpType.getFlag(SEPARATIONEVENT)){
				HandleEvent(MOVEUSE_EVENT_SEPARATION, NONE, Help, Obj);
				if(!Obj.exists()){
					throw DESTROYED;
				}
			}
		}
		Help = Next;
	}
}

void CollisionEvent(Object Obj, Object Dest){
	if(!Obj.exists()){
		error("CollisionEvent: Übergebenes Objekt existiert nicht.\n");
		throw ERROR;
	}

	if(!Dest.exists()){
		error("CollisionEvent: Übergebener Container existiert nicht.\n");
		throw ERROR;
	}

	ObjectType DestType = Dest.getObjectType();
	if(!DestType.isMapContainer()){
		return;
	}

	ObjectType ObjType = Obj.getObjectType();
	if(ObjType.getFlag(TELEPORTABSOLUTE) || ObjType.getFlag(TELEPORTRELATIVE)){
		int TeleportEffect;
		int TeleportDestX, TeleportDestY, TeleportDestZ;
		if(ObjType.getFlag(TELEPORTABSOLUTE)){
			UnpackAbsoluteCoordinate((int)Obj.getAttribute(ABSTELEPORTDESTINATION),
									&TeleportDestX, &TeleportDestY, &TeleportDestZ);
			TeleportEffect = (int)ObjType.getAttribute(ABSTELEPORTEFFECT);
		}else{
			int DisplacementX, DisplacementY, DisplacementZ;
			GetObjectCoordinates(Obj, &TeleportDestX, &TeleportDestY, &TeleportDestZ);
			UnpackRelativeCoordinate((int)ObjType.getAttribute(RELTELEPORTDISPLACEMENT),
									&DisplacementX, &DisplacementY, &DisplacementZ);
			TeleportDestX += DisplacementX;
			TeleportDestY += DisplacementY;
			TeleportDestZ += DisplacementZ;
			TeleportEffect = (int)ObjType.getAttribute(RELTELEPORTEFFECT);
		}

		Object TeleportDest = GetMapContainer(TeleportDestX, TeleportDestY, TeleportDestZ);
		MoveAllObjects(Obj.getNextObject(), TeleportDest, NONE, true);
		GraphicalEffect(TeleportDestX, TeleportDestY, TeleportDestZ, TeleportEffect);

		// TODO(fusion): Is this even possible? Maybe `MoveAllObjects` could
		// trigger a separation event that destroys `Obj`?
		if(!Obj.exists()){
			throw DESTROYED;
		}
	}else{
		// NOTE(fusion): Collision event for `Obj` against objects at `Dest`.
		if(ObjType.getFlag(COLLISIONEVENT)){
			Object Help = GetFirstContainerObject(Dest);
			while(Help != NONE){
				Object Next = Help.getNextObject();

				// IMPORTANT(fusion): Same as below.
				if(Next == Obj){
					Next = Next.getNextObject();
				}

				if(Help != Obj){
					HandleEvent(MOVEUSE_EVENT_COLLISION, NONE, Obj, Help);
					if(!Obj.exists()){
						throw DESTROYED;
					}
				}

				Help = Next;
			}
		}

		// NOTE(fusion): Collision event for objects at `Dest` against `Obj`.
		Object Help = GetFirstContainerObject(Dest);
		while(Help != NONE){
			Object Next = Help.getNextObject();

			// IMPORTANT(fusion): This is very subtle but the collision event can
			// move or destroy either object, causing it to be removed from the
			// container list while we're still iterating it.
			//  It won't be a problem most of the time, but if Next happens to be
			// Obj, and it gets moved, we could end up iterating the remainder of
			// whatever list Obj now belongs to, causing side effects like double
			// collision effects, etc...
			//  Note that this cannot happen to Help simply because it is already
			// behind the Next pointer, so checking only for Obj here is enough.
			if(Next == Obj){
				Next = Next.getNextObject();
			}

			if(Help != Obj){
				ObjectType HelpType = Help.getObjectType();
				if(HelpType.getFlag(TELEPORTABSOLUTE) || HelpType.getFlag(TELEPORTRELATIVE)){
					int TeleportEffect;
					int TeleportDestX, TeleportDestY, TeleportDestZ;
					if(HelpType.getFlag(TELEPORTABSOLUTE)){
						UnpackAbsoluteCoordinate((int)Help.getAttribute(ABSTELEPORTDESTINATION),
												&TeleportDestX, &TeleportDestY, &TeleportDestZ);
						TeleportEffect = (int)HelpType.getAttribute(ABSTELEPORTEFFECT);
					}else{
						int DisplacementX, DisplacementY, DisplacementZ;
						GetObjectCoordinates(Help, &TeleportDestX, &TeleportDestY, &TeleportDestZ);
						UnpackRelativeCoordinate((int)HelpType.getAttribute(RELTELEPORTDISPLACEMENT),
												&DisplacementX, &DisplacementY, &DisplacementZ);
						TeleportDestX += DisplacementX;
						TeleportDestY += DisplacementY;
						TeleportDestZ += DisplacementZ;
						TeleportEffect = (int)HelpType.getAttribute(RELTELEPORTEFFECT);
					}

					Object TeleportDest = GetMapContainer(TeleportDestX, TeleportDestY, TeleportDestZ);
					MoveOneObject(Obj, TeleportDest);
					GraphicalEffect(TeleportDestX, TeleportDestY, TeleportDestZ, TeleportEffect);
				}else if(HelpType.getFlag(COLLISIONEVENT)){
					HandleEvent(MOVEUSE_EVENT_COLLISION, NONE, Help, Obj);
				}

				if(!Obj.exists()){
					throw DESTROYED;
				}
			}

			Help = Next;
		}
	}
}

// Event Loading / Initialization
// =============================================================================
void LoadParameters(TReadScriptFile *Script, int *Parameters, int NumberOfParameters, ...){
	if(NumberOfParameters > MOVEUSE_MAX_PARAMETERS){
		error("LoadParameters: Zu viele Parameter (%d).\n", NumberOfParameters);
		Script->error("too many parameters");
	}

	if(NumberOfParameters == 0){
		return;
	}

	if(Script->Token != SPECIAL || Script->getSpecial() != '('){
		Script->error("'(' expected");
	}

	va_list ap;
	va_start(ap, NumberOfParameters);
	for(int i = 0; i < NumberOfParameters; i += 1){
		if(i > 0){
			Script->readSymbol(',');
		}

		int ParamType = va_arg(ap, int);
		switch(ParamType){
			case MOVEUSE_PARAMETER_OBJECT:{
				const char *Object = Script->readIdentifier();
				if(strcmp(Object, "null") == 0){
					Parameters[i] = 0; // MOVEUSE_OBJECT_NULL ?
				}else if(strcmp(Object, "obj1") == 0){
					Parameters[i] = 1; // MOVEUSE_OBJECT_1 ?
				}else if(strcmp(Object, "obj2") == 0){
					Parameters[i] = 2; // MOVEUSE_OBJECT_2 ?
				}else if(strcmp(Object, "user") == 0){
					Parameters[i] = 3; // MOVEUSE_OBJECT_USER ?
				}else if(strcmp(Object, "temp") == 0){
					Parameters[i] = 4; // MOVEUSE_OBJECT_TEMP ?
				}else{
					Script->error("Object expected");
				}
				break;
			}

			case MOVEUSE_PARAMETER_TYPE:{
				int TypeID = Script->readNumber();
				Parameters[i] = TypeID;
				if(!ObjectTypeExists(TypeID)){
					Script->error("Unknown object type");
				}
				break;
			}

			case MOVEUSE_PARAMETER_FLAG:{
				int Flag = GetFlagByName(Script->readIdentifier());
				Parameters[i] = Flag;
				if(Flag == -1){
					Script->error("Unknown flag");
				}
				break;
			}

			case MOVEUSE_PARAMETER_TYPEATTRIBUTE:{
				int TypeAttribute = GetTypeAttributeByName(Script->readIdentifier());
				Parameters[i] = TypeAttribute;
				if(TypeAttribute == -1){
					Script->error("Unknown type attribute");
				}
				break;
			}

			case MOVEUSE_PARAMETER_INSTANCEATTRIBUTE:{
				int InstanceAttribute = GetInstanceAttributeByName(Script->readIdentifier());
				Parameters[i] = InstanceAttribute;
				if(InstanceAttribute == -1){
					Script->error("Unknown instance attribute");
				}
				break;
			}

			case MOVEUSE_PARAMETER_COORDINATE:{
				int AbsX, AbsY, AbsZ;
				Script->readCoordinate(&AbsX, &AbsY, &AbsZ);
				Parameters[i] = PackAbsoluteCoordinate(AbsX, AbsY, AbsZ);
				break;
			}

			case MOVEUSE_PARAMETER_VECTOR:{
				int RelX, RelY, RelZ;
				Script->readCoordinate(&RelX, &RelY, &RelZ);
				Parameters[i] = PackRelativeCoordinate(RelX, RelY, RelZ);
				break;
			}

			case MOVEUSE_PARAMETER_RIGHT:{
				const char *Right = Script->readIdentifier();
				if(strcmp(Right, "premium_account") == 0){
					Parameters[i] = PREMIUM_ACCOUNT;
				}else if(strcmp(Right, "special_moveuse") == 0){
					Parameters[i] = SPECIAL_MOVEUSE;
				}else{
					Script->error("Unknown right");
				}
				break;
			}

			case MOVEUSE_PARAMETER_SKILL:{
				int SkillNr = GetSkillByName(Script->readIdentifier());
				Parameters[i] = SkillNr;
				if(SkillNr == -1){
					Script->error("Unknown skill");
				}
				break;
			}

			case MOVEUSE_PARAMETER_NUMBER:{
				Parameters[i] = Script->readNumber();
				break;
			}

			case MOVEUSE_PARAMETER_TEXT:{
				const char *Text = Script->readString();
				Parameters[i] = AddDynamicString(Text);
				break;
			}

			case MOVEUSE_PARAMETER_COMPARISON:{
				int Operator = (int)Script->readSpecial();
				Parameters[i] = Operator;
				if(strchr("<=>GLN", Operator) == NULL){
					Script->error("Unknown comparison operator");
				}
				break;
			}
		}
	}
	va_end(ap);

	Script->readSymbol(')');
	Script->nextToken();
}

void LoadCondition(TReadScriptFile *Script, TMoveUseCondition *Condition){
	char Identifier[MAX_IDENT_LENGTH];
	strcpy(Identifier, Script->getIdentifier());

	Script->nextToken();
	if(strcmp(Identifier, "isposition") == 0){
		Condition->Condition = MOVEUSE_CONDITION_ISPOSITION;
		LoadParameters(Script, Condition->Parameters, 2,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_COORDINATE);
	}else if(strcmp(Identifier, "istype") == 0){
		Condition->Condition = MOVEUSE_CONDITION_ISTYPE;
		LoadParameters(Script, Condition->Parameters, 2,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_TYPE);
	}else if(strcmp(Identifier, "iscreature") == 0){
		Condition->Condition = MOVEUSE_CONDITION_ISCREATURE;
		LoadParameters(Script, Condition->Parameters, 1,
								MOVEUSE_PARAMETER_OBJECT);
	}else if(strcmp(Identifier, "isplayer") == 0){
		Condition->Condition = MOVEUSE_CONDITION_ISPLAYER;
		LoadParameters(Script, Condition->Parameters, 1,
								MOVEUSE_PARAMETER_OBJECT);
	}else if(strcmp(Identifier, "hasflag") == 0){
		Condition->Condition = MOVEUSE_CONDITION_HASFLAG;
		LoadParameters(Script, Condition->Parameters, 2,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_FLAG);
	}else if(strcmp(Identifier, "hastypeattribute") == 0){
		Condition->Condition = MOVEUSE_CONDITION_HASTYPEATTRIBUTE;
		LoadParameters(Script, Condition->Parameters, 4,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_TYPEATTRIBUTE,
								MOVEUSE_PARAMETER_COMPARISON,
								MOVEUSE_PARAMETER_NUMBER);
	}else if(strcmp(Identifier, "hasinstanceattribute") == 0){
		Condition->Condition = MOVEUSE_CONDITION_HASINSTANCEATTRIBUTE;
		LoadParameters(Script, Condition->Parameters, 4,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_INSTANCEATTRIBUTE,
								MOVEUSE_PARAMETER_COMPARISON,
								MOVEUSE_PARAMETER_NUMBER);
	}else if(strcmp(Identifier, "hastext") == 0){
		Condition->Condition = MOVEUSE_CONDITION_HASTEXT;
		LoadParameters(Script, Condition->Parameters, 2,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_TEXT);
	}else if(strcmp(Identifier, "ispeaceful") == 0){
		Condition->Condition = MOVEUSE_CONDITION_ISPEACEFUL;
		LoadParameters(Script, Condition->Parameters, 1,
								MOVEUSE_PARAMETER_OBJECT);
	}else if(strcmp(Identifier, "maylogout") == 0){
		Condition->Condition = MOVEUSE_CONDITION_MAYLOGOUT;
		LoadParameters(Script, Condition->Parameters, 1,
								MOVEUSE_PARAMETER_OBJECT);
	}else if(strcmp(Identifier, "hasprofession") == 0){
		Condition->Condition = MOVEUSE_CONDITION_HASPROFESSION;
		LoadParameters(Script, Condition->Parameters, 2,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_NUMBER);
	}else if(strcmp(Identifier, "haslevel") == 0){
		Condition->Condition = MOVEUSE_CONDITION_HASLEVEL;
		LoadParameters(Script, Condition->Parameters, 3,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_COMPARISON,
								MOVEUSE_PARAMETER_NUMBER);
	}else if(strcmp(Identifier, "hasright") == 0){
		Condition->Condition = MOVEUSE_CONDITION_HASRIGHT;
		LoadParameters(Script, Condition->Parameters, 2,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_RIGHT);
	}else if(strcmp(Identifier, "hasquestvalue") == 0){
		Condition->Condition = MOVEUSE_CONDITION_HASQUESTVALUE;
		LoadParameters(Script, Condition->Parameters, 4,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_NUMBER,
								MOVEUSE_PARAMETER_COMPARISON,
								MOVEUSE_PARAMETER_NUMBER);
	}else if(strcmp(Identifier, "testskill") == 0){
		Condition->Condition = MOVEUSE_CONDITION_TESTSKILL;
		LoadParameters(Script, Condition->Parameters, 4,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_SKILL,
								MOVEUSE_PARAMETER_NUMBER,
								MOVEUSE_PARAMETER_NUMBER);
	}else if(strcmp(Identifier, "countobjects") == 0){
		Condition->Condition = MOVEUSE_CONDITION_COUNTOBJECTS;
		LoadParameters(Script, Condition->Parameters, 3,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_COMPARISON,
								MOVEUSE_PARAMETER_NUMBER);
	}else if(strcmp(Identifier, "countobjectsonmap") == 0){
		Condition->Condition = MOVEUSE_CONDITION_COUNTOBJECTSONMAP;
		LoadParameters(Script, Condition->Parameters, 3,
								MOVEUSE_PARAMETER_COORDINATE,
								MOVEUSE_PARAMETER_COMPARISON,
								MOVEUSE_PARAMETER_NUMBER);
	}else if(strcmp(Identifier, "isobjectthere") == 0){
		Condition->Condition = MOVEUSE_CONDITION_ISOBJECTTHERE;
		LoadParameters(Script, Condition->Parameters, 2,
								MOVEUSE_PARAMETER_COORDINATE,
								MOVEUSE_PARAMETER_TYPE);
	}else if(strcmp(Identifier, "iscreaturethere") == 0){
		Condition->Condition = MOVEUSE_CONDITION_ISCREATURETHERE;
		LoadParameters(Script, Condition->Parameters, 1,
								MOVEUSE_PARAMETER_COORDINATE);
	}else if(strcmp(Identifier, "isplayerthere") == 0){
		Condition->Condition = MOVEUSE_CONDITION_ISPLAYERTHERE;
		LoadParameters(Script, Condition->Parameters, 1,
								MOVEUSE_PARAMETER_COORDINATE);
	}else if(strcmp(Identifier, "isobjectininventory") == 0){
		Condition->Condition = MOVEUSE_CONDITION_ISOBJECTININVENTORY;
		LoadParameters(Script, Condition->Parameters, 3,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_TYPE,
								MOVEUSE_PARAMETER_NUMBER);
	}else if(strcmp(Identifier, "isprotectionzone") == 0){
		Condition->Condition = MOVEUSE_CONDITION_ISPROTECTIONZONE;
		LoadParameters(Script, Condition->Parameters, 1,
								MOVEUSE_PARAMETER_OBJECT);
	}else if(strcmp(Identifier, "ishouse") == 0){
		Condition->Condition = MOVEUSE_CONDITION_ISHOUSE;
		LoadParameters(Script, Condition->Parameters, 1,
								MOVEUSE_PARAMETER_OBJECT);
	}else if(strcmp(Identifier, "ishouseowner") == 0){
		Condition->Condition = MOVEUSE_CONDITION_ISHOUSEOWNER;
		LoadParameters(Script, Condition->Parameters, 2,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_OBJECT);
	}else if(strcmp(Identifier, "isdressed") == 0){
		Condition->Condition = MOVEUSE_CONDITION_ISDRESSED;
		LoadParameters(Script, Condition->Parameters, 1,
								MOVEUSE_PARAMETER_OBJECT);
	}else if(strcmp(Identifier, "random") == 0){
		Condition->Condition = MOVEUSE_CONDITION_RANDOM;
		LoadParameters(Script, Condition->Parameters, 1,
								MOVEUSE_PARAMETER_NUMBER);
	}else{
		Script->error("invalid condition");
	}
}

void LoadAction(TReadScriptFile *Script, TMoveUseAction *Action){
	char Identifier[MAX_IDENT_LENGTH];
	strcpy(Identifier, Script->getIdentifier());

	Script->nextToken();
	if(strcmp(Identifier, "createonmap") == 0){
		Action->Action = MOVEUSE_ACTION_CREATEONMAP;
		LoadParameters(Script, Action->Parameters, 3,
								MOVEUSE_PARAMETER_COORDINATE,
								MOVEUSE_PARAMETER_TYPE,
								MOVEUSE_PARAMETER_NUMBER);
	}else if(strcmp(Identifier, "create") == 0){
		Action->Action = MOVEUSE_ACTION_CREATE;
		LoadParameters(Script, Action->Parameters, 3,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_TYPE,
								MOVEUSE_PARAMETER_NUMBER);
	}else if(strcmp(Identifier, "monsteronmap") == 0){
		Action->Action = MOVEUSE_ACTION_MONSTERONMAP;
		LoadParameters(Script, Action->Parameters, 2,
								MOVEUSE_PARAMETER_COORDINATE,
								MOVEUSE_PARAMETER_NUMBER);
	}else if(strcmp(Identifier, "monster") == 0){
		Action->Action = MOVEUSE_ACTION_MONSTER;
		LoadParameters(Script, Action->Parameters, 2,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_NUMBER);
	}else if(strcmp(Identifier, "effectonmap") == 0){
		Action->Action = MOVEUSE_ACTION_EFFECTONMAP;
		LoadParameters(Script, Action->Parameters, 2,
								MOVEUSE_PARAMETER_COORDINATE,
								MOVEUSE_PARAMETER_NUMBER);
	}else if(strcmp(Identifier, "effect") == 0){
		Action->Action = MOVEUSE_ACTION_EFFECT;
		LoadParameters(Script, Action->Parameters, 2,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_NUMBER);
	}else if(strcmp(Identifier, "textonmap") == 0){
		Action->Action = MOVEUSE_ACTION_TEXTONMAP;
		LoadParameters(Script, Action->Parameters, 3,
								MOVEUSE_PARAMETER_COORDINATE,
								MOVEUSE_PARAMETER_TEXT,
								MOVEUSE_PARAMETER_NUMBER);
	}else if(strcmp(Identifier, "text") == 0){
		Action->Action = MOVEUSE_ACTION_TEXT;
		LoadParameters(Script, Action->Parameters, 3,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_TEXT,
								MOVEUSE_PARAMETER_NUMBER);
	}else if(strcmp(Identifier, "changeonmap") == 0){
		Action->Action = MOVEUSE_ACTION_CHANGEONMAP;
		LoadParameters(Script, Action->Parameters, 4,
								MOVEUSE_PARAMETER_COORDINATE,
								MOVEUSE_PARAMETER_TYPE,
								MOVEUSE_PARAMETER_TYPE,
								MOVEUSE_PARAMETER_NUMBER);
	}else if(strcmp(Identifier, "change") == 0){
		Action->Action = MOVEUSE_ACTION_CHANGE;
		LoadParameters(Script, Action->Parameters, 3,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_TYPE,
								MOVEUSE_PARAMETER_NUMBER);
	}else if(strcmp(Identifier, "changerel") == 0){
		Action->Action = MOVEUSE_ACTION_CHANGEREL;
		LoadParameters(Script, Action->Parameters, 5,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_VECTOR,
								MOVEUSE_PARAMETER_TYPE,
								MOVEUSE_PARAMETER_TYPE,
								MOVEUSE_PARAMETER_NUMBER);
	}else if(strcmp(Identifier, "setattribute") == 0){
		Action->Action = MOVEUSE_ACTION_SETATTRIBUTE;
		LoadParameters(Script, Action->Parameters, 3,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_INSTANCEATTRIBUTE,
								MOVEUSE_PARAMETER_NUMBER);
	}else if(strcmp(Identifier, "changeattribute") == 0){
		Action->Action = MOVEUSE_ACTION_CHANGEATTRIBUTE;
		LoadParameters(Script, Action->Parameters, 3,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_INSTANCEATTRIBUTE,
								MOVEUSE_PARAMETER_NUMBER);
	}else if(strcmp(Identifier, "setquestvalue") == 0){
		Action->Action = MOVEUSE_ACTION_SETQUESTVALUE;
		LoadParameters(Script, Action->Parameters, 3,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_NUMBER,
								MOVEUSE_PARAMETER_NUMBER);
	}else if(strcmp(Identifier, "damage") == 0){
		Action->Action = MOVEUSE_ACTION_DAMAGE;
		LoadParameters(Script, Action->Parameters, 4,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_NUMBER,
								MOVEUSE_PARAMETER_NUMBER);
	}else if(strcmp(Identifier, "setstart") == 0){
		Action->Action = MOVEUSE_ACTION_SETSTART;
		LoadParameters(Script, Action->Parameters, 2,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_COORDINATE);
	}else if(strcmp(Identifier, "writename") == 0){
		Action->Action = MOVEUSE_ACTION_WRITENAME;
		LoadParameters(Script, Action->Parameters, 3,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_TEXT,
								MOVEUSE_PARAMETER_OBJECT);
	}else if(strcmp(Identifier, "writetext") == 0){
		Action->Action = MOVEUSE_ACTION_WRITETEXT;
		LoadParameters(Script, Action->Parameters, 2,
								MOVEUSE_PARAMETER_TEXT,
								MOVEUSE_PARAMETER_OBJECT);
	}else if(strcmp(Identifier, "logout") == 0){
		Action->Action = MOVEUSE_ACTION_LOGOUT;
		LoadParameters(Script, Action->Parameters, 1,
								MOVEUSE_PARAMETER_OBJECT);
	}else if(strcmp(Identifier, "moveallonmap") == 0){
		Action->Action = MOVEUSE_ACTION_MOVEALLONMAP;
		LoadParameters(Script, Action->Parameters, 2,
								MOVEUSE_PARAMETER_COORDINATE,
								MOVEUSE_PARAMETER_COORDINATE);
	}else if(strcmp(Identifier, "moveall") == 0){
		Action->Action = MOVEUSE_ACTION_MOVEALL;
		LoadParameters(Script, Action->Parameters, 2,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_COORDINATE);
	}else if(strcmp(Identifier, "moveallrel") == 0){
		Action->Action = MOVEUSE_ACTION_MOVEALLREL;
		LoadParameters(Script, Action->Parameters, 2,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_VECTOR);
	}else if(strcmp(Identifier, "movetoponmap") == 0){
		Action->Action = MOVEUSE_ACTION_MOVETOPONMAP;
		LoadParameters(Script, Action->Parameters, 3,
								MOVEUSE_PARAMETER_COORDINATE,
								MOVEUSE_PARAMETER_TYPE,
								MOVEUSE_PARAMETER_COORDINATE);
	}else if(strcmp(Identifier, "movetop") == 0){
		Action->Action = MOVEUSE_ACTION_MOVETOP;
		LoadParameters(Script, Action->Parameters, 2,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_COORDINATE);
	}else if(strcmp(Identifier, "movetoprel") == 0){
		Action->Action = MOVEUSE_ACTION_MOVETOPREL;
		LoadParameters(Script, Action->Parameters, 2,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_VECTOR);
	}else if(strcmp(Identifier, "move") == 0){
		Action->Action = MOVEUSE_ACTION_MOVE;
		LoadParameters(Script, Action->Parameters, 2,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_COORDINATE);
	}else if(strcmp(Identifier, "moverel") == 0){
		Action->Action = MOVEUSE_ACTION_MOVEREL;
		LoadParameters(Script, Action->Parameters, 3,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_VECTOR);
	}else if(strcmp(Identifier, "retrieve") == 0){
		Action->Action = MOVEUSE_ACTION_RETRIEVE;
		LoadParameters(Script, Action->Parameters, 3,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_VECTOR,
								MOVEUSE_PARAMETER_VECTOR);
	}else if(strcmp(Identifier, "deleteallonmap") == 0){
		Action->Action = MOVEUSE_ACTION_DELETEALLONMAP;
		LoadParameters(Script, Action->Parameters, 1,
								MOVEUSE_PARAMETER_COORDINATE);
	}else if(strcmp(Identifier, "deletetoponmap") == 0){
		Action->Action = MOVEUSE_ACTION_DELETETOPONMAP;
		LoadParameters(Script, Action->Parameters, 2,
								MOVEUSE_PARAMETER_COORDINATE,
								MOVEUSE_PARAMETER_TYPE);
	}else if(strcmp(Identifier, "deleteonmap") == 0){
		Action->Action = MOVEUSE_ACTION_DELETEONMAP;
		LoadParameters(Script, Action->Parameters, 2,
								MOVEUSE_PARAMETER_COORDINATE,
								MOVEUSE_PARAMETER_TYPE);
	}else if(strcmp(Identifier, "delete") == 0){
		Action->Action = MOVEUSE_ACTION_DELETE;
		LoadParameters(Script, Action->Parameters, 1,
								MOVEUSE_PARAMETER_OBJECT);
	}else if(strcmp(Identifier, "deleteininventory") == 0){
		Action->Action = MOVEUSE_ACTION_DELETEININVENTORY;
		LoadParameters(Script, Action->Parameters, 3,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_TYPE,
								MOVEUSE_PARAMETER_NUMBER);
	}else if(strcmp(Identifier, "description") == 0){
		Action->Action = MOVEUSE_ACTION_DESCRIPTION;
		LoadParameters(Script, Action->Parameters, 2,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_OBJECT);
	}else if(strcmp(Identifier, "loaddepot") == 0){
		Action->Action = MOVEUSE_ACTION_LOADDEPOT;
		LoadParameters(Script, Action->Parameters, 4,
								MOVEUSE_PARAMETER_COORDINATE,
								MOVEUSE_PARAMETER_TYPE,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_NUMBER);
	}else if(strcmp(Identifier, "savedepot") == 0){
		Action->Action = MOVEUSE_ACTION_SAVEDEPOT;
		LoadParameters(Script, Action->Parameters, 4,
								MOVEUSE_PARAMETER_COORDINATE,
								MOVEUSE_PARAMETER_TYPE,
								MOVEUSE_PARAMETER_OBJECT,
								MOVEUSE_PARAMETER_NUMBER);
	}else if(strcmp(Identifier, "sendmail") == 0){
		Action->Action = MOVEUSE_ACTION_SENDMAIL;
		LoadParameters(Script, Action->Parameters, 1,
								MOVEUSE_PARAMETER_OBJECT);
	}else if(strcmp(Identifier, "nop") == 0){
		Action->Action = MOVEUSE_ACTION_NOP;
		LoadParameters(Script, Action->Parameters, 0);
	}else{
		Script->error("invalid action");
	}
}

void LoadDataBase(void){
	print(1, "Lade Move/Use-Datenbank ...\n");

	char FileName[4096];
	snprintf(FileName, sizeof(FileName), "%s/moveuse.dat", DATAPATH);

	for(int i = 0; i < NARRAY(MoveUseDatabases); i += 1){
		MoveUseDatabases[i].NumberOfRules = 0;
	}

	TReadScriptFile Script;
	Script.open(FileName);
	Script.nextToken();
	while(Script.Token != ENDOFFILE){
		const char *Identifier = Script.getIdentifier();

		if(strcmp(Identifier, "begin") == 0){
			Script.readString();
			Script.nextToken();
			continue;
		}else if(strcmp(Identifier, "end") == 0){
			Script.nextToken();
			continue;
		}

		// NOTE(fusion): `TReadScriptFile::error` will always throw but the
		// compiler might not be able to detect that and issue a warning.
		int EventType = 0;
		if(strcmp(Identifier, "use") == 0){
			EventType = MOVEUSE_EVENT_USE;
		}else if(strcmp(Identifier, "multiuse") == 0){
			EventType = MOVEUSE_EVENT_MULTIUSE;
		}else if(strcmp(Identifier, "movement") == 0){
			EventType = MOVEUSE_EVENT_MOVEMENT;
		}else if(strcmp(Identifier, "collision") == 0){
			EventType = MOVEUSE_EVENT_COLLISION;
		}else if(strcmp(Identifier, "separation") == 0){
			EventType = MOVEUSE_EVENT_SEPARATION;
		}else{
			Script.error("Unknown event type");
		}

		Script.readSymbol(',');

		MoveUseDatabases[EventType].NumberOfRules += 1;
		TMoveUseRule *Rule = MoveUseDatabases[EventType].Rules.at(
					MoveUseDatabases[EventType].NumberOfRules);

		// NOTE(fusion): Conditions.
		Rule->FirstCondition = NumberOfMoveUseConditions + 1;
		while(true){
			Script.nextToken();
			NumberOfMoveUseConditions += 1;
			TMoveUseCondition *Condition = MoveUseConditions.at(NumberOfMoveUseConditions);
			Condition->Modifier = MOVEUSE_MODIFIER_NORMAL;
			if(Script.Token == SPECIAL){
				if(Script.getSpecial() == '!'){
					Script.nextToken();
					Condition->Modifier = MOVEUSE_MODIFIER_INVERT;
				}else if(Script.getSpecial() == '~'){
					Script.nextToken();
					Condition->Modifier = MOVEUSE_MODIFIER_TRUE;
				}
			}
			LoadCondition(&Script, Condition);

			if(Script.Token != SPECIAL || Script.getSpecial() != ','){
				break;
			}
		}
		Rule->LastCondition = NumberOfMoveUseConditions;

		if(Script.Token != SPECIAL || Script.getSpecial() != 'I'){
			Script.error("'->' expected");
		}

		// NOTE(fusion): Actions.
		Rule->FirstAction = NumberOfMoveUseActions + 1;
		while(true){
			Script.nextToken();
			NumberOfMoveUseActions += 1;
			TMoveUseAction *Action = MoveUseActions.at(NumberOfMoveUseActions);
			LoadAction(&Script, Action);

			if(Script.Token != SPECIAL || Script.getSpecial() != ','){
				break;
			}
		}
		Rule->LastAction = NumberOfMoveUseActions;

		// NOTE(fusion): Optional tag?
		if(Script.Token == STRING){
			Script.nextToken();
		}
	}

	Script.close();

	print(1, "Move/Use-Datenbank mit %d/%d/%d/%d/%d Regeln gelesen.\n",
			MoveUseDatabases[MOVEUSE_EVENT_USE].NumberOfRules,
			MoveUseDatabases[MOVEUSE_EVENT_MULTIUSE].NumberOfRules,
			MoveUseDatabases[MOVEUSE_EVENT_MOVEMENT].NumberOfRules,
			MoveUseDatabases[MOVEUSE_EVENT_COLLISION].NumberOfRules,
			MoveUseDatabases[MOVEUSE_EVENT_SEPARATION].NumberOfRules);
}

void InitMoveUse(void){
	NumberOfMoveUseConditions = 0;
	NumberOfMoveUseActions = 0;
	DelayedMails = 0;
	LoadDataBase();
}

void ExitMoveUse(void){
	for(int i = 0; i < DelayedMails; i += 1){
		error("ExitMoveUse: Paket an %u wurde nicht zugestellt.\n",
				DelayedMail.at(i)->CharacterID);
	}
}
