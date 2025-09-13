#include "cr.hh"
#include "config.hh"
#include "houses.hh"
#include "info.hh"
#include "operate.hh"

// TShortway
// =============================================================================
struct TShortwayPoint {
	TShortwayPoint(void){
		this->x = 0;
		this->y = 0;
		this->Waypoints = -1;
		this->Waylength = -1;
		this->Heuristic = -1;
		this->Predecessor = NULL;
		this->NextToExpand = NULL;
	}

	int x;
	int y;
	int Waypoints;
	int Waylength;
	int Heuristic;
	TShortwayPoint *Predecessor;
	TShortwayPoint *NextToExpand;
};

struct TShortway{
	TShortway(TCreature *Creature, int VisibleX, int VisibleY);
	~TShortway(void);
	void FillMap(void);
	void ClearMap(void);
	void Expand(TShortwayPoint *Node);
	bool Calculate(int DestX, int DestY, bool MustReach, int MaxSteps);

	// DATA
	// =================
	matrix<TShortwayPoint> *Map;
	TShortwayPoint *FirstToExpand;
	TCreature *Creature;
	int VisibleX;
	int VisibleY;
	int StartX;
	int StartY;
	int StartZ;
	int MinWaypoints;
};

TShortway::TShortway(TCreature *Creature, int VisibleX, int VisibleY){
	if(Creature == NULL){
		error(Translate("TShortway::TShortway: Übergebene Kreatur ist NULL.\n",
						"TShortway::TShortway: Surrendered creature is NULL.\n"));
		return;
	}

	if(VisibleX < 1 || VisibleX > 100 || VisibleY < 1 || VisibleY > 100){
		error(Translate("TShortway::TShortway: Ungültiger Sichtbarkeitsbereich %d*%d.\n",
						"TShortway::TShortway: Invalid visibility range %d*%d.\n"), VisibleX, VisibleY);
		return;
	}

	this->Creature = Creature;
	this->VisibleX = VisibleX;
	this->VisibleY = VisibleY;
	this->StartX = Creature->posx;
	this->StartY = Creature->posy;
	this->StartZ = Creature->posz;
	this->Map = new matrix<TShortwayPoint>(
				-(VisibleX + 1), +(VisibleX + 1),
				-(VisibleY + 1), +(VisibleY + 1));
	this->FillMap();
}

TShortway::~TShortway(void){
	if(this->Map != NULL){
		delete this->Map;
	}
}

void TShortway::FillMap(void){
	this->MinWaypoints = 1000;

	for(int X = -this->VisibleX; X <= this->VisibleX; X += 1)
	for(int Y = -this->VisibleY; Y <= this->VisibleY; Y += 1){
		int FieldX = this->StartX + X;
		int FieldY = this->StartY + Y;
		int FieldZ = this->StartZ;

		int Waypoints = -1;
		Object Obj = GetFirstObject(FieldX, FieldY, FieldZ);
		if(Obj.exists()){
			ObjectType ObjType = Obj.getObjectType();
			if(ObjType.getFlag(BANK) && !ObjType.getFlag(UNPASS)){
				Waypoints = (int)ObjType.getAttribute(WAYPOINTS);
				if(Waypoints == 0){
					error(Translate("TShortway::FillMap: Ungültiger Wegpunkte-Wert %d für Bank %d.\n",
									"TShortway::FillMap: Invalid waypoint value %d for Bank %d.\n"),
							Waypoints, ObjType.TypeID);
					Waypoints = -1;
				}

				if(!this->Creature->MovePossible(FieldX, FieldY, FieldZ, false, false)){
					Waypoints = -1;
				}

				if(Waypoints > 0 && Waypoints < this->MinWaypoints){
					this->MinWaypoints = Waypoints;
				}
			}
		}

		TShortwayPoint *Node = this->Map->at(X, Y);
		Node->x = X;
		Node->y = Y;
		Node->Waypoints = Waypoints;
	}
}

void TShortway::ClearMap(void){
	for(int X = -this->VisibleX; X <= this->VisibleX; X += 1)
	for(int Y = -this->VisibleY; Y <= this->VisibleY; Y += 1){
		TShortwayPoint *Node = this->Map->at(X, Y);
		Node->Waylength = INT_MAX;
		Node->Heuristic = INT_MAX;
		Node->Predecessor = NULL;
		Node->NextToExpand = NULL;
	}
}

void TShortway::Expand(TShortwayPoint *Node){
	if(Node == NULL){
		error(Translate("TShortway::Expand: Übergebener Knoten ist NULL.\n",
						"TShortway::Expand: Passed Node is NULL.\n"));
		return;
	}

	this->FirstToExpand = Node->NextToExpand;

	int MinNeighborWaylength = Node->Waylength + Node->Waypoints;
	if(MinNeighborWaylength >= this->Map->at(0, 0)->Waylength){
		return;
	}

	for(int OffsetX = -1; OffsetX <= 1; OffsetX += 1)
	for(int OffsetY = -1; OffsetY <= 1; OffsetY += 1){
		if(OffsetX == 0 && OffsetY == 0){
			continue;
		}

		TShortwayPoint *Neighbor = this->Map->at(Node->x + OffsetX, Node->y + OffsetY);

		// NOTE(fusion): The minimum neighbor waylength already contains the cost
		// of a single step. Diagonal steps are three times more expensive so we
		// add waypoints two more times.
		int NeighborWaylength = MinNeighborWaylength;
		if(OffsetX != 0 && OffsetY != 0){
			NeighborWaylength += Node->Waypoints * 2;
		}

		if(NeighborWaylength < Neighbor->Waylength){
			Neighbor->Waylength = NeighborWaylength;
			Neighbor->Predecessor = Node;
			if((Neighbor->x != 0 || Neighbor->y != 0) && Neighbor->Waypoints != -1){
				// NOTE(fusion): Remove from expand list if it was already expanded upon.
				if(Neighbor->Heuristic != INT_MAX){
					TShortwayPoint *Prev = NULL;
					TShortwayPoint *Cur = this->FirstToExpand;
					while(Cur != NULL && Cur != Neighbor){
						Prev = Cur;
						Cur = Cur->NextToExpand;
					}

					if(Cur != Neighbor){
						error(Translate("TShortway::Expand: Knoten steht nicht in der ExpandList.\n",
										"TShortway::Expand: Node is not in the ExpandList.\n"));
					}else if(Prev == NULL){
						this->FirstToExpand = Neighbor->NextToExpand;
					}else{
						Prev->NextToExpand = Neighbor->NextToExpand;
					}
				}

				// NOTE(fusion): Compute heuristic using the manhattan distance.
				int Distance = std::abs(Neighbor->x) + std::abs(Neighbor->y);
				Neighbor->Heuristic = Neighbor->Waylength
						+ Neighbor->Waypoints * 1
						+ this->MinWaypoints * (Distance - 1);

				// NOTE(fusion): Insert into expand list.
				{
					TShortwayPoint *Prev = NULL;
					TShortwayPoint *Cur = this->FirstToExpand;
					while(Cur != NULL && Cur->Heuristic < Neighbor->Heuristic){
						Prev = Cur;
						Cur = Cur->NextToExpand;
					}

					if(Prev == NULL){
						this->FirstToExpand = Neighbor;
					}else{
						Prev->NextToExpand = Neighbor;
					}
					Neighbor->NextToExpand = Cur;
				}
			}
		}
	}
}

