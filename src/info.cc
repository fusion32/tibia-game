#include "info.hh"
#include "cr.hh"
#include "magic.hh"

const char *GetLiquidName(int LiquidType){
	const char *LiquidName;
	switch(LiquidType) {
		case LIQUID_NONE:		LiquidName = "nothing"; break;
		case LIQUID_WATER:		LiquidName = "water"; break;
		case LIQUID_WINE:		LiquidName = "wine"; break;
		case LIQUID_BEER:		LiquidName = "beer"; break;
		case LIQUID_MUD:		LiquidName = "mud"; break;
		case LIQUID_BLOOD:		LiquidName = "blood"; break;
		case LIQUID_SLIME:		LiquidName = "slime"; break;
		case LIQUID_OIL:		LiquidName = "oil"; break;
		case LIQUID_URINE:		LiquidName = "urine"; break;
		case LIQUID_MILK:		LiquidName = "milk"; break;
		case LIQUID_MANA:		LiquidName = "manafluid"; break;
		case LIQUID_LIFE:		LiquidName = "lifefluid"; break;
		case LIQUID_LEMONADE:	LiquidName = "lemonade"; break;
		default:{
			error(Translate("GetLiquidName: Ungültiger Flüssigkeitstyp %d\n",
							"GetLiquidName: Invalid fluid type %d\n"), LiquidType);
			LiquidName = "unknown";
			break;
		}
	}
	return LiquidName;
}

uint8 GetLiquidColor(int LiquidType){
	uint8 LiquidColor;
	switch(LiquidType){
		case LIQUID_NONE:		LiquidColor = LIQUID_COLORLESS; break;
		case LIQUID_WATER:		LiquidColor = LIQUID_BLUE; break;
		case LIQUID_WINE:		LiquidColor = LIQUID_PURPLE; break;
		case LIQUID_BEER:		LiquidColor = LIQUID_BROWN; break;
		case LIQUID_MUD:		LiquidColor = LIQUID_BROWN; break;
		case LIQUID_BLOOD:		LiquidColor = LIQUID_RED; break;
		case LIQUID_SLIME:		LiquidColor = LIQUID_GREEN; break;
		case LIQUID_OIL:		LiquidColor = LIQUID_BROWN; break;
		case LIQUID_URINE:		LiquidColor = LIQUID_YELLOW; break;
		case LIQUID_MILK:		LiquidColor = LIQUID_WHITE; break;
		case LIQUID_MANA:		LiquidColor = LIQUID_PURPLE; break;
		case LIQUID_LIFE:		LiquidColor = LIQUID_RED; break;
		case LIQUID_LEMONADE:	LiquidColor = LIQUID_YELLOW; break;
		default:{
			error(Translate("GetLiquidColor: Ungültiger Flüssigkeitstyp %d\n",
							"GetLiquidColor: Invalid fluid type %d\n"), LiquidType);
			LiquidColor = LIQUID_COLORLESS;
			break;
		}
	}
	return LiquidColor;
}

const char *GetName(Object Obj){
	char ObjectName[50] = {};
	ObjectType ObjType = Obj.getObjectType();
	if(ObjType.isCreatureContainer()){
		TCreature *Creature = GetCreature(Obj);
		if(Creature != NULL){
			snprintf(ObjectName, sizeof(ObjectName), "%s", Creature->Name);
		}else{
			error(Translate("GetName: Kreatur %d existiert nicht.\n",
							"GetName: Creature %d does not exist.\n"), Obj.getCreatureID());
		}
	}else{
		// IMPORTANT(fusion): `ObjectType::getName` returns the same static buffer
		// from `Plural`.
		const char *TypeName = ObjType.getName(1);
		if(TypeName != NULL){
			strcpy(ObjectName, TypeName);
		}

		int LiquidType = LIQUID_NONE;
		if(ObjType.getFlag(LIQUIDCONTAINER)){
			LiquidType = (int)Obj.getAttribute(CONTAINERLIQUIDTYPE);
		}else if(ObjType.getFlag(LIQUIDPOOL)){
			LiquidType = (int)Obj.getAttribute(POOLLIQUIDTYPE);
		}

		if(LiquidType != LIQUID_NONE){
			strcat(ObjectName, " of ");
			strcat(ObjectName, GetLiquidName(LiquidType));
		}
	}

	int Count = 1;
	if(ObjType.getFlag(CUMULATIVE)){
		Count = (int)Obj.getAttribute(AMOUNT);
	}

	// IMPORTANT(fusion): `Plural` will return a static buffer.
	return Plural(ObjectName, Count);
}

const char *GetInfo(Object Obj){
	if(!Obj.exists()){
		error(Translate("GetInfo: Übergebenes Objekt existiert nicht.\n",
						"GetInfo: Passed object does not exist.\n"));
		return NULL;
	}

	return Obj.getObjectType().getDescription();
}

int GetWeight(Object Obj, int Count){
	if(!Obj.exists()){
		error(Translate("GetWeight: Übergebenes Objekt existiert nicht.\n",
						"GetWeight: Passed object does not exist.\n"));
		return 0;
	}

	// TODO(fusion): Why do some UNTAKE items have weight, and even worse, hardcoded?

	int Result = 0;
	ObjectType ObjType = Obj.getObjectType();
	if(ObjType.getFlag(TAKE)){
		int Weight = (int)ObjType.getAttribute(WEIGHT);
		if(!ObjType.getFlag(CUMULATIVE)){
			Count = 1;
		}else if(Count == -1){
			Count = (int)Obj.getAttribute(AMOUNT);
		}
		Result = Weight * Count;
	}else if(ObjType.TypeID == 2904){ // LARGE AMPHORA
		Result = 19500;
	}else if(ObjType.TypeID == 3458){ // ANVIL
		Result = 50000;
	}else if(ObjType.TypeID == 3510){ // COAL BASIN
		Result = 22800;
	}else if(ObjType.TypeID == 4311){ // DEAD HUMAN
		Result = 80000;
	}else{
		error(Translate("GetWeight: Objekttyp %d ist nicht nehmbar.\n",
						"GetWeight: Object type %d is not removable.\n"), ObjType.TypeID);
	}
	return Result;
}

int GetCompleteWeight(Object Obj){
	int Result = GetWeight(Obj, -1);
	ObjectType ObjType = Obj.getObjectType();
	if(ObjType.getFlag(CONTAINER) || ObjType.getFlag(CHEST)){
		Object Help = GetFirstContainerObject(Obj);
		Result += GetRowWeight(Help);
	}
	return Result;
}

