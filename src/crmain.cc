#include "cr.hh"
#include "communication.hh"
#include "config.hh"
#include "enums.hh"
#include "info.hh"
#include "operate.hh"
#include "writer.hh"

#include <dirent.h>

TRaceData RaceData[MAX_RACES];
priority_queue<uint32, uint32> ToDoQueue(5000, 1000);

static TCreature *HashList[1000];
static matrix<uint32> *FirstChainCreature;
static vector<TCreature*> CreatureList(0, 10000, 1000, NULL);
static int FirstFreeCreature;
static uint32 NextCreatureID;

static int KilledCreatures[MAX_RACES];
static int KilledPlayers[MAX_RACES];

static priority_queue<uint32, TAttackWave*> AttackWaveQueue(100, 100);

// TFindCreatures
// =============================================================================
TFindCreatures::TFindCreatures(int RadiusX, int RadiusY, int CenterX, int CenterY, int Mask){
	this->initSearch(RadiusX, RadiusY, CenterX, CenterY, Mask);
}

TFindCreatures::TFindCreatures(int RadiusX, int RadiusY, uint32 CreatureID, int Mask){
	TCreature *Creature = GetCreature(CreatureID);
	if(Creature == NULL){
		error(Translate("TFindCreatures::TFindCreatures: Kreatur existiert nicht.\n",
						"TFindCreatures::TFindCreatures: Creature does not exist.\n"));
		this->finished = true;
		return;
	}

	this->initSearch(RadiusX, RadiusY, Creature->posx, Creature->posy, Mask);
	this->SkipID = Creature->ID;
}

TFindCreatures::TFindCreatures(int RadiusX, int RadiusY, Object Obj, int Mask){
	if(!Obj.exists()){
		error(Translate("TFindCreatures::TFindCreatures: Übergebenes Objekt existiert nicht.\n",
						"TFindCreatures::TFindCreatures: Passed object does not exist.\n"));
		this->finished = true;
		return;
	}

	int ObjX, ObjY, ObjZ;
	GetObjectCoordinates(Obj, &ObjX, &ObjY, &ObjZ);
	this->initSearch(RadiusX, RadiusY, ObjX, ObjY, Mask);
}

void TFindCreatures::initSearch(int RadiusX, int RadiusY, int CenterX, int CenterY, int Mask){
	this->startx = CenterX - RadiusX;
	this->starty = CenterY - RadiusY;
	this->endx = CenterX + RadiusX;
	this->endy = CenterY + RadiusY;
	// NOTE(fusion): See `TFindCreatures::getNext` for an explanation on the -1.
	this->blockx = (this->startx / 16) - 1;
	this->blocky = (this->starty / 16);
	this->ActID = 0;
	this->SkipID = 0;
	this->Mask = Mask;
	this->finished = false;
}

uint32 TFindCreatures::getNext(void){
	if(this->finished){
		return 0;
	}

	int StartBlockX = this->startx / 16;
	int EndBlockX = this->endx / 16;
	int EndBlockY = this->endy / 16;
	while(true){
		while(this->ActID == 0){
			this->blockx += 1;
			if(this->blockx > EndBlockX){
				this->blockx = StartBlockX;
				this->blocky += 1;
				if(this->blocky > EndBlockY){
					this->finished = true;
					return 0;
				}
			}

			uint32 *FirstID = FirstChainCreature->boundedAt(this->blockx, this->blocky);
			if(FirstID != NULL){
				this->ActID = *FirstID;
			}else{
				this->ActID = 0;
			}
		}

		TCreature *Creature = GetCreature(this->ActID);
		if(Creature == NULL){
			error(Translate("TFindCreatures::getNext: Kreatur existiert nicht.\n",
							"TFindCreatures::getNext: Creature does not exist.\n"));
			this->ActID = 0;
			continue;
		}

		this->ActID = Creature->NextChainCreature;
		if(Creature->ID == this->SkipID
				|| Creature->posx < this->startx || Creature->posx > this->endx
				|| Creature->posy < this->starty || Creature->posy > this->endy
				|| (Creature->Type == PLAYER  && (this->Mask & 0x01) == 0)
				|| (Creature->Type == NPC     && (this->Mask & 0x02) == 0)
				|| (Creature->Type == MONSTER && (this->Mask & 0x04) == 0)){
			continue;
		}

		return Creature->ID;
	}
}

// TCreature
// =============================================================================
TCreature::TCreature(void) :
		TSkillBase(),
		Combat(),
		ToDoList(0, 20, 10)
{
	this->Combat.Master = this;
	this->ID = 0;
	this->NextHashEntry = NULL;
	this->NextChainCreature =  0;
	this->Name[0] = 0;
	this->Murderer[0] = 0;
	this->OrgOutfit = {};
	this->Outfit = {};
	this->startx = 0;
	this->starty = 0;
	this->startz = 0;
	this->posx = 0;
	this->posy = 0;
	this->posz = 0;
	this->Sex = 1;
	this->Race = 0;
	this->Direction = DIRECTION_SOUTH;
	this->Radius = INT_MAX;
	this->IsDead = false;
	this->LoseInventory = LOSE_INVENTORY_ALL;
	this->LoggingOut = false;
	this->LogoutAllowed = false;
	this->EarliestLogoutRound = 0;
	this->EarliestProtectionZoneRound = 0;
	this->EarliestYellRound = 0;
	this->EarliestTradeChannelRound = 0;
	this->EarliestSpellTime = 0;
	this->EarliestMultiuseTime = 0;
	this->EarliestWalkTime = 0;
	this->LifeEndRound = 0;
	this->FirstKnowingConnection = NULL;
	this->SummonedCreatures = 0;
	this->FireDamageOrigin = 0;
	this->PoisonDamageOrigin = 0;
	this->EnergyDamageOrigin = 0;
	this->CrObject = NONE;
	this->ActToDo = 0;
	this->NrToDo = 0;
	this->NextWakeup = 0;
	this->Stop = false;
	this->LockToDo = false;
	this->Connection = NULL;

	for(int i = 0; i < NARRAY(this->Skills); i += 1){
		this->NewSkill((uint16)i, this);
	}
}

TCreature::~TCreature(void){
	// TODO(fusion): Bruuhh... these exceptions...
	if(this->IsDead){
		int Race = this->Race;
		int PoolLiquid = LIQUID_NONE;
		if(RaceData[Race].Blood == BT_BLOOD){
			PoolLiquid = LIQUID_BLOOD;
		}else if(RaceData[Race].Blood == BT_SLIME){
			PoolLiquid = LIQUID_SLIME;
		}

		if(PoolLiquid != LIQUID_NONE){
			try{
				CreatePool(GetMapContainer(this->CrObject),
						GetSpecialObject(BLOOD_POOL),
						PoolLiquid);
			}catch(RESULT r){
				if(r != NOROOM && r != DESTROYED){
					error(Translate("TCreature::~TCreature: Kann Blutlache nicht setzen (Exc %d, Pos [%d,%d,%d]).\n",
									"TCreature::~TCreature: Cannot set Blood Pool (Exc %d, Pos [%d,%d,%d]).\n"),
							r, this->posx, this->posy, this->posz);
				}
			}
		}

		ObjectType CorpseType = (this->Sex == 1) // MALE ?
				? RaceData[Race].MaleCorpse
				: RaceData[Race].FemaleCorpse;

		if(CorpseType.getFlag(MAGICFIELD)){
			Object Obj = GetFirstObject(this->posx, this->posy, this->posz);
			while(Obj != NONE){
				Object Next = Obj.getNextObject();
				if(Obj.getObjectType().getFlag(MAGICFIELD)){
					try{
						Delete(Obj, -1);
					}catch(RESULT r){
						error(Translate("TCreature::~TCreature: Exception %d beim Löschen eines Feldes.\n",
										"TCreature::~TCreature: Exception %d when deleting a field.\n"), r);
					}
				}
				Obj = Next;
			}
		}

		try{
			Object Con = GetMapContainer(this->posx, this->posy, this->posz);
			Object Corpse = Create(Con, CorpseType, 0);
			Log("game", Translate("Tod von %s: LoseInventory=%d.\n",
								  "Death of %s: LoseInventory=%d.\n"), this->Name, this->LoseInventory);

			if(this->Type == PLAYER){
				char Help[128];
				sprintf(Help, "You recognize %s", this->Name);
				if(this->Murderer[0] != 0){
					if(this->Sex == 1){ // MALE ?
						strcat(Help, ". He was killed by ");
					}else{
						strcat(Help, ". She was killed by ");
					}
					strcat(Help, this->Murderer);
				}
				Change(Corpse, TEXTSTRING, AddDynamicString(Help));
			}

			if(this->LoseInventory != LOSE_INVENTORY_NONE){
				for(int Position = INVENTORY_FIRST;
						Position <= INVENTORY_LAST;
						Position += 1){
					Object Item = GetBodyObject(this->ID, Position);
					if(Item == NONE){
						continue;
					}

					if(this->LoseInventory == LOSE_INVENTORY_ALL
							|| Item.getObjectType().getFlag(CONTAINER)
							|| random(0, 9) == 0){
						::Move(0, Item, Corpse, -1, false, NONE);
					}
				}
			}

			if(this->Type == PLAYER && this->LoseInventory != LOSE_INVENTORY_ALL){
				((TPlayer*)this)->SaveInventory();
			}
		}catch(RESULT r){
			error(Translate("TCreature::~TCreature: Kann Leiche/Inventory nicht ablegen (Exc %d, Pos [%d,%d,%d], %s).\n",
							"TCreature::~TCreature: Cannot drop corpse/inventory (Exc %d, Pos [%d,%d,%d], %s).\n"),
					r, this->posx, this->posy, this->posz, this->Name);
		}
	}

	if(this->CrObject != NONE && this->CrObject.exists()){
		this->DelOnMap();
	}

	this->ToDoClear();

	if(this->Type == PLAYER && this->Connection != NULL){
		this->Connection->Logout(30, true);
	}

	this->DelInCrList();

	if(this->ID != 0){
		this->DelID();
	}

	for(TKnownCreature *KnownCreature = this->FirstKnowingConnection;
			KnownCreature != NULL;
			KnownCreature = KnownCreature->Next){
		if(KnownCreature->CreatureID != this->ID){
			error(Translate("TCreature::~TCreature: Verkettungsfehler bei Kreatur %u.\n",
							"TCreature::~TCreature: Creature chaining error %u.\n"), this->ID);
		}
		KnownCreature->State = KNOWNCREATURE_FREE;
	}
}