bool TShortway::Calculate(int DestX, int DestY, bool MustReach, int MaxSteps){
	if(this->Map == NULL){
		error(Translate("TShortway::Calculate: Karte existiert nicht.\n",
						"TShortway::Calculate: Map does not exist.\n"));
		return false;
	}

	// NOTE(fusion): Transform dest to relative coordinates.
	DestX -= this->StartX;
	DestY -= this->StartY;

	// NOTE(fusion): Check if already at destination.
	if(DestX == 0 && DestY == 0){
		return true;
	}

	// NOTE(fusion): Check if out of range.
	if(std::abs(DestX) > this->VisibleX || std::abs(DestY) > this->VisibleY){
		return false;
	}

	// NOTE(fusion): Find shortest path from the destination to the origin.
	this->ClearMap();
	this->FirstToExpand = this->Map->at(DestX, DestY);
	this->FirstToExpand->Waylength = 0;
	while(this->FirstToExpand != NULL){
		this->Expand(this->FirstToExpand);
	}

	// NOTE(fusion): Check if the origin was reached from the destination.
	TShortwayPoint *Node = this->Map->at(0, 0);
	if(Node->Waylength == INT_MAX){
		return false;
	}

	// NOTE(fusion): Walk back from the origin to reconstruct the path.
	int CurDistance = std::max<int>(
			std::abs(Node->x - DestX),
			std::abs(Node->y - DestY));
	Node = Node->Predecessor;
	while(Node != NULL && MaxSteps > 0
			&& (MustReach || CurDistance > 1)){
		TToDoEntry TD = {};
		TD.Code = TDGo;
		TD.Go.x = this->StartX + Node->x;
		TD.Go.y = this->StartY + Node->y;
		TD.Go.z = this->StartZ;
		Creature->ToDoAdd(TD);

		CurDistance = std::max<int>(
			std::abs(Node->x - DestX),
			std::abs(Node->y - DestY));
		Node = Node->Predecessor;
		MaxSteps -= 1;
	}

	return true;
}

// TCreature
// =============================================================================
bool TCreature::SetOnMap(void){
	int LoginX = this->posx;
	int LoginY = this->posy;
	int LoginZ = this->posz;
	bool Player = (this->Type == PLAYER);
	if(!SearchLoginField(&LoginX, &LoginY, &LoginZ, 1, Player)){
		bool Found = false;
		if(IsHouse(LoginX, LoginY, LoginZ)){
			uint16 HouseID = GetHouseID(LoginX, LoginY, LoginZ);
			GetExitPosition(HouseID, &LoginX, &LoginY, &LoginZ);
			Found = SearchLoginField(&LoginX, &LoginY, &LoginZ, 1, Player);
		}

		if(!Found){
			LoginX = this->startx;
			LoginY = this->starty;
			LoginZ = this->startz;
		}
	}

	Object Con = GetMapContainer(LoginX, LoginY, LoginZ);
	if(Con == NONE){
		error(Translate("TCreature::SetOnMap: Kartencontainer für Punkt [%d,%d,%d] existiert nicht.\n",
						"TCreature::SetOnMap: Map container for point [%d,%d,%d] does not exist.\n"),
				LoginX, LoginY, LoginZ);
		return false;
	}

	this->posx = LoginX;
	this->posy = LoginY;
	this->posz = LoginZ;

	// NOTE(fusion): `Create` automatically sets `this->CrObject` and creates
	// its body container slots.
	bool Result = true;
	try{
		Create(Con, TYPEID_CREATURE_CONTAINER, this->ID);
	}catch(RESULT r){
		error(Translate("TCreature::SetOnMap: Kann Kreatur nicht setzen ([%d,%d,%d] - Exception %d).\n",
						"TCreature::SetOnMap: Cannot set creature ([%d,%d,%d] - Exception %d).\n"),
				LoginX, LoginY, LoginZ, r);
		if(this->Type == PLAYER){
			SendResult(this->Connection, r);
		}
		Result = false;
	}
	return Result;
}

bool TCreature::DelOnMap(void){
	Object Obj = this->CrObject;
	if(Obj == NONE){
		return true;
	}

	// TODO(fusion): I feel `Delete` should also manage `this->CrObject` automatically.
	bool Result = true;
	this->CrObject = NONE;
	try{
		Delete(Obj, -1);
	}catch(RESULT r){
		error("TCreature::DelOnMap: Error Deleting CreatureObject: %d\n", r);
		Result = false;
	}
	return Result;
}

void TCreature::Go(int DestX, int DestY, int DestZ){
	// NOTE(fusion): This is the execution function for `ToDoGo` which computes
	// the path step by step. If the destination is outside the range of a single
	// step, then it is an error.
	int OrigX = this->posx;
	int OrigY = this->posy;
	int OrigZ = this->posz;
	int Distance = std::max<int>(std::abs(OrigX - DestX), std::abs(OrigY - DestY));
	if(Distance > 1 || OrigZ != DestZ){
		throw NOTACCESSIBLE;
	}

	// TODO(fusion): See note in `TPlayer::CheckState`.
	int DrunkLevel = this->Skills[SKILL_DRUNKEN]->TimerValue();
	if(DrunkLevel > 0 && this->Skills[SKILL_DRUNKEN]->Get() == 0){
		int StaggerChance = std::max<int>(7 - DrunkLevel, 1);
		if(rand() % StaggerChance == 0){
			DestX = OrigX;
			DestY = OrigY;
			switch(rand() % 4){
				case DIRECTION_NORTH:	DestY -= 1; break;
				case DIRECTION_EAST:	DestX += 1; break;
				case DIRECTION_SOUTH:	DestY += 1; break;
				case DIRECTION_WEST:	DestX -= 1; break;
			}

			if(this->ToDoClear() && this->Type == PLAYER){
				SendSnapback(this->Connection);
			}

			int TalkMode = (this->Type == MONSTER) ? TALK_ANIMAL_LOW : TALK_SAY;
			this->ToDoTalk(TalkMode, NULL, "Hicks!", false);
			this->ToDoStart();
		}
	}

	if(!this->MovePossible(DestX, DestY, DestZ, true, false)){
		bool DiagonalMove = (OrigX != DestX && OrigY != DestY);
		if(this->Type == PLAYER && !DiagonalMove){
			// TODO(fusion): These are quite similar to `MagicClimbing`. Perhaps
			// there is an inlined function here that checks whether climbing is
			// possible.
			if(DestZ > 0 && GetHeight(OrigX, OrigY, OrigZ) >= 24
					&& !CoordinateFlag(OrigX, OrigY, OrigZ - 1, BANK)
					&& !CoordinateFlag(OrigX, OrigY, OrigZ - 1, UNPASS)
					&& this->MovePossible(DestX, DestY, DestZ - 1, true, true)){
				DestZ -= 1;
			}else if(DestZ < 15 && GetHeight(DestX, DestY, DestZ + 1) >= 24
					&& !CoordinateFlag(DestX, DestY, DestZ, BANK)
					&& !CoordinateFlag(DestX, DestY, DestZ, UNPASS)
					&& this->MovePossible(DestX, DestY, DestZ + 1, true, true)){
				DestZ += 1;
			}
		}

		if(this->posz == DestZ){
			throw MOVENOTPOSSIBLE;
		}
	}

	Object Dest = GetMapContainer(DestX, DestY, DestZ);
	::Move(this->ID, this->CrObject, Dest, -1, false, NONE);
}