int GetRowWeight(Object Obj){
	int Result = 0;
	while(Obj != NONE){
		// TODO(fusion): This is probably `GetCompleteWeight` inlined.
		Result += GetWeight(Obj, -1);
		ObjectType ObjType = Obj.getObjectType();
		if(ObjType.getFlag(CONTAINER) || ObjType.getFlag(CHEST)){
			Object Help = GetFirstContainerObject(Obj);
			Result += GetRowWeight(Help);
		}
		Obj = Obj.getNextObject();
	}
	return Result;
}

uint32 GetObjectCreatureID(Object Obj){
	if(!Obj.exists()){
		error(Translate("GetObjectCreatureID: Übergebenes Objekt existiert nicht.\n",
						"GetObjectCreatureID: Passed object does not exist.\n"));
		return 0;
	}

	uint32 CreatureID = 0;
	while(Obj != NONE){
		ObjectType ObjType = Obj.getObjectType();
		if(ObjType.isCreatureContainer()){
			CreatureID = Obj.getCreatureID();
			break;
		}else if(ObjType.isMapContainer()){
			break;
		}
		Obj = Obj.getContainer();
	}
	return CreatureID;
}

// TODO(fusion): I'm not sure about this one. Objects inside containers would
// also return the body position of the container even if it isn't activelly
// equipped.
int GetObjectBodyPosition(Object Obj){
	if(!Obj.exists()){
		error(Translate("GetObjectBodyPosition: Übergebenes Objekt existiert nicht.\n",
						"GetObjectBodyPosition: Passed object does not exist.\n"));
		return 0;
	}

	int Position = 0;
	while(Obj != NONE){
		ObjectType ObjType = Obj.getObjectType();
		if(ObjType.isBodyContainer()){
			// NOTE(fusion): Body container type ids match inventory slots exactly.
			Position = ObjType.TypeID;
			break;
		}else if(ObjType.isMapContainer()){
			break;
		}
		Obj = Obj.getContainer();
	}
	return Position;
}

int GetObjectRNum(Object Obj){
	if(!Obj.exists()){
		error(Translate("GetObjectRNum: Übergebenes Objekt existiert nicht.\n",
						"GetObjectRNum: Passed object does not exist.\n"));
		return 0;
	}

	ObjectType ObjType = Obj.getObjectType();
	if(ObjType.isMapContainer()){
		error(Translate("GetObjectRNum: Objekt ist MapContainer.\n",
						"GetObjectRNum: Object is MapContainer.\n"));
		return 0;
	}

	int Result = 0;
	Object Con = Obj.getContainer();
	Object Help = GetFirstContainerObject(Con);
	while(Help != NONE && Help != Obj){
		Result += 1;
		Help = Help.getNextObject();
	}

	if(Help != Obj){
		error(Translate("GetObjectRNum: Objekt liegt nicht in Container\n",
						"GetObjectRNum: Object is not in container\n"));
		Result = 0;
	}

	return Result;
}

bool ObjectInRange(uint32 CreatureID, Object Obj, int Range){
	TCreature *Creature = GetCreature(CreatureID);
	if(Creature == NULL){
		error(Translate("ObjectInRange: Ungültige Kreatur CreatureID=%d übergeben.\n",
						"ObjectInRange: Invalid creature CreatureID=%d passed.\n"), CreatureID);
		return false;
	}

	if(!Obj.exists()){
		error(Translate("ObjectInRange: Übergebenes Objekt existiert nicht.\n",
						"ObjectInRange: Passed object does not exist.\n"));
		return false;
	}

	int ObjX, ObjY, ObjZ;
	GetObjectCoordinates(Obj, &ObjX, &ObjY, &ObjZ);
	return Creature->posz == ObjZ
		&& std::abs(Creature->posx - ObjX) <= Range
		&& std::abs(Creature->posy - ObjY) <= Range;
}

bool ObjectAccessible(uint32 CreatureID, Object Obj, int Range){
	if(!Obj.exists()){
		error(Translate("ObjectAccessible: Übergebenes Objekt existiert nicht.\n",
						"ObjectAccessible: Passed object does not exist.\n"));
		return false;
	}

	ObjectType ObjType = Obj.getObjectType();
	if(!ObjType.isCreatureContainer()){
		uint32 OwnerID = GetObjectCreatureID(Obj);
		if(OwnerID != 0){
			return OwnerID == CreatureID;
		}
	}

	if(ObjType.getFlag(HANG)){
		int ObjX, ObjY, ObjZ;
		GetObjectCoordinates(Obj, &ObjX, &ObjY, &ObjZ);

		bool HookSouth = CoordinateFlag(ObjX, ObjY, ObjZ, HOOKSOUTH);
		bool HookEast = CoordinateFlag(ObjX, ObjY, ObjZ, HOOKEAST);
		if(HookSouth || HookEast){
			TCreature *Creature = GetCreature(CreatureID);
			if(Creature == NULL){
				error(Translate("ObjectAccessible: Kreatur existiert nicht.\n",
								"ObjectAccessible: Creature does not exist.\n"));
				return false;
			}

			if(HookSouth){
				if(Creature->posy < ObjY
						|| Creature->posy > (ObjY + Range)
						|| Creature->posx < (ObjX - Range)
						|| Creature->posx > (ObjX + Range)){
					return false;
				}
			}

			if(HookEast){
				if(Creature->posx < ObjX
						|| Creature->posx > (ObjX + Range)
						|| Creature->posy < (ObjY - Range)
						|| Creature->posy > (ObjY + Range)){
					return false;
				}
			}
		}
	}

	return ObjectInRange(CreatureID, Obj, Range);
}

int ObjectDistance(Object Obj1, Object Obj2){
	if(!Obj1.exists() || !Obj2.exists()){
		error(Translate("ObjectDistance: Übergebene Objekte existieren nicht.\n",
						"ObjectDistance: Passed objects do not exist.\n"));
		return INT_MAX;
	}

	int Distance = INT_MAX;
	int ObjX1, ObjY1, ObjZ1;
	int ObjX2, ObjY2, ObjZ2;
	GetObjectCoordinates(Obj1, &ObjX1, &ObjY1, &ObjZ1);
	GetObjectCoordinates(Obj2, &ObjX2, &ObjY2, &ObjZ2);
	if(ObjZ1 == ObjZ2){
		Distance = std::max<int>(
				std::abs(ObjX1 - ObjX2),
				std::abs(ObjY1 - ObjY2));
	}
	return Distance;
}