void TCreature::SetID(uint32 CharacterID){
	if(this->ID != 0){
		error(Translate("TCreature::SetID: ID ist schon gesetzt.\n",
						"TCreature::SetID: ID is already set.\n"));
	}

	uint32 CreatureID = 0;
	if(CharacterID == 0){
		bool Found = false;
		for(int Attempts = 0; Attempts < 16; Attempts += 1){
			CreatureID = NextCreatureID++;
			if(GetCreature(CreatureID) == NULL){
				Found = true;
				break;
			}
		}

		if(!Found){
			error(Translate("TCreature::SetID: 16x hintereinander doppelte ID. Verwende nun doppelte ID %d\n",
							"TCreature::SetID: 16x duplicate ID in a row. Now use duplicate ID %d\n"), CreatureID);
		}
	}else{
		CreatureID = CharacterID;
		if(GetCreature(CreatureID) != NULL){
			error(Translate("TCreature::SetID: Doppelte Character-ID %d gefunden.\n",
							"TCreature::SetID: Duplicate character ID %d found.\n"), CharacterID);
		}
	}

	uint32 ListIndex = CreatureID % NARRAY(HashList);
	this->ID = CreatureID;
	this->NextHashEntry = HashList[ListIndex];
	HashList[ListIndex] = this;
}

void TCreature::DelID(void){
	uint32 ListIndex = this->ID % NARRAY(HashList);
	TCreature *First = HashList[ListIndex];
	if(First == NULL){
		error(Translate("TCreature::DelID: Hasheintrag nicht gefunden id = %d\n",
						"TCreature::DelID: Hash entry not found id = %d\n"), this->ID);
		return;
	}

	if(First->ID == this->ID){
		HashList[ListIndex] = this->NextHashEntry;
	}else{
		TCreature *Prev = First;
		TCreature *Current = First->NextHashEntry;
		while(true){
			if(Current == NULL){
				error(Translate("TCreature::DelID: id=%d nicht gefunden.\n",
								"TCreature::DelID: id=%d not found.\n"), this->ID);
				return;
			}

			if(Current->ID == this->ID){
				Prev->NextHashEntry = this->NextHashEntry;
				break;
			}

			Prev = Current;
			Current = Current->NextHashEntry;
		}
	}
}

void TCreature::SetInCrList(void){
	*CreatureList.at(FirstFreeCreature) = this;
	FirstFreeCreature += 1;
}

void TCreature::DelInCrList(void){
	// TODO(fusion): See note in `ProcessCreatures`.
	for(int Index = 0; Index < FirstFreeCreature; Index += 1){
		TCreature **Current = CreatureList.at(Index);
		if(*Current == this){
			TCreature **Last = CreatureList.at(FirstFreeCreature - 1);
			*Current = *Last;
			*Last = NULL;
			FirstFreeCreature -= 1;

			// TODO(fusion): The original function wouldn't break here. Maybe it
			// is possible to have duplicates in `CreatureList`?
			//break;
		}
	}
}

void TCreature::StartLogout(bool Force, bool StopFight){
	this->LoggingOut = true;
	if(Force || LagDetected()){
		this->LogoutAllowed = true;
	}

	if(this->Type == PLAYER && this->Connection != NULL){
		this->Connection->Logout(0, true);
	}

	this->Combat.StopAttack(StopFight ? 0 : 60);
}

int TCreature::LogoutPossible(void){
	if(!this->LogoutAllowed && !this->IsDead && !GameEnding()){
		if(this->EarliestLogoutRound > RoundNr && !LagDetected()){
			return 1; // LOGOUT_COMBAT ?
		}

		if(IsNoLogoutField(this->posx, this->posy, this->posz)){
			return 2; // LOGOUT_FIELD ?
		}

		this->LogoutAllowed = true;
	}

	return 0; // LOGOUT_OK ?
}

void TCreature::BlockLogout(int Delay, bool BlockProtectionZone){
	if(WorldType == NON_PVP){
		BlockProtectionZone = false;
	}

	if(this->Type == PLAYER && !CheckRight(this->ID, NO_LOGOUT_BLOCK)){
		if(BlockProtectionZone || this->EarliestProtectionZoneRound > RoundNr){
			uint32 EarliestProtectionZoneRound = RoundNr + Delay;
			if(this->EarliestProtectionZoneRound < EarliestProtectionZoneRound){
				this->EarliestProtectionZoneRound = EarliestProtectionZoneRound;
			}
		}else if(this->Connection == NULL){
			// NOTE(fusion): This is a failsafe to avoid extending the earliest
			// logout round of a player that got disconnected in combat.
			return;
		}

		uint32 EarliestLogoutRound = RoundNr + Delay;
		if(this->EarliestLogoutRound < EarliestLogoutRound){
			this->EarliestLogoutRound = EarliestLogoutRound;
		}

		((TPlayer*)this)->CheckState();
	}
}

int TCreature::GetHealth(void){
	int MaxHitPoints = this->Skills[SKILL_HITPOINTS]->Max;
	if(MaxHitPoints <= 0){
		if(!this->IsDead){
			error(Translate("TCreature::GetHealth: MaxHitpoints von %s ist %d, obwohl sie nicht tot ist.\n",
							"TCreature::GetHealth: MaxHitpoints of %s is %d, although she is not dead.\n"),
					this->Name, MaxHitPoints);
		}
		return 0;
	}

	int CurrentHitPoints = this->Skills[SKILL_HITPOINTS]->Get();
	int Health = CurrentHitPoints * 100 / MaxHitPoints;
	if(Health <= 0){
		Health = (int)(CurrentHitPoints != 0);
	}
	return Health;
}

int TCreature::GetSpeed(void){
	TSkill *GoStrength = this->Skills[SKILL_GO_STRENGTH];
	if(GoStrength == NULL){
		error(Translate("TCreature::GetSpeed: Kein Skill GOSTRENGTH vorhanden.\n",
						"TCreature::GetSpeed: No GOSTRENGTH skill available.\n"));
		return 0;
	}

	return GoStrength->Get() * 2 + 80;
}