void TCreature::Rotate(int Direction){
	this->Direction = Direction;
	AnnounceChangedObject(this->CrObject, OBJECT_CHANGED);
}

void TCreature::Rotate(TCreature *Target){
	if(Target == NULL){
		error(Translate("TCreature::Rotate: Target ist NULL.\n",
						"TCreature::Rotate: Target is NULL.\n"));
		return;
	}

	int Direction = this->Direction;
	int OffsetX = Target->posx - this->posx;
	int OffsetY = Target->posy - this->posy;
	int DistanceX = std::abs(OffsetX);
	int DistanceY = std::abs(OffsetY);
	if(DistanceY > DistanceX){
		Direction = (OffsetY < 0) ? DIRECTION_NORTH : DIRECTION_SOUTH;
	}else{
		Direction = (OffsetX < 0) ? DIRECTION_WEST : DIRECTION_EAST;
	}

	this->Rotate(Direction);
}

void TCreature::Move(Object Obj, int DestX, int DestY, int DestZ, uint8 Count){
	// NOTE(fusion): What a disaster.

	if(!Obj.exists()){
		throw NOTACCESSIBLE;
	}

	if(Obj == this->CrObject){
		this->Go(DestX, DestY, DestZ);
		return;
	}

	ObjectType ObjType = Obj.getObjectType();
	if(ObjType.isCreatureContainer()){
		this->Combat.DelayAttack(2000);
	}

	int MoveCount = (int)Count;
	if(ObjType.getFlag(CUMULATIVE)){
		int ObjAmount = (int)Obj.getAttribute(AMOUNT);
		if(MoveCount > ObjAmount){
			MoveCount = ObjAmount;
		}
	}

	if(DestX == 0xFFFF){ // SPECIAL_COORDINATE ?
		if(DestY == INVENTORY_ANY){
			// NOTE(fusion): `CheckInventoryDestination` will throw if it's not
			// possible to place the object on the chosen container. We want to
			// find a slot that doesn't make it throw while giving priority to
			// non hand or ammo slots.
			for(int Position = INVENTORY_FIRST;
					Position <= INVENTORY_LAST;
					Position += 1){
				try{
					Object Con = GetBodyContainer(this->ID, Position);
					CheckInventoryDestination(Obj, Con, false);
				}catch(RESULT r){
					continue;
				}

				DestY = Position;
				if(Position != INVENTORY_RIGHTHAND
						&& Position != INVENTORY_LEFTHAND
						&& Position != INVENTORY_AMMO){
					break;
				}
			}

			// NOTE(fusion): No appropriate inventory slot was found so we now
			// fallback to inventory containers. For whatever reason we don't
			// give priority to the bag slot.
			if(DestY == INVENTORY_ANY){
				for(int Position = INVENTORY_FIRST;
						Position <= INVENTORY_LAST;
						Position += 1){
					Object Con = GetBodyObject(this->ID, Position);
					if(Con != NONE && Con.getObjectType().getFlag(CONTAINER)){
						try{
							CheckContainerDestination(Obj, Con);
						}catch(RESULT r){
							continue;
						}

						DestY = Position;
					}
				}
			}

			if(DestY == INVENTORY_ANY){
				throw NOROOM;
			}
		}

		Object DestCon = NONE;
		Object DestObj = NONE;
		if(DestY >= INVENTORY_FIRST && DestY <= INVENTORY_LAST){
			DestCon = GetBodyContainer(this->ID, DestY);
			DestObj = GetBodyObject(this->ID, DestY);
		}else if(DestY >= CONTAINER_FIRST && DestY <= CONTAINER_LAST){
			DestCon = GetBodyContainer(this->ID, DestY);
			if(DestZ < 254){
				// TODO(fusion): The last argument to `GetObject` is the object
				// type we expect to find and it seems that the type id of a map
				// container (which is 0) can be used as a wildcard for any object
				// it finds.
				DestObj = GetObject(this->ID, DestX, DestY, DestZ, DestZ, 0);
			}else if(DestZ == 254){
				if(DestCon == NONE){
					throw NOTACCESSIBLE;
				}
				DestCon = DestCon.getContainer();
			}
		}else{
			error(Translate("TCreature::Move: Ungültiger Containercode %d.\n",
							"TCreature::Move: Invalid container code %d.\n"), DestY);
			throw ERROR;
		}

		if(DestObj != NONE){
			ObjectType DestObjType = DestObj.getObjectType();
			if(DestObjType.getFlag(CONTAINER)){
				DestCon = DestObj;
				DestObj = NONE;
			}else if(DestObjType.getFlag(CUMULATIVE) && DestObjType == ObjType){
				int DestAmount = (int)DestObj.getAttribute(AMOUNT);
				int MergeCount = MoveCount;
				if((DestAmount + MergeCount) > 100){
					MergeCount = 100 - DestAmount;
				}

				if(MergeCount > 0){
					try{
						::Merge(this->ID, Obj, DestObj, MergeCount, NONE);
						MoveCount -= MergeCount;
						if(MoveCount <= 0){
							return;
						}
					}catch(RESULT r){
						if(r == TOOHEAVY){
							throw;
						}
					}
				}

				DestObj = NONE;
			}
		}

		if(DestCon == NONE){
			throw OUTOFRANGE;
		}

		try{
			::Move(this->ID, Obj, DestCon, MoveCount, false, DestObj);
		}catch(RESULT r){
			// NOTE(fusion): Attempt to exchange inventory items.
			if(DestY >= INVENTORY_FIRST && DestY <= INVENTORY_LAST
					&& DestObj != NONE
					&& (r == NOROOM
						|| r == HANDSNOTFREE
						|| r == HANDBLOCKED
						|| r == ONEWEAPONONLY)){
				Object ObjCon = Obj.getContainer();
				::Move(this->ID, DestObj, ObjCon, -1, false, NONE);
				::Move(this->ID, Obj, DestCon, MoveCount, false, DestObj);
			}else{
				throw;
			}
		}
	}else{
		Object Dest = GetMapContainer(DestX, DestY, DestZ);
		if(!Dest.exists()){
			throw ERROR;
		}

		if(ObjType.getFlag(HANG)
				&& (CoordinateFlag(DestX, DestY, DestZ, HOOKSOUTH)
					|| CoordinateFlag(DestY, DestY, DestZ, HOOKEAST))
				&& !ObjectInRange(this->ID, Dest, 1)){
			if(this->ToDoClear() && this->Type == PLAYER){
				SendSnapback(this->Connection);
			}

			// NOTE(fusion): Pick up object if it's not in our inventory.
			if(GetObjectCreatureID(Obj) != this->ID){
				this->ToDoMove(Obj, 0xFFFF, 0, 0, 1);
			}

			// NOTE(fusion): Walk to destination and try again.
			this->ToDoGo(DestX, DestY, DestZ, false, INT_MAX);
			this->ToDoMove(Obj, DestX, DestY, DestZ, 1);
			this->ToDoStart();
		}else{
			::Move(this->ID, Obj, Dest, MoveCount, false, NONE);
		}
	}
}