Object GetBodyContainer(uint32 CreatureID, int Position){
	if((Position < INVENTORY_FIRST || Position > INVENTORY_LAST)
	&& (Position < CONTAINER_FIRST || Position > CONTAINER_LAST)){
		error(Translate("GetBodyContainer: ungültige Position: %d\n",
						"GetBodyContainer: invalid position: %d\n"), Position);
		return NONE;
	}

	TCreature *Creature = GetCreature(CreatureID);
	if(Creature == NULL){
		error(Translate("GetBodyContainer: Kreatur %d existiert nicht.\n",
						"GetBodyContainer: Creature %d does not exist.\n"), CreatureID);
		return NONE;
	}

	if(Position >= INVENTORY_FIRST && Position <= INVENTORY_LAST){
		if(!Creature->CrObject.exists()){
			error(Translate("GetBodyContainer: Kreatur-Objekt von %s existiert nicht (Pos %d).\n",
							"GetBodyContainer: Creature object of %s does not exist (pos %d).\n"),
					Creature->Name, Position);
			return NONE;
		}

		return GetContainerObject(Creature->CrObject, Position - INVENTORY_FIRST);
	}else{
		if(Creature->Type != PLAYER){
			error(Translate("GetBodyContainer: Nur Spieler haben offene Container.\n",
							"GetBodyContainer: Only players have open containers.\n"));
			return NONE;
		}

		return ((TPlayer*)Creature)->GetOpenContainer(Position - CONTAINER_FIRST);
	}
}

Object GetBodyObject(uint32 CreatureID, int Position){
	if(Position < INVENTORY_FIRST || Position > INVENTORY_LAST){
		error(Translate("GetBodyObject: ungültige Position %d\n",
						"GetBodyObject: Invalid position %d\n"), Position);
		return NONE;
	}

	Object Obj = NONE;
	Object Con = GetBodyContainer(CreatureID, Position);
	if(Con != NONE){
		Obj = GetFirstContainerObject(Con);
	}
	return Obj;
}

Object GetTopObject(int x, int y, int z, bool Move){
	Object Obj = GetFirstObject(x, y, z);
	if(Obj != NONE){
		while(true){
			Object Next = Obj.getNextObject();
			if(Next == NONE){
				break;
			}

			ObjectType ObjType = Obj.getObjectType();
			if(!ObjType.getFlag(BANK)
					&& !ObjType.getFlag(CLIP)
					&& !ObjType.getFlag(BOTTOM)
					&& !ObjType.getFlag(TOP)
					&& (!Move || !ObjType.isCreatureContainer())){
				break;
			}

			Obj = Next;
		}
	}
	return Obj;
}

Object GetContainer(uint32 CreatureID, int x, int y, int z){
	if(x == 0xFFFF){ // SPECIAL_COORDINATE ?
		return GetBodyContainer(CreatureID, y);
	}else{
		return GetMapContainer(x, y, z);
	}
}

Object GetObject(uint32 CreatureID, int x, int y, int z, int RNum, ObjectType Type){
	Object Obj = NONE;
	if(x == 0xFFFF){ // SPECIAL_COORDINATE ?
		if(y >= INVENTORY_FIRST && y <= INVENTORY_LAST){
			Obj = GetBodyObject(CreatureID, y);
		}else if(y >= CONTAINER_FIRST && y <= CONTAINER_LAST){
			Object Con = GetBodyContainer(CreatureID, y);
			if(Con != NONE){
				Obj = GetContainerObject(Con, RNum);
			}
		}else if(y != INVENTORY_ANY){
			error(Translate("GetObject: Ungültiger ContainerCode x=%d,y=%d,z=%d,RNum=%d,Type=%d.\n",
							"GetObject: Invalid ContainerCode x=%d,y=%d,z=%d,RNum=%d,Type=%d.\n"),
					x, y, z, RNum, Type.TypeID);
		}
	}else if(RNum != -1){
		Obj = GetFirstObject(x, y, z);
		while(Obj != NONE){
			if(Obj.getObjectType().getDisguise() == Type){
				break;
			}
			Obj = Obj.getNextObject();
		}
	}else{
		Obj = GetTopObject(x, y, z, false);
	}

	// NOTE(fusion): `Type` can be a map container (TypeID = 0) as a wildcard for
	// any object found.
	if(Obj != NONE && !Type.isMapContainer()
			&& Obj.getObjectType().getDisguise() != Type){
		Obj = NONE;
	}

	return Obj;
}

Object GetRowObject(Object Obj, ObjectType Type, uint32 Value, bool Recurse){
	Object Result = NONE;
	while(Obj != NONE){
		ObjectType ObjType = Obj.getObjectType();
		if(Recurse && ObjType.getFlag(CONTAINER)){
			Object Help = GetFirstContainerObject(Obj);
			Result = GetRowObject(Help, Type, Value, Recurse);
			if(Result != NONE){
				break;
			}
		}

		if(ObjType == Type
				&& (!ObjType.getFlag(LIQUIDCONTAINER) || Obj.getAttribute(CONTAINERLIQUIDTYPE) == Value)
				&& (!ObjType.getFlag(KEY)             || Obj.getAttribute(KEYNUMBER)           == Value)){
			Result = Obj;
			break;
		}

		Obj = Obj.getNextObject();
	}
	return Result;
}

Object GetInventoryObject(uint32 CreatureID, ObjectType Type, uint32 Value){
	TCreature *Creature = GetCreature(CreatureID);
	if(Creature == NULL){
		error(Translate("GetInventoryObject: Kreatur %d existiert nicht.\n",
						"GetInventoryObject: Creature %d does not exist.\n"),CreatureID);
		return NONE;
	}

	Object Result = NONE;

	// NOTE(fusion): Search inventory containers.
	{
		Object BodyCon = GetFirstContainerObject(Creature->CrObject);
		while(BodyCon != NONE){
			Object BodyObj = GetFirstContainerObject(BodyCon);
			if(BodyObj != NONE && BodyObj.getObjectType().getFlag(CONTAINER)){
				Object Help = GetFirstContainerObject(BodyObj);
				Result = GetRowObject(Help, Type, Value, true);
				if(Result != NONE){
					break;
				}
			}
			BodyCon = BodyCon.getNextObject();
		}
	}

	// NOTE(fusion): Search inventory slots.
	if(Result == NONE){
		Object BodyCon = GetFirstContainerObject(Creature->CrObject);
		while(BodyCon != NONE){
			Object BodyObj = GetFirstContainerObject(BodyCon);
			Result = GetRowObject(BodyObj, Type, Value, false);
			if(Result != NONE){
				break;
			}
			BodyCon = BodyCon.getNextObject();
		}
	}

	return Result;
}

bool IsHeldByContainer(Object Obj, Object Con){
	// TODO(fusion): Why do we check for map container in some loops?
	while(Obj != NONE && !Obj.getObjectType().isMapContainer()){
		if(Obj == Con){
			return true;
		}
		Obj = Obj.getContainer();
	}
	return false;
}