int TCreature::Damage(TCreature *Attacker, int Damage, int DamageType){
	if(this->IsDead || this->Type == NPC){
		return 0;
	}

	if(Attacker != NULL && this->Type == PLAYER){
		if(this->Connection != NULL){
			SendMarkCreature(this->Connection, Attacker->ID, COLOR_BLACK);
		}

		if(Attacker->Type == PLAYER
				&& DamageType != DAMAGE_POISON_PERIODIC
				&& DamageType != DAMAGE_FIRE_PERIODIC
				&& DamageType != DAMAGE_ENERGY_PERIODIC){
			Damage = (Damage + 1) / 2;
		}
	}

	// NOTE(fusion): `Responsible` is either the attacker or its master. It is
	// always valid if `Attacker` is not NULL.
	uint32 AttackerID = 0;
	TCreature *Responsible = Attacker;
	if(Attacker != NULL){
		AttackerID = Attacker->ID;

		uint32 MasterID = Attacker->GetMaster();
		if(MasterID != 0){
			// NOTE(fusion): This is very subtle but we could hit a case where the
			// master logs out or dies but the summon has a ToDoAttack queued, in
			// which case it would try to attack before checking whether it should
			// despawn in `TMonster::IdleStimulus`, causing `Responsible` to be NULL
			// even though `Attacker` is not.
			TCreature *Master = GetCreature(MasterID);
			if(Master != NULL){
				Responsible = Master;
			}
		}

		Attacker->BlockLogout(60, this->Type == PLAYER);
		if(Responsible != Attacker){
			Responsible->BlockLogout(60, this->Type == PLAYER);
		}

		if(this->Type == PLAYER && Responsible->Type == PLAYER){
			((TPlayer*)Responsible)->RecordAttack(this->ID);
		}
	}

	if(this->Type == PLAYER){
		if(CheckRight(this->ID, INVULNERABLE)){
			Damage = 0;
		}

		for(int Position = INVENTORY_FIRST;
				Position <= INVENTORY_LAST && Damage > 0;
				Position += 1){
			Object Obj = GetBodyObject(this->ID, Position);
			if(Obj == NONE){
				continue;
			}

			ObjectType ObjType = Obj.getObjectType();
			if(ObjType.getFlag(PROTECTION) && ObjType.getFlag(CLOTHES)
					&& (int)ObjType.getAttribute(BODYPOSITION) == Position
					&& (ObjType.getAttribute(PROTECTIONDAMAGETYPES) & DamageType) != 0){
				int DamageReduction = ObjType.getAttribute(DAMAGEREDUCTION);
				Damage = (Damage * (100 - DamageReduction)) / 100;
				if(ObjType.getFlag(WEAROUT)){
					// TODO(fusion): Ugh... The try..catch block might be used only
					// when changing the object's type.
					try{
						uint32 RemainingUses = Obj.getAttribute(REMAININGUSES);
						if(RemainingUses > 1){
							Change(Obj, REMAININGUSES, RemainingUses - 1);
						}else{
							ObjectType WearOutType = (int)ObjType.getAttribute(WEAROUTTARGET);
							Change(Obj, WearOutType, 0);
						}
					}catch(RESULT r){
						error(Translate("TCreature::Damage: Exception %d beim Abnutzen von Objekt %d.\n",
										"TCreature::Damage: Exception %d when wearing out object %d.\n"),
								r, ObjType.TypeID);
					}
				}
			}else if(ObjType.getFlag(PROTECTION) && !ObjType.getFlag(CLOTHES)){
				error(Translate("TCreature::Damage: Objekt %d hat PROTECTION, aber nicht CLOTHES.\n",
								"TCreature::Damage: Object %d has PROTECTION, but not CLOTHES.\n"),
						ObjType.TypeID);
			}
		}
	}

	if(Damage <= 0){
		GraphicalEffect(this->CrObject, EFFECT_POFF);
		return 0;
	}

	if(DamageType == DAMAGE_POISON_PERIODIC){
		if(RaceData[this->Race].NoPoison){
			return 0;
		}

		if(Damage > this->Skills[SKILL_POISON]->TimerValue()){
			this->PoisonDamageOrigin = AttackerID;
			this->SetTimer(SKILL_POISON, Damage, 3, 3, -1);
		}

		this->DamageStimulus(AttackerID, Damage, DamageType);
		return Damage;
	}else if(DamageType == DAMAGE_FIRE_PERIODIC){
		if(RaceData[this->Race].NoBurning){
			return 0;
		}

		this->FireDamageOrigin = AttackerID;
		this->SetTimer(SKILL_BURNING, Damage / 10, 8, 8, -1);
		this->DamageStimulus(AttackerID, Damage, DamageType);
		return Damage;
	}else if(DamageType == DAMAGE_ENERGY_PERIODIC){
		if(RaceData[this->Race].NoEnergy){
			return 0;
		}

		// TODO(fusion): Shouldn't we use `Damage / 25` here?
		this->EnergyDamageOrigin = AttackerID;
		this->SetTimer(SKILL_ENERGY, Damage / 20, 10, 10, -1);
		this->DamageStimulus(AttackerID, Damage, DamageType);
		return Damage;
	}

	if((DamageType == DAMAGE_PHYSICAL && RaceData[this->Race].NoHit)
	|| (DamageType == DAMAGE_POISON && RaceData[this->Race].NoPoison)
	|| (DamageType == DAMAGE_FIRE && RaceData[this->Race].NoBurning)
	|| (DamageType == DAMAGE_ENERGY && RaceData[this->Race].NoEnergy)
	|| (DamageType == DAMAGE_LIFEDRAIN && RaceData[this->Race].NoLifeDrain)){
		GraphicalEffect(this->CrObject, EFFECT_BLOCK_HIT);
		return 0;
	}

	if(DamageType == DAMAGE_PHYSICAL){
		Damage -= this->Combat.GetArmorStrength();
		if(Damage <= 0){
			GraphicalEffect(this->CrObject, EFFECT_BLOCK_HIT);
			return 0;
		}
	}

	this->DamageStimulus(AttackerID, Damage, DamageType);

	// NOTE(fusion): Remove non-player invisibility. Might as well be an inlined
	// function.
	if(this->Type != PLAYER && this->IsInvisible()){
		this->SetTimer(SKILL_ILLUSION, 0, 0, 0, -1);
		this->Outfit = this->OrgOutfit;
		AnnounceChangedCreature(this->ID, CREATURE_OUTFIT_CHANGED);
		NotifyAllCreatures(this->CrObject, OBJECT_CHANGED, NONE);
	}

	if(DamageType == DAMAGE_MANADRAIN){
		int ManaPoints = this->Skills[SKILL_MANA]->Get();
		if(Damage > ManaPoints){
			Damage = ManaPoints;
		}

		if(Damage > 0){
			this->Skills[SKILL_MANA]->Change(-Damage);
			if(this->Type == PLAYER && this->Connection != NULL){
				SendMessage(this->Connection, TALK_STATUS_MESSAGE,
						"You lose %d mana.", Damage);
			}
			GraphicalEffect(this->CrObject, EFFECT_MAGIC_RED);
			TextualEffect(this->CrObject, COLOR_BLUE, "%d", Damage);
		}

		return Damage;
	}

	if(this->Skills[SKILL_MANASHIELD]->TimerValue() > 0
			|| this->Skills[SKILL_MANASHIELD]->Get() > 0){
		// NOTE(fusion): We only send these if the attack was fully absorbed,
		// else it'd be overwritten by whatever effect and messages we send
		// next, when the victim's hitpoints are actually touched.
		int ManaPoints = this->Skills[SKILL_MANA]->Get();
		if(Damage <= ManaPoints){
			this->Skills[SKILL_MANA]->Change(-Damage);
			GraphicalEffect(this->CrObject, EFFECT_MANA_HIT);
			TextualEffect(this->CrObject, COLOR_BLUE, "%d", Damage);
			if(this->Type == PLAYER && this->Connection != NULL){
				if(Attacker != NULL){
					SendMessage(this->Connection, TALK_STATUS_MESSAGE,
							"You lose %d mana blocking an attack by %s.",
							Damage, Attacker->Name);
				}else{
					SendMessage(this->Connection, TALK_STATUS_MESSAGE,
							"You lose %d mana.", Damage);
				}
				SendPlayerData(this->Connection);
			}
			return Damage;
		}

		this->Skills[SKILL_MANA]->Set(0);
		Damage -= ManaPoints;
	}

	int HitPoints = this->Skills[SKILL_HITPOINTS]->Get();
	if(Damage > HitPoints){
		Damage = HitPoints;
	}

	this->Skills[SKILL_HITPOINTS]->Change(-Damage);
	if(Attacker != NULL){
		ASSERT(Responsible != NULL);
		if(Responsible == Attacker){
			this->Combat.AddDamageToCombatList(Attacker->ID, Damage);
		}else{
			this->Combat.AddDamageToCombatList(Attacker->ID, Damage / 2);
			this->Combat.AddDamageToCombatList(Responsible->ID, Damage / 2);
		}
	}

	int HitEffect = EFFECT_NONE;
	int TextColor = COLOR_BLACK;
	int SplashLiquid = LIQUID_NONE;
	if(DamageType == DAMAGE_PHYSICAL){
		switch(RaceData[this->Race].Blood){
			case BT_BLOOD:{
				HitEffect = EFFECT_BLOOD_HIT;
				TextColor = COLOR_RED;
				SplashLiquid = LIQUID_BLOOD;
				break;
			}
			case BT_SLIME:{
				HitEffect = EFFECT_POISON_HIT;
				TextColor = COLOR_LIGHTGREEN;
				SplashLiquid = LIQUID_SLIME;
				break;
			}
			case BT_BONES:{
				HitEffect = EFFECT_BONE_HIT;
				TextColor = COLOR_LIGHTGRAY;
				break;
			}
			case BT_FIRE:{
				HitEffect = EFFECT_FIRE_HIT;
				TextColor = COLOR_ORANGE;
				break;
			}
			case BT_ENERGY:{
				HitEffect = EFFECT_ENERGY_HIT;
				TextColor = COLOR_LIGHTBLUE;
				break;
			}
			default:{
				error(Translate("TCreature::Damage: Ungültiger Bluttyp %d für Rasse %d.\n",
								"TCreature::Damage: Invalid blood type %d for race %d.\n"),
						RaceData[this->Race].Blood, this->Race);
				break;
			}
		}
	}else if(DamageType == DAMAGE_POISON){
		HitEffect = EFFECT_POISON;
		TextColor = COLOR_LIGHTGREEN;
	}else if(DamageType == DAMAGE_FIRE){
		HitEffect = EFFECT_FIRE_HIT;
		TextColor = COLOR_ORANGE;
	}else if(DamageType == DAMAGE_ENERGY){
		HitEffect = EFFECT_ENERGY_HIT;
		TextColor = COLOR_LIGHTBLUE;
	}else if(DamageType == DAMAGE_LIFEDRAIN){
		HitEffect = EFFECT_MAGIC_RED;
		TextColor = COLOR_RED;
	}else{
		// TODO(fusion): The original decompiled function would return here but
		// I don't think it's a good idea because it would skip death handling.
		error(Translate("TCreature::Damage: Ungültiger Schadenstyp %d.\n",
						"TCreature::Damage: Invalid damage type %d.\n"), DamageType);
		//return Damage;
	}

	if(HitEffect != EFFECT_NONE){
		GraphicalEffect(this->CrObject, HitEffect);
		TextualEffect(this->CrObject, TextColor, "%d", Damage);
		if(SplashLiquid != LIQUID_NONE){
			try{
				CreatePool(GetMapContainer(this->CrObject),
							GetSpecialObject(BLOOD_SPLASH),
							SplashLiquid);
			}catch(RESULT r){
				// TODO(fusion): Ignore?
			}
		}
	}

	if(this->Type == PLAYER && this->Connection != NULL){
		if(Attacker != NULL){
			SendMessage(this->Connection, TALK_STATUS_MESSAGE,
					"You lose %d hitpoint%s due to an attack by %s.",
					Damage, (Damage != 1 ? "s" : ""), Attacker->Name);
		}else{
			SendMessage(this->Connection, TALK_STATUS_MESSAGE,
					"You lose %d hitpoint%s.",
					Damage, (Damage != 1 ? "s" : ""));
		}
		SendPlayerData(this->Connection);
	}

	if(Damage == HitPoints){
		if(this->Type == PLAYER){
			for(int Position = INVENTORY_FIRST;
					Position <= INVENTORY_LAST;
					Position += 1){
				Object Obj = GetBodyObject(this->ID, Position);
				if(Obj == NONE){
					continue;
				}

				ObjectType ObjType = Obj.getObjectType();
				if(ObjType.getFlag(CLOTHES) && (int)ObjType.getAttribute(BODYPOSITION) == Position){
					ObjectType AmuletOfLossType = GetNewObjectType(77, 12);
					if(ObjType == AmuletOfLossType){
						Log("game", "%s stirbt mit Amulett of Loss.\n", this->Name);
						this->LoseInventory = LOSE_INVENTORY_NONE;
						try{
							Delete(Obj, -1);
							// TODO(fusion): Shouldn't we break here? We could also
							// just check if there is an amulet of loss in the necklace
							// container instead of iterating over all inventory.
						}catch(RESULT r){
							error(Translate("TCreature::Damage: Exception %d beim Löschen von Objekt %d.\n",
											"TCreature::Damage: Exception %d when deleting object %d.\n"),
									r, AmuletOfLossType.TypeID);
						}
					}
				}
			}
		}

		int OldLevel = this->Skills[SKILL_LEVEL]->Get();
		this->Death();
		if(Attacker != NULL && this->Type == PLAYER){
			Attacker->BlockLogout(900, true);
			if(Responsible != Attacker){
				Responsible->BlockLogout(900, true);
			}
		}

		uint32 MurdererID = 0;
		char Remark[30] = {};
		if(Attacker == NULL){
			AddKillStatistics(0, this->Race);
			if(DamageType == DAMAGE_PHYSICAL){
				strcpy(Remark, "a hit");
			}else if(DamageType == DAMAGE_POISON){
				strcpy(Remark, "poison");
			}else if(DamageType == DAMAGE_FIRE){
				strcpy(Remark, "fire");
			}else if(DamageType == DAMAGE_ENERGY){
				strcpy(Remark, "energy");
			}else{
				// NOTE(fusion): We probably don't expect any other damage type
				// as the cause of death when there is no attacker.
				error(Translate("TCreature::Damage: Ungültiger Schadenstyp %d als Todesursache.\n",
								"TCreature::Damage: Invalid damage type %d as cause of death.\n"), DamageType);
			}
		}else{
			AddKillStatistics(Attacker->Race, this->Race);
			strcpy(this->Murderer, Attacker->Name);

			if(Responsible->Type == PLAYER){
				MurdererID = Responsible->ID;
			}

			if(Attacker->Type != PLAYER){
				strcpy(Remark, Attacker->Name);
			}
		}

		if(this->Type == PLAYER){
			((TPlayer*)this)->RecordDeath(MurdererID, OldLevel, Remark);

			uint32 MostDangerousID = this->Combat.GetMostDangerousAttacker();
			if(MostDangerousID != 0
					&& MostDangerousID != MurdererID
					&& IsCreaturePlayer(MostDangerousID)){
				// TODO(fusion): The original function is confusing at this point
				// but it seems correct that the remark is included only with the
				// murderer.
				((TPlayer*)this)->RecordDeath(MostDangerousID, OldLevel, "");
			}
		}
	}

	AnnounceChangedCreature(this->ID, CREATURE_HEALTH_CHANGED);
	return Damage;
}