void TCreature::Trade(Object Obj, uint32 PartnerID){
	if(this->Type != PLAYER){
		error(Translate("TCreature::Trade: Nur Spieler können handeln.\n",
						"TCreature::Trade: Only players can trade.\n"));
		throw ERROR;
	}

	if(!Obj.exists() || !ObjectAccessible(this->ID, Obj, 1)){
		throw NOTACCESSIBLE;
	}

	if(this->ID == PartnerID){
		throw CROSSREFERENCE;
	}

	TPlayer *Partner = GetPlayer(PartnerID);
	if(Partner == NULL || Partner->Type != PLAYER){
		throw PLAYERNOTONLINE;
	}

	if(((TPlayer*)this)->TradeObject != NONE){
		throw ALREADYTRADING;
	}

	if(CountObjects(Obj) > 100){
		throw TOOMANYOBJECTS;
	}

	if(ObjectDistance(this->CrObject, Partner->CrObject) > 2){
		throw OUTOFRANGE;
	}

	if(!ThrowPossible(this->posx, this->posy, this->posz,
			Partner->posx, Partner->posy, Partner->posz, 0)){
		throw CANNOTTHROW;
	}

	if(Partner->TradeObject != NONE && Partner->TradePartner != this->ID){
		throw PARTNERTRADING;
	}

	// NOTE(fusion): Check if one object is contained by the other.
	if(Partner->TradeObject != NONE){
		Object Help = Partner->TradeObject;
		while(Help != NONE && !Help.getObjectType().isMapContainer()){
			if(Help == Obj){
				throw NOTACCESSIBLE;
			}
			Help = Help.getContainer();
		}

		Help = Obj;
		while(Help != NONE && !Help.getObjectType().isMapContainer()){
			if(Help == Partner->TradeObject){
				throw NOTACCESSIBLE;
			}
			Help = Help.getContainer();
		}
	}

	((TPlayer*)this)->TradeObject = Obj;
	((TPlayer*)this)->TradePartner = PartnerID;
	((TPlayer*)this)->TradeAccepted = false;

	if(Partner->TradeObject != NONE){
		SendTradeOffer(Partner->Connection, this->Name, false, Obj);
		SendTradeOffer(this->Connection, this->Name, true, Obj);
		SendTradeOffer(this->Connection, Partner->Name, false, Partner->TradeObject);
	}else{
		SendMessage(Partner->Connection, TALK_INFO_MESSAGE,
				"%s wants to trade with you.", this->Name);
		SendTradeOffer(this->Connection, this->Name, true, Obj);
	}
}

void TCreature::Use(Object Obj1, Object Obj2, uint8 Dummy){
	if(!Obj1.exists()){
		throw DESTROYED;
	}

	if(Obj2 != NONE){
		if(!Obj2.exists()){
			throw DESTROYED;
		}

		bool DistUse = Obj1.getObjectType().getFlag(DISTUSE);
		if(!DistUse && !ObjectInRange(this->ID, Obj2, 1)){
			int ObjX2, ObjY2, ObjZ2;
			GetObjectCoordinates(Obj2, &ObjX2, &ObjY2, &ObjZ2);

			if(this->ToDoClear() && this->Type == PLAYER){
				SendSnapback(this->Connection);
			}

			// NOTE(fusion): Pick up object 1 if it's not in our inventory.
			if(GetObjectCreatureID(Obj1) != this->ID){
				this->ToDoMove(Obj1, 0xFFFF, 0, 0, 1);
			}

			// NOTE(fusion): Walk to object 2 and try again.
			this->ToDoGo(ObjX2, ObjY2, ObjZ2, false, INT_MAX);
			// TODO(fusion): Missing `Dummy` here? Probably not because this
			// could only be triggered if object 2 exists and Dummy is only
			// used when opening containers.
			this->ToDoUse(2, Obj1, Obj2);
			this->ToDoStart();
			return;
		}

		if(DistUse && !ObjectInRange(this->ID, Obj2, 7)){
			throw OUTOFRANGE;
		}

		this->EarliestMultiuseTime = ServerMilliseconds + 1000;
	}

	::Use(this->ID, Obj1, Obj2, Dummy);
}

void TCreature::Turn(Object Obj){
	if(!Obj.exists()){
		throw DESTROYED;
	}

	::Turn(this->ID, Obj);
}

void TCreature::Attack(void){
	this->Combat.Attack();
}

void TCreature::Execute(void){
	while(true){
		if(!this->LockToDo || this->IsDead || this->NextWakeup > ServerMilliseconds){
			break;
		}

		if(this->NrToDo <= this->ActToDo){
			this->ToDoClear();
			this->IdleStimulus();
			break;
		}

		uint32 Delay = this->CalculateDelay();
		if(Delay > 0){
			if(this->Stop){
				this->ToDoClear();
				if(this->Type == PLAYER){
					SendSnapback(this->Connection);
				}
			}else{
				this->NextWakeup = ServerMilliseconds + Delay;
				ToDoQueue.insert(this->NextWakeup, this->ID);
			}
			break;
		}

		TToDoEntry TD = *this->ToDoList.at(this->ActToDo);
		this->ActToDo += 1;
		try{
			switch(TD.Code){
				case TDGo:{
					this->Go(TD.Go.x, TD.Go.y, TD.Go.z);
					break;
				}

				case TDRotate:{
					this->Rotate(TD.Rotate.Direction);
					break;
				}

				case TDMove:{
					this->Move(Object(TD.Move.Obj), TD.Move.x, TD.Move.y, TD.Move.z, (uint8)TD.Move.Count);
					break;
				}

				case TDTrade:{
					this->Trade(Object(TD.Trade.Obj), TD.Trade.Partner);
					break;
				}

				case TDUse:{
					this->Use(Object(TD.Use.Obj1), Object(TD.Use.Obj2), TD.Use.Dummy);
					break;
				}

				case TDTurn:{
					this->Turn(Object(TD.Turn.Obj));
					break;
				}

				case TDAttack:{
					this->Attack();
					break;
				}

				case TDTalk:{
					const char *Text = GetDynamicString(TD.Talk.Text);
					if(Text != NULL){
						const char *Addressee = GetDynamicString(TD.Talk.Addressee);
						Talk(this->ID, TD.Talk.Mode, Addressee, Text, TD.Talk.CheckSpamming);
					}else{
						error(Translate("TCreature::Execute: Text ist NULL bei %s.\n",
										"TCreature::Execute: Text is NULL at %s.\n"), this->Name);
					}
					break;
				}

				case TDChangeState:{
					if(this->Type == NPC){
						ChangeNPCState(this, TD.ChangeState.NewState, true);
					}
					break;
				}

				default:{
					break;
				}
			}
		}catch(RESULT r){
			bool SnapbackNecessary = (this->ToDoClear() || this->Stop);
			if(r == EXHAUSTED){
				this->ToDoWait(1000);
				this->ToDoStart();
			}else{
				this->ToDoYield();
			}

			if(this->Type == PLAYER){
				SendResult(this->Connection, r);
				if(SnapbackNecessary
						&& r != MOVENOTPOSSIBLE
						&& r != NOTINVITED
						&& r != ENTERPROTECTIONZONE){
					SendSnapback(this->Connection);
				}
			}
			break;
		}

		if(this->Stop){
			this->ToDoClear();
			if(this->Type == PLAYER){
				SendSnapback(this->Connection);
			}
			break;
		}
	}
}