int CountObjectsInContainer(Object Con){
	if(!Con.exists()){
		error(Translate("CountObjectsInContainer: Container existiert nicht.\n",
						"CountObjectsInContainer: Container does not exist.\n"));
		return 0;
	}

	int Count = 0;
	Object Obj = GetFirstContainerObject(Con);
	while(Obj != NONE){
		Count += 1;
		Obj = Obj.getNextObject();
	}
	return Count;
}

int CountObjects(Object Obj){
	if(!Obj.exists()){
		return 0;
	}

	int Count = 1;
	if(Obj.getObjectType().getFlag(CONTAINER)){
		Object Help = GetFirstContainerObject(Obj);
		while(Help != NONE){
			Count += CountObjects(Help);
			Help = Help.getNextObject();
		}
	}
	return Count;
}

int CountObjects(Object Obj, ObjectType Type, uint32 Value){
	if(!Obj.exists()){
		return 0;
	}

	// TODO(fusion): This is different from the other `CountObjects` as it does
	// check other objects in the chain.
	int Count = 0;
	while(Obj != NONE){
		ObjectType ObjType = Obj.getObjectType();
		if(ObjType.getFlag(CONTAINER)){
			Object Help = GetFirstContainerObject(Obj);
			// BUG(fusion): We probably meant to pass `Value` down the the call stack?
			Count += CountObjects(Help, Type, 0);
		}

		if(ObjType == Type
				&& (!ObjType.getFlag(LIQUIDCONTAINER) || Obj.getAttribute(CONTAINERLIQUIDTYPE) == Value)
				&& (!ObjType.getFlag(KEY)             || Obj.getAttribute(KEYNUMBER)           == Value)){
			if(ObjType.getFlag(CUMULATIVE)){
				Count += (int)Obj.getAttribute(AMOUNT);
			}else{
				Count += 1;
			}
		}

		Obj = Obj.getNextObject();
	}
	return Count;
}

int CountInventoryObjects(uint32 CreatureID, ObjectType Type, uint32 Value){
	TCreature *Creature = GetCreature(CreatureID);
	if(Creature == NULL){
		error(Translate("CountInventoryObjects: Kreatur %d existiert nicht; Objekttyp %d.\n",
						"CountInventoryObjects: Creature %d does not exist; object type %d.\n"),
				CreatureID, Type.TypeID);
		return 0;
	}

	if(Creature->CrObject == NONE){
		error(Translate("CountInventoryObjects: Kreatur %s hat kein Kreatur-Objekt.\n",
						"CountInventoryObjects: Creature %s has no creature object.\n"),
				Creature->Name);
		return 0;
	}

	Object Help = GetFirstContainerObject(Creature->CrObject);
	return CountObjects(Help, Type, Value);
}

int CountMoney(Object Obj){
	if(!Obj.exists()){
		return 0;
	}

	int Result = 0;
	while(Obj != NONE){
		ObjectType ObjType = Obj.getObjectType();
		if(ObjType.getFlag(CONTAINER)){
			Object Help = GetFirstContainerObject(Obj);
			Result += CountMoney(Help);
		}

		// TODO(fusion): This was three different if statements but I don't think
		// we'd want the same object type to be all special money objects.
		if(ObjType == GetSpecialObject(MONEY_ONE)){
			Result += (int)Obj.getAttribute(AMOUNT);
		}else if(ObjType == GetSpecialObject(MONEY_HUNDRED)){
			Result += (int)Obj.getAttribute(AMOUNT) * 100;
		}else if(ObjType == GetSpecialObject(MONEY_TENTHOUSAND)){
			Result += (int)Obj.getAttribute(AMOUNT) * 10000;
		}

		Obj = Obj.getNextObject();
	}
	return Result;
}

int CountInventoryMoney(uint32 CreatureID){
	TCreature *Creature = GetCreature(CreatureID);
	if(Creature == NULL){
		error(Translate("CountInventoryMoney: Kreatur %d existiert nicht.\n",
						"CountInventoryMoney: Creature %d does not exist.\n"), CreatureID);
		return 0;
	}

	if(Creature->CrObject == NONE){
		error(Translate("CountInventoryMoney: Kreatur %s hat kein Kreatur-Objekt.\n",
						"CountInventoryMoney: Creature %s has no creature object.\n"), Creature->Name);
		return 0;
	}

	Object Help = GetFirstContainerObject(Creature->CrObject);
	return CountMoney(Help);
}

void CalculateChange(int Amount, int *Gold, int *Platinum, int *Crystal){
	// TODO(fusion): This function is kind of a mess. We should simplify or
	// get rid of it entirely. It is used when removing gold from a creature
	// but doesnt doesn't report errors outside from the `error` logging. It
	// expects the caller to also check whether the amount of gold, platinum,
	// and crystal is sufficient? We'll see when it gets applied.

	int Go = *Gold;
	int Pl = *Platinum;
	int Cr = *Crystal;

	print(3, Translate("Zahle %d mit %d/%d/%d Münzen...\n",
					   "Pay %d with %d/%d/%d coins...\n"), Amount, Go, Pl, Cr);
	if((Cr * 10000 + Pl * 100 + Go) < Amount){
		error(Translate("CalculateChange: %d/%d/%d Münzen reichen nicht zur Bezahlung von %d.\n",
						"CalculateChange: %d/%d/%d coins are not enough to pay %d.\n"),
				Go, Pl, Cr, Amount);
		return;
	}

	int AmountCr = Amount / 10000;
	int AmountRem = Amount % 10000;
	if((Pl * 100 + Go) < AmountRem){
		Cr = AmountCr + 1;
		Pl = (AmountRem - 10000) / 100;
		Go = (AmountRem - 10000) % 100;
	}else{
		if(Cr < AmountCr){
			AmountRem = Amount - Cr * 10000;
		}else{
			Cr = AmountCr;
		}

		int AmountPl = AmountRem / 100;
		int AmountGo = AmountRem % 100;
		if(Go < AmountGo){
			Pl = AmountPl + 1;
			Go = AmountGo - 100;
		}else if(Pl < AmountPl){
			Go = AmountRem - Pl * 100;
		}else{
			Pl = AmountPl;
			Go = AmountGo;
		}
	}

	print(3, Translate("Verwende %d/%d/%d Münzen.\n",
					   "Use %d/%d/%d coins.\n"), Go, Pl, Cr);
	*Gold = Go;
	*Platinum = Pl;
	*Crystal = Cr;

	if((Cr * 10000 + Pl * 100 + Go) != Amount){
		error(Translate("CalculateChange: Fehlerhafte Berechnung: %d/%d/%d Münzen für %d.\n",
						"CalculateChange: Incorrect calculation: %d/%d/%d coins for %d.\n"),
				Go, Pl, Cr, Amount);
	}
}