void TCreature::Death(void){
	this->IsDead = true;
	this->LoggingOut = true;
}

bool TCreature::MovePossible(int x, int y, int z, bool Execute, bool Jump){
	bool Result;

	if(Jump){
		Result = JumpPossible(x, y, z, false);
	}else{
		Result = CoordinateFlag(x, y, z, BANK)
			&& !CoordinateFlag(x, y, z, UNPASS);
	}

	if(Result && !Execute && CoordinateFlag(x, y, z, AVOID)){
		Result = false;
	}

	return Result;
}

bool TCreature::IsPeaceful(void){
	return true;
}

uint32 TCreature::GetMaster(void){
	return 0;
}

void TCreature::TalkStimulus(uint32 SpeakerID, const char *Text){
	// no-op
}

void TCreature::DamageStimulus(uint32 AttackerID, int Damage, int DamageType){
	// no-op
}

void TCreature::IdleStimulus(void){
	// no-op
}

void TCreature::CreatureMoveStimulus(uint32 CreatureID, int Type){
	if(CreatureID == 0 || CreatureID == this->ID
			|| this->IsDead
			|| this->Combat.AttackDest != CreatureID
			|| this->Combat.ChaseMode != CHASE_MODE_CLOSE
			|| this->Combat.EarliestAttackTime <= (ServerMilliseconds + 200)){
		return;
	}

	if(Type != OBJECT_CHANGED
			|| !this->LockToDo
			|| this->ActToDo >= this->NrToDo
			|| this->ToDoList.at(this->ActToDo)->Code != TDAttack){
		return;
	}

	TCreature *Target = GetCreature(this->Combat.AttackDest);
	if(Target == NULL){
		return;
	}

	int Distance = ObjectDistance(this->CrObject, Target->CrObject);
	if(Distance <= 1){
		return;
	}

	// TODO(fusion): Review this.
	try{
		if(this->ToDoClear() && this->Type == PLAYER){
			SendSnapback(this->Connection);
		}
		this->ToDoWait(200);
		this->ToDoAttack();
		this->ToDoStart();
	}catch(RESULT r){
		if(this->Type == PLAYER){
			SendResult(this->Connection, r);
		}
		this->ToDoClear();
		this->ToDoWaitUntil(this->Combat.EarliestAttackTime);
		this->ToDoStart();
	}
}

void TCreature::AttackStimulus(uint32 AttackerID){
	// no-op
}

// Creature Management
// =============================================================================
bool IsCreaturePlayer(uint32 CreatureID){
	return CreatureID < 0x40000000;
}

TCreature *GetCreature(uint32 CreatureID){
	if(CreatureID == 0){
		return NULL;
	}

	TCreature *Creature = HashList[CreatureID % NARRAY(HashList)];
	while(Creature != NULL && Creature->ID != CreatureID){
		Creature = Creature->NextHashEntry;
	}

	return Creature;
}

TCreature *GetCreature(Object Obj){
	return GetCreature(Obj.getCreatureID());
}

void InsertChainCreature(TCreature *Creature, int CoordX, int CoordY){
	if(Creature == NULL){
		// TODO(fusion): Maybe a typo on the name of the function? I thought it
		// could be some type of macro because there was no function name mismatch
		// until now.
		error(Translate("DeleteChainCreature: Übegebene Kreatur existiert nicht.\n",
						"DeleteChainCreature: Surrendered creature does not exist.\n"));
		return;
	}

	if(CoordX == 0){
		CoordX = Creature->posx;
	}

	if(CoordY == 0){
		CoordY = Creature->posy;
	}

	int ChainX = CoordX / 16;
	int ChainY = CoordY / 16;
	uint32 *FirstID = FirstChainCreature->at(ChainX, ChainY);
	Creature->NextChainCreature = *FirstID;
	*FirstID = Creature->ID;
}

void DeleteChainCreature(TCreature *Creature){
	if(Creature == NULL){
		error(Translate("DeleteChainCreature: Übegebene Kreatur existiert nicht.\n",
						"DeleteChainCreature: Surrendered creature does not exist.\n"));
		return;
	}

	// NOTE(fusion): All creatures in each 16x16 region form a creature linked
	// list, despite its current floor. Whether that is a good idea is a whole
	// other matter.
	int ChainX = Creature->posx / 16;
	int ChainY = Creature->posy / 16;
	uint32 *FirstID = FirstChainCreature->at(ChainX, ChainY);

	if(*FirstID == Creature->ID){
		*FirstID = Creature->NextChainCreature;
	}else{
		uint32 CurrentID = *FirstID;
		while(true){
			if(CurrentID == 0){
				error(Translate("DeleteChainCreature: Kreatur nicht gefunden.\n",
								"DeleteChainCreature: Creature not found.\n"));
				return;
			}

			TCreature *Current = GetCreature(CurrentID);
			if(Current == NULL){
				error(Translate("DeleteChainCreature: Kreatur existiert nicht.\n",
								"DeleteChainCreature: Creature does not exist.\n"));
				return;
			}

			if(Current->NextChainCreature == Creature->ID){
				Current->NextChainCreature = Creature->NextChainCreature;
				break;
			}

			CurrentID = Current->NextChainCreature;
		}
	}
}

void MoveChainCreature(TCreature *Creature, int CoordX, int CoordY){
	if(Creature == NULL){
		error(Translate("DeleteChainCreature: Übegebene Kreatur existiert nicht.\n",
						"DeleteChainCreature: Surrendered creature does not exist.\n"));
		return;
	}

	int NewChainX = CoordX / 16;
	int NewChainY = CoordY / 16;
	int OldChainX = Creature->posx / 16;
	int OldChainY = Creature->posy / 16;

	if(NewChainX != OldChainX || NewChainY != OldChainY){
		DeleteChainCreature(Creature);
		InsertChainCreature(Creature, CoordX, CoordY);
	}
}

void ProcessCreatures(void){
	for(int Index = 0; Index < FirstFreeCreature; Index += 1){
		TCreature *Creature = *CreatureList.at(Index);
		if(Creature == NULL){
			error(Translate("ProcessCreatures: Kreatur %d existiert nicht.\n",
							"ProcessCreatures: Creature %d does not exist.\n"), Index);
			continue;
		}

		// TODO(fusion): I'm almost sure this is processing ITEM regen rather
		// FOOD regen. It wouldn't make a lot of sense to have this plus what
		// happens in `TSkillFed::Event` if we didn't consider things like the
		// life ring, etc...
		int RegenInterval = Creature->Skills[SKILL_FED]->Get();
		if(RegenInterval > 0 && (RoundNr % RegenInterval) == 0 && !Creature->IsDead
				&& !IsProtectionZone(Creature->posx, Creature->posy, Creature->posz)){
			Creature->Skills[SKILL_HITPOINTS]->Change(1);
			Creature->Skills[SKILL_MANA]->Change(4);
			if(Creature->Type == PLAYER){
				SendPlayerData(Creature->Connection);
			}
		}

		if(Creature->Type == PLAYER){
			if(Creature->Connection != NULL){
				((TPlayer*)Creature)->CheckState();
			}

			if(Creature->EarliestLogoutRound != 0 && Creature->EarliestLogoutRound <= RoundNr){
				((TPlayer*)Creature)->ClearPlayerkillingMarks();
				Creature->EarliestLogoutRound = 0;
			}
		}

		if(!Creature->IsDead && Creature->Skills[SKILL_HITPOINTS]->Get() <= 0){
			error(Translate("ProcessCreatures: Kreatur %s ist nicht tot, obwohl sie keine HP mehr hat.\n",
							"ProcessCreatures: Creature %s is not dead even though it has no HP left.\n"), Creature->Name);
			Creature->Death();
		}

		if(Creature->LoggingOut && Creature->LogoutPossible() == 0){ // LOGOUT_POSSIBLE ?
			if(Creature->IsDead && Creature->Skills[SKILL_HITPOINTS]->Get() > 0){
				error(Translate("ProcessCreatures: Kreatur %s hat HP, obwohl sie tot ist.\n",
								"ProcessCreatures: Creature %s has HP even though it is dead.\n"), Creature->Name);
				Creature->Skills[SKILL_HITPOINTS]->Set(0);
			}

			// TODO(fusion): Creatures are removed from `CreatureList` with a swap
			// and pop. Since we're iterating it RIGHT NOW, we need to process the
			// the current index AGAIN because it'll now contain the creature that
			// was previously at the end of the list. The annoying part here is that
			// this removal occurs implicitly in the creature's destructor.
			delete Creature;
			Index -= 1;
		}
	}
}