uint32 TCreature::CalculateDelay(void){
	uint32 Delay = 0;
	TToDoEntry *TD = this->ToDoList.at(this->ActToDo);
	switch(TD->Code){
		case TDWait:{
			// TODO(fusion): I'm not sure about having `EarliestWalkTime` here.
			uint32 WaitTime = TD->Wait.Time;
			if(WaitTime < this->EarliestWalkTime){
				WaitTime = this->EarliestWalkTime;
			}

			if(WaitTime > ServerMilliseconds){
				Delay = WaitTime - ServerMilliseconds;
			}
			break;
		}

		case TDGo:{
			if(this->EarliestWalkTime > ServerMilliseconds){
				Delay = this->EarliestWalkTime - ServerMilliseconds;
			}
			break;
		}

		case TDUse:{
			if(TD->Use.Obj2 != 0){
				if(this->EarliestMultiuseTime > ServerMilliseconds){
					Delay = this->EarliestMultiuseTime - ServerMilliseconds;
				}
			}
			break;
		}

		case TDAttack:{
			uint32 EarliestAttackTime = this->Combat.EarliestAttackTime;
			if(EarliestAttackTime < this->EarliestSpellTime){
				EarliestAttackTime = this->EarliestSpellTime;
			}

			if(EarliestAttackTime > ServerMilliseconds){
				Delay = EarliestAttackTime - ServerMilliseconds;
			}
			break;
		}

		default:{
			break;
		}
	}
	return Delay;
}

bool TCreature::ToDoClear(void){
	bool SnapbackNecessary = false;
	for(int i = 0; i < this->NrToDo; i += 1){
		TToDoEntry *TD = this->ToDoList.at(i);
		switch(TD->Code){
			case TDGo:{
				if(this->ActToDo <= i){
					SnapbackNecessary = true;
				}
				break;
			}

			case TDTalk:{
				DeleteDynamicString(TD->Talk.Text);
				DeleteDynamicString(TD->Talk.Addressee);
				break;
			}

			case TDChangeState:{
				if(this->ActToDo <= i && this->Type == NPC){
					ChangeNPCState(this, TD->ChangeState.NewState, false);
				}
				break;
			}

			default:{
				break;
			}
		}
	}

	this->LockToDo = false;
	this->ActToDo = 0;
	this->NrToDo = 0;
	this->Stop = false;
	return SnapbackNecessary;
}

void TCreature::ToDoAdd(TToDoEntry TD){
	if(this->LockToDo){
		if(this->ToDoClear() && this->Type == PLAYER){
			SendSnapback(this->Connection);
		}
	}

	*this->ToDoList.at(this->NrToDo) = TD;
	this->NrToDo += 1;
}

void TCreature::ToDoStop(void){
	if(this->LockToDo){
		this->Stop = true;
	}else if(this->Type == PLAYER){
		SendSnapback(this->Connection);
	}
}

void TCreature::ToDoStart(void){
	if(this->NrToDo != 0){
		this->LockToDo = true;
		this->ActToDo = 0;

		uint32 Delay = this->CalculateDelay();
		if(Delay < 1){
			Delay = 1;
		}

		uint32 NextWakeup = ServerMilliseconds + Delay;
		ToDoQueue.insert(NextWakeup, this->ID);
		this->NextWakeup = NextWakeup;
	}
}

void TCreature::ToDoYield(void){
	if(!this->LockToDo){
		this->ToDoWait(0);
		this->ToDoStart();
	}
}

void TCreature::ToDoWait(int Delay){
	TToDoEntry TD = {};
	TD.Code = TDWait;
	TD.Wait.Time = ServerMilliseconds + Delay;
	this->ToDoAdd(TD);
}

void TCreature::ToDoWaitUntil(uint32 Time){
	TToDoEntry TD = {};
	TD.Code = TDWait;
	TD.Wait.Time = Time;
	this->ToDoAdd(TD);
}

void TCreature::ToDoGo(int DestX, int DestY, int DestZ, bool MustReach, int MaxSteps){
	if(this->posz > DestZ){
		throw UPSTAIRS;
	}else if(this->posz < DestZ){
		throw DOWNSTAIRS;
	}

	if(this->LockToDo){
		TToDoEntry *Last = this->ToDoList.at(this->NrToDo - 1);
		if(Last->Code == TDGo
				&& Last->Go.x == DestX
				&& Last->Go.y == DestY
				&& Last->Go.z == DestZ){
			// TODO(fusion): Why? Shouldn't we just return here?
			throw NOERROR;
		}
	}

	int DistanceX = std::abs(DestX - this->posx);
	int DistanceY = std::abs(DestY - this->posy);
	int MaxDistance = std::max<int>(DistanceX, DistanceY);
	if(MaxDistance == 0 || (!MustReach && MaxDistance <= 1)){
		return;
	}

	// NOTE(fusion): The number of steps between two points is the same as the
	// their manhattan distance, if we exclude diagonal movement. We can skip
	// the path finder if we know we're step away from the destination.
	if(DistanceX + DistanceY == 1){
		TToDoEntry TD = {};
		TD.Code = TDGo;
		TD.Go.x = DestX;
		TD.Go.y = DestY;
		TD.Go.z = DestZ;
		this->ToDoAdd(TD);
	}else{
		int VisibleX = (this->Type == PLAYER) ? 7 : 10;
		int VisibleY = (this->Type == PLAYER) ? 7 : 10;
		TShortway Shortway(this, VisibleX, VisibleY);
		if(!Shortway.Calculate(DestX, DestY, MustReach, MaxSteps)){
			this->ToDoClear();
			if(this->Type == PLAYER){
				SendSnapback(this->Connection);
			}
			throw NOWAY;
		}
	}
}