int GetHeight(int x, int y, int z){
	int Result = 0;
	Object Obj = GetFirstObject(x, y, z);
	while(Obj != NONE){
		ObjectType ObjType = Obj.getObjectType();
		if(ObjType.getFlag(HEIGHT)){
			Result += (int)ObjType.getAttribute(ELEVATION);
		}
		Obj = Obj.getNextObject();
	}
	return Result;
}

bool JumpPossible(int x, int y, int z, bool AvoidPlayers){
	bool HasBank = false;
	Object Obj = GetFirstObject(x, y, z);
	while(Obj != NONE){
		ObjectType ObjType = Obj.getObjectType();

		if(ObjType.getFlag(BANK)){
			HasBank = true;
		}

		if(ObjType.getFlag(UNPASS) && ObjType.getFlag(UNMOVE)){
			return false;
		}

		if(AvoidPlayers && ObjType.isCreatureContainer()){
			TCreature *Creature = GetCreature(Obj);
			if(Creature != NULL && Creature->Type == PLAYER){
				return false;
			}
		}

		Obj = Obj.getNextObject();
	}
	return HasBank;
}

bool FieldPossible(int x, int y, int z, int FieldType){
	Object Obj = GetFirstObject(x, y, z);

	// NOTE(fusion): Only the first object on a given coordinate can be a bank
	// on regular circumstances, so it makes sense to check it right away to
	// determine whether a bank is present.
	if(Obj == NONE || !Obj.getObjectType().getFlag(BANK)){
		return false;
	}

	while(Obj != NONE){
		ObjectType ObjType = Obj.getObjectType();
		if(!ObjType.isCreatureContainer()){
			if(ObjType.getFlag(UNPASS)){
				return false;
			}

			if(ObjType.getFlag(UNLAY)){
				return false;
			}
		}

		if(FieldType == FIELD_TYPE_MAGICWALL || FieldType == FIELD_TYPE_WILDGROWTH){
			if(ObjType.getFlag(UNPASS)){
				return false;
			}
		}

		Obj = Obj.getNextObject();
	}
	return true;
}

bool SearchFreeField(int *x, int *y, int *z, int Distance, uint16 HouseID, bool Jump){
	int OffsetX = 0;
	int OffsetY = 0;
	int CurrentDistance = 0;
	int CurrentDirection = DIRECTION_EAST;
	while(CurrentDistance <= Distance){
		int FieldX = *x + OffsetX;
		int FieldY = *y + OffsetY;
		int FieldZ = *z;

		// TODO(fusion): This is probably some form of the `TCreature::MovePossible`
		// function inlined.
		bool MovePossible;
		if(Jump){
			MovePossible = JumpPossible(FieldX, FieldY, FieldZ, true);
		}else{
			MovePossible = CoordinateFlag(FieldX, FieldY, FieldZ, BANK)
					&& !CoordinateFlag(FieldX, FieldY, FieldZ, UNPASS);

			// TODO(fusion): This one I'm not so sure.
			if(MovePossible && CoordinateFlag(FieldX, FieldY, FieldZ, AVOID)){
				MovePossible = CoordinateFlag(FieldX, FieldY, FieldZ, BED);
			}
		}

		if(MovePossible){
			if(HouseID == 0xFFFF || !IsHouse(FieldX, FieldY, FieldZ)
			|| (HouseID != 0 && GetHouseID(FieldX, FieldY, FieldZ) == HouseID)){
				*x = FieldX;
				*y = FieldY;
				return true;
			}
		}


		// NOTE(fusion): We're spiraling out from the initial coordinate.
		// TODO(fusion): This function used directions different from the ones
		// used by creatures and defined in `enums.hh` so I made it use them
		// instead, LOL.
		if(CurrentDirection == DIRECTION_NORTH){
			OffsetY -= 1;
			if(OffsetY <= -CurrentDistance){
				CurrentDirection = DIRECTION_WEST;
			}
		}else if(CurrentDirection == DIRECTION_WEST){
			OffsetX -= 1;
			if(OffsetX <= -CurrentDistance){
				CurrentDirection = DIRECTION_SOUTH;
			}
		}else if(CurrentDirection == DIRECTION_SOUTH){
			OffsetY += 1;
			if(OffsetY >= CurrentDistance){
				CurrentDirection = DIRECTION_EAST;
			}
		}else{
			ASSERT(CurrentDirection == DIRECTION_EAST);
			OffsetX += 1;
			if(OffsetX > CurrentDistance){
				CurrentDistance = OffsetX;
				CurrentDirection = DIRECTION_NORTH;
			}
		}
	}

	return false;
}

// NOTE(fusion): This is a helper function for `SearchLoginField` and improves
// the readability of an otherwise convoluted function.
static bool LoginPossible(int x, int y, int z, uint16 HouseID, bool Player){
	Object Obj = GetFirstObject(x, y, z);
	if(Obj == NONE){
		return false;
	}

	if(Player && IsNoLogoutField(x, y, z)){
		return false;
	}

	if(IsHouse(x, y, z) && (HouseID == 0 || GetHouseID(x, y, z) != HouseID)){
		return false;
	}

	bool HasBank = false;
	while(Obj != NONE){
		ObjectType ObjType = Obj.getObjectType();
		if(ObjType.getFlag(BANK)){
			HasBank = true;
		}

		if(ObjType.isCreatureContainer()){
			return false;
		}

		if(ObjType.getFlag(UNPASS) && ObjType.getFlag(UNMOVE)){
			return false;
		}

		if(!Player && ObjType.getFlag(AVOID) && ObjType.getFlag(UNMOVE)){
			return false;
		}

		Obj = Obj.getNextObject();
	}
	return HasBank;
}

bool SearchLoginField(int *x, int *y, int *z, int Distance, bool Player){
	uint16 HouseID = GetHouseID(*x, *y, *z);
	if(SearchFreeField(x, y, z, Distance, HouseID, false)
			&& (!Player || !IsNoLogoutField(*x, *y, *z))){
		return true;
	}

	int OffsetX = 0;
	int OffsetY = 0;
	int CurrentDistance = 0;
	int CurrentDirection = DIRECTION_EAST;
	while(CurrentDistance <= Distance){
		int FieldX = *x + OffsetX;
		int FieldY = *y + OffsetY;
		int FieldZ = *z;

		if(LoginPossible(FieldX, FieldY, FieldZ, HouseID, Player)){
			*x = FieldX;
			*y = FieldY;
			return true;
		}

		// NOTE(fusion): Same as `SearchFreeField`.
		if(CurrentDirection == DIRECTION_NORTH){
			OffsetY -= 1;
			if(OffsetY <= -CurrentDistance){
				CurrentDirection = DIRECTION_WEST;
			}
		}else if(CurrentDirection == DIRECTION_WEST){
			OffsetX -= 1;
			if(OffsetX <= -CurrentDistance){
				CurrentDirection = DIRECTION_SOUTH;
			}
		}else if(CurrentDirection == DIRECTION_SOUTH){
			OffsetY += 1;
			if(OffsetY >= CurrentDistance){
				CurrentDirection = DIRECTION_EAST;
			}
		}else{
			ASSERT(CurrentDirection == DIRECTION_EAST);
			OffsetX += 1;
			if(OffsetX > CurrentDistance){
				CurrentDistance = OffsetX;
				CurrentDirection = DIRECTION_NORTH;
			}
		}
	}
	return false;
}