void ProcessSkills(void){
	for(int Index = 0; Index < FirstFreeCreature; Index += 1){
		TCreature *Creature = *CreatureList.at(Index);
		if(Creature == NULL){
			error(Translate("ProcessSkills: Kreatur %d existiert nicht.\n",
							"ProcessSkills: Creature %d does not exist.\n"), Index);
			continue;
		}

		Creature->ProcessSkills();
	}
}

void MoveCreatures(int Delay){
	ServerMilliseconds += Delay;
	while(ToDoQueue.Entries > 0){
		auto Entry = *ToDoQueue.Entry->at(1);
		uint32 ExecutionTime = Entry.Key;
		uint32 CreatureID = Entry.Data;
		if(ExecutionTime > ServerMilliseconds){
			break;
		}

		ToDoQueue.deleteMin();
		TCreature *Creature = GetCreature(CreatureID);
		if(Creature != NULL){
			Creature->Execute();
		}
	}
}

// Kill Statistics
// =============================================================================
void AddKillStatistics(int AttackerRace, int DefenderRace){
	// NOTE(fusion): I think the race name can be "human" only for players,
	// which means we're probably tracking how many creatures are killed by
	// players with `KilledCreatures`, and how many players are killed by
	// creatures with `KilledPlayers`.

	if(strcmp(RaceData[AttackerRace].Name, "human") == 0){
		KilledCreatures[DefenderRace] += 1;
	}

	if(strcmp(RaceData[DefenderRace].Name, "human") == 0){
		KilledPlayers[AttackerRace] += 1;
	}
}

void WriteKillStatistics(void){
	// TODO(fusion): Using the same names with local and global variables is
	// trash. I'd personally have all globals with a `g_` prefix but I'm trying
	// to not change the original code too much.

	// TODO(fusion): The way we manage race names here is a disaster but could
	// make sense if there is a constant `MAX_RACE_NAME` somewhere. I've also
	// seen the length of 30 often used with name strings so it could also be
	// a general constant `MAX_NAME`.

	int NumberOfRaces = 0;
	char *RaceNames = new char[MAX_RACES * 30];
	int *KilledPlayers = new int[MAX_RACES];
	int *KilledCreatures = new int[MAX_RACES];
	for(int Race = 0; Race < MAX_RACES; Race += 1){
		if(::KilledCreatures[Race] == 0 && ::KilledPlayers[Race] == 0){
			continue;
		}

		char *Name = &RaceNames[NumberOfRaces * 30];
		if(Race == 0){
			strcpy(Name, "(fire/poison/energy)");
		}else{
			sprintf(Name, "%s %s", RaceData[Race].Article, RaceData[Race].Name);
			// TODO(fusion): The original function had a call to `Plural` and
			// `Capitals` but didn't seem to put the results back into `Name`.
		}

		KilledPlayers[NumberOfRaces] = ::KilledPlayers[Race];
		KilledCreatures[NumberOfRaces] = ::KilledCreatures[Race];
		NumberOfRaces += 1;
	}

	KillStatisticsOrder(NumberOfRaces, RaceNames, KilledPlayers, KilledCreatures);
	InitKillStatistics();
}

void InitKillStatistics(void){
	for(int i = 0; i < MAX_RACES; i += 1){
		KilledCreatures[i] = 0;
		KilledPlayers[i] = 0;
	}
}

void ExitKillStatistics(void){
	WriteKillStatistics();
}

// Race
// =============================================================================
TRaceData::TRaceData(void) :
		Skill(1, 5, 5),
		Talk(1, 5, 5),
		Item(1, 5, 5),
		Spell(1, 5, 5)
{
	this->Name[0] = 0;
	this->Article[0] = 0;
	this->Outfit.OutfitID = 0;
	this->Outfit.ObjectType = 0;
	this->Blood = BT_BLOOD;
	this->ExperiencePoints = 0;
	this->FleeThreshold = 0;
	this->Attack = 0;
	this->Defend = 0;
	this->Armor = 0;
	this->Poison = 0;
	this->SummonCost = 0;
	this->LoseTarget = 0;
	this->Strategy[0] = 100;
	this->Strategy[1] = 0;
	this->Strategy[2] = 0;
	this->Strategy[3] = 0;
	this->KickBoxes = false;
	this->KickCreatures = false;
	this->SeeInvisible = false;
	this->Unpushable = false;
	this->DistanceFighting = false;
	this->NoSummon = false;
	this->NoIllusion = false;
	this->NoConvince = false;
	this->NoBurning = false;
	this->NoPoison = false;
	this->NoEnergy = false;
	this->NoHit = false;
	this->NoLifeDrain = false;
	this->NoParalyze = false;
	this->Skills = 0;
	this->Talks = 0;
	this->Items = 0;
	this->Spells = 0;
}

bool IsRaceValid(int Race){
	return Race >= 1 && Race < MAX_RACES;
}

int GetRaceByName(const char *RaceName){
	int Result = 0;
	for(int Race = 1; Race < MAX_RACES; Race += 1){
		if(stricmp(RaceName, RaceData[Race].Name) == 0){
			Result = Race;
			break;
		}
	}
	return Result;
}

const char *GetRaceName(int Race){
	if(!IsRaceValid(Race)){
		error(Translate("GetRaceName: Ungültige Rassen-Nummer %d.\n",
						"GetRaceName: Invalid race number %d.\n"), Race);
		return NULL;
	}

	return RaceData[Race].Name;
}

TOutfit GetRaceOutfit(int Race){
	if(!IsRaceValid(Race)){
		error(Translate("GetRaceOutfit: Ungültige Rassen-Nummer %d.\n",
						"GetRaceOutfit: Invalid race number %d.\n"), Race);
		return RaceData[1].Outfit;
	}

	return RaceData[Race].Outfit;
}

bool GetRaceNoSummon(int Race){
	if(!IsRaceValid(Race)){
		error(Translate("GetRaceNoSummon: Ungültige Rassen-Nummer %d.\n",
						"GetRaceNoSummon: Invalid race number %d.\n"), Race);
		return true;
	}

	return RaceData[Race].NoSummon;
}

bool GetRaceNoConvince(int Race){
	if(!IsRaceValid(Race)){
		error(Translate("GetRaceNoConvince: Ungültige Rassen-Nummer %d.\n",
						"GetRaceNoConvince: Invalid race number %d.\n"), Race);
		return true;
	}

	return RaceData[Race].NoConvince;
}

bool GetRaceNoIllusion(int Race){
	if(!IsRaceValid(Race)){
		error(Translate("GetRaceNoIllusion: Ungültige Rassen-Nummer %d.\n",
						"GetRaceNoIllusion: Invalid race number %d.\n"), Race);
		return true;
	}

	return RaceData[Race].NoIllusion;
}

bool GetRaceNoParalyze(int Race){
	if(!IsRaceValid(Race)){
		error(Translate("GetRaceNoParalyze: Ungültige Rassen-Nummer %d.\n",
						"GetRaceNoParalyze: Invalid race number %d.\n"), Race);
		return true;
	}

	return RaceData[Race].NoParalyze;
}

int GetRaceSummonCost(int Race){
	if(!IsRaceValid(Race)){
		error(Translate("GetRaceSummonCost: Ungültige Rassen-Nummer %d.\n",
						"GetRaceSummonCost: Invalid race number %d.\n"), Race);
		return 0;
	}

	return RaceData[Race].SummonCost;
}

int GetRacePoison(int Race){
	if(!IsRaceValid(Race)){
		error(Translate("GetRacePoison: Ungültige Rassen-Nummer %d.\n",
						"GetRacePoison: Invalid race number %d.\n"), Race);
		return 0;
	}

	return RaceData[Race].Poison;
}

bool GetRaceUnpushable(int Race){
	if(!IsRaceValid(Race)){
		error(Translate("GetRaceUnpushable: Ungültige Rassen-Nummer %d.\n",
						"GetRaceUnpushable: Invalid race number %d.\n"), Race);
		return true;
	}

	return RaceData[Race].Unpushable;
}

// TODO(fusion): Probably move this somewhere else?
TOutfit ReadOutfit(TReadScriptFile *Script){
	TOutfit Outfit = {};
	Script->readSymbol('(');
	Outfit.OutfitID = Script->readNumber();
	Script->readSymbol(',');
	if(Outfit.OutfitID == 0){
		Outfit.ObjectType = (uint16)Script->readNumber();
	}else{
		memcpy(Outfit.Colors, Script->readBytesequence(), sizeof(Outfit.Colors));
	}
	Script->readSymbol(')');
	return Outfit;
}

// TODO(fusion): Probably move this somewhere else?
void WriteOutfit(TWriteScriptFile *Script, TOutfit Outfit){
	Script->writeText("(");
	Script->writeNumber(Outfit.OutfitID);
	Script->writeText(",");
	if(Outfit.OutfitID == 0){
		Script->writeNumber((int)Outfit.ObjectType);
	}else{
		Script->writeBytesequence(Outfit.Colors, sizeof(Outfit.Colors));
	}
	Script->writeText(")");
}