void TCreature::ToDoRotate(int Direction){
	if(Direction != DIRECTION_NORTH
			&& Direction != DIRECTION_EAST
			&& Direction != DIRECTION_SOUTH
			&& Direction != DIRECTION_WEST){
		throw ERROR;
	}

	TToDoEntry TD = {};
	TD.Code = TDRotate;
	TD.Rotate.Direction = Direction;
	this->ToDoAdd(TD);
}

void TCreature::ToDoMove(int ObjX, int ObjY, int ObjZ, ObjectType Type, uint8 RNum,
						int DestX, int DestY, int DestZ, uint8 Count){
	Object Obj = GetObject(this->ID, ObjX, ObjY, ObjZ, RNum, Type);
	if(!Obj.exists()){
		throw NOTACCESSIBLE;
	}

	// TODO(fusion): This could be an inlined function.
	if(ObjX != 0xFFFF){ // SPECIAL_COORDINATE ?
		if(this->posz > ObjZ){
			throw UPSTAIRS;
		}else if(this->posz < ObjZ){
			throw DOWNSTAIRS;
		}

		if(!ObjectInRange(this->ID, Obj, 1)){
			this->ToDoGo(ObjX, ObjY, ObjZ, false, INT_MAX);
		}
	}

	int Delay = 100;
	if(Obj.getObjectType().isCreatureContainer()){
		Object DestBank = GetFirstObject(DestX, DestY, DestZ);
		if(DestBank == NONE || !DestBank.getObjectType().getFlag(BANK)){
			throw NOTACCESSIBLE;
		}

		TCreature *Creature = GetCreature(Obj);
		if(Creature == NULL){
			error(Translate("TCreature::ToDoMove: Kreatur existiert nicht.\n",
							"TCreature::ToDoMove: Creature does not exist.\n"));
			throw ERROR;
		}

		Delay = 1000;
		if(this->EarliestWalkTime > ServerMilliseconds){
			Delay += (int)(this->EarliestWalkTime - ServerMilliseconds);
		}
	}

	this->ToDoWait(Delay);

	TToDoEntry TD = {};
	TD.Code = TDMove;
	TD.Move.Obj = Obj.ObjectID;
	TD.Move.x = DestX;
	TD.Move.y = DestY;
	TD.Move.z = DestZ;
	TD.Move.Count = Count;
	this->ToDoAdd(TD);
}

void TCreature::ToDoMove(Object Obj, int DestX, int DestY, int DestZ, uint8 Count){
	if(!Obj.exists()){
		throw NOTACCESSIBLE;
	}

	if(!ObjectInRange(this->ID, Obj, 1)){
		this->ToDoGo(DestX, DestY, DestZ, false, INT_MAX);
	}

	int Delay = 100;
	if(Obj.getObjectType().isCreatureContainer()){
		// TODO(fusion): We don't add the delay until the earliest walk time
		// like in the other variant above.
		Delay = 1000;
	}

	this->ToDoWait(Delay);

	TToDoEntry TD = {};
	TD.Code = TDMove;
	TD.Move.Obj = Obj.ObjectID;
	TD.Move.x = DestX;
	TD.Move.y = DestY;
	TD.Move.z = DestZ;
	TD.Move.Count = Count;
	this->ToDoAdd(TD);
}

void TCreature::ToDoTrade(int ObjX, int ObjY, int ObjZ, ObjectType Type, uint8 RNum,
						uint32 TradePartner){
	Object Obj = GetObject(this->ID, ObjX, ObjY, ObjZ, RNum, Type);
	if(!Obj.exists()){
		throw NOTACCESSIBLE;
	}

	ObjectType ObjType = Obj.getObjectType();
	if(ObjType.getFlag(UNMOVE)){
		throw NOTMOVABLE;
	}

	if(!ObjType.getFlag(TAKE)){
		throw NOTTAKABLE;
	}

	if(ObjType.isCreatureContainer()){
		error(Translate("TCreature::ToDoTrade: Objekt ist eine Kreatur.\n",
						"TCreature::ToDoTrade: Object is a creature.\n"));
		throw ERROR;
	}

	if(TradePartner == 0){
		error(Translate("TCreature::ToDoTrade: Handelspartner ist Null.\n",
						"TCreature::ToDoTrade: Trading partner Null.\n"));
		throw ERROR;
	}

	TCreature *Creature = GetCreature(TradePartner);
	if(Creature == NULL){
		throw PLAYERNOTONLINE;
	}

	if(Creature->Type != PLAYER){
		error(Translate("TCreature::ToDoTrade: Handelspartner von %s ist kein Spieler.\n",
						"TCreature::ToDoTrade: Trading partner of %s is not a player.\n"), this->Name);
		throw ERROR;
	}

	// TODO(fusion): This could be an inlined function.
	if(ObjX != 0xFFFF){ // SPECIAL_COORDINATE ?
		if(this->posz > ObjZ){
			throw UPSTAIRS;
		}else if(this->posz < ObjZ){
			throw DOWNSTAIRS;
		}

		if(!ObjectInRange(this->ID, Obj, 1)){
			this->ToDoGo(ObjX, ObjY, ObjZ, false, INT_MAX);
		}
	}

	TToDoEntry TD = {};
	TD.Code = TDTrade;
	TD.Trade.Obj = Obj.ObjectID;
	TD.Trade.Partner = TradePartner;
	this->ToDoAdd(TD);
}

void TCreature::ToDoUse(uint8 Count, int ObjX1, int ObjY1, int ObjZ1, ObjectType Type1, uint8 RNum1,
						uint8 Dummy, int ObjX2, int ObjY2, int ObjZ2, ObjectType Type2, uint8 RNum2){
	Object Obj1 = GetObject(this->ID, ObjX1, ObjY1, ObjZ1, RNum1, Type1);
	if(!Obj1.exists()){
		throw NOTACCESSIBLE;
	}

	Object Obj2 = NONE;
	if(Count >= 2){
		Obj2 = GetObject(this->ID, ObjX2, ObjY2, ObjZ2, RNum2, Type2);
		if(!Obj2.exists()){
			throw NOTACCESSIBLE;
		}
	}

	// TODO(fusion): This could be an inlined function.
	if(ObjX1 != 0xFFFF){ // SPECIAL_COORDINATE ?
		if(this->posz > ObjZ1){
			throw UPSTAIRS;
		}else if(this->posz < ObjZ1){
			throw DOWNSTAIRS;
		}

		if(!ObjectInRange(this->ID, Obj1, 1)){
			this->ToDoGo(ObjX1, ObjY1, ObjZ1, false, INT_MAX);
		}
	}

	this->ToDoWait(100);

	TToDoEntry TD = {};
	TD.Code = TDUse;
	TD.Use.Obj1 = Obj1.ObjectID;
	TD.Use.Obj2 = Obj2.ObjectID;
	// TODO(fusion): This is the value sent by the client on single object use
	// that contains the next free container index.
	TD.Use.Dummy = Dummy;
	this->ToDoAdd(TD);
}