bool SearchSpawnField(int *x, int *y, int *z, int Distance, bool Player){
	// TODO(fusion): It seems `Distance` can be a negative number to do some
	// extended search?
	bool Minimize = true;
	if(Distance < 0){
		Minimize = false;
		Distance = -Distance;
	}

	uint16 HouseID = GetHouseID(*x, *y, *z);
	matrix<int> Map(-Distance, Distance, -Distance, Distance, INT_MAX);
	*Map.at(0, 0) = 0;

	int BestX = 0;
	int BestY = 0;
	int BestTieBreaker = -1;
	int ExpansionPhase = 0;
	while(true){
		bool Found = false;
		bool Expanded = false;
		for(int OffsetY = -Distance; OffsetY <= Distance; OffsetY += 1)
		for(int OffsetX = -Distance; OffsetX <= Distance; OffsetX += 1){
			if(*Map.at(OffsetX, OffsetY) != ExpansionPhase){
				continue;
			}

			int FieldX = *x + OffsetX;
			int FieldY = *y + OffsetY;
			int FieldZ = *z;
			if(IsHouse(FieldX, FieldY, FieldZ) && (HouseID == 0 || GetHouseID(FieldX, FieldY, FieldZ) != HouseID)){
				continue;
			}

			if(!Player && IsProtectionZone(FieldX, FieldY, FieldZ)){
				continue;
			}

			Object Obj = GetFirstObject(FieldX, FieldY, FieldZ);
			if(Obj == NONE){
				continue;
			}

			bool ExpansionPossible = true;
			bool LoginPossible = true;
			bool LoginBad = false;
			while(Obj != NONE){
				ObjectType ObjType = Obj.getObjectType();
				if(ObjType.isCreatureContainer()){
					LoginPossible = false;
				}

				if(ObjType.getFlag(UNPASS)){
					if(ObjType.getFlag(UNMOVE)){
						ExpansionPossible = false;
						LoginPossible = false;
					}else{
						LoginBad = true;
					}
				}

				if(ObjType.getFlag(AVOID)){
					if(ObjType.getFlag(UNMOVE)){
						ExpansionPossible = false;
						LoginPossible = LoginPossible && !Player;
					}else{
						LoginBad = true;
					}
				}

				Obj = Obj.getNextObject();
			}

			if(ExpansionPossible || ExpansionPhase == 0){
				for(int NeighborOffsetY = OffsetY - 1; NeighborOffsetY <= OffsetY + 1; NeighborOffsetY += 1)
				for(int NeighborOffsetX = OffsetX - 1; NeighborOffsetX <= OffsetX + 1; NeighborOffsetX += 1){
					if(-Distance <= NeighborOffsetX && NeighborOffsetX <= Distance
					&& -Distance <= NeighborOffsetY && NeighborOffsetY <= Distance){
						int *NeighborPhase = Map.at(NeighborOffsetX, NeighborOffsetY);
						if(*NeighborPhase > ExpansionPhase){
							*NeighborPhase = ExpansionPhase
									+ std::abs(NeighborOffsetX - OffsetX)
									+ std::abs(NeighborOffsetY - OffsetY);
						}
					}
				}
				Expanded = true;
			}

			if(LoginPossible && (!Player || !IsNoLogoutField(FieldX, FieldY, FieldZ))){
				int TieBreaker = random(0, 99);
				if(!LoginBad){
					TieBreaker += 100;
				}

				if(TieBreaker > BestTieBreaker){
					Found = true;
					BestX = FieldX;
					BestY = FieldY;
					BestTieBreaker = TieBreaker;
				}
			}
		}

		if((Found && Minimize) || !Expanded){
			break;
		}

		ExpansionPhase += 1;
	}

	bool Result = false;
	if(BestTieBreaker >= 0){
		*x = BestX;
		*y = BestY;
		Result = true;
	}

	return Result;
}

bool SearchFlightField(uint32 FugitiveID, uint32 PursuerID, int *x, int *y, int *z){
	TCreature *Fugitive = GetCreature(FugitiveID);
	if(Fugitive == NULL){
		error(Translate("SearchFlightField: Flüchtling existiert nicht.\n",
						"SearchFlightField: Refugee does not exist.\n"));
		return false;
	}

	TCreature *Pursuer = GetCreature(PursuerID);
	if(Pursuer == NULL){
		error(Translate("SearchFlightField: Verfolger existiert nicht.\n",
						"SearchFlightField: Pursuer does not exist.\n"));
		return false;
	}

	if(Fugitive->posz != Pursuer->posz){
		error(Translate("SearchFlightField: Flüchtling und Verfolger sind auf verschiedenen Ebenen.\n",
						"SearchFlightField: Refugee and pursuer are on different levels.\n"));
		return false;
	}

	int Dir[8];
	Dir[0] = (Fugitive->posy <= Pursuer->posy)                                    ? DIRECTION_NORTH     : DIRECTION_NONE;
	Dir[1] = (Fugitive->posx >= Pursuer->posx)                                    ? DIRECTION_EAST      : DIRECTION_NONE;
	Dir[2] = (Fugitive->posy >= Pursuer->posy)                                    ? DIRECTION_SOUTH     : DIRECTION_NONE;
	Dir[3] = (Fugitive->posx <= Pursuer->posx)                                    ? DIRECTION_WEST      : DIRECTION_NONE;
	Dir[4] = (Fugitive->posx <= Pursuer->posx && Fugitive->posy >= Pursuer->posy) ? DIRECTION_SOUTHWEST : DIRECTION_NONE;
	Dir[5] = (Fugitive->posx >= Pursuer->posx && Fugitive->posy >= Pursuer->posy) ? DIRECTION_SOUTHEAST : DIRECTION_NONE;
	Dir[6] = (Fugitive->posx >= Pursuer->posx && Fugitive->posy <= Pursuer->posy) ? DIRECTION_NORTHEAST : DIRECTION_NONE;
	Dir[7] = (Fugitive->posx <= Pursuer->posx && Fugitive->posy <= Pursuer->posy) ? DIRECTION_NORTHWEST : DIRECTION_NONE;
	RandomShuffle(&Dir[0], 4);
	RandomShuffle(&Dir[4], 4);
	for(int i = 0; i < NARRAY(Dir); i += 1){
		if(Dir[i] == DIRECTION_NONE){
			continue;
		}

		int FieldX = Fugitive->posx;
		int FieldY = Fugitive->posy;
		int FieldZ = Fugitive->posz;
		switch(Dir[i]){
			case DIRECTION_NORTH:                  FieldY -= 1; break;
			case DIRECTION_EAST:      FieldX += 1;              break;
			case DIRECTION_SOUTH:                  FieldY += 1; break;
			case DIRECTION_WEST:      FieldX -= 1;              break;
			case DIRECTION_SOUTHWEST: FieldX -= 1; FieldY += 1; break;
			case DIRECTION_SOUTHEAST: FieldX += 1; FieldY += 1; break;
			case DIRECTION_NORTHWEST: FieldX -= 1; FieldY -= 1; break;
			case DIRECTION_NORTHEAST: FieldX += 1; FieldY -= 1; break;
			default:{
				error(Translate("SearchFlightField: Ungültige Richtung %d.\n",
								"SearchFlightField: Invalid direction %d.\n"), Dir[i]);
				return false;
			}
		}

		if(Fugitive->MovePossible(FieldX, FieldY, FieldZ, false, false)){
			*x = FieldX;
			*y = FieldY;
			*z = FieldZ;
			return true;
		}
	}

	return false;
}