void LoadRace(const char *FileName){
	TReadScriptFile Script;

	Script.open(FileName);

	// NOTE(fusion): It seems we expect `RaceNumber` to be the first attribute
	// declared in a race file.
	if(strcmp(Script.readIdentifier(), "racenumber") != 0){
		Script.error("race number expected");
	}

	Script.readSymbol('=');

	int RaceNumber = Script.readNumber();
	if(!IsRaceValid(RaceNumber)){
		Script.error("illegal race number");
	}

	TRaceData *Race = &RaceData[RaceNumber];
	if(Race->Name[0] != 0){
		Script.error("race already defined");
	}

	Race->Outfit = TOutfit{};
	Race->Outfit.OutfitID = RaceNumber;

	while(true){
		Script.nextToken();
		if(Script.Token == ENDOFFILE){
			Script.close();
			break;
		}

		char Identifier[MAX_IDENT_LENGTH];
		strcpy(Identifier, Script.getIdentifier());
		Script.readSymbol('=');

		if(strcmp(Identifier, "name") == 0){
			strcpy(Race->Name, Script.readString());
		}else if(strcmp(Identifier, "article") == 0){
			strcpy(Race->Article, Script.readString());
		}else if(strcmp(Identifier, "outfit") == 0){
			Race->Outfit = ReadOutfit(&Script);
		}else if(strcmp(Identifier, "corpse") == 0){
			int CorpseTypeID = Script.readNumber();
			Race->MaleCorpse = CorpseTypeID;
			Race->FemaleCorpse = CorpseTypeID;
		}else if(strcmp(Identifier, "corpses") == 0){
			Race->MaleCorpse = Script.readNumber();
			Script.readSymbol(',');
			Race->FemaleCorpse = Script.readNumber();
		}else if(strcmp(Identifier, "blood") == 0){
			const char *Blood = Script.readIdentifier();
			if(strcmp(Blood, "blood") == 0){
				Race->Blood = BT_BLOOD;
			}else if(strcmp(Blood, "slime") == 0){
				Race->Blood = BT_SLIME;
			}else if(strcmp(Blood, "bones") == 0){
				Race->Blood = BT_BONES;
			}else if(strcmp(Blood, "fire") == 0){
				Race->Blood = BT_FIRE;
			}else if(strcmp(Blood, "energy") == 0){
				Race->Blood = BT_ENERGY;
			}else{
				Script.error("unknown blood type");
			}
		}else if(strcmp(Identifier, "experience") == 0){
			Race->ExperiencePoints = Script.readNumber();
		}else if(strcmp(Identifier, "summoncost") == 0){
			Race->SummonCost = Script.readNumber();
		}else if(strcmp(Identifier, "fleethreshold") == 0){
			Race->FleeThreshold = Script.readNumber();
		}else if(strcmp(Identifier, "attack") == 0){
			Race->Attack = Script.readNumber();
		}else if(strcmp(Identifier, "defend") == 0){
			Race->Defend = Script.readNumber();
		}else if(strcmp(Identifier, "armor") == 0){
			Race->Armor = Script.readNumber();
		}else if(strcmp(Identifier, "poison") == 0){
			Race->Poison = Script.readNumber();
		}else if(strcmp(Identifier, "losetarget") == 0){
			Race->LoseTarget = Script.readNumber();
		}else if(strcmp(Identifier, "strategy") == 0){
			Script.readSymbol('(');
			Race->Strategy[0] = Script.readNumber();
			Script.readSymbol(',');
			Race->Strategy[1] = Script.readNumber();
			Script.readSymbol(',');
			Race->Strategy[2] = Script.readNumber();
			Script.readSymbol(',');
			Race->Strategy[3] = Script.readNumber();
			Script.readSymbol(')');
		}else if(strcmp(Identifier, "flags") == 0){
			Script.readSymbol('{');
			do{
				const char *Flag = Script.readIdentifier();
				if(strcmp(Flag, "kickboxes") == 0){
					Race->KickBoxes = true;
				}else if(strcmp(Flag, "kickcreatures") == 0){
					Race->KickCreatures = true;
				}else if(strcmp(Flag, "seeinvisible") == 0){
					Race->SeeInvisible = true;
				}else if(strcmp(Flag, "unpushable") == 0){
					Race->Unpushable = true;
				}else if(strcmp(Flag, "distancefighting") == 0){
					Race->DistanceFighting = true;
				}else if(strcmp(Flag, "nosummon") == 0){
					Race->NoSummon = true;
				}else if(strcmp(Flag, "noconvince") == 0){
					Race->NoConvince = true;
				}else if(strcmp(Flag, "noillusion") == 0){
					Race->NoIllusion = true;
				}else if(strcmp(Flag, "noburning") == 0){
					Race->NoBurning = true;
				}else if(strcmp(Flag, "nopoison") == 0){
					Race->NoPoison = true;
				}else if(strcmp(Flag, "noenergy") == 0){
					Race->NoEnergy = true;
				}else if(strcmp(Flag, "nohit") == 0){
					Race->NoHit = true;
				}else if(strcmp(Flag, "nolifedrain") == 0){
					Race->NoLifeDrain = true;
				}else if(strcmp(Flag, "noparalyze") == 0){
					Race->NoParalyze = true;
				}else{
					Script.error("unknown flag");
				}
			}while(Script.readSpecial() != '}');
		}else if(strcmp(Identifier, "skills") == 0){
			Script.readSymbol('{');
			do{
				Script.readSymbol('(');
				int SkillNr = GetSkillByName(Script.readIdentifier());
				if(SkillNr == -1){
					Script.error("unknown skill name");
				}

				// NOTE(fusion): Skills are indexed from 1.
				Race->Skills += 1;
				TSkillData *SkillData = Race->Skill.at(Race->Skills);
				SkillData->Nr = SkillNr;
				Script.readSymbol(',');
				SkillData->Actual = Script.readNumber();
				Script.readSymbol(',');
				SkillData->Minimum = Script.readNumber();
				Script.readSymbol(',');
				SkillData->Maximum = Script.readNumber();
				Script.readSymbol(',');
				SkillData->NextLevel = Script.readNumber();
				Script.readSymbol(',');
				SkillData->FactorPercent = Script.readNumber();
				Script.readSymbol(',');
				SkillData->AddLevel = Script.readNumber();
				Script.readSymbol(')');
			}while(Script.readSpecial() != '}');
		}else if(strcmp(Identifier, "talk") == 0){
			Script.readSymbol('{');
			do{
				// NOTE(fusion): Talks are indexed from 1.
				Race->Talks += 1;
				*Race->Talk.at(Race->Talks) = AddDynamicString(Script.readString());
			}while(Script.readSpecial() != '}');
		}else if(strcmp(Identifier, "inventory") == 0){
			Script.readSymbol('{');
			do{
				// NOTE(fusion): Items are indexed from 1.
				Race->Items += 1;
				TItemData *ItemData = Race->Item.at(Race->Items);
				Script.readSymbol('(');
				ItemData->Type = Script.readNumber();
				Script.readSymbol(',');
				ItemData->Maximum = Script.readNumber();
				Script.readSymbol(',');
				ItemData->Probability = Script.readNumber();
				Script.readSymbol(')');
			}while(Script.readSpecial() != '}');
		}else if(strcmp(Identifier, "spells") == 0){
			Script.readSymbol('{');
			do{
				// NOTE(fusion): Spells are indexed from 1.
				Race->Spells += 1;
				TSpellData *SpellData = Race->Spell.at(Race->Spells);

				// NOTE(fusion): Spell shape.
				{
					const char *SpellShape = Script.readIdentifier();
					if(strcmp(SpellShape, "actor") == 0){
						SpellData->Shape = SHAPE_ACTOR;
						Script.readSymbol('(');
						SpellData->ShapeParam1 = Script.readNumber();
						Script.readSymbol(')');
					}else if(strcmp(SpellShape, "victim") == 0){
						SpellData->Shape = SHAPE_VICTIM;
						Script.readSymbol('(');
						SpellData->ShapeParam1 = Script.readNumber();
						Script.readSymbol(',');
						SpellData->ShapeParam2 = Script.readNumber();
						Script.readSymbol(',');
						SpellData->ShapeParam3 = Script.readNumber();
						Script.readSymbol(')');
					}else if(strcmp(SpellShape, "origin") == 0){
						SpellData->Shape = SHAPE_ORIGIN;
						Script.readSymbol('(');
						SpellData->ShapeParam1 = Script.readNumber();
						Script.readSymbol(',');
						SpellData->ShapeParam2 = Script.readNumber();
						Script.readSymbol(')');
					}else if(strcmp(SpellShape, "destination") == 0){
						SpellData->Shape = SHAPE_DESTINATION;
						Script.readSymbol('(');
						SpellData->ShapeParam1 = Script.readNumber();
						Script.readSymbol(',');
						SpellData->ShapeParam2 = Script.readNumber();
						Script.readSymbol(',');
						SpellData->ShapeParam3 = Script.readNumber();
						Script.readSymbol(',');
						SpellData->ShapeParam4 = Script.readNumber();
						Script.readSymbol(')');
					}else if(strcmp(SpellShape, "angle") == 0){
						SpellData->Shape = SHAPE_ANGLE;
						Script.readSymbol('(');
						SpellData->ShapeParam1 = Script.readNumber();
						Script.readSymbol(',');
						SpellData->ShapeParam2 = Script.readNumber();
						Script.readSymbol(',');
						SpellData->ShapeParam3 = Script.readNumber();
						Script.readSymbol(')');
					}else{
						Script.error("unknown spell shape");
					}
				}

				Script.readSymbol('I');

				// NOTE(fusion): Spell impact.
				{
					const char *SpellImpact = Script.readIdentifier();
					if(strcmp(SpellImpact, "damage") == 0){
						SpellData->Impact = IMPACT_DAMAGE;
						Script.readSymbol('(');
						SpellData->ImpactParam1 = Script.readNumber();
						Script.readSymbol(',');
						SpellData->ImpactParam2 = Script.readNumber();
						Script.readSymbol(',');
						SpellData->ImpactParam3 = Script.readNumber();
						Script.readSymbol(')');
					}else if(strcmp(SpellImpact, "field") == 0){
						SpellData->Impact = IMPACT_FIELD;
						Script.readSymbol('(');
						SpellData->ImpactParam1 = Script.readNumber();
						Script.readSymbol(')');
					}else if(strcmp(SpellImpact, "healing") == 0){
						SpellData->Impact = IMPACT_HEALING;
						Script.readSymbol('(');
						SpellData->ImpactParam1 = Script.readNumber();
						Script.readSymbol(',');
						SpellData->ImpactParam2 = Script.readNumber();
						Script.readSymbol(')');
					}else if(strcmp(SpellImpact, "speed") == 0){
						SpellData->Impact = IMPACT_SPEED;
						Script.readSymbol('(');
						SpellData->ImpactParam1 = Script.readNumber();
						Script.readSymbol(',');
						SpellData->ImpactParam2 = Script.readNumber();
						Script.readSymbol(',');
						SpellData->ImpactParam3 = Script.readNumber();
						Script.readSymbol(')');
					}else if(strcmp(SpellImpact, "drunken") == 0){
						SpellData->Impact = IMPACT_DRUNKEN;
						Script.readSymbol('(');
						SpellData->ImpactParam1 = Script.readNumber();
						Script.readSymbol(',');
						SpellData->ImpactParam2 = Script.readNumber();
						Script.readSymbol(',');
						SpellData->ImpactParam3 = Script.readNumber();
						Script.readSymbol(')');
					}else if(strcmp(SpellImpact, "strength") == 0){
						SpellData->Impact = IMPACT_STRENGTH;
						Script.readSymbol('(');
						SpellData->ImpactParam1 = Script.readNumber();
						Script.readSymbol(',');
						SpellData->ImpactParam2 = Script.readNumber();
						Script.readSymbol(',');
						SpellData->ImpactParam3 = Script.readNumber();
						Script.readSymbol(',');
						SpellData->ImpactParam4 = Script.readNumber();
						Script.readSymbol(')');
					}else if(strcmp(SpellImpact, "outfit") == 0){
						SpellData->Impact = IMPACT_OUTFIT;
						Script.readSymbol('(');
						TOutfit Outfit = ReadOutfit(&Script);
						SpellData->ImpactParam1 = Outfit.OutfitID;
						SpellData->ImpactParam2 = (int)Outfit.PackedData;
						Script.readSymbol(',');
						SpellData->ImpactParam3 = Script.readNumber();
						Script.readSymbol(')');
					}else if(strcmp(SpellImpact, "summon") == 0){
						SpellData->Impact = IMPACT_SUMMON;
						Script.readSymbol('(');
						SpellData->ImpactParam1 = Script.readNumber();
						Script.readSymbol(',');
						SpellData->ImpactParam2 = Script.readNumber();
						Script.readSymbol(')');
					}else{
						Script.error("unknown spell impact");
					}
				}

				Script.readSymbol(':');

				// NOTE(fusion): Spell delay.
				{
					SpellData->Delay = Script.readNumber();
					if(SpellData->Delay == 0){
						Script.error("zero spell delay");
					}
				}
			}while(Script.readSpecial() != '}');
		}else{
			Script.error("unknown race property");
		}
	}
}