void TCreature::ToDoUse(uint8 Count, Object Obj1, Object Obj2){
	if(!Obj1.exists()){
		throw NOTACCESSIBLE;
	}

	if(Count <= 1){
		Obj2 = NONE;
	}else if(!Obj2.exists()){
		throw NOTACCESSIBLE;
	}

	if(!ObjectInRange(this->ID, Obj1, 1)){
		int ObjX1, ObjY1, ObjZ1;
		GetObjectCoordinates(Obj1, &ObjX1, &ObjY1, &ObjZ1);
		this->ToDoGo(ObjX1, ObjY1, ObjZ1, false, INT_MAX);
	}

	this->ToDoWait(100);

	TToDoEntry TD = {};
	TD.Code = TDUse;
	TD.Use.Obj1 = Obj1.ObjectID;
	TD.Use.Obj2 = Obj2.ObjectID;
	// TODO(fusion): Not sure why we set this value at all.
	TD.Use.Dummy = Count;
	this->ToDoAdd(TD);
}

void TCreature::ToDoTurn(int ObjX, int ObjY, int ObjZ, ObjectType Type, uint8 RNum){
	Object Obj = GetObject(this->ID, ObjX, ObjY, ObjZ, RNum, Type);
	if(!Obj.exists()){
		throw NOTACCESSIBLE;
	}

	// TODO(fusion): This could be an inlined function.
	if(ObjX != 0xFFFF){ // SPECIAL_COORDINATE ?
		if(this->posz > ObjZ){
			throw UPSTAIRS;
		}else if(this->posz < ObjZ){
			throw DOWNSTAIRS;
		}

		if(!ObjectInRange(this->ID, Obj, 1)){
			this->ToDoGo(ObjX, ObjY, ObjZ, false, INT_MAX);
		}
	}

	this->ToDoWait(100);

	TToDoEntry TD = {};
	TD.Code = TDTurn;
	TD.Turn.Obj = Obj.ObjectID;
	this->ToDoAdd(TD);
}

void TCreature::ToDoAttack(void){
	this->Combat.CanToDoAttack();
	if(this->Combat.GetDistance() != 1){
		this->ToDoWait(100);
	}

	TToDoEntry TD = {};
	TD.Code = TDAttack;
	this->ToDoAdd(TD);
}

void TCreature::ToDoTalk(int Mode, const char *Addressee, const char *Text, bool CheckSpamming){
	TToDoEntry TD = {};

	if(Text == NULL || Text[0] == 0){
		error(Translate("TCreature::ToDoTalk: Text ist NULL bei %s.\n",
						"TCreature::ToDoTalk: Text is NULL at %s.\n"), this->Name);

		// TODO(fusion): The original code would attempt to call `AddDynamicString`
		// even after this check but it doesn't check whether `Text` is NULL and
		// calls `strlen` immediately on it which could be a problem.
		TD.Talk.Text = 0;
	}else{
		TD.Talk.Text = AddDynamicString(Text);
	}

	if(Addressee == NULL){
		TD.Talk.Addressee = 0;
	}else{
		TD.Talk.Addressee = AddDynamicString(Addressee);
	}

	TD.Code = TDTalk;
	TD.Talk.Mode = Mode;
	TD.Talk.CheckSpamming = CheckSpamming;
	this->ToDoAdd(TD);
}

void TCreature::ToDoChangeState(int NewState){
	TToDoEntry TD = {};
	TD.Code = TDChangeState;
	TD.ChangeState.NewState = NewState;
	this->ToDoAdd(TD);
}

void TCreature::NotifyGo(void){
	// IMPORTANT(fusion): This and the function that does move `this->CrObject`
	// should be the only ones where the output of `GetObjectCoordinates` will
	// differ from `this->posx`, `this->posy`, and `this->posz`.

	int DestX, DestY, DestZ;
	GetObjectCoordinates(this->CrObject, &DestX, &DestY, &DestZ);
	MoveChainCreature(this, DestX, DestY);

	int OrigX = this->posx;
	int OrigY = this->posy;
	int OrigZ = this->posz;
	bool DiagonalMove = (DestX != OrigX && DestY != OrigY && DestZ == OrigZ);

	// IMPORTANT(fusion): `SendFloors` and `SendRow` will use the current creature
	// position to know where to pull fields from, so we need to keep them updated
	// as we go.
	if(this->Type == PLAYER && this->Connection != NULL){
		int DistanceX = std::abs(DestX - OrigX);
		int DistanceY = std::abs(DestY - OrigY);
		int DistanceZ = std::abs(DestZ - OrigZ);
		if(DistanceX <= 1 && DistanceY <= 1 && DistanceZ <= 1){
			while(this->posz < DestZ){
				this->posx -= 1;
				this->posy -= 1;
				this->posz += 1;
				SendFloors(this->Connection, false);
			}

			while(this->posz > DestZ){
				this->posx += 1;
				this->posy += 1;
				this->posz -= 1;
				SendFloors(this->Connection, true);
			}

			while(this->posx < DestX){
				this->posx += 1;
				SendRow(this->Connection, DIRECTION_EAST);
			}

			while(this->posx > DestX){
				this->posx -= 1;
				SendRow(this->Connection, DIRECTION_WEST);
			}

			while(this->posy < DestY){
				this->posy += 1;
				SendRow(this->Connection, DIRECTION_SOUTH);
			}

			while(this->posy > DestY){
				this->posy -= 1;
				SendRow(this->Connection, DIRECTION_NORTH);
			}
		}else{
			this->posx = DestX;
			this->posy = DestY;
			this->posz = DestZ;
			SendFullScreen(this->Connection);
		}
	}else{
		this->posx = DestX;
		this->posy = DestY;
		this->posz = DestZ;
	}

	if(this->Type == PLAYER){
		// NOTE(fusion): Check open containers.
		for(int ContainerNr = 0;
				ContainerNr < NARRAY(TPlayer::OpenContainer);
				ContainerNr += 1){
			Object Con = ((TPlayer*)this)->GetOpenContainer(ContainerNr);
			if(Con == NONE){
				continue;
			}

			if(!Con.exists()){
				error(Translate("TCreature::NotifyGo: OpenContainer existiert nicht. (%s, [%d,%d,%d]->[%d,%d,%d])\n",
								"TCreature::NotifyGo: OpenContainer does not exist. (%s, [%d,%d,%d]->[%d,%d,%d])\n"),
						this->Name, OrigX, OrigY, OrigZ, DestX, DestY, DestZ);
				continue;
			}

			if(!ObjectAccessible(this->ID, Con, 1)){
				((TPlayer*)this)->SetOpenContainer(ContainerNr, NONE);
				SendCloseContainer(this->Connection, ContainerNr);
			}
		}

		// NOTE(fusion): Check trade.
		Object TradeObject = ((TPlayer*)this)->TradeObject;
		if(TradeObject != NONE){
			TPlayer *Partner = GetPlayer(((TPlayer*)this)->TradePartner);
			if(!TradeObject.exists()){
				error(Translate("TCreature::NotifyGo: Handelsobjekt existiert nicht mehr.\n",
								"TCreature::NotifyGo: Trading object no longer exists.\n"));
				error("# Händler %s an [%d,%d,%d]\n", this->Name, DestX, DestY, DestZ);
				if(Partner != NULL){
					error("# Partner %s an [%d,%d,%d]\n", Partner->Name,
							Partner->posx, Partner->posy, Partner->posz);
				}
			}

			if(Partner == NULL || !ObjectAccessible(this->ID, TradeObject, 1)
					|| ObjectDistance(this->CrObject, Partner->CrObject) > 2
					|| !ThrowPossible(this->posx, this->posy, this->posz,
							Partner->posx, Partner->posy, Partner->posz, 0)){
				SendCloseTrade(this->Connection);
				SendMessage(this->Connection, TALK_FAILURE_MESSAGE, "Trade cancelled.");
				((TPlayer*)this)->RejectTrade();
			}
		}
	}

	int Waypoints = 0;
	Object Bank = GetFirstObject(DestX, DestY, DestZ);
	while(Bank != NONE){
		ObjectType BankType = Bank.getObjectType();
		if(BankType.getFlag(BANK)){
			Waypoints = BankType.getAttribute(WAYPOINTS);
			break;
		}
		Bank = Bank.getNextObject();
	}

	if(Bank != NONE){
		// NOTE(fusion): Diagonal movement has three times the delay of a regular one.
		if(DiagonalMove){
			Waypoints *= 3;
		}

		// NOTE(fusion): Compute the step interval and quantize it to be a multiple
		// of `Beat`, rounding up.
		int Delay = (Waypoints * 1000) / this->GetSpeed();
		int BeatCount = (Delay + Beat - 1) / Beat;
		this->EarliestWalkTime = ServerMilliseconds + BeatCount * Beat;
	}else{
		error(Translate("TCreature::NotifyGo: Auf Feld [%d,%d,%d] befindet sich kein Bank.\n",
						"TCreature::NotifyGo: There is no bank on field [%d,%d,%d].\n"),
				DestX, DestY, DestZ);
	}
}