bool SearchSummonField(int *x, int *y, int *z, int Distance){
	int BestX = 0;
	int BestY = 0;
	int BestTieBreaker = -1;
	for(int OffsetY = -Distance; OffsetY <= Distance; OffsetY += 1)
	for(int OffsetX = -Distance; OffsetX <= Distance; OffsetX += 1){
		int TieBreaker = random(0, 99);
		if(TieBreaker <= BestTieBreaker){
			continue;
		}

		int FieldX = *x + OffsetX;
		int FieldY = *y + OffsetY;
		int FieldZ = *z;
		if(CoordinateFlag(FieldX, FieldY, FieldZ, BANK)
				&& !CoordinateFlag(FieldX, FieldY, FieldZ, UNPASS)
				&& !CoordinateFlag(FieldX, FieldY, FieldZ, AVOID)
				&& !IsProtectionZone(FieldX, FieldY, FieldZ)
				&& !IsHouse(FieldX, FieldY, FieldZ)
				&& ThrowPossible(*x, *y, *z, FieldX, FieldY, FieldZ, 0)){
			BestX = FieldX;
			BestY = FieldY;
			BestTieBreaker = TieBreaker;
		}
	}

	bool Result = false;
	if(BestTieBreaker >= 0){
		*x = BestX;
		*y = BestY;
		Result = true;
	}

	return Result;
}

bool ThrowPossible(int OrigX, int OrigY, int OrigZ,
			int DestX, int DestY, int DestZ, int Power){
	// NOTE(fusion): `MinZ` contains the highest floor we're able to throw. We'll
	// iterate from it towards the destination floor, checking the line between the
	// origin and destination until we find a valid throwing path (or not).
	int MinZ = std::max<int>(OrigZ - Power, 0);
	for(int CurZ = OrigZ - 1; CurZ >= MinZ; CurZ -= 1){
		Object Obj = GetFirstObject(OrigX, OrigY, CurZ);
		if(Obj != NONE && Obj.getObjectType().getFlag(BANK)){
			MinZ = CurZ + 1;
			break;
		}
	}

	// NOTE(fusion): I'm using `T` as the parameter for the line between the
	// origin and destination.
	int MaxT = std::max<int>(
			std::abs(DestX - OrigX),
			std::abs(DestY - OrigY));

	int StartT = 1;
	if((DestX < OrigX && CoordinateFlag(OrigX, OrigY, OrigZ, HOOKEAST))
	|| (DestY < OrigY && CoordinateFlag(OrigX, OrigY, OrigZ, HOOKSOUTH))){
		StartT = 0;
	}

	while(MinZ <= DestZ){
		int LastX = OrigX;
		int LastY = OrigY;
		if(DestX != OrigX || DestY != OrigY){
			// NOTE(fusion): Get the current coordinates with linear interpolation.
			for(int T = StartT; T <= MaxT; T += 1){
				int CurX = (OrigX * (MaxT - T) + DestX * T) / MaxT;
				int CurY = (OrigY * (MaxT - T) + DestY * T) / MaxT;
				int CurZ = MinZ;
				if(CoordinateFlag(CurX, CurY, CurZ, UNTHROW)){
					break;
				}

				LastX = CurX;
				LastY = CurY;
			}
		}

		if(LastX == DestX && LastY == DestY){
			int LastZ = MinZ;
			for(; LastZ < DestZ; LastZ += 1){
				Object Obj = GetFirstObject(DestX, DestY, LastZ);
				if(Obj != NONE && Obj.getObjectType().getFlag(BANK)){
					break;
				}
			}

			if(LastZ == DestZ){
				return true;
			}
		}

		MinZ += 1;
	}

	return false;
}

void GetCreatureLight(uint32 CreatureID, int *Brightness, int *Color){
	TCreature *Creature = GetCreature(CreatureID);
	if(Creature == NULL){
		error(Translate("GetCreatureLight: Kreatur existiert nicht.\n",
						"GetCreatureLight: Creature does not exist.\n"));
		*Brightness = 0;
		*Color = 0;
		return;
	}

	int OutBrightness = 0;
	if(Creature->Skills[SKILL_LIGHT] != NULL){
		OutBrightness = Creature->Skills[SKILL_LIGHT]->TimerValue();
	}

	if(Creature->Type == PLAYER && CheckRight(CreatureID, ILLUMINATE)){
		if(OutBrightness < 7){
			OutBrightness = 7;
		}
	}

	int OutRed   = 5 * OutBrightness;
	int OutGreen = 5 * OutBrightness;
	int OutBlue  = 5 * OutBrightness;
	for(int Position = INVENTORY_FIRST;
			Position <= INVENTORY_LAST;
			Position += 1){
		Object Obj = GetBodyObject(CreatureID, Position);
		if(Obj == NONE){
			continue;
		}

		ObjectType ObjType = Obj.getObjectType();
		if(!ObjType.getFlag(LIGHT)){
			continue;
		}

		int ObjBrightness = (int)ObjType.getAttribute(BRIGHTNESS);
		int ObjColor      = (int)ObjType.getAttribute(LIGHTCOLOR);
		int ObjRed        = (ObjColor / 36)     * ObjBrightness;
		int ObjGreen      = (ObjColor % 36 / 6) * ObjBrightness;
		int ObjBlue       = (ObjColor % 36 % 6) * ObjBrightness;

		if(OutBrightness < ObjBrightness){
			OutBrightness = ObjBrightness;
		}

		if(OutRed < ObjRed){
			OutRed = ObjRed;
		}

		if(OutGreen < ObjGreen){
			OutGreen = ObjGreen;
		}

		if(OutBlue < ObjBlue){
			OutBlue = ObjBlue;
		}
	}

	int OutColor = 0;
	if(OutBrightness > 0){
		OutColor = (OutRed / OutBrightness) * 36
				+ (OutGreen / OutBrightness) * 6
				+ (OutBlue / OutBrightness);
	}

	*Brightness = OutBrightness;
	*Color      = OutColor;
}