void LoadRaces(void){
	// TODO(fusion): It is possible to leak `MonsterDir` if `LoadRace` throws on
	// errors (which it does). This is usually not a problem because failing here
	// means we're cascading back to `InitAll` which will report the error and
	// exit, but it is something to keep in mind for any functions that uses
	// exceptions with non-RAII resources.

	DIR *MonsterDir = opendir(MONSTERPATH);
	if(MonsterDir == NULL){
		error(Translate("LoadRaces: Unterverzeichnis %s nicht gefunden\n",
						"LoadRaces: Subdirectory %s not found\n"), MONSTERPATH);
		throw "Cannot load races";
	}

	char FileName[4096];
	while(dirent *DirEntry = readdir(MonsterDir)){
		if(DirEntry->d_type != DT_REG){
			continue;
		}

		const char *FileExt = findLast(DirEntry->d_name, '.');
		if(FileExt == NULL || strcmp(FileExt, ".mon") != 0){
			continue;
		}

		snprintf(FileName, sizeof(FileName), "%s/%s", MONSTERPATH, DirEntry->d_name);
		LoadRace(FileName);
	}

	closedir(MonsterDir);
}

// Monster Raid
// =============================================================================
TAttackWave::TAttackWave(void) :
		ExtraItem(1, 5, 5)
{
	this->x = 0;
	this->y = 0;
	this->z = 0;
	this->Spread = 0;
	this->Race = -1;
	this->MinCount = 0;
	this->MaxCount = 0;
	this->Radius = INT_MAX;
	this->Lifetime = 0;
	this->Message = 0;
	this->ExtraItems = 0;
}

TAttackWave::~TAttackWave(void){
	if(this->Message != 0){
		DeleteDynamicString(this->Message);
	}
}

void LoadMonsterRaid(const char *FileName, int Start,
		bool *Type, int *Date, int *Interval, int *Duration){
	if(FileName == NULL){
		error(Translate("LoadMonsterRaid: Dateiname ist NULL.\n",
						"LoadMonsterRaid: File name is NULL.\n"));
		throw "cannot load monster raid";
	}

	// TODO(fusion): The original function would only write to these output
	// variables when `Start` was negative. Instead, we assign dummy variables
	// if any of the pointers is NULL to allow for any reads and writes while
	// loading the raid.
	bool DummyType;
	int DummyDate;
	int DummyInterval;
	int DummyDuration;
	if(Type == NULL)		{ Type = &DummyType; }
	if(Date == NULL)		{ Date = &DummyDate; }
	if(Interval == NULL)	{ Interval = &DummyInterval; }
	if(Duration == NULL)	{ Duration = &DummyDuration; }

	*Type = false;
	*Date = 0;
	*Interval = 0;
	*Duration = 0;

	if(Start >= 0){
		print(1, Translate("Plane Raid %s ein für Runde %d.\n",
						   "Scheduling Raid %s for round %d.\n"), FileName, Start);
	}

	// NOTE(fusion): We expect the first few attributes describing the raid to
	// be in an exact order, followed by attack waves. Attack wave attributes
	// don't need to be in any specific order but specifying `Delay` will wrap
	// the previous wave (if any) and start a new one.

	TReadScriptFile Script;
	Script.open(FileName);

	// NOTE(fusion): Optional `Description` attribute.
	Script.nextToken();
	if(strcmp(Script.getIdentifier(), "description") == 0){
		Script.readSymbol('=');
		Script.readString();
		Script.nextToken();
	}

	if(strcmp(Script.getIdentifier(), "type") == 0){
		// NOTE(fusion): The type can be either "BigRaid" or "SmallRaid" so it uses
		// a boolean for `Type` to tell whether it is a big raid or not. It could be
		// renamed to `BigRaid` or something.
		Script.readSymbol('=');
		*Type = (strcmp(Script.readIdentifier(), "bigraid") == 0);
	}else{
		Script.error("type expected");
	}

	Script.nextToken();
	if(strcmp(Script.getIdentifier(), "date") == 0){
		Script.readSymbol('=');
		*Date = Script.readNumber();
	}else if(strcmp(Script.getIdentifier(), "interval") == 0){
		Script.readSymbol('=');
		*Interval = Script.readNumber();
	}else{
		Script.error("date or interval expected");
	}

	Script.nextToken();
	while(Script.Token != ENDOFFILE){
		if(strcmp(Script.getIdentifier(), "delay") != 0){
			Script.error("delay expected");
		}

		Script.readSymbol('=');
		int Delay = Script.readNumber();
		TAttackWave *Wave = new TAttackWave;
		while(true){
			Script.nextToken();
			if(Script.Token == ENDOFFILE){
				break;
			}

			if(strcmp(Script.getIdentifier(), "delay") == 0){
				break;
			}

			char Identifier[MAX_IDENT_LENGTH];
			strcpy(Identifier, Script.getIdentifier());
			Script.readSymbol('=');
			if(strcmp(Identifier, "location") == 0){
				Script.readString();
			}else if(strcmp(Identifier, "position") == 0){
				Script.readCoordinate(&Wave->x, &Wave->y, &Wave->z);
			}else if(strcmp(Identifier, "spread") == 0){
				Wave->Spread = Script.readNumber();
			}else if(strcmp(Identifier, "race") == 0){
				Wave->Race = Script.readNumber();
				if(!IsRaceValid(Wave->Race)){
					Script.error("illegal race number");
				}
			}else if(strcmp(Identifier, "count") == 0){
				Script.readSymbol('(');
				Wave->MinCount = Script.readNumber();
				Script.readSymbol(',');
				Wave->MaxCount = Script.readNumber();
				Script.readSymbol(')');

				if(Wave->MaxCount < Wave->MinCount){
					Script.error("mincount greater than maxcount");
				}

				if(Wave->MinCount < 0 || Wave->MaxCount < 1){
					Script.error("illegal number of monsters");
				}
			}else if(strcmp(Identifier, "radius") == 0){
				Wave->Radius = Script.readNumber();
			}else if(strcmp(Identifier, "lifetime") == 0){
				Wave->Lifetime = Script.readNumber();
			}else if(strcmp(Identifier, "message") == 0){
				Wave->Message = AddDynamicString(Script.readString());
			}else if(strcmp(Identifier, "inventory") == 0){
				Script.readSymbol('{');
				do{
					// NOTE(fusion): Items are indexed from 1.
					Wave->ExtraItems += 1;
					TItemData *ItemData = Wave->ExtraItem.at(Wave->ExtraItems);
					Script.readSymbol('(');
					ItemData->Type = Script.readNumber();
					Script.readSymbol(',');
					ItemData->Maximum = Script.readNumber();
					Script.readSymbol(',');
					ItemData->Probability = Script.readNumber();
					Script.readSymbol(')');
				}while(Script.readSpecial() != '}');
			}else{
				Script.error("unknown attack wave property");
			}
		}

		if(Wave->x == 0){
			Script.error("position expected");
		}

		if(Wave->Race == -1){
			Script.error("race expected");
		}

		if(Wave->MaxCount == 0){
			Script.error("count expected");
		}

		int WaveEnd = Delay + (Wave->Lifetime != 0 ? Wave->Lifetime : 3600);
		if(*Duration < WaveEnd){
			*Duration = WaveEnd;
		}

		if(Start >= 0){
			AttackWaveQueue.insert(Start + Delay, Wave);
		}else{
			delete Wave;
		}
	}

	Script.close();
}