void TCreature::NotifyTurn(Object DestCon){
	int DestX, DestY, DestZ;
	GetObjectCoordinates(DestCon, &DestX, &DestY, &DestZ);

	// NOTE(fusion): This is somewhat similar to `TCreature::Rotate`.
	int OffsetX = DestX - this->posx;
	int OffsetY = DestY - this->posy;
	if(OffsetX > 0){
		this->Direction = DIRECTION_EAST;
	}else if(OffsetX < 0){
		this->Direction = DIRECTION_WEST;
	}else if(OffsetY < 0){
		this->Direction = DIRECTION_NORTH;
	}else if(OffsetY > 0){
		this->Direction = DIRECTION_SOUTH;
	}
}

void TCreature::NotifyCreate(void){
    InsertChainCreature(this, 0, 0);
}

void TCreature::NotifyDelete(void){
	if(this->Type == PLAYER){
		if(this->Connection != NULL){
			this->Connection->Logout(30, true);
			this->LoggingOut = true;
		}

		((TPlayer*)this)->RejectTrade();
	}

	DeleteChainCreature(this);
}

void TCreature::NotifyChangeInventory(void){
	if(this->CrObject == NONE){
		return;
	}

	if(!this->CrObject.exists()){
		error(Translate("TCreature::NotifyChangeInventory: Kreatur-Objekt existiert nicht.\n",
						"TCreature::NotifyChangeInventory: Creature object does not exist.\n"));
		error(Translate("# Kreatur: %s, Position: %d/%d/%d.\n",
						"# Creature: %s, Position: %d/%d/%d.\n"),
				this->Name, this->posx, this->posy, this->posz);
		return;
	}

	this->Combat.CheckCombatValues();
	if(this->Type == PLAYER){
		int NewDelta[NARRAY(this->Skills)] = {};
		for(int Position = INVENTORY_FIRST;
				Position <= INVENTORY_LAST;
				Position += 1){
			Object Obj = GetBodyObject(this->ID, Position);
			if(Obj == NONE){
				continue;
			}

			ObjectType ObjType = Obj.getObjectType();
			if(!ObjType.getFlag(SKILLBOOST)){
				continue;
			}

			if(!ObjType.getFlag(CLOTHES)){
				error(Translate("TCreature::NotifyChangeInventory: Objekt %d hat SKILLBOOST, aber nicht CLOTHES.\n",
								"TCreature::NotifyChangeInventory: Object %d has SKILLBOOST, but not CLOTHES.\n"),
						ObjType.TypeID);
				continue;
			}

			if((int)ObjType.getAttribute(BODYPOSITION) != Position){
				continue;
			}

			int SkillNr = (int)ObjType.getAttribute(SKILLNUMBER);
			if(SkillNr < 0 || SkillNr >= NARRAY(this->Skills)){
				error(Translate("TCreature::NotifyChangeInventory: Objekt %d hat ungültige SKILLNUMBER %d.\n",
								"TCreature::NotifyChangeInventory: Object %d has invalid SKILLNUMBER %d.\n"),
						ObjType.TypeID, SkillNr);
				continue;
			}

			// IMPORTANT(fusion): The value for the skill modification is stored
			// as uint32 but is bitcast from a signed integer on load, meaning that
			// that bitcasting it back should retrieve the original signed value.
			//	See `LoadObjects`.
			NewDelta[SkillNr] += (int)ObjType.getAttribute(SKILLMODIFICATION);
		}

		bool SkillsChanged = false;
		for(int SkillNr = 0;
				SkillNr < NARRAY(this->Skills);
				SkillNr += 1){
			TSkill *Skill = this->Skills[SkillNr];
			if(Skill->DAct != NewDelta[SkillNr]){
				SkillsChanged = true;
				Skill->DAct = NewDelta[SkillNr];
				if(SkillNr == SKILL_GO_STRENGTH){
					AnnounceChangedCreature(this->ID, CREATURE_SPEED_CHANGED);
				}else if(SkillNr == SKILL_ILLUSION){
					if(NewDelta[SKILL_ILLUSION] > 0){
						if(this->Outfit.OutfitID != 0 || this->Outfit.ObjectType != 0){
							if(Skill->TimerValue() != 0){
								this->SetTimer(SKILL_ILLUSION, 0, 0, 0, -1);
							}
							this->Outfit.OutfitID = 0;
							this->Outfit.ObjectType = 0;
						}
					}else if(Skill->TimerValue() == 0){
						this->Outfit = this->OrgOutfit;
					}
					AnnounceChangedCreature(this->ID, CREATURE_OUTFIT_CHANGED);
				}
			}
		}

		if(SkillsChanged){
			SendPlayerSkills(this->Connection);
			((TPlayer*)this)->CheckState();
		}

		SendPlayerData(this->Connection);
	}
}