int GetInventoryWeight(uint32 CreatureID){
	TCreature *Creature = GetCreature(CreatureID);
	if(Creature == NULL){
		error(Translate("GetInventoryWeight: Kreatur %d existiert nicht.\n",
						"GetInventoryWeight: Creature %d does not exist.\n"), CreatureID);
		return 0;
	}

	Object Help = GetFirstContainerObject(Creature->CrObject);
	return GetRowWeight(Help);
}

bool CheckRight(uint32 CharacterID, RIGHT Right){
	TPlayer *Player = GetPlayer(CharacterID);
	if(Player == NULL){
		error(Translate("CheckRight: Spieler existiert nicht; Right=%d.\n",
						"CheckRight: Player does not exist; Right=%d.\n"), Right);
		return false;
	}

	if(!CheckBitIndex(NARRAY(Player->Rights), Right)){
		error(Translate("CheckRight: Ungültige Rechtnummer %d.\n",
						"CheckRight: Invalid Right number %d.\n"), Right);
		return false;
	}

	return CheckBit(Player->Rights, Right);
}

bool CheckBanishmentRight(uint32 CharacterID, int Reason, int Action){
	TPlayer *Player = GetPlayer(CharacterID);
	if(Player == NULL){
		error(Translate("CheckBanishmentRight: Spieler existiert nicht.\n",
						"CheckBanishmentRight: Player does not exist.\n"));
		return false;
	}

	// NOTE(fusion): Banishment rights range from 18 to 49 and match banishment
	// reasons exactly when subtracting 18.

	if(Reason < 0 || Reason > 31){
		error(Translate("CheckBanishmentRight: Ungültiger Banngrund %d von Spieler %d.\n",
						"CheckBanishmentRight: Invalid ban reason %d from player %d.\n"),
				Reason, CharacterID);
		return false;
	}

	if(Action < 0 || Action > 6){
		error(Translate("CheckBanishmentRight: Ungültige Aktion %d von Spieler %d.\n",
						"CheckBanishmentRight: Invalid action %d by player %d.\n"),
				Action, CharacterID);
		return false;
	}

	bool Result = false;
	if(CheckRight(CharacterID, (RIGHT)(Reason + 18))){
		bool Name = (Reason >= 0 && Reason <= 8);
		bool Statement = (Reason >= 9 && Reason <= 27) || Reason == 29;
		switch(Action){
			case 0:{
				Result = CheckRight(CharacterID, NOTATION);
				break;
			}

			case 1:{
				Result = Name && CheckRight(CharacterID, NAMELOCK);
				break;
			}

			case 2:{
				Result = !Name && CheckRight(CharacterID, BANISHMENT);
				break;
			}

			case 3:{
				Result = Name && CheckRight(CharacterID, NAMELOCK)
						&& CheckRight(CharacterID, BANISHMENT);
				break;
			}

			case 4:{
				Result = !Name && CheckRight(CharacterID, BANISHMENT)
						&& CheckRight(CharacterID, FINAL_WARNING);
				break;
			}

			case 5:{
				Result = Name && CheckRight(CharacterID, NAMELOCK)
						&& CheckRight(CharacterID, BANISHMENT)
						&& CheckRight(CharacterID, FINAL_WARNING);
				break;
			}

			case 6:{
				Result = Statement && CheckRight(CharacterID, STATEMENT_REPORT);
				break;
			}
		}
	}

	return Result;
}

const char *GetBanishmentReason(int Reason){
	const char *Result;
	switch(Reason){
		case 0:  Result = "NAME_INSULTING"; break;
		case 1:  Result = "NAME_SENTENCE"; break;
		case 2:  Result = "NAME_NONSENSICAL_LETTERS"; break;
		case 3:  Result = "NAME_BADLY_FORMATTED"; break;
		case 4:  Result = "NAME_NO_PERSON"; break;
		case 5:  Result = "NAME_CELEBRITY"; break;
		case 6:  Result = "NAME_COUNTRY"; break;
		case 7:  Result = "NAME_FAKE_IDENTITY"; break;
		case 8:  Result = "NAME_FAKE_POSITION"; break;
		case 9:  Result = "STATEMENT_INSULTING"; break;
		case 10: Result = "STATEMENT_SPAMMING"; break;
		case 11: Result = "STATEMENT_ADVERT_OFFTOPIC"; break;
		case 12: Result = "STATEMENT_ADVERT_MONEY"; break;
		case 13: Result = "STATEMENT_NON_ENGLISH"; break;
		case 14: Result = "STATEMENT_CHANNEL_OFFTOPIC"; break;
		case 15: Result = "STATEMENT_VIOLATION_INCITING"; break;
		case 16: Result = "CHEATING_BUG_ABUSE"; break;
		case 17: Result = "CHEATING_GAME_WEAKNESS"; break;
		case 18: Result = "CHEATING_MACRO_USE"; break;
		case 19: Result = "CHEATING_MODIFIED_CLIENT"; break;
		case 20: Result = "CHEATING_HACKING"; break;
		case 21: Result = "CHEATING_MULTI_CLIENT"; break;
		case 22: Result = "CHEATING_ACCOUNT_TRADING"; break;
		case 23: Result = "CHEATING_ACCOUNT_SHARING"; break;
		case 24: Result = "GAMEMASTER_THREATENING"; break;
		case 25: Result = "GAMEMASTER_PRETENDING"; break;
		case 26: Result = "GAMEMASTER_INFLUENCE"; break;
		case 27: Result = "GAMEMASTER_FALSE_REPORTS"; break;
		case 28: Result = "KILLING_EXCESSIVE_UNJUSTIFIED"; break;
		case 29: Result = "DESTRUCTIVE_BEHAVIOUR"; break;
		case 30: Result = "SPOILING_AUCTION"; break;
		case 31: Result = "INVALID_PAYMENT"; break;
		default:{
			error(Translate("GetBanishmentReason: Ungültiger Verbannungsgrund %d.\n",
							"GetBanishmentReason: Invalid ban reason %d.\n"), Reason);
			Result = "";
			break;
		}
	}
	return Result;
}

void InitInfo(void){
	// no-op
}

void ExitInfo(void){
	// no-op
}