void LoadMonsterRaids(void){
	DIR *MonsterDir = opendir(MONSTERPATH);
	if(MonsterDir == NULL){
		error(Translate("LoadMonsterRaids: Unterverzeichnis %s nicht gefunden.\n",
						"LoadMonsterRaids: Subdirectory %s not found.\n"), MONSTERPATH);
		throw "Cannot load monster raids";
	}

	// TODO(fusion): The `Date` attribute used by raid files is a unix timestamp
	// which is usually what `time(NULL)` returns. This should work fine until
	// 2038 when the timestamp value exceeds the capacity of a 32-bit signed
	// integer.
	int Now = (int)std::min<time_t>(time(NULL), INT_MAX);

	int Hour, Minute;
	GetRealTime(&Hour, &Minute);

	int SecondsToReboot = (RebootTime - (Hour * 60 + Minute)) * 60;
	if(SecondsToReboot < 0){
		SecondsToReboot += 86400;
	}

	char BigRaidName[4096] = {};
	int BigRaidDuration = 0;
	int BigRaidTieBreaker = -1;

	char FileName[4096];
	while(dirent *DirEntry = readdir(MonsterDir)){
		if(DirEntry->d_type != DT_REG){
			continue;
		}

		const char *FileExt = findLast(DirEntry->d_name, '.');
		if(FileExt == NULL || strcmp(FileExt, ".evt") != 0){
			continue;
		}

		bool BigRaid;
		int Date, Interval, Duration;
		snprintf(FileName, sizeof(FileName), "%s/%s", MONSTERPATH, DirEntry->d_name);
		LoadMonsterRaid(FileName, -1, &BigRaid, &Date, &Interval, &Duration);
		print(1, "Raid %s: Date %d, Interval %d, Duration %d\n",
				FileName, Date, Interval, Duration);

		if(Date > 0){
			if(Now <= Date && Date <= (Now + SecondsToReboot)){
				int Start = RoundNr + (Date - Now);
				LoadMonsterRaid(FileName, Start, NULL, NULL, NULL, NULL);
				if(BigRaid){
					BigRaidName[0] = 0;
					BigRaidDuration = 0;
					BigRaidTieBreaker = 100;
				}
			}
		}else if(Duration <= SecondsToReboot){
			// NOTE(fusion): `Interval` specifies an average raid interval
			// in seconds. With this function being called at startup, usually
			// after a reboot, we can expect `SecondsToReboot` to be close to
			// a day very regularly. Meaning we can approximate the condition
			// below to `random(1, AverageIntervalDays) == 1` which hopefully
			// makes more sense.
			if(random(0, Interval - 1) < SecondsToReboot){
				if(!BigRaid){
					int Start = RoundNr + random(0, SecondsToReboot - Duration);
					LoadMonsterRaid(FileName, Start, NULL, NULL, NULL, NULL);
				}else{
					int TieBreaker = random(0, 99);
					if(TieBreaker > BigRaidTieBreaker){
						strcpy(BigRaidName, FileName);
						BigRaidDuration = Duration;
						BigRaidTieBreaker = TieBreaker;
					}
				}
			}
		}
	}

	closedir(MonsterDir);

	if(BigRaidName[0] != 0){
		int Start = RoundNr + random(0, SecondsToReboot - BigRaidDuration);
		LoadMonsterRaid(BigRaidName, Start, NULL, NULL, NULL, NULL);
	}
}

void ProcessMonsterRaids(void){
	while(AttackWaveQueue.Entries > 0){
		auto Entry = *AttackWaveQueue.Entry->at(1);
		uint32 ExecutionRound = Entry.Key;
		TAttackWave *Wave = Entry.Data;
		if(ExecutionRound > RoundNr){
			break;
		}

		AttackWaveQueue.deleteMin();
		print(2, Translate("Angriff von Monstern der Rasse %d.\n",
						   "Attack by monsters of race %d.\n"), Wave->Race);
		if(Wave->Message != 0){
			BroadcastMessage(TALK_EVENT_MESSAGE, "%s", GetDynamicString(Wave->Message));
		}

		int NumSpawned = 0;
		TCreature *Spawned[64] = {};

		// NOTE(fusion): The original function would use `alloca` to allocate the
		// buffer for spawned creatures. Inspecting some raid files, the maximum
		// count I could find was 40. I don't think it is realistic for this number
		// to get much higher, mostly because you can always split a large wave
		// into multiple smaller ones, and because going crazy with alloca could
		// cause the stack to blow up.
		int Count = random(Wave->MinCount, Wave->MaxCount);
		if(Count > NARRAY(Spawned)){
			Count = NARRAY(Spawned);
		}

		for(int i = 0; i < Count; i += 1){
			int SpawnX = Wave->x + random(-Wave->Spread, Wave->Spread);
			int SpawnY = Wave->y + random(-Wave->Spread, Wave->Spread);
			int SpawnZ = Wave->z;
			if(!SearchFreeField(&SpawnX, &SpawnY, &SpawnZ, 1, 0, false)
					|| IsProtectionZone(SpawnX, SpawnY, SpawnZ)){
				continue;
			}

			TCreature *Creature = CreateMonster(Wave->Race, SpawnX, SpawnY, SpawnZ, 0, 0, true);
			if(Creature != NULL){
				Creature->Radius = Wave->Radius;
				if(Wave->Lifetime != 0){
					Creature->LifeEndRound = RoundNr + Wave->Lifetime;
				}

				Spawned[NumSpawned] = Creature;
				NumSpawned += 1;
			}
		}

		if(NumSpawned > 0){
			int ExtraItems = Wave->ExtraItems;
			for(int i = 1; i <= ExtraItems; i += 1){
				TItemData *ItemData = Wave->ExtraItem.at(i);
				if(random(0, 999) > ItemData->Probability){
					continue;
				}

				TCreature *Creature = Spawned[random(0, NumSpawned - 1)];
				Object Bag = GetBodyObject(Creature->ID, INVENTORY_BAG);
				if(Bag == NONE){
					try{
						Bag = Create(GetBodyContainer(Creature->ID, INVENTORY_BAG),
								GetSpecialObject(DEFAULT_CONTAINER),
								0);
					}catch(RESULT r){
						error(Translate("ProcessMonsterRaids: Exception %d bei Rasse %d beim Erstellen des Rucksacks.\n",
										"ProcessMonsterRaids: Exception %d for race %d when creating the backpack.\n"), r, Wave->Race);
						continue;
					}
				}

				ObjectType ItemType = ItemData->Type;
				int Amount = random(1, ItemData->Maximum);
				int Repeat = 1;
				if(!ItemType.getFlag(CUMULATIVE)){
					Repeat = Amount;
					Amount = 0;
				}

				print(2, Translate("Verteile %d Objekte vom Typ %d.\n",
								   "Distribute %d objects of type %d.\n"), Amount, ItemType.TypeID);
				for(int j = 0; j < Repeat; j += 1){
					// TODO(fusion): What's the difference between using `Create`
					// and `CreateAtCreature` here? Maybe it checks carry strength,
					// but then why? Don't they have some check to make sure items
					// are not dropped onto the map? This is very confusing and
					// using exception handlers all over the place doesn't help
					// either.
					Object Item = NONE;
					try{
						// TODO(fusion): Possibly an inlined function to check for
						// weapons or equipment?
						if(ItemType.getFlag(WEAPON)
								|| ItemType.getFlag(SHIELD)
								|| ItemType.getFlag(BOW)
								|| ItemType.getFlag(THROW)
								|| ItemType.getFlag(WAND)
								|| ItemType.getFlag(WEAROUT)
								|| ItemType.getFlag(EXPIRE)
								|| ItemType.getFlag(EXPIRESTOP)){
							Item = Create(Bag, ItemType, 0);
						}else{
							Item = CreateAtCreature(Creature->ID, ItemType, Amount);
						}
					}catch(RESULT r){
						error(Translate("ProcessMonsterRaids: Exception %d bei Rasse %d, ggf. CarryStrength erhöhen.\n",
										"ProcessMonsterRaids: Exception %d for race %d, increase CarryStrength if necessary.\n"), r, Wave->Race);
						break;
					}

					if(Item.getContainer().getObjectType().isMapContainer()){
						error(Translate("ProcessMonsterRaids: Objekt fällt auf die Karte. CarryStrength für Rasse %d erhöhen.\n",
										"ProcessMonsterRaids: Object drops onto the map. Increase CarryStrength for race %d.\n"), Wave->Race);
						Delete(Item, -1);
						// TODO(fusion): Should probably stop this inner loop here.
					}
				}

				// NOTE(fusion): `Bag` could be empty if we failed to add any
				// items to it in the loop above.
				if(GetFirstContainerObject(Bag) == NONE){
					Delete(Bag, -1);
				}
			}
		}

		delete Wave;
	}
}

// Initialization
// =============================================================================
void InitCr(void){
	NextCreatureID = 0x40000000;
	FirstFreeCreature = 0;
	FirstChainCreature = new matrix<uint32>(
				SectorXMin * 2, SectorXMax * 2 + 1,
				SectorYMin * 2, SectorYMax * 2 + 1,
				0);

	LoadRaces();
	LoadMonsterRaids();
	InitCrskill();
	InitPlayer();
	InitNonplayer();
	InitKillStatistics();
}

void ExitCr(void){
	ExitKillStatistics();
	ExitPlayer();
	ExitNonplayer();
	ExitCrskill();

	delete FirstChainCreature;
}
