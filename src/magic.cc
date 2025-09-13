#include "magic.hh"
#include "config.hh"
#include "houses.hh"
#include "info.hh"
#include "operate.hh"
#include "writer.hh"

#include <fstream>
#include <sstream>

struct TCircle {
	int x[32];
	int y[32];
	int Count;
};

struct TSpellList {
	uint8 Syllable[10];
	uint8 RuneGr;
	uint8 RuneNr;
	const char *Comment;
	uint16 Level;
	uint16 RuneLevel;
	uint16 Flags;
	int Mana;
	int SoulPoints;
	int Amount;
};

static TSpellList SpellList[256];
static TCircle Circle[10];

static const char SpellSyllable[51][6] = {
	"",
	"al",
	"ad",
	"ex",
	"ut",
	"om",
	"para",
	"ana",
	"evo",
	"ori",
	"mort",
	"lux",
	"liber",
	"vita",
	"flam",
	"pox",
	"hur",
	"moe",
	"ani",
	"ina",
	"eta",
	"amo",
	"hora",
	"gran",
	"cogni",
	"res",
	"mas",
	"vis",
	"som",
	"aqua",
	"frigo",
	"tera",
	"ura",
	"sio",
	"grav",
	"ito",
	"pan",
	"vid",
	"isa",
	"iva",
	"con",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
};

static bool IsAggressionValid(TCreature *Actor, TCreature *Victim){
	ASSERT(Actor != NULL && Victim != NULL);

	if(Actor == Victim){
		return false;
	}

	if(WorldType == NON_PVP && Actor->IsPeaceful() && Victim->IsPeaceful()){
		return false;
	}

	if(GetRaceNoParalyze(Victim->Race)){
		return false;
	}

	if(Victim->Type == PLAYER && CheckRight(Victim->ID, INVULNERABLE)){
		return false;
	}

	return true;
}

// TImpact
// =============================================================================
void TImpact::handleField(int a, int b, int c){
	// no-op
}

void TImpact::handleCreature(TCreature *Victim){
	// no-op
}

bool TImpact::isAggressive(void){
	return true;
}

// TDamageImpact
// =============================================================================
TDamageImpact::TDamageImpact(TCreature *Actor, int DamageType, int Power, bool AllowDefense){
	if(Actor == NULL){
		error(Translate("TDamageImpact::TDamageImpact: Actor ist NULL.\n",
						"TDamageImpact::TDamageImpact: Actor is NULL.\n"));
	}

	this->Actor = Actor;
	this->DamageType = DamageType;
	this->Power = Power;
	this->AllowDefense = AllowDefense;
}

void TDamageImpact::handleCreature(TCreature *Victim){
	if(Victim == NULL){
		error(Translate("TDamageImpact::handleCreature: Opfer existiert nicht.\n",
						"TDamageImpact::handleCreature: Victim does not exist.\n"));
		return;
	}

	TCreature *Actor = this->Actor;
	if(Actor != NULL && Actor != Victim){
		if(WorldType != NON_PVP || !Actor->IsPeaceful() || !Victim->IsPeaceful()){
			int DamageType = this->DamageType;
			int Damage = this->Power;
			if(DamageType == DAMAGE_PHYSICAL && this->AllowDefense){
				// TODO(fusion): Shouldn't we clamp `Damage` to zero?
				Damage -= Victim->Combat.GetDefendDamage();
			}
			Victim->Damage(Actor, Damage, DamageType);
		}
	}
}

// TFieldImpact
// =============================================================================
TFieldImpact::TFieldImpact(TCreature *Actor, int FieldType){
	if(Actor == NULL){
		error(Translate("TFieldImpact::TFieldImpact: Actor ist NULL.\n",
						"TFieldImpact::TFieldImpact: Actor is NULL.\n"));
	}

	this->Actor = Actor;
	this->FieldType = FieldType;
}

void TFieldImpact::handleField(int x, int y, int z){
	TCreature *Actor = this->Actor;
	if(Actor != NULL){
		bool Peaceful = (WorldType == NON_PVP && Actor->IsPeaceful());
		CreateField(x, y, z, this->FieldType, Actor->ID, Peaceful);
	}
}

// THealingImpact
// =============================================================================
THealingImpact::THealingImpact(TCreature *Actor, int Power){
	if(Actor == NULL){
		error(Translate("THealingImpact::THealingImpact: Actor ist NULL.\n",
						"THealingImpact::THealingImpact: Actor is NULL.\n"));
	}

	if(Power < 0){
		error(Translate("THealingImpact::THealingImpact: Power ist negativ (Actor: %s).\n",
						"THealingImpact::THealingImpact: Power is negative (Actor: %s).\n"),
				(Actor != NULL ? Actor->Name : "(unknown)"));
	}

	this->Actor = Actor;
	this->Power = Power;
}

void THealingImpact::handleCreature(TCreature *Victim){
	if(Victim == NULL){
		error(Translate("THealingImpact::handleCreature: Opfer existiert nicht.\n",
						"THealingImpact::handleCreature: Victim does not exist.\n"));
		return;
	}

	if(this->Actor != NULL && this->Power >= 0){
		int HitPoints = Victim->Skills[SKILL_HITPOINTS]->Get();
		if(HitPoints > 0){
			Victim->Skills[SKILL_HITPOINTS]->Change(this->Power);

			// NOTE(fusion): Remove paralyze.
			if(Victim->Skills[SKILL_GO_STRENGTH]->MDAct < 0){
				Victim->SetTimer(SKILL_GO_STRENGTH, 0, 0, 0, -1);
			}
		}
	}
}

bool THealingImpact::isAggressive(void){
	return false;
}

// TSpeedImpact
// =============================================================================
TSpeedImpact::TSpeedImpact(TCreature *Actor, int Percent, int Duration){
	if(Actor == NULL){
		error(Translate("TSpeedImpact::TSpeedImpact: Actor ist NULL.\n",
						"TSpeedImpact::TSpeedImpact: Actor is NULL.\n"));
	}

	this->Actor = Actor;
	this->Percent = Percent;
	this->Duration = Duration;
}

void TSpeedImpact::handleCreature(TCreature *Victim){
	if(Victim == NULL){
		error(Translate("TSpeedImpact::handleCreature: Opfer existiert nicht.\n",
						"TSpeedImpact::handleCreature: Victim does not exist.\n"));
		return;
	}

	TCreature *Actor = this->Actor;
	if(Actor == NULL){
		return;
	}

	int Percent = this->Percent;
	if(Percent < 0 && !IsAggressionValid(Actor, Victim)){
		return;
	}

	TSkill *GoStrength = Victim->Skills[SKILL_GO_STRENGTH];
	if(Percent < -100){
		// TODO(fusion): Not sure what's this about.
		GoStrength->SetMDAct(-GoStrength->Act - 20);
	}else{
		GoStrength->SetMDAct((GoStrength->Act * Percent) / 100);
	}

	Victim->SetTimer(SKILL_GO_STRENGTH, this->Duration, 1, 1, -1);
}

// TDrunkenImpact
// =============================================================================
TDrunkenImpact::TDrunkenImpact(TCreature *Actor, int Power, int Duration){
	if(Actor == NULL){
		error(Translate("TDrunkenImpact::TDrunkenImpact: Actor ist NULL.\n",
						"TDrunkenImpact::TDrunkenImpact: Actor is NULL.\n"));
	}

	if(Power > 6){
		error(Translate("TDrunkenImpact::TDrunkenImpact: Power ist zu groß (%d).\n",
						"TDrunkenImpact::TDrunkenImpact: Power is too big (%d).\n"), Power);
		Power = 6;
	}

	this->Actor = Actor;
	this->Power = Power;
	this->Duration = Duration;
}

void TDrunkenImpact::handleCreature(TCreature *Victim){
	if(Victim == NULL){
		error(Translate("TDrunkenImpact::handleCreature: Opfer existiert nicht.\n",
						"TDrunkenImpact::handleCreature: Victim does not exist.\n"));
		return;
	}

	TCreature *Actor = this->Actor;
	if(Actor == NULL || !IsAggressionValid(Actor, Victim)){
		return;
	}

	int Power = this->Power;
	int Duration = this->Duration;
	TSkill *Drunken = Victim->Skills[SKILL_DRUNKEN];
	if(Drunken->TimerValue() <= Power){
		Victim->SetTimer(SKILL_DRUNKEN, Power, Duration, Duration, -1);
	}
}

// TStrengthImpact
// =============================================================================
TStrengthImpact::TStrengthImpact(TCreature *Actor, int Skills, int Percent, int Duration){
	if(Actor == NULL){
		error(Translate("TStrengthImpact::TStrengthImpact: Actor ist NULL.\n",
						"TStrengthImpact::TStrengthImpact: Actor is NULL.\n"));
	}

	this->Actor = Actor;
	this->Skills = Skills;
	this->Percent = Percent;
	this->Duration = Duration;
}

void TStrengthImpact::handleCreature(TCreature *Victim){
	if(Victim  == NULL){
		error(Translate("TStrengthImpact::handleCreature: Opfer existiert nicht.\n",
						"TStrengthImpact::handleCreature: Victim does not exist.\n"));
		return;
	}

	TCreature *Actor = this->Actor;
	if(Actor == NULL){
		return;
	}

	int Percent = this->Percent;
	if(Percent < 0 && !IsAggressionValid(Actor, Victim)){
		return;
	}

	int Skills = this->Skills;
	int Duration = this->Duration;
	for(int SkillNr = 6; SkillNr <= 11; SkillNr += 1){
		if((Skills & 1) == 0
				&& (SkillNr == SKILL_SWORD
					|| SkillNr == SKILL_CLUB
					|| SkillNr == SKILL_AXE
					|| SkillNr == SKILL_SWORD)){
			continue;
		}

		if((Skills & 2) == 0 && SkillNr == SKILL_DISTANCE){
			continue;
		}

		if((Skills & 4) == 0 && SkillNr == SKILL_SHIELDING){
			continue;
		}

		TSkill *Skill = Victim->Skills[SkillNr];
		if(Percent < -100){
			Skill->SetMDAct(-Skill->Act - 20);
		}else{
			Skill->SetMDAct((Skill->Act * Percent) / 100);
		}
		Victim->SetTimer(SkillNr, Duration, 1, 1, -1);
	}
}

// TOutfitImpact
// =============================================================================
TOutfitImpact::TOutfitImpact(TCreature *Actor, TOutfit Outfit, int Duration){
	if(Actor == NULL){
		error(Translate("TOutfitImpact::TOutfitImpact: Actor ist NULL.\n",
						"TOutfitImpact::TOutfitImpact: Actor is NULL.\n"));
	}

	this->Actor = Actor;
	this->Outfit = Outfit;
	this->Duration = Duration;
}

void TOutfitImpact::handleCreature(TCreature *Victim){
	if(Victim == NULL){
		error(Translate("TOutfitImpact::handleCreature: Opfer existiert nicht.\n",
						"TOutfitImpact::handleCreature: Victim does not exist.\n"));
		return;
	}

	Victim->Outfit = this->Outfit;
	Victim->SetTimer(SKILL_ILLUSION, 1, this->Duration, this->Duration, -1);
}

// TSummonImpact
// =============================================================================
TSummonImpact::TSummonImpact(TCreature *Actor, int Race, int Maximum){
	if(Actor == NULL){
		error(Translate("TSummonImpact::TSummonImpact: Actor ist NULL.\n",
						"TSummonImpact::TSummonImpact: Actor is NULL.\n"));
	}

	if(!IsRaceValid(Race)){
		error(Translate("TSummonImpact::TSummonImpact: Ungültige Rassennummer %d.\n",
						"TSummonImpact::TSummonImpact: Invalid race number %d.\n"), Race);
	}

	this->Actor = Actor;
	this->Race = Race;
	this->Maximum = Maximum;
}

void TSummonImpact::handleField(int x, int y, int z){
	TCreature *Actor = this->Actor;
	int Race = this->Race;
	int Maximum = this->Maximum;
	if(Actor != NULL
			&& IsRaceValid(Race)
			&& Actor->SummonedCreatures < Maximum){
		int x, y, z;
		if(SearchSummonField(&x, &y, &z, 2)){
			CreateMonster(Race, x, y, z, 0, Actor->ID, true);
		}
	}
}

// Spell Primitives
// =============================================================================
void ActorShapeSpell(TCreature *Actor, TImpact *Impact, int Effect){
	if(Actor == NULL){
		error(Translate("ActorShapeSpell: Sprecher existiert nicht.\n",
						"ActorShapeSpell: Caster does not exist.\n"));
		return;
	}

	if(Impact->isAggressive() && IsProtectionZone(Actor->posx, Actor->posy, Actor->posz)){
		return;
	}

	Impact->handleCreature(Actor);
	if(Effect != EFFECT_NONE){
		GraphicalEffect(Actor->posx, Actor->posy, Actor->posz, Effect);
	}
}

void VictimShapeSpell(TCreature *Actor, TCreature *Victim,
		int Range, int Animation, TImpact *Impact, int Effect){
	if(Actor == NULL){
		error(Translate("VictimShapeSpell: Sprecher existiert nicht.\n",
						"VictimShapeSpell: Caster does not exist.\n"));
		return;
	}

	if(Victim == NULL || Actor->posz != Victim->posz){
		return;
	}

	int Distance = std::max<int>(
			std::abs(Actor->posx - Victim->posx),
			std::abs(Actor->posy - Victim->posy));
	if(Distance > Range){
		return;
	}

	// TODO(fusion): We don't check whether `Actor` is inside a protection zone.
	// It might be checked elsewhere.
	if(Impact->isAggressive() && IsProtectionZone(Victim->posx, Victim->posy, Victim->posz)){
		return;
	}

	if(!ThrowPossible(Actor->posx, Actor->posy, Actor->posz,
			Victim->posx, Victim->posy, Victim->posz, 0)){
		return;
	}

	if(Animation != ANIMATION_NONE && Distance > 0){
		Missile(Actor->CrObject, Victim->CrObject, Animation);
	}

	Impact->handleCreature(Victim);

	if(Effect != EFFECT_NONE){
		GraphicalEffect(Victim->posx, Victim->posy, Victim->posz, Effect);
	}
}

// TODO(fusion): This function wasn't in the debug symbols but it was repeated
// in both `OriginShapeSpell` and `CircleShapeSpell` so I'm almost sure it was
// inlined there.
static void ExecuteCircleSpell(int DestX, int DestY, int DestZ,
						int Radius, TImpact *Impact, int Effect){
	// TODO(fusion): This clamping wasn't present in the original function but
	// it is probably a good idea.
	if(Radius >= NARRAY(Circle)){
		Radius = NARRAY(Circle) - 1;
	}

	bool Aggressive = Impact->isAggressive();
	for(int R = 0; R <= Radius; R += 1){
		int CirclePoints = Circle[R].Count;
		for(int Point = 0; Point < CirclePoints; Point += 1){
			int FieldX = DestX + Circle[R].x[Point];
			int FieldY = DestY + Circle[R].y[Point];
			int FieldZ = DestZ;

			if(Aggressive && IsProtectionZone(FieldX, FieldY, FieldZ)){
				continue;
			}

			if(!ThrowPossible(DestX, DestY, DestZ, FieldX, FieldY, FieldZ, 0)){
				continue;
			}

			Impact->handleField(FieldX, FieldY, FieldZ);

			Object Obj = GetFirstObject(FieldX, FieldY, FieldZ);
			while(Obj != NONE){
				if(Obj.getObjectType().isCreatureContainer()){
					TCreature *Victim = GetCreature(Obj);
					if(Victim != NULL){
						Impact->handleCreature(Victim);
					}
				}
				Obj = Obj.getNextObject();
			}

			if(Effect != EFFECT_NONE){
				GraphicalEffect(FieldX, FieldY, FieldZ, Effect);
			}
		}
	}
}

void OriginShapeSpell(TCreature *Actor, int Radius, TImpact *Impact, int Effect){
	if(Actor == NULL){
		error(Translate("OriginShapeSpell: Übergebene Kreatur existiert nicht.\n",
						"OriginShapeSpell: Creature does not exist.\n"));
		return;
	}

	ExecuteCircleSpell(Actor->posx, Actor->posy, Actor->posz, Radius, Impact, Effect);
}

void CircleShapeSpell(TCreature *Actor, int DestX, int DestY, int DestZ,
		int Range, int Animation, int Radius, TImpact *Impact, int Effect){
	if(Actor == NULL){
		error(Translate("CircleShapeSpell: Sprecher existiert nicht.\n",
						"CircleShapeSpell: Caster does not exist.\n"));
		return;
	}

	int Distance = std::max<int>(
			std::abs(Actor->posx - DestX),
			std::abs(Actor->posy - DestY));
	if(Distance > Range || Actor->posz != DestZ){
		return;
	}

	if(!ThrowPossible(Actor->posx, Actor->posy, Actor->posz, DestX, DestY, DestZ, 0)){
		return;
	}

	if(Animation != ANIMATION_NONE && Distance > 0){
		Missile(Actor->CrObject, GetMapContainer(DestX, DestY, DestZ), Animation);
	}

	ExecuteCircleSpell(DestX, DestY, DestZ, Radius, Impact, Effect);
}

void DestinationShapeSpell(TCreature *Actor, TCreature *Victim,
		int Range, int Animation, int Radius, TImpact *Impact, int Effect){
	if(Actor == NULL){
		error(Translate("DestinationShapeSpell: Sprecher existiert nicht.\n",
						"DestinationShapeSpell: Caster does not exist.\n"));
		return;
	}

	if(Victim != NULL){
		CircleShapeSpell(Actor, Victim->posx, Victim->posy, Victim->posz,
				Range, Animation, Radius, Impact, Effect);
	}
}

void AngleShapeSpell(TCreature *Actor, int Angle, int Range, TImpact *Impact, int Effect){
	if(Actor == NULL){
		error(Translate("AngleShapeSpell: Übergebene Kreatur existiert nicht.\n",
						"AngleShapeSpell: Creature does not exist.\n"));
		return;
	}

	int ActorX = Actor->posx;
	int ActorY = Actor->posy;
	int ActorZ = Actor->posz;
	int Direction = Actor->Direction;
	bool Aggressive = Impact->isAggressive();
	for(int Forward = 1; Forward <= Range; Forward += 1){
		int Left = -(Forward * Angle) / 90;
		int Right = +(Forward * Angle) / 90;
		for(int Across = Left; Across <= Right; Across += 1){
			int FieldX = ActorX;
			int FieldY = ActorY;
			int FieldZ = ActorZ;
			if(Direction == DIRECTION_NORTH){
				FieldX += Across;
				FieldY -= Forward;
			}else if(Direction == DIRECTION_EAST){
				FieldX += Forward;
				FieldY += Across;
			}else if(Direction == DIRECTION_SOUTH){
				FieldX -= Across;
				FieldY += Forward;
			}else if(Direction == DIRECTION_WEST){
				FieldX -= Forward;
				FieldY -= Across;
			}else{
				error(Translate("AngleShapeSpell: Ungültige Blickrichtung %d.\n",
								"AngleShapeSpell: Invalid direction %d.\n"), Direction);
				return;
			}

			if(Aggressive && IsProtectionZone(FieldX, FieldY, FieldZ)){
				continue;
			}

			if(!ThrowPossible(ActorX, ActorY, ActorZ, FieldX, FieldY, FieldZ, 0)){
				continue;
			}

			Impact->handleField(FieldX, FieldY, FieldZ);

			Object Obj = GetFirstObject(FieldX, FieldY, FieldZ);
			while(Obj != NONE){
				if(Obj.getObjectType().isCreatureContainer()){
					TCreature *Victim = GetCreature(Obj);
					if(Victim != NULL){
						Impact->handleCreature(Victim);
					}
				}
				Obj = Obj.getNextObject();
			}

			if(Effect != EFFECT_NONE){
				GraphicalEffect(FieldX, FieldY, FieldZ, Effect);
			}
		}
	}
}

void CheckSpellbook(TCreature *Actor, int SpellNr){
	if(Actor == NULL){
		error(Translate("CheckSpellbook: Übergebene Kreatur existiert nicht.\n",
						"CheckSpellbook: Creature does not exist.\n"));
		throw ERROR;
	}

	if(Actor->Type == PLAYER && !CheckRight(Actor->ID, ALL_SPELLS)
			&& !((TPlayer*)Actor)->SpellKnown(SpellNr)){
		throw SPELLUNKNOWN;
	}
}

void CheckAccount(TCreature *Actor, int SpellNr){
	if(Actor == NULL){
		error(Translate("CheckAccount: Übergebene Kreatur existiert nicht.\n",
						"CheckAccount: Creature does not exist.\n"));
		throw ERROR;
	}

	// TODO(fusion): Why is this the only function that checks the spell number?
	if(SpellNr < 1 || SpellNr >= NARRAY(SpellList)){
		error(Translate("CheckAccount: Ungültige Spruchnummer %d.\n",
						"CheckAccount: Invalid spell number %d.\n"), SpellNr);
		throw ERROR;
	}

	if(Actor->Type == PLAYER && (SpellList[SpellNr].Flags & 2) != 0
			&& !CheckRight(Actor->ID, PREMIUM_ACCOUNT)){
		throw NOPREMIUMACCOUNT;
	}
}

void CheckLevel(TCreature *Actor, int SpellNr){
	if(Actor == NULL){
		error(Translate("CheckLevel: Übergebene Kreatur existiert nicht.\n",
						"CheckLevel: Creature does not exist.\n"));
		throw ERROR;
	}

	if(Actor->Type == PLAYER && !CheckRight(Actor->ID, ALL_SPELLS)){
		TSkill *Level = Actor->Skills[SKILL_LEVEL];
		if(Level == NULL){
			error(Translate("CheckLevel: Kein Skill MAGLEVEL.\n",
							"CheckLevel: No skill MAGLEVEL.\n"));
			throw ERROR;
		}

		if(Level->Get() < SpellList[SpellNr].Level){
			throw LOWLEVEL;
		}
	}
}

void CheckRuneLevel(TCreature *Actor, int SpellNr){
	if(Actor == NULL){
		error(Translate("CheckRuneLevel: Übergebene Kreatur existiert nicht.\n",
						"CheckRuneLevel: Creature does not exist.\n"));
		throw ERROR;
	}

	if(Actor->Type == PLAYER && !CheckRight(Actor->ID, ALL_SPELLS)){
		TSkill *MagicLevel = Actor->Skills[SKILL_MAGIC_LEVEL];
		if(MagicLevel == NULL){
			error(Translate("CheckRuneLevel: Kein Skill MAGLEVEL.\n",
							"CheckRuneLevel: No skill MAGLEVEL.\n"));
			throw ERROR;
		}

		if(MagicLevel->Get() < SpellList[SpellNr].RuneLevel){
			throw LOWMAGICLEVEL;
		}
	}
}

void CheckMagicItem(TCreature *Actor, ObjectType Type){
	if(Actor == NULL){
		error(Translate("CheckMagicObject: Übergebene Kreatur existiert nicht.\n",
						"CheckMagicObject: Creature does not exist.\n"));
		throw ERROR;
	}

	if(Actor->Type == PLAYER && !CheckRight(Actor->ID, ALL_SPELLS)
			&& CountInventoryObjects(Actor->ID, Type, 0) == 0){
		throw MAGICITEM;
	}
}

void CheckRing(TCreature *Actor, int SpellNr){
	if(Actor == NULL){
		error(Translate("CheckRing: Übergebene Kreatur existiert nicht.\n",
						"CheckRing: Creature does not exist.\n"));
		throw ERROR;
	}

	// TODO(fusion): Not sure why this was present in the original function or
	// what is the purpose of this function.
#if 0
	if(Actor->Type == PLAYER){
		CheckRight(Actor->ID, ALL_SPELLS);
	}
#endif
}

void CheckAffectedPlayers(TCreature *Actor, int x, int y, int z){
	if(Actor == NULL){
		error(Translate("CheckAffectedPlayers: Übergebene Kreatur existiert nicht.\n",
						"CheckAffectedPlayers: Creature does not exist.\n"));
		throw ERROR;
	}

	if(WorldType == NORMAL
			&& Actor->Type == PLAYER
			&& Actor->Combat.SecureMode == SECURE_MODE_ENABLED){
		Object Obj = GetFirstObject(x, y, z);
		while(Obj != NONE){
			if(Obj.getObjectType().isCreatureContainer()){
				uint32 TargetID = Obj.getCreatureID();
				if(IsCreaturePlayer(TargetID) && Actor->ID != TargetID
						&& !((TPlayer*)Actor)->IsAttackJustified(TargetID)){
					throw SECUREMODE;
				}
			}
			Obj = Obj.getNextObject();
		}
	}
}

void CheckMana(TCreature *Actor, int ManaPoints, int SoulPoints, int Delay){
	if(Actor == NULL){
		error(Translate("CheckMana: Übergebene Kreatur existiert nicht.\n",
						"CheckMana: Creature does not exist.\n"));
		throw ERROR;
	}

	if(Actor->Type != PLAYER || ManaPoints < 0){
		return;
	}

	TSkill *Mana = Actor->Skills[SKILL_MANA];
	if(Mana == NULL){
		error(Translate("CheckMana: Kein Skill MANA!\n",
						"CheckMana: No skill MANA!\n"));
		throw ERROR;
	}

	TSkill *Soul = Actor->Skills[SKILL_SOUL];
	if(Soul == NULL){
		error(Translate("CheckMana: Kein Skill SOULPOINTS!\n",
						"CheckMana: No skill SOULPOINTS!\n"));
		throw ERROR;
	}

	if(!CheckRight(Actor->ID, UNLIMITED_MANA)){
		if(Mana->Get() < ManaPoints){
			throw NOTENOUGHMANA;
		}

		if(Soul->Get() < SoulPoints){
			throw NOTENOUGHSOULPOINTS;
		}

		Mana->Change(-ManaPoints);
		Soul->Change(-SoulPoints);
	}

	if(ManaPoints > 0){
		Actor->Skills[SKILL_MAGIC_LEVEL]->Increase(ManaPoints);
	}

	uint32 EarliestSpellTime = ServerMilliseconds + Delay;
	if(Actor->EarliestSpellTime < EarliestSpellTime){
		Actor->EarliestSpellTime = EarliestSpellTime;
	}
}

int ComputeDamage(TCreature *Actor, int SpellNr, int Damage, int Variation){
	if(Variation != 0){
		Damage += random(-Variation, Variation);
	}

	if(Actor != NULL && Actor->Type == PLAYER){
		int Level = Actor->Skills[SKILL_LEVEL]->Get();
		int MagicLevel = Actor->Skills[SKILL_MAGIC_LEVEL]->Get();
		int Multiplier = Level * 2 + MagicLevel * 3;
		if(SpellNr != 0){
			if((SpellList[SpellNr].Flags & 4) != 0 && Multiplier > 100){
				Multiplier = 100;
			}

			if((SpellList[SpellNr].Flags & 8) != 0 && Multiplier < 100){
				Multiplier = 100;
			}
		}
		Damage = (Damage * Multiplier) / 100;
	}

	return Damage;
}

bool IsAggressiveSpell(int SpellNr){
	if(SpellNr < 1 || SpellNr >= NARRAY(SpellList)){
		error(Translate("IsAggressiveSpell: Ungültige Spruchnummer %d.\n",
						"IsAggressiveSpell: Invalid spell number %d.\n"), SpellNr);
		return false;
	}

	return (SpellList[SpellNr].Flags & 1) != 0;
}

void MassCombat(TCreature *Actor, Object Target, int ManaPoints, int SoulPoints,
		int Damage, int Effect, int Radius, int DamageType, int Animation){
	if(!Target.exists()){
		error(Translate("MassCombat: Übergebenes Ziel existiert nicht.\n",
						"MassCombat: Target does not exist.\n"));
		throw ERROR;
	}

	if(Actor == NULL){
		error(Translate("MassCombat: Übergebene Kreatur existiert nicht.\n",
						"MassCombat: Creature does not exist.\n"));
		throw ERROR;
	}

	int TargetX, TargetY, TargetZ;
	GetObjectCoordinates(Target, &TargetX, &TargetY, &TargetZ);
	CheckAffectedPlayers(Actor, TargetX, TargetY, TargetZ);
	if(!ThrowPossible(Actor->posx, Actor->posy, Actor->posz,
				TargetX, TargetY, TargetZ, 0)){
		throw CANNOTTHROW;
	}

	int Delay = (WorldType == PVP_ENFORCED) ? 1000 : 2000;
	CheckMana(Actor, ManaPoints, SoulPoints, Delay);

	TDamageImpact Impact(Actor, DamageType, Damage, false);
	CircleShapeSpell(Actor, TargetX, TargetY, TargetZ,
			INT_MAX, Animation, Radius, &Impact, Effect);
}

void AngleCombat(TCreature *Actor, int ManaPoints, int SoulPoints,
		int Damage, int Effect, int Range, int Angle, int DamageType){
	if(Actor == NULL){
		error(Translate("AngleCombat: Übergebene Kreatur existiert nicht.\n",
						"AngleCombat: Creature does not exist.\n"));
		throw ERROR;
	}

	int Delay = 2000;
	if(WorldType == PVP_ENFORCED || (Range == 1 && Angle == 0)){
		Delay = 1000;
	}
	CheckMana(Actor, ManaPoints, SoulPoints, Delay);

	TDamageImpact Impact(Actor, DamageType, Damage, false);
	AngleShapeSpell(Actor, Angle, Range, &Impact, Effect);
}

void Combat(TCreature *Actor, Object Target, int ManaPoints, int SoulPoints,
		int Damage, int Effect, int Animation, int DamageType){
	if(!Target.exists()){
		error(Translate("Combat: Übergebenes Ziel existiert nicht.\n",
						"Combat: Target does not exist.\n"));
		throw ERROR;
	}

	if(Actor == NULL){
		error(Translate("Combat: Übergebene Kreatur existiert nicht.\n",
						"Combat: Creature does not exist.\n"));
		throw ERROR;
	}

	int TargetX, TargetY, TargetZ;
	GetObjectCoordinates(Target, &TargetX, &TargetY, &TargetZ);
	if(!Target.getObjectType().isCreatureContainer()){
		Target = GetFirstSpecObject(TargetX, TargetY, TargetZ, TYPEID_CREATURE_CONTAINER);
	}

	if(Target == NONE){
		throw NOCREATURE;
	}

	CheckAffectedPlayers(Actor, TargetX, TargetY, TargetZ);
	if(!ThrowPossible(Actor->posx, Actor->posy, Actor->posz,
			TargetX, TargetY, TargetZ, 0)){
		throw CANNOTTHROW;
	}

	int Delay = (WorldType == PVP_ENFORCED) ? 1000 : 2000;
	CheckMana(Actor, ManaPoints, SoulPoints, Delay);

	TDamageImpact Impact(Actor, DamageType, Damage, false);
	CircleShapeSpell(Actor, TargetX, TargetY, TargetZ,
			INT_MAX, Animation, 0, &Impact, Effect);
}

int GetDirection(int dx, int dy){
	// TODO(fusion): This function originally returned directions different from
	// the ones used by creatures. I've converted it to use the same values which
	// are also defined in `enums.hh` for simplicity.
	int Result;
	if(dx == 0){
		if(dy < 0){
			Result = DIRECTION_NORTH;
		}else if(dy > 0){
			Result = DIRECTION_SOUTH;
		}else{
			Result = DIRECTION_NONE;
		}
	}else{
		// NOTE(fusion): This function uses the approximate tangent value, avoiding
		// floating point calculations. The tangent is unique and odd in the interval
		// (-PI/2, +PI/2) but since that only covers half the unit circle, we need to
		// mirror results to the other half by comparing the sign of `dx`. The Y-axis
		// is also inverted in Tibia, so we need to negate `dy`.
		constexpr int Tangent_67_5 = 618;	// => 618 / 256 ~ 2.41 ~ tan(67.5 deg)
		constexpr int Tangent_22_5 = 106;	// => 106 / 256 ~ 0.41 ~ tan(22.5 deg)
		int Tangent = (-dy * 256) / dx;		// => (dy * 256) / dx ~ (dy / dx) * 256
		if(Tangent >= Tangent_67_5){
			Result = (dx < 0) ? DIRECTION_SOUTH     : DIRECTION_NORTH;
		}else if(Tangent >= Tangent_22_5){
			Result = (dx < 0) ? DIRECTION_SOUTHWEST : DIRECTION_NORTHEAST;
		}else if(Tangent >= -Tangent_22_5){
			Result = (dx < 0) ? DIRECTION_WEST      : DIRECTION_EAST;
		}else if(Tangent >= -Tangent_67_5){
			Result = (dx < 0) ? DIRECTION_NORTHWEST : DIRECTION_SOUTHEAST;
		}else{
			Result = (dx < 0) ? DIRECTION_NORTH     : DIRECTION_SOUTH;
		}
	}
	return Result;
}

// Spell Functions
// =============================================================================
void KillAllMonsters(TCreature *Actor, int Effect, int Radius){
	if(Actor == NULL){
		error(Translate("KillAllMonsters: Übergebene Kreatur existiert nicht.\n",
						"KillAllMonsters: Creature does not exist.\n"));
		throw ERROR;
	}

	if(Actor->Type == PLAYER && !CheckRight(Actor->ID, CREATE_MONSTERS)){
		return;
	}

	// TODO(fusion): This is similar to `ExecuteCircleSpell` which makes me think
	// it got inlined and whatever `TImpact` this is got devirtualized.
	if(Radius >= NARRAY(Circle)){
		Radius = NARRAY(Circle) - 1;
	}

	int CenterX = Actor->posx;
	int CenterY = Actor->posy;
	int CenterZ = Actor->posz;
	for(int R = 0; R <= Radius; R += 1){
		int CirclePoints = Circle[R].Count;
		for(int Point = 0; Point < CirclePoints; Point += 1){
			int FieldX = CenterX + Circle[R].x[Point];
			int FieldY = CenterY + Circle[R].y[Point];
			int FieldZ = CenterZ;

			if(IsProtectionZone(FieldX, FieldY, FieldZ)){
				continue;
			}

			if(!ThrowPossible(CenterX, CenterY, CenterZ, FieldX, FieldY, FieldZ, 0)){
				continue;
			}

			Object Obj = GetFirstObject(FieldX, FieldY, FieldZ);
			while(Obj != NONE){
				if(Obj.getObjectType().isCreatureContainer()){
					TCreature *Victim = GetCreature(Obj);
					if(Victim == NULL){
						error(Translate("KillAllMonsters: Ungültige Kreatur.\n",
										"KillAllMonsters: Invalid creature provided.\n"));
					}else if(Actor != Victim && Victim->Type == MONSTER){
						print(3, Translate("Töte %s...\n",
										   "Kill %s...\n"), Victim->Name);
						Victim->Kill();
					}
				}
				Obj = Obj.getNextObject();
			}

			if(Effect != EFFECT_NONE){
				GraphicalEffect(FieldX, FieldY, FieldZ, Effect);
			}
		}
	}
}

void CreateField(int x, int y, int z, int FieldType, uint32 Owner, bool Peaceful){
	if(!FieldPossible(x, y, z, FieldType)){
		return;
	}

	SPECIALMEANING Meaning;
	switch(FieldType){
		case FIELD_TYPE_FIRE:{
			if(Peaceful){
				Meaning = MAGICFIELD_FIRE_HARMLESS;
			}else{
				Meaning = MAGICFIELD_FIRE_DANGEROUS;
			}
			break;
		}

		case FIELD_TYPE_POISON:{
			if(Peaceful){
				Meaning = MAGICFIELD_POISON_HARMLESS;
			}else{
				Meaning = MAGICFIELD_POISON_DANGEROUS;
			}
			break;
		}

		case FIELD_TYPE_ENERGY:{
			if(Peaceful){
				Meaning = MAGICFIELD_ENERGY_HARMLESS;
			}else{
				Meaning = MAGICFIELD_ENERGY_DANGEROUS;
			}
			break;
		}

		case FIELD_TYPE_MAGICWALL:{
			Meaning = MAGICFIELD_MAGICWALL;
			break;
		}

		case FIELD_TYPE_WILDGROWTH:{
			Meaning = MAGICFIELD_RUSHWOOD;
			break;
		}

		default:{
			error(Translate("CreateField: Ungültiger Feldtyp %d.\n",
							"CreateField: Invalid field type %d.\n"), FieldType);
			throw ERROR;
		}
	}

	// NOTE(fusion): Delete other magic fields?
	Object Obj = GetFirstObject(x, y, z);
	while(Obj != NONE){
		Object Next = Obj.getNextObject();
		if(Obj.getObjectType().getFlag(MAGICFIELD)){
			Delete(Obj, -1);
		}
		Obj = Next;
	}

	// NOTE(fusion): Create field, at last.
	try{
		Create(GetMapContainer(x, y, z),
				GetSpecialObject(Meaning),
				Owner);
	}catch(RESULT r){
		if(r != DESTROYED){
			throw;
		}
	}
}

void CreateField(TCreature *Actor, Object Target, int ManaPoints, int SoulPoints, int FieldType){
	if(Actor == NULL){
		error(Translate("CreateField: Ungültige Kreatur übergeben.\n",
						"CreateField: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(!Target.exists()){
		error(Translate("CreateField: Übergebenes Objekt existiert nicht.\n",
						"CreateField: Passed object does not exist.\n"));
		throw ERROR;
	}

	int TargetX, TargetY, TargetZ;
	GetObjectCoordinates(Target, &TargetX, &TargetY, &TargetZ);
	if(!FieldPossible(TargetX, TargetY, TargetZ, FieldType)){
		throw NOROOM;
	}

	CheckAffectedPlayers(Actor, TargetX, TargetY, TargetZ);
	if(!ThrowPossible(Actor->posx, Actor->posy, Actor->posz,
			TargetX, TargetY, TargetZ, 0)){
		throw CANNOTTHROW;
	}

	int Delay = (WorldType == PVP_ENFORCED) ? 1000 : 2000;
	CheckMana(Actor, ManaPoints, SoulPoints, Delay);

	int Animation = ANIMATION_ENERGY;
	if(FieldType == FIELD_TYPE_FIRE){
		Animation = ANIMATION_FIRE;
	}
	Missile(Actor->CrObject, Target, Animation);

	bool Peaceful = (WorldType == NON_PVP && Actor->IsPeaceful());
	CreateField(TargetX, TargetY, TargetZ, FieldType, Actor->ID, Peaceful);

	if(FieldType == FIELD_TYPE_FIRE
			|| FieldType == FIELD_TYPE_POISON
			|| FieldType == FIELD_TYPE_ENERGY){
		Actor->BlockLogout(60, true);
	}
}

void CreateField(TCreature *Actor, int ManaPoints, int SoulPoints, int FieldType){
	if(Actor == NULL){
		error(Translate("CreateField: Ungültige Kreatur übergeben.\n",
						"CreateField: Invalid creature provided.\n"));
		throw ERROR;
	}

	// TODO(fusion): This is probably an inlined function `TCreature::GetForwardPosition`.
	int TargetX = Actor->posx;
	int TargetY = Actor->posy;
	int TargetZ = Actor->posz;
	switch(Actor->Direction){
		case DIRECTION_NORTH:	TargetY -= 1; break;
		case DIRECTION_EAST:	TargetX += 1; break;
		case DIRECTION_SOUTH:	TargetY += 1; break;
		case DIRECTION_WEST:	TargetX -= 1; break;
	}

	if(Actor->Type == PLAYER && !CheckRight(Actor->ID, ATTACK_EVERYWHERE)
			&& IsProtectionZone(TargetX, TargetY, TargetZ)){
		throw PROTECTIONZONE;
	}

	Object Target = GetMapContainer(TargetX, TargetY, TargetZ);
	CreateField(Actor, Target, ManaPoints, SoulPoints, FieldType);
}

void MassCreateField(TCreature *Actor, Object Target,
		int ManaPoints, int SoulPoints, int FieldType, int Radius){
	if(Actor == NULL){
		error(Translate("MassCreateField: Ungültige Kreatur übergeben.\n",
						"MassCreateField: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(!Target.exists()){
		error(Translate("MassCreateField: Übergebenes Objekt existiert nicht.\n",
						"MassCreateField: Passed object does not exist.\n"));
		throw ERROR;
	}

	int TargetX, TargetY, TargetZ;
	GetObjectCoordinates(Target, &TargetX, &TargetY, &TargetZ);
	CheckAffectedPlayers(Actor, TargetX, TargetY, TargetZ);
	if(!ThrowPossible(Actor->posx, Actor->posy, Actor->posz,
			TargetX, TargetY, TargetZ, 0)){
		throw CANNOTTHROW;
	}

	int Delay = (WorldType == PVP_ENFORCED) ? 1000 : 2000;
	CheckMana(Actor, ManaPoints, SoulPoints, Delay);

	if(Actor->posx != TargetX || Actor->posy != TargetY || Actor->posz != TargetZ){
		int Animation = ANIMATION_ENERGY;
		if(FieldType == FIELD_TYPE_FIRE){
			Animation = ANIMATION_FIRE;
		}
		Missile(Actor->CrObject, Target, Animation);
	}

	// TODO(fusion): Same as `KillAllMonsters`.
	if(Radius >= NARRAY(Circle)){
		Radius = NARRAY(Circle) - 1;
	}

	bool Peaceful = (WorldType == NON_PVP && Actor->IsPeaceful());
	for(int R = 0; R <= Radius; R += 1){
		int CirclePoints = Circle[R].Count;
		for(int Point = 0; Point < CirclePoints; Point += 1){
			int FieldX = TargetX + Circle[R].x[Point];
			int FieldY = TargetY + Circle[R].y[Point];
			int FieldZ = TargetZ;

			if(IsProtectionZone(FieldX, FieldY, FieldZ)){
				continue;
			}

			if(!ThrowPossible(TargetX, TargetY, TargetZ, FieldX, FieldY, FieldZ, 0)){
				continue;
			}

			CreateField(FieldX, FieldY, FieldZ, FieldType, Actor->ID, Peaceful);
		}
	}

	if(FieldType == FIELD_TYPE_FIRE
			|| FieldType == FIELD_TYPE_POISON
			|| FieldType == FIELD_TYPE_ENERGY){
		Actor->BlockLogout(60, true);
	}
}

void CreateFieldWall(TCreature *Actor, Object Target,
		int ManaPoints, int SoulPoints, int FieldType, int Width){
	if(Actor == NULL){
		error(Translate("CreateFieldWall: Ungültige Kreatur übergeben.\n",
						"CreateFieldWall: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(!Target.exists()){
		error(Translate("CreateFieldWall: Übergebenes Objekt existiert nicht.\n",
						"CreateFieldWall: Passed object does not exist.\n"));
		throw ERROR;
	}

	int ActorX = Actor->posx;
	int ActorY = Actor->posy;
	int ActorZ = Actor->posz;
	int TargetX, TargetY, TargetZ;
	GetObjectCoordinates(Target, &TargetX, &TargetY, &TargetZ);
	CheckAffectedPlayers(Actor, TargetX, TargetY, TargetZ);
	if(!ThrowPossible(ActorX, ActorY, ActorZ, TargetX, TargetY, TargetZ, 0)){
		throw CANNOTTHROW;
	}

	int Delay = (WorldType == PVP_ENFORCED) ? 1000 : 2000;
	CheckMana(Actor, ManaPoints, SoulPoints, Delay);

	if(ActorX != TargetX || ActorY != TargetY || ActorZ != TargetZ){
		int Animation = ANIMATION_ENERGY;
		if(FieldType == FIELD_TYPE_FIRE){
			Animation = ANIMATION_FIRE;
		}
		Missile(Actor->CrObject, Target, Animation);
	}

	int StepX, StepY;
	int Direction = GetDirection(TargetX - ActorX, TargetY - ActorY);
	switch(Direction){
		case DIRECTION_NORTH:
		case DIRECTION_SOUTH:{
			StepX = 1;
			StepY = 0;
			break;
		}

		case DIRECTION_EAST:
		case DIRECTION_WEST:{
			StepX = 0;
			StepY = 1;
			break;
		}

		case DIRECTION_SOUTHWEST:
		case DIRECTION_NORTHEAST:{
			StepX = 1;
			StepY = 1;
			break;
		}

		case DIRECTION_SOUTHEAST:
		case DIRECTION_NORTHWEST:{
			StepX = -1;
			StepY = 1;
			break;
		}

		case DIRECTION_NONE:{
			throw NOROOM;
		}

		default:{
			error(Translate("CreateFieldWall: Ungültige Richtung %d.\n",
							"CreateFieldWall: Invalid direction %d.\n"), Direction);
			throw ERROR;
		}
	}

	bool Peaceful = (WorldType == NON_PVP && Actor->IsPeaceful());
	CreateField(TargetX, TargetY, TargetZ, FieldType, Actor->ID, Peaceful);
	for(int i = 1; i <= Width; i += 1){
		// NOTE(fusion): Forward.
		{
			int FieldX = TargetX + i * StepX;
			int FieldY = TargetY + i * StepY;
			int FieldZ = TargetZ;
			if(ThrowPossible(ActorX, ActorY, ActorZ, FieldX, FieldY, FieldZ, 0)
					&& !IsProtectionZone(FieldX, FieldY, FieldZ)){
				CreateField(FieldX, FieldY, FieldZ, FieldType, Actor->ID, Peaceful);
			}
		}

		// NOTE(fusion): Forward diagonal.
		if(StepX != 0 && StepY != 0){
			int FieldX = TargetX + i * StepX;
			int FieldY = TargetY + (i - 1) * StepY;
			int FieldZ = TargetZ;
			if(ThrowPossible(ActorX, ActorY, ActorZ, FieldX, FieldY, FieldZ, 0)
					&& !IsProtectionZone(FieldX, FieldY, FieldZ)){
				CreateField(FieldX, FieldY, FieldZ, FieldType, Actor->ID, Peaceful);
			}
		}

		// NOTE(fusion): Backward.
		{
			int FieldX = TargetX - i * StepX;
			int FieldY = TargetY - i * StepY;
			int FieldZ = TargetZ;
			if(ThrowPossible(ActorX, ActorY, ActorZ, FieldX, FieldY, FieldZ, 0)
					&& !IsProtectionZone(FieldX, FieldY, FieldZ)){
				CreateField(FieldX, FieldY, FieldZ, FieldType, Actor->ID, Peaceful);
			}
		}

		// NOTE(fusion): Backward Diagonal.
		if(StepX != 0 && StepY != 0){
			int FieldX = TargetX - i * StepX;
			int FieldY = TargetY - (i - 1) * StepY;
			int FieldZ = TargetZ;
			if(ThrowPossible(ActorX, ActorY, ActorZ, FieldX, FieldY, FieldZ, 0)
					&& !IsProtectionZone(FieldX, FieldY, FieldZ)){
				CreateField(FieldX, FieldY, FieldZ, FieldType, Actor->ID, Peaceful);
			}
		}
	}

	if(FieldType == FIELD_TYPE_FIRE
			|| FieldType == FIELD_TYPE_POISON
			|| FieldType == FIELD_TYPE_ENERGY){
		Actor->BlockLogout(60, true);
	}
}

void DeleteField(TCreature *Actor, Object Target, int ManaPoints, int SoulPoints){
	if(Actor == NULL){
		error(Translate("DeleteField: Ungültige Kreatur übergeben.\n",
						"DeleteField: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(!Target.exists()){
		error(Translate("DeleteField: Übergebenes Objekt existiert nicht.\n",
						"DeleteField: Passed object does not exist.\n"));
		throw ERROR;
	}

	int TargetX, TargetY, TargetZ;
	GetObjectCoordinates(Target, &TargetX, &TargetY, &TargetZ);
	if(!FieldPossible(TargetX, TargetY, TargetZ, 0)){
		throw NOROOM;
	}

	CheckMana(Actor, ManaPoints, SoulPoints, 1000);

	Object Obj = GetFirstObject(TargetX, TargetY, TargetZ);
	while(Obj != NONE){
		Object Next = Obj.getNextObject();
		if(Obj.getObjectType().getFlag(MAGICFIELD)){
			Delete(Obj, -1);
		}
		Obj = Next;
	}

	GraphicalEffect(TargetX, TargetY, TargetZ, EFFECT_POFF);
}

void CleanupField(TCreature *Actor, Object Target, int ManaPoints, int SoulPoints){
	if(Actor == NULL){
		error(Translate("CleanupField: Ungültige Kreatur übergeben.\n",
						"CleanupField: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(!Target.exists()){
		error(Translate("CleanupField: Übergebenes Objekt existiert nicht.\n",
						"CleanupField: Passed object does not exist.\n"));
		throw ERROR;
	}

	int TargetX, TargetY, TargetZ;
	GetObjectCoordinates(Target, &TargetX, &TargetY, &TargetZ);

	int Distance = std::max<int>(
			std::abs(Actor->posx - TargetX),
			std::abs(Actor->posy - TargetY));
	if(Distance > 1 || Actor->posz != TargetZ){
		throw OUTOFRANGE;
	}

	CheckMana(Actor, ManaPoints, SoulPoints, 1000);
	Object Obj = GetFirstObject(TargetX, TargetY, TargetZ);
	while(Obj != NONE){
		Object Next = Obj.getNextObject();
		ObjectType ObjType = Obj.getObjectType();
		// TODO(fusion): It seems that corpse type can be either 0 for human
		// corpses or 1 for other/monster corpses, so we're avoiding deleting
		// human corpses here.
		if(!ObjType.getFlag(UNMOVE) && !ObjType.isCreatureContainer()
		&& (!ObjType.getFlag(CORPSE) || ObjType.getAttribute(CORPSETYPE) != 0)){
			Delete(Obj, -1);
		}
		Obj = Next;
	}
	GraphicalEffect(TargetX, TargetY, TargetZ, EFFECT_POFF);
	Actor->BlockLogout(60, true);
}

void CleanupField(TCreature *Actor){
	if(Actor == NULL){
		error(Translate("CleanupField: Ungültige Kreatur übergeben.\n",
						"CleanupField: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(Actor->Type != PLAYER){
		error(Translate("CleanupField: Zauberspruch kann nur von Spielern angewendet werden.\n",
						"CleanupField: Spell can only be used by players.\n"));
		throw ERROR;
	}

	if(!CheckRight(Actor->ID, CLEANUP_FIELDS)){
		return;
	}

	// TODO(fusion): This is probably an inlined function `TCreature::GetForwardPosition`.
	int TargetX = Actor->posx;
	int TargetY = Actor->posy;
	int TargetZ = Actor->posz;
	switch(Actor->Direction){
		case DIRECTION_NORTH:	TargetY -= 1; break;
		case DIRECTION_EAST:	TargetX += 1; break;
		case DIRECTION_SOUTH:	TargetY += 1; break;
		case DIRECTION_WEST:	TargetX -= 1; break;
	}

	Object Target = GetMapContainer(TargetX, TargetY, TargetZ);
	CleanupField(Actor, Target, 0, 0);
}

void Teleport(TCreature *Actor, const char *Param){
	if(Actor == NULL){
		error(Translate("Teleport: Ungültige Kreatur übergeben.\n",
						"Teleport: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(Param == NULL){
		error(Translate("Teleport: Param ist NULL.\n",
						"Teleport: Param is NULL.\n"));
		throw ERROR;
	}

	if(Actor->Type != PLAYER){
		error(Translate("Teleport: Zauberspruch kann nur von Spielern angewendet werden.\n",
						"Teleport: Spell can only be used by players.\n"));
		throw ERROR;
	}

	int DestX = Actor->posx;
	int DestY = Actor->posy;
	int DestZ = Actor->posz;
	uint16 HouseID = 0xFFFF; // NOTE(fusion): See `SearchFreeField`.
	int MDGoStrength = Actor->Skills[SKILL_GO_STRENGTH]->MDAct;

	if(stricmp(Param, "up") == 0){
		if(!CheckRight(Actor->ID, TELEPORT_VERTICAL)){
			return;
		}
		DestZ -= 1;
	}else if(stricmp(Param, "down") == 0){
		if(!CheckRight(Actor->ID, TELEPORT_VERTICAL)){
			return;
		}
		DestZ += 1;
	}else if(stricmp(Param, "fast") == 0){
		if(!CheckRight(Actor->ID, MODIFY_GOSTRENGTH)){
			return;
		}
		MDGoStrength = 100;
	}else if(stricmp(Param, "fastest") == 0){
		if(!CheckRight(Actor->ID, MODIFY_GOSTRENGTH)){
			return;
		}
		MDGoStrength = 200;
	}else if(stricmp(Param, "slow") == 0
			|| stricmp(Param, "normal") == 0){
		if(!CheckRight(Actor->ID, MODIFY_GOSTRENGTH)){
			return;
		}
		MDGoStrength = 0;
	}else{
		int ParamX, ParamY, ParamZ;
		if(sscanf(Param, "%d,%d,%d", &ParamX, &ParamY, &ParamZ) == 3
		|| sscanf(Param, "[%d,%d,%d]", &ParamX, &ParamY, &ParamZ) == 3){
			if(!CheckRight(Actor->ID, TELEPORT_TO_COORDINATE)){
				return;
			}

			if(IsOnMap(ParamX, ParamY, ParamZ)){
				DestX = ParamX;
				DestY = ParamY;
				DestZ = ParamZ;
			}else if(IsOnMap(DestX + ParamX, DestY + ParamY, DestZ + ParamZ)){
				DestX += ParamX;
				DestY += ParamY;
				DestZ += ParamZ;
			}else{
				SendMessage(Actor->Connection, TALK_FAILURE_MESSAGE, "Invalid coordinates.");
				return;
			}
		}else{
			if(!CheckRight(Actor->ID, TELEPORT_TO_MARK)){
				return;
			}

			if(GetMarkPosition(Param, &ParamX, &ParamY, &ParamZ)){
				DestX = ParamX;
				DestY = ParamY;
				DestZ = ParamZ;
				// TODO(fusion): Not sure why we do this here. Maybe `TELEPORT_TO_MARK`
				// was also assigned to a couple of non GM characters?
				HouseID = GetHouseID(DestX, DestY, DestZ);
			}else{
				SendMessage(Actor->Connection, TALK_FAILURE_MESSAGE, "There is no mark of this name.");
				return;
			}
		}
	}

	if(DestX != Actor->posx || DestY != Actor->posy || DestZ != Actor->posz){
		if(!SearchFreeField(&DestX, &DestY, &DestZ, 1, HouseID, true)){
			throw NOROOM;
		}
		Object Dest = GetMapContainer(DestX, DestY, DestZ);
		Move(0, Actor->CrObject, Dest, -1, false, NONE);
		GraphicalEffect(DestX, DestY, DestZ, EFFECT_ENERGY);
	}else if(MDGoStrength != Actor->Skills[SKILL_GO_STRENGTH]->MDAct){
		Actor->Skills[SKILL_GO_STRENGTH]->SetMDAct(MDGoStrength);
		AnnounceChangedCreature(Actor->ID, CREATURE_SPEED_CHANGED);
		GraphicalEffect(DestX, DestY, DestZ, EFFECT_MAGIC_BLUE);
	}
}

void TeleportToCreature(TCreature *Actor, const char *Name){
	if(Actor == NULL){
		error(Translate("TeleportToCreature: Ungültige Kreatur übergeben.\n",
						"TeleportToCreature: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(Actor->Type != PLAYER){
		error(Translate("TeleportToCreature: Zauberspruch kann nur von Spielern angewendet werden.\n",
						"TeleportToCreature: Spell can only be used by players.\n"));
		throw ERROR;
	}

	if(!CheckRight(Actor->ID, TELEPORT_TO_CHARACTER)){
		return;
	}

	if(Name == NULL || Name[0] == 0){
		SendMessage(Actor->Connection, TALK_FAILURE_MESSAGE, "You must enter a name.");
		return;
	}

	TPlayer *Player;
	bool IgnoreGamemasters = !CheckRight(Actor->ID, READ_GAMEMASTER_CHANNEL);
	switch(IdentifyPlayer(Name, false, IgnoreGamemasters, &Player)){
		case  0:	break; // PLAYERFOUND ?
		case -1:	throw PLAYERNOTONLINE;
		case -2:	throw NAMEAMBIGUOUS;
		default:{
			error(Translate("TeleportToCreature: Ungültiger Rückgabewert von IdentifyPlayer.\n",
							"TeleportToCreature: Invalid return value from IdentifyPlayer.\n"));
			throw ERROR;
		}
	}

	if(Actor == Player){
		GraphicalEffect(Actor->CrObject, EFFECT_ENERGY);
		return;
	}

	GraphicalEffect(Actor->CrObject, EFFECT_POFF);

	// TODO(fusion): I assume `SearchFreeField` won't modify the input position
	// so we're either teleporting to a nearby free position or to the player's
	// position if it's not protection zone.
	int DestX = Player->posx;
	int DestY = Player->posy;
	int DestZ = Player->posz;
	uint16 HouseID = GetHouseID(DestX, DestY, DestZ);
	if(!SearchFreeField(&DestX, &DestY, &DestZ, 1, HouseID, true)
			|| IsProtectionZone(DestX, DestY, DestZ)){
		throw NOROOM;
	}

	Object Dest = GetMapContainer(DestX, DestY, DestZ);
	Move(0, Actor->CrObject, Dest, -1, false, NONE);
	GraphicalEffect(DestX, DestY, DestZ, EFFECT_ENERGY);
	Log("banish", Translate("%s teleportiert sich zu %s.\n",
							"%s teleports to %s.\n"), Actor->Name, Player->Name);
}

void TeleportPlayerToMe(TCreature *Actor, const char *Name){
	if(Actor == NULL){
		error(Translate("TeleportPlayerToMe: Ungültige Kreatur übergeben.\n",
						"TeleportPlayerToMe: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(Actor->Type != PLAYER){
		error(Translate("TeleportPlayerToMe: Zauberspruch kann nur von Spielern angewendet werden.\n",
						"TeleportPlayerToMe: Spell can only be used by players.\n"));
		throw ERROR;
	}

	if(!CheckRight(Actor->ID, RETRIEVE)){
		return;
	}

	if(Name == NULL || Name[0] == 0){
		SendMessage(Actor->Connection, TALK_FAILURE_MESSAGE, "You must enter a name.");
		return;
	}

	TPlayer *Player;
	bool IgnoreGamemasters = !CheckRight(Actor->ID, READ_GAMEMASTER_CHANNEL);
	switch(IdentifyPlayer(Name, false, IgnoreGamemasters, &Player)){
		case  0:	break; // PLAYERFOUND ?
		case -1:	throw PLAYERNOTONLINE;
		case -2:	throw NAMEAMBIGUOUS;
		default:{
			error(Translate("TeleportPlayerToMe: Ungültiger Rückgabewert von IdentifyPlayer.\n",
							"TeleportPlayerToMe: Invalid return value from IdentifyPlayer.\n"));
			throw ERROR;
		}
	}

	GraphicalEffect(Player->CrObject, EFFECT_POFF);

	int DestX = Actor->posx;
	int DestY = Actor->posy;
	int DestZ = Actor->posz;
	uint16 HouseID = GetHouseID(DestX, DestY, DestZ);
	if(!SearchFreeField(&DestX, &DestY, &DestZ, 1, HouseID, false)){
		throw NOROOM;
	}

	Object Dest = GetMapContainer(DestX, DestY, DestZ);
	Move(0, Player->CrObject, Dest, -1, false, NONE);
	GraphicalEffect(DestX, DestY, DestZ, EFFECT_ENERGY);
}

void MagicRope(TCreature *Actor, int ManaPoints, int SoulPoints){
	if(Actor == NULL){
		error(Translate("MagicRope: Ungültige Kreatur übergeben.\n",
						"MagicRope: Invalid creature provided.\n"));
		throw ERROR;
	}

	int OrigX = Actor->posx;
	int OrigY = Actor->posy;
	int OrigZ = Actor->posz;
	if(!CoordinateFlag(OrigX, OrigY, OrigZ, ROPESPOT)){
		throw NOTACCESSIBLE;
	}

	CheckMana(Actor, ManaPoints, SoulPoints, 1000);

	Object Dest = GetMapContainer(OrigX, OrigY + 1, OrigZ - 1);
	Move(0, Actor->CrObject, Dest, -1, false, NONE);
	GraphicalEffect(OrigX, OrigY, OrigZ, EFFECT_ENERGY);
}

void MagicClimbing(TCreature *Actor, int ManaPoints, int SoulPoints, const char *Param){
	if(Actor == NULL){
		error(Translate("MagicClimbing: Ungültige Kreatur übergeben.\n",
						"MagicClimbing: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(Param == NULL){
		error(Translate("MagicClimbing: Ungültige Richtung übergeben.\n",
						"MagicClimbing: Invalid direction provided.\n"));
		throw ERROR;
	}

	int OrigX = Actor->posx;
	int OrigY = Actor->posy;
	int OrigZ = Actor->posz;

	// TODO(fusion): This is probably an inlined function `TCreature::GetForwardPosition`.
	int DestX = OrigX;
	int DestY = OrigY;
	int DestZ = OrigZ;
	switch(Actor->Direction){
		case DIRECTION_NORTH:	DestY -= 1; break;
		case DIRECTION_EAST:	DestX += 1; break;
		case DIRECTION_SOUTH:	DestY += 1; break;
		case DIRECTION_WEST:	DestX -= 1; break;
	}

	if(stricmp(Param, "up") == 0){
		if(OrigZ > 0 && !CoordinateFlag(OrigX, OrigY, OrigZ - 1, BANK)
				&& !CoordinateFlag(OrigX, OrigY, OrigZ - 1, UNPASS)
				&& Actor->MovePossible(DestX, DestY, OrigZ - 1, true, true)){
			DestZ -= 1;
		}
	}else if(stricmp(Param, "down") == 0){
		if(OrigZ < 15 && !CoordinateFlag(DestX, DestY, OrigZ, BANK)
				&& !CoordinateFlag(DestX, DestY, OrigZ, UNPASS)
				&& Actor->MovePossible(DestX, DestY, OrigZ + 1, true, true)){
			DestZ += 1;
		}
	}

	if(DestZ == OrigZ){
		throw NOTACCESSIBLE;
	}

	CheckMana(Actor, ManaPoints, SoulPoints, 1000);

	Object Dest = GetMapContainer(DestX, DestY, DestZ);
	Move(0, Actor->CrObject, Dest, -1, false, NONE);
	GraphicalEffect(OrigX, OrigY, OrigZ, EFFECT_ENERGY);
}

void MagicClimbing(TCreature *Actor, const char *Param){
	// TODO(fusion): I think this is a version used by GM characters.
	if(Actor == NULL){
		error(Translate("MagicClimbing: Ungültige Kreatur übergeben.\n",
						"MagicClimbing: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(Param == NULL){
		error(Translate("MagicClimbing: Ungültige Richtung übergeben.\n",
						"MagicClimbing: Invalid direction provided.\n"));
		throw ERROR;
	}

	if(Actor->Type != PLAYER){
		error(Translate("MagicClimbing: Zauberspruch kann nur von Spielern angewendet werden.\n",
						"MagicClimbing: Spell can only be used by players.\n"));
		throw ERROR;
	}

	if(CheckRight(Actor->ID, LEVITATE)){
		MagicClimbing(Actor, 0, 0, Param);
	}
}

void CreateThing(TCreature *Actor, const char *Param1, const char *Param2){
	if(Actor == NULL){
		error(Translate("CreateThing: Ungültige Kreatur übergeben.\n",
						"CreateThing: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(Param1 == NULL){
		error(Translate("CreateThing: Ungültiger Parameter \"Param1\".\n",
						"CreateThing: Invalider Parameter \"Param1\".\n"));
		throw ERROR;
	}

	if(Actor->Type != PLAYER){
		error(Translate("CreateThing: Zauberspruch kann nur von Spielern angewendet werden.\n",
						"CreateThing: Spell can only be used by players.\n"));
		throw ERROR;
	}

	if(!CheckRight(Actor->ID, CREATE_OBJECTS)){
		return;
	}

	int TypeID = atoi(Param1);
	if(TypeID != 0){
		if(TypeID < 100 || !ObjectTypeExists(TypeID)
				|| ObjectType(TypeID).getFlag(UNMOVE)){
			TypeID = 0;
		}
	}else{
		TypeID = GetObjectTypeByName(Param1, true).TypeID;
	}

	if(TypeID == 0){
		SendMessage(Actor->Connection, TALK_FAILURE_MESSAGE,
				"There is no such takeable object.");
		return;
	}

	int Count = (Param2 != NULL ? atoi(Param2) : 1);
	if(Count < 1 || Count > 100){
		SendMessage(Actor->Connection, TALK_FAILURE_MESSAGE,
				"You may only create 1 to 100 objects.");
		return;
	}

	ObjectType ObjType(TypeID);
	if(!ObjType.getFlag(TAKE) && Count > 0){
		SendMessage(Actor->Connection, TALK_FAILURE_MESSAGE,
				"You may only create one untakeable object.");
		return;
	}

	if(!ObjType.getFlag(CUMULATIVE)){
		for(int i = 0; i < Count; i += 1){
			CreateAtCreature(Actor->ID, ObjType, 1);
		}
	}else{
		CreateAtCreature(Actor->ID, ObjType, Count);
	}

	GraphicalEffect(Actor->posx, Actor->posy, Actor->posz, EFFECT_MAGIC_GREEN);
}

void CreateMoney(TCreature *Actor, const char *Param){
	if(Actor == NULL){
		error(Translate("CreateMoney: Ungültige Kreatur übergeben.\n",
						"CreateMoney: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(Param == NULL){
		error(Translate("CreateMoney: Ungültiger Parameter \"Param1\".\n",
						"CreateMoney: Invalider Parameter \"Param1\".\n"));
		throw ERROR;
	}

	if(Actor->Type != PLAYER){
		error(Translate("CreateMoney: Zauberspruch kann nur von Spieler angewendet werden.\n",
						"CreateMoney: Spell can only be used by players.\n"));
		throw ERROR;
	}

	if(!CheckRight(Actor->ID, CREATE_MONEY)){
		return;
	}

	int Amount = atoi(Param);
	if(Amount < 1 || Amount > 1000000){
		SendMessage(Actor->Connection, TALK_FAILURE_MESSAGE,
				"You may only create 1 to 1,000,000 gold.");
	}

	int Crystal		= (Amount / 10000);
	int Platinum	= (Amount % 10000) / 100;
	int Gold		= (Amount % 10000) % 100;

	if(Crystal > 0){
		CreateAtCreature(Actor->ID, GetSpecialObject(MONEY_TENTHOUSAND), Crystal);
	}

	if(Platinum > 0){
		CreateAtCreature(Actor->ID, GetSpecialObject(MONEY_HUNDRED), Platinum);
	}

	if(Gold > 0){
		CreateAtCreature(Actor->ID, GetSpecialObject(MONEY_ONE), Gold);
	}

	GraphicalEffect(Actor->posx, Actor->posy, Actor->posz, EFFECT_MAGIC_GREEN);
}

void CreateFood(TCreature *Actor, int ManaPoints, int SoulPoints){
	if(Actor == NULL){
		error(Translate("CreateFood: Ungültige Kreatur übergeben.\n",
						"CreateFood: Invalid creature provided.\n"));
		throw ERROR;
	}

	CheckMana(Actor, ManaPoints, SoulPoints, 1000);

	int Count = (rand() % 2) + 1;
	for(int i = 0; i < Count; i += 1){
		uint8 Group, Number;
		switch(rand() % 7){
			default: // NOTE(fusion): To avoid compiler warnings.
			case 0: Group = 134; Number =  0; break;
			case 1: Group = 130; Number =  2; break;
			case 2: Group = 136; Number =  0; break;
			case 3: Group = 130; Number = 13; break;
			case 4: Group = 131; Number =  1; break;
			case 5: Group = 131; Number =  9; break;
			case 6: Group = 134; Number =  1; break;
		}
		CreateAtCreature(Actor->ID, GetNewObjectType(Group, Number), 1);
	}

	GraphicalEffect(Actor->posx, Actor->posy, Actor->posz, EFFECT_MAGIC_GREEN);
}

void CreateArrows(TCreature *Actor, int ManaPoints, int SoulPoints, int ArrowType, int Count){
	if(Actor == NULL){
		error(Translate("CreateArrows: Ungültige Kreatur übergeben.\n",
						"CreateArrows: Invalid creature provided.\n"));
		throw ERROR;
	}

	CheckMana(Actor, ManaPoints, SoulPoints, 1000);

	uint8 Number;
	switch(ArrowType){
		case 0: Number = 1; break;
		case 1: Number = 2; break;
		case 2: Number = 3; break;
		case 3: Number = 0; break;
		case 4: Number = 4; break;
		default:{
			error(Translate("CreateArrows: Ungültiger Pfeiltyp %d.\n",
							"CreateArrows: Invalid arrow type %d.\n"), ArrowType);
			throw ERROR;
		}
	}

	CreateAtCreature(Actor->ID, GetNewObjectType(94, Number), Count);
	GraphicalEffect(Actor->posx, Actor->posy, Actor->posz, EFFECT_MAGIC_BLUE);
}

void SummonCreature(TCreature *Actor, int ManaPoints, int Race, bool God){
	if(Actor == NULL){
		error(Translate("SummonCreature: Ungültige Kreatur übergeben.\n",
						"SummonCreature: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(!IsRaceValid(Race)){
		error(Translate("SummonCreature: Ungültige Rassennummer %d übergeben.\n",
						"SummonCreature: Invalid race number %d provided.\n"), Race);
		throw ERROR;
	}

	// TODO(fusion): These checks are weird. We should probably just split the
	// function in two?
	if(God){
		// TODO(fusion): What happened to checking if the creature is a player
		// before using `CheckRight`?
		if(!CheckRight(Actor->ID, CREATE_MONSTERS)){
			return;
		}
	}else if(Actor->Type == PLAYER){
		if(!CheckRight(Actor->ID, CREATE_MONSTERS) && GetRaceNoSummon(Race)){
			throw NOTACCESSIBLE;
		}

		if(Actor->SummonedCreatures >= 2){
			throw TOOMANYSLAVES;
		}
	}

	int SummonX = Actor->posx;
	int SummonY = Actor->posy;
	int SummonZ = Actor->posz;
	if(!SearchSummonField(&SummonX, &SummonY, &SummonZ, 2)){
		throw NOROOM;
	}

	uint32 Master = 0;
	if(!God){
		int Delay = (WorldType == PVP_ENFORCED) ? 1000 : 2000;
		ManaPoints += GetRaceSummonCost(Race);
		CheckMana(Actor, ManaPoints, 0, Delay);
		Master = Actor->ID;
	}

	CreateMonster(Race, SummonX, SummonY, SummonZ, 0, Master, true);
	GraphicalEffect(Actor->posx, Actor->posy, Actor->posz, EFFECT_MAGIC_BLUE);
}

void SummonCreature(TCreature *Actor, int ManaPoints, const char *RaceName, bool God){
	if(Actor == NULL){
		error(Translate("SummonCreature: Ungültige Kreatur übergeben.\n",
						"SummonCreature: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(RaceName == NULL){
		error(Translate("SummonCreature: Ungültiger Rassenname übergeben.\n",
						"SummonCreature: Invalid race name provided.\n"));
		throw ERROR;
	}

	int Race = GetRaceByName(RaceName);
	if(Race == 0){
		throw CREATURENOTEXISTING;
	}

	SummonCreature(Actor, ManaPoints, Race, God);
}

void StartMonsterraid(TCreature *Actor, const char *RaidName){
	if(Actor == NULL){
		error(Translate("StartMonsterraid: Ungültige Kreatur übergeben.\n",
						"StartMonsterraid: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(RaidName == NULL){
		error(Translate("StartMonsterraid: Ungültiger Raidname übergeben.\n",
						"StartMonsterraid: Invalid raid name provided.\n"));
		throw ERROR;
	}

	// TODO(fusion): What happened to checking if the creature is a player
	// before using `CheckRight`?
	if(!CheckRight(Actor->ID, CREATE_MONSTERS)){
		return;
	}

	char RaidNameLower[512];
	strcpy(RaidNameLower, RaidName);
	strLower(RaidNameLower);

	char FileName[4096];
	snprintf(FileName, sizeof(FileName), "%s/%s.evt", MONSTERPATH, RaidNameLower);
	if(FileExists(FileName)){
		LoadMonsterRaid(FileName, RoundNr, NULL, NULL, NULL, NULL);
		GraphicalEffect(Actor->posx, Actor->posy, Actor->posz, EFFECT_MAGIC_BLUE);
	}else{
		GraphicalEffect(Actor->posx, Actor->posy, Actor->posz, EFFECT_POFF);
	}
}

void RaiseDead(TCreature *Actor, Object Target, int ManaPoints, int SoulPoints){
	if(Actor == NULL){
		error(Translate("RaiseDead: Ungültige Kreatur übergeben.\n",
						"RaiseDead: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(!Target.exists()){
		error(Translate("RaiseDead: Übergebenes Objekt existiert nicht.\n",
						"RaiseDead: Passed object does not exist.\n"));
		throw ERROR;
	}

	int TargetX, TargetY, TargetZ;
	GetObjectCoordinates(Target, &TargetX, &TargetY, &TargetZ);

	// TODO(fusion): Could this be some `CheckRange` function inlined?
	int Distance = std::max<int>(
			std::abs(Actor->posx - TargetX),
			std::abs(Actor->posy - TargetY));
	if(Distance > 1 || Actor->posz != TargetZ){
		throw OUTOFRANGE;
	}

	Object Obj = GetFirstObject(TargetX, TargetY, TargetZ);
	while(Obj != NONE){
		ObjectType ObjType = Obj.getObjectType();
		// TODO(fusion): Same as in `CleanupField` except we're looking for a
		// non human corpse to summon a skeleton from.
		if(ObjType.getFlag(CORPSE) && ObjType.getAttribute(CORPSETYPE) == 1){
			break;
		}
		Obj = Obj.getNextObject();
	}

	if(Obj == NONE){
		throw NOTUSABLE;
	}

	if(!SearchFreeField(&TargetX, &TargetY, &TargetZ, 1, 0, false)
			|| IsProtectionZone(TargetX, TargetY, TargetZ)){
		throw NOROOM;
	}

	int Delay = (WorldType == PVP_ENFORCED) ? 1000 : 2000;
	CheckMana(Actor, ManaPoints, SoulPoints, Delay);
	Delete(Obj, -1);

	// NOTE(fusion): The race of a common skeleton is 33 but it should probably
	// be a constant somewhere.
	CreateMonster(33, TargetX, TargetY, TargetZ, 0, Actor->ID, true);
}

void MassRaiseDead(TCreature *Actor, Object Target, int ManaPoints, int SoulPoints, int Radius){
	if(Actor == NULL){
		error(Translate("MassRaiseDead: Ungültige Kreatur übergeben.\n",
						"MassRaiseDead: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(!Target.exists()){
		error(Translate("MassRaiseDead: Übergebenes Objekt existiert nicht.\n",
						"MassRaiseDead: Passed object does not exist.\n"));
		throw ERROR;
	}

	// TODO(fusion): Unless `ThrowPossible` also does some max range check, this
	// spell doesn't check range or floor.
	int TargetX, TargetY, TargetZ;
	GetObjectCoordinates(Target, &TargetX, &TargetY, &TargetZ);
	if(!ThrowPossible(Actor->posx, Actor->posy, Actor->posz, TargetX, TargetY, TargetZ, 0)){
		throw CANNOTTHROW;
	}

	int Delay = (WorldType == PVP_ENFORCED) ? 1000 : 2000;
	CheckMana(Actor, ManaPoints, SoulPoints, Delay);

	if(Actor->posx != TargetX || Actor->posy != TargetY || Actor->posz != TargetZ){
		Missile(Actor->CrObject, Target, ANIMATION_ENERGY);
	}

	// TODO(fusion): Same as `KillAllMonsters`.
	if(Radius >= NARRAY(Circle)){
		Radius = NARRAY(Circle) - 1;
	}

	for(int R = 0; R <= Radius; R += 1){
		int CirclePoints = Circle[R].Count;
		for(int Point = 0; Point < CirclePoints; Point += 1){
			int FieldX = TargetX + Circle[R].x[Point];
			int FieldY = TargetY + Circle[R].y[Point];
			int FieldZ = TargetZ;

			if(!ThrowPossible(TargetX, TargetY, TargetZ, FieldX, FieldY, FieldZ, 0)){
				continue;
			}

			GraphicalEffect(FieldX, FieldY, FieldZ, EFFECT_MAGIC_BLUE);
			Object Obj = GetFirstObject(FieldX, FieldY, FieldZ);
			while(Obj != NONE){
				Object Next = Obj.getNextObject();
				ObjectType ObjType = Obj.getObjectType();
				// NOTE(fusion): Same as `RaiseDead`.
				if(ObjType.getFlag(CORPSE) && ObjType.getAttribute(CORPSETYPE) == 1){
					int SummonX = FieldX;
					int SummonY = FieldY;
					int SummonZ = FieldZ;
					if(SearchFreeField(&SummonX, &SummonY, &SummonZ, 1, 0, false)
							&& !IsProtectionZone(SummonX, SummonY, SummonZ)){
						Delete(Obj, -1);
						CreateMonster(33, SummonX, SummonY, SummonZ, 0, Actor->ID, false);
					}
				}
				Obj = Next;
			}
		}
	}
}

void Heal(TCreature *Actor, int ManaPoints, int SoulPoints, int Amount){
	if(Actor == NULL){
		error(Translate("Heal: Ungültige Kreatur übergeben.\n",
						"Heal: Invalid creature provided.\n"));
		throw ERROR;
	}

	// TODO(fusion): This looks weird, then you peek at `IsPeaceful` and realize
	// it actually determines whether the creature is a player's summon, then it
	// gets weirder.
	if(WorldType == NON_PVP && !Actor->IsPeaceful()){
		throw NOTACCESSIBLE;
	}

	CheckMana(Actor, ManaPoints, SoulPoints, 1000);

	TSkill *HitPoints = Actor->Skills[SKILL_HITPOINTS];
	if(HitPoints == NULL){
		error(Translate("Heal: Skill HITPOINTS existiert nicht.\n",
						"Heal: Skill HITPOINTS does not exist.\n"));
		throw ERROR;
	}

	if(HitPoints->Get() > 0){
		// TODO(fusion): This looks a lot like `THealingImpact::handleCreature`
		// and I'm starting to think most of these spells use a single `TImpact`
		// on the stack to execute their effects. We don't see any sign of them
		// because they either get devirtualized by the optimizer or don't exist.
		HitPoints->Change(Amount);
		if(Actor->Skills[SKILL_GO_STRENGTH]->MDAct < 0){
			Actor->SetTimer(SKILL_GO_STRENGTH, 0, 0, 0, -1);
		}

		GraphicalEffect(Actor->posx, Actor->posy, Actor->posz, EFFECT_MAGIC_BLUE);
	}
}

void MassHeal(TCreature *Actor, Object Target, int ManaPoints, int SoulPoints, int Amount, int Radius){
	if(Actor == NULL){
		error(Translate("MassHeal: Ungültige Kreatur übergeben.\n",
						"MassHeal: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(!Target.exists()){
		error(Translate("MassHeal: Übergebenes Ziel existiert nicht.\n",
						"MassHeal: Target does not exist.\n"));
		throw ERROR;
	}

	// TODO(fusion): Same as `MassRaiseDead`.
	int TargetX, TargetY, TargetZ;
	GetObjectCoordinates(Target, &TargetX, &TargetY, &TargetZ);
	if(!ThrowPossible(Actor->posx, Actor->posy, Actor->posz, TargetX, TargetY, TargetZ, 0)){
		throw CANNOTTHROW;
	}

	CheckMana(Actor, ManaPoints, SoulPoints, 1000);

	if(Actor->posx != TargetX || Actor->posy != TargetY || Actor->posz != TargetZ){
		Missile(Actor->CrObject, Target, ANIMATION_ENERGY);
	}

	// TODO(fusion): Same as `KillAllMonsters`.
	if(Radius >= NARRAY(Circle)){
		Radius = NARRAY(Circle) - 1;
	}

	for(int R = 0; R <= Radius; R += 1){
		int CirclePoints = Circle[R].Count;
		for(int Point = 0; Point < CirclePoints; Point += 1){
			int FieldX = TargetX + Circle[R].x[Point];
			int FieldY = TargetY + Circle[R].y[Point];
			int FieldZ = TargetZ;

			if(!ThrowPossible(TargetX, TargetY, TargetZ, FieldX, FieldY, FieldZ, 0)){
				continue;
			}

			GraphicalEffect(FieldX, FieldY, FieldZ, EFFECT_MAGIC_BLUE);
			Object Obj = GetFirstObject(FieldX, FieldY, FieldZ);
			while(Obj != NONE){
				ObjectType ObjType = Obj.getObjectType();
				if(ObjType.isCreatureContainer()){
					TCreature *Victim = GetCreature(Obj);
					if(Victim == NULL){
						error(Translate("MassHeal: Ungültige Kreatur.\n",
										"MassHeal: Invalid creature provided.\n"));
					}else if(WorldType != NON_PVP || Victim->IsPeaceful()){
						// TODO(fusion): Do we really want to throw here? If not
						// having hitpoints is a problem, it should have been
						// enforced ealier for all creatures.
						TSkill *HitPoints = Victim->Skills[SKILL_HITPOINTS];
						if(HitPoints == NULL){
							error(Translate("MassHeal: Skill HITPOINTS existiert nicht.\n",
											"MassHeal: Skill HITPOINTS does not exist.\n"));
							throw ERROR;
						}

						if(HitPoints->Get() > 0){
							HitPoints->Change(Amount);
							if(Victim->Skills[SKILL_GO_STRENGTH]->MDAct < 0){
								Victim->SetTimer(SKILL_GO_STRENGTH, 0, 0, 0, -1);
							}
						}
					}
				}
				Obj = Obj.getNextObject();
			}
		}
	}
}

void HealFriend(TCreature *Actor, const char *TargetName, int ManaPoints, int SoulPoints, int Amount){
	if(Actor == NULL){
		error(Translate("HealFriend: Ungültige Kreatur übergeben.\n",
						"HealFriend: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(TargetName == NULL){
		error(Translate("HealFriend: Ungültigen Namen übergeben.\n",
						"HealFriend: Invalid name provided.\n"));
		throw ERROR;
	}

	TPlayer *Target;
	switch(IdentifyPlayer(TargetName, false, true, &Target)){
		default:
		case  0:	break; // PLAYERFOUND ?
		case -1:	throw PLAYERNOTONLINE;
		case -2:	throw NAMEAMBIGUOUS;
	}

	// TODO(fusion): These distances are related to the client's viewport.
	if(Actor->posz != Target->posz
			|| std::abs(Actor->posx - Target->posx) > 7
			|| std::abs(Actor->posy - Target->posy) > 5){
		throw OUTOFRANGE;
	}

	TSkill *HitPoints = Target->Skills[SKILL_HITPOINTS];
	if(HitPoints == NULL){
		error(Translate("HealFriend: Skill HITPOINTS existiert nicht.\n",
						"HealFriend: Skill HITPOINTS does not exist.\n"));
		throw ERROR;
	}

	if(HitPoints->Get() <= 0){
		throw PLAYERNOTONLINE;
	}

	CheckMana(Actor, ManaPoints, SoulPoints, 1000);

	HitPoints->Change(Amount);
	if(Target->Skills[SKILL_GO_STRENGTH]->MDAct < 0){
		Target->SetTimer(SKILL_GO_STRENGTH, 0, 0, 0, -1);
	}

	GraphicalEffect(Actor->posx, Actor->posy, Actor->posz, EFFECT_MAGIC_BLUE);
	GraphicalEffect(Target->posx, Target->posy, Target->posz, EFFECT_MAGIC_GREEN);
}

void RefreshMana(TCreature *Actor, int ManaPoints, int SoulPoints, int Amount){
	if(Actor == NULL){
		error(Translate("RefreshMana: Ungültige Kreatur übergeben.\n",
						"RefreshMana: Invalid creature provided.\n"));
		throw ERROR;
	}

	CheckMana(Actor, ManaPoints, SoulPoints, 1000);

	TSkill *Mana = Actor->Skills[SKILL_MANA];
	if(Mana == NULL){
		error(Translate("RefreshMana: Skill MANA existiert nicht.\n",
						"RefreshMana: Skill MANA does not exist.\n"));
		throw ERROR;
	}

	Mana->Change(Amount);
	GraphicalEffect(Actor->posx, Actor->posy, Actor->posz, EFFECT_MAGIC_BLUE);
}

void MagicGoStrength(TCreature *Actor, TCreature *Target, int ManaPoints, int SoulPoints, int Percent, int Duration){
	if(Actor == NULL){
		error(Translate("MagicGoStrength: Übergebene Kreatur existiert nicht.\n",
						"MagicGoStrength: Creature does not exist.\n"));
		throw ERROR;
	}

	if(Target == NULL){
		error(Translate("MagicGoStrength: Übergebene Ziel Kreatur existiert nicht.\n",
                                "MagicGoStrength: Creature does not exist.\n"));
		throw ERROR;
	}

	if(Percent < 0){
		if(GetRaceNoParalyze(Target->Race)){
			throw NOTACCESSIBLE;
		}

		if(WorldType == NON_PVP && Actor->IsPeaceful() && Target->IsPeaceful()){
			throw ATTACKNOTALLOWED;
		}
	}

	CheckMana(Actor, ManaPoints, SoulPoints, 1000);

	TSkill *GoStrength = Target->Skills[SKILL_GO_STRENGTH];
	if(Percent < -100){
		GoStrength->SetMDAct(-GoStrength->Act - 20);
	}else{
		GoStrength->SetMDAct((GoStrength->Act * Percent) / 100);
	}

	Target->SetTimer(SKILL_GO_STRENGTH, Duration, 10, 10, -1);

	// TODO(fusion): We should probably check if the actor is different from the
	// target before blocking the actor's logout or sending the first graphical
	// effect.
	if(Percent < 0){
		Actor->BlockLogout(60, Target->Type == PLAYER);
	}

	GraphicalEffect(Actor->posx, Actor->posy, Actor->posz, EFFECT_MAGIC_GREEN);
	GraphicalEffect(Target->posx, Target->posy, Target->posz, EFFECT_MAGIC_RED);
}

void Shielding(TCreature *Actor, int ManaPoints, int SoulPoints, int Duration){
	if(Actor == NULL){
		error(Translate("Shielding: Ungültige Kreatur übergeben.\n",
						"Shielding: Invalid creature provided.\n"));
		throw ERROR;
	}

	CheckMana(Actor, ManaPoints, SoulPoints, 1000);
	Actor->SetTimer(SKILL_MANASHIELD, 1, Duration, Duration, -1);
	GraphicalEffect(Actor->posx, Actor->posy, Actor->posz, EFFECT_MAGIC_BLUE);
}

void NegatePoison(TCreature *Actor, TCreature *Target, int ManaPoints, int SoulPoints){
	if(Actor == NULL){
		error(Translate("NegatePoison: Ungültige Kreatur übergeben.\n",
						"NegatePoison: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(Target == NULL){
		error(Translate("NegatePoison: Zielkreatur existiert nicht.\n",
						"NegatePoison: Target creature does not exist.\n"));
		throw ERROR;
	}

	CheckMana(Actor, ManaPoints, SoulPoints, 1000);
	Target->SetTimer(SKILL_POISON, 0, 0, 0, -1);
	GraphicalEffect(Target->posx, Target->posy, Target->posz, EFFECT_MAGIC_BLUE);
}

void Enlight(TCreature *Actor, int ManaPoints, int SoulPoints, int Radius, int Duration){
	if(Actor == NULL){
		error(Translate("Enlight: Ungültige Kreatur übergeben.\n",
						"Enlight: Invalid creature provided.\n"));
		throw ERROR;
	}

	CheckMana(Actor, ManaPoints, SoulPoints, 1000);
	if(Actor->Skills[SKILL_LIGHT]->TimerValue() <= Radius){
		Actor->SetTimer(SKILL_LIGHT, Radius, Duration / Radius, Duration / Radius, -1);
	}
	GraphicalEffect(Actor->posx, Actor->posy, Actor->posz, EFFECT_MAGIC_BLUE);
}

void Invisibility(TCreature *Actor, int ManaPoints, int SoulPoints, int Duration){
	if(Actor == NULL){
		error(Translate("Invisibility: Ungültige Kreatur übergeben.\n",
						"Invisibility: Invalid creature provided.\n"));
		throw ERROR;
	}

	CheckMana(Actor, ManaPoints, SoulPoints, 1000);
	Actor->Outfit = TOutfit{};
	Actor->SetTimer(SKILL_ILLUSION, 1, Duration, Duration, -1);
	GraphicalEffect(Actor->posx, Actor->posy, Actor->posz, EFFECT_MAGIC_BLUE);
}

void CancelInvisibility(TCreature *Actor, Object Target, int ManaPoints, int SoulPoints, int Radius){
	if(Actor == NULL){
		error(Translate("CancelInvisibility: Ungültige Kreatur übergeben.\n",
						"CancelInvisibility: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(!Target.exists()){
		error(Translate("CancelInvisibility: Übergebenes Ziel existiert nicht.\n",
						"CancelInvisibility: Target does not exist.\n"));
		throw ERROR;
	}

	int TargetX, TargetY, TargetZ;
	GetObjectCoordinates(Target, &TargetX, &TargetY, &TargetZ);
	if(!ThrowPossible(Actor->posx, Actor->posy, Actor->posz, TargetX, TargetY, TargetZ, 0)){
		throw CANNOTTHROW;
	}

	CheckMana(Actor, ManaPoints, SoulPoints, 1000);

	if(Actor->posx != TargetX || Actor->posy != TargetY || Actor->posz != TargetZ){
		Missile(Actor->CrObject, Target, ANIMATION_ENERGY);
	}

	// TODO(fusion): Same as `KillAllMonsters`.
	if(Radius >= NARRAY(Circle)){
		Radius = NARRAY(Circle) - 1;
	}

	// TODO(fusion): We don't include the origin (R=0) to avoid dispelling the
	// caster as this is only used with the actor also being the target. We could
	// instead just filter the actor while looping.
	for(int R = 1; R <= Radius; R += 1){
		int CirclePoints = Circle[R].Count;
		for(int Point = 0; Point < CirclePoints; Point += 1){
			int FieldX = TargetX + Circle[R].x[Point];
			int FieldY = TargetY + Circle[R].y[Point];
			int FieldZ = TargetZ;

			if(IsProtectionZone(FieldX, FieldY, FieldZ)){
				continue;
			}

			if(!ThrowPossible(TargetX, TargetY, TargetZ, FieldX, FieldY, FieldZ, 0)){
				continue;
			}

			int Effect = EFFECT_MAGIC_BLUE;
			Object Obj = GetFirstObject(FieldX, FieldY, FieldZ);
			while(Obj != NONE){
				ObjectType ObjType = Obj.getObjectType();
				if(ObjType.isCreatureContainer()){
					TCreature *Victim = GetCreature(Obj);
					if(Victim == NULL){
						error(Translate("CancelInvisibility: Ungültige Kreatur.\n",
										"CancelInvisibility: Invalid creature provided.\n"));
					}else if(Victim->IsInvisible()
							&& (WorldType != NON_PVP || !Victim->IsPeaceful())){
						Victim->SetTimer(SKILL_ILLUSION, 0, 0, 0, -1);
						if(Victim->Skills[SKILL_ILLUSION]->Get() == 0){
							Victim->Outfit = Victim->OrgOutfit;
							AnnounceChangedCreature(Victim->ID, CREATURE_OUTFIT_CHANGED);
							NotifyAllCreatures(Victim->CrObject, OBJECT_CHANGED, NONE);
						}else{
							Effect = EFFECT_BLOCK_HIT;

							// NOTE(fusion): If the victim still has an illusion effect up, it
							// must come from an item and it seems there is a chance to destroy
							// it on pvp enforced worlds.
							// TODO(fusion): This is probably an inlined function.
							if(WorldType == PVP_ENFORCED){
								for(int Position = INVENTORY_FIRST;
										Position <= INVENTORY_LAST;
										Position += 1){
									Object Item = GetBodyObject(Victim->ID, Position);
									if(Item == NONE){
										continue;
									}

									ObjectType ItemType = Item.getObjectType();
									if(ItemType.getFlag(SKILLBOOST)
									&& (int)ItemType.getAttribute(BODYPOSITION) == Position
									&& (int)ItemType.getAttribute(SKILLNUMBER) == SKILL_ILLUSION){
										if(random(1, 5) == 1){
											Delete(Item, -1);
										}
										break;
									}
								}
							}
						}
					}
				}
				Obj = Obj.getNextObject();
			}

			GraphicalEffect(FieldX, FieldY, FieldZ, Effect);
		}
	}
}

void CreatureIllusion(TCreature *Actor, int ManaPoints, int SoulPoints, const char *RaceName, int Duration){
	if(Actor == NULL){
		error(Translate("CreatureIllusion: Ungültige Kreatur übergeben.\n",
						"CreatureIllusion: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(RaceName == NULL){
		error(Translate("CreatureIllusion: Ungültiger Rassenname übergeben.\n",
						"CreatureIllusion: Invalid race name provided.\n"));
		throw ERROR;
	}

	int Race = GetRaceByName(RaceName);
	if(Race == 0){
		throw CREATURENOTEXISTING;
	}

	if(GetRaceNoIllusion(Race)){
		throw NOTACCESSIBLE;
	}

	CheckMana(Actor, ManaPoints, SoulPoints, 1000);
	if(Actor->Skills[SKILL_ILLUSION]->Get() == 0){
		Actor->Outfit = GetRaceOutfit(Race);
		Actor->SetTimer(SKILL_ILLUSION, 1, Duration, Duration, -1);
	}

	GraphicalEffect(Actor->posx, Actor->posy, Actor->posz, EFFECT_MAGIC_BLUE);
}

void ObjectIllusion(TCreature *Actor, int ManaPoints, int SoulPoints, Object Target, int Duration){
	if(Actor == NULL){
		error(Translate("ObjectIllusion: Ungültige Kreatur übergeben.\n",
						"ObjectIllusion: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(!Target.exists()){
		error(Translate("ObjectIllusion: Übergebenes Objekt existiert nicht.\n",
						"ObjectIllusion: Passed object does not exist.\n"));
		throw ERROR;
	}

	ObjectType TargetType = Target.getObjectType();
	if(TargetType.isCreatureContainer() || TargetType.getFlag(UNMOVE)){
		throw NOTMOVABLE;
	}

	CheckMana(Actor, ManaPoints, SoulPoints, 1000);
	if(Actor->Skills[SKILL_ILLUSION]->Get() == 0){
		Actor->Outfit.OutfitID = 0;
		Actor->Outfit.ObjectType = (uint16)TargetType.getDisguise().TypeID;
		Actor->SetTimer(SKILL_ILLUSION, 1, Duration, Duration, -1);
	}

	GraphicalEffect(Actor->posx, Actor->posy, Actor->posz, EFFECT_MAGIC_BLUE);
}

void ChangeData(TCreature *Actor, const char *Param){
	if(Actor == NULL){
		error(Translate("ChangeData: Ungültige Kreatur übergeben.\n",
						"ChangeData: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(Param == NULL){
		error(Translate("ChangeData: Ungültiger Parameter übergeben.\n",
						"ChangeData: Invalid parameter provided.\n"));
		throw ERROR;
	}

	if(Actor->Type != PLAYER){
		error(Translate("ChangeData: Zauberspruch kann nur von Spielern angewendet werden.\n",
						"ChangeData: Spell can only be used by players.\n"));
		throw ERROR;
	}

	if(!CheckRight(Actor->ID, CREATE_OBJECTS)){
		return;
	}

	Object RightHand = GetBodyObject(Actor->ID, INVENTORY_RIGHTHAND);
	Object LeftHand = GetBodyObject(Actor->ID, INVENTORY_LEFTHAND);
	if(RightHand != NONE && LeftHand != NONE){
		SendMessage(Actor->Connection, TALK_FAILURE_MESSAGE, "First drop one object.");
		return;
	}

	Object Obj = RightHand;
	if(Obj == NONE){
		Obj = LeftHand;
	}

	if(Obj == NONE){
		Obj = GetFirstObject(Actor->posx, Actor->posy, Actor->posz);
		while(Obj != NONE){
			if(Obj.getObjectType().getFlag(KEYDOOR)){
				break;
			}
			Obj = Obj.getNextObject();
		}
	}

	if(Obj != NONE){
		int Attribute = -1;
		int Value = atoi(Param);
		ObjectType ObjType = Obj.getObjectType();
		if(ObjType.getFlag(LIQUIDCONTAINER)){
			Attribute = CONTAINERLIQUIDTYPE;
		}else if(ObjType.getFlag(KEY)){
			if(Value > 0){
				Attribute = KEYNUMBER;
			}
		}else if(ObjType.getFlag(KEYDOOR)){
			if(Value > 0){
				Attribute = KEYHOLENUMBER;
			}
		}else if(ObjType.getFlag(CUMULATIVE)){
			if(Value > 0 && Value <= 100){
				Attribute = AMOUNT;
			}
		}else if(ObjType.getFlag(RUNE)){
			if(Value > 0 && Value <= 99){
				Attribute = CHARGES;
			}
		}

		if(Attribute != -1){
			Change(Obj, (INSTANCEATTRIBUTE)Attribute, Value);
			GraphicalEffect(Actor->posx, Actor->posy, Actor->posz, EFFECT_MAGIC_GREEN);
		}else{
			SendMessage(Actor->Connection, TALK_FAILURE_MESSAGE,
					"You can't change %s in this way.", GetName(Obj));
		}
	}
}

void EnchantObject(TCreature *Actor, int ManaPoints, int SoulPoints, ObjectType OldType, ObjectType NewType){
	if(Actor == NULL){
		error(Translate("EnchantObject: Ungültige Kreatur übergeben.\n",
						"EnchantObject: Invalid creature provided.\n"));
		throw ERROR;
	}

	Object Obj = GetBodyObject(Actor->ID, INVENTORY_RIGHTHAND);
	if(Obj == NONE || Obj.getObjectType() != OldType){
		Obj = GetBodyObject(Actor->ID, INVENTORY_LEFTHAND);
		if(Obj == NONE || Obj.getObjectType() != OldType){
			throw MAGICITEM;
		}
	}

	CheckMana(Actor, ManaPoints, SoulPoints, 1000);
	Change(Obj, NewType, 0);
	GraphicalEffect(Actor->posx, Actor->posy, Actor->posz, EFFECT_MAGIC_GREEN);
}

void Convince(TCreature *Actor, TCreature *Target){
	// TODO(fusion): All of a sudden we're not checking if `Actor` is NULL?

	if(Target->Type != MONSTER){
		throw ATTACKNOTALLOWED;
	}

	if(Actor->Type == PLAYER && Actor->SummonedCreatures >= 2){
		throw TOOMANYSLAVES;
	}

	if(WorldType == NON_PVP && Actor->IsPeaceful() && Target->IsPeaceful()){
		throw ATTACKNOTALLOWED;
	}

	if(GetRaceNoConvince(Target->Race)){
		throw NOTACCESSIBLE;
	}

	int SummonCost = GetRaceSummonCost(Target->Race);
	int Delay = (WorldType == PVP_ENFORCED) ? 1000 : 2000;
	CheckMana(Actor, SummonCost, 0, Delay);
	ConvinceMonster(Actor, Target);
	GraphicalEffect(Actor->posx, Actor->posy, Actor->posz, EFFECT_MAGIC_BLUE);
	GraphicalEffect(Target->posx, Target->posy, Target->posz, EFFECT_BONE_HIT);
}

void Challenge(TCreature *Actor, int ManaPoints, int SoulPoints, int Radius){
	// TODO(fusion): All of a sudden we're not checking if `Actor` is NULL?
	CheckMana(Actor, ManaPoints, SoulPoints, 1000);

	// TODO(fusion): Same as `KillAllMonsters`.
	if(Radius >= NARRAY(Circle)){
		Radius = NARRAY(Circle) - 1;
	}

	int ActorX = Actor->posx;
	int ActorY = Actor->posx;
	int ActorZ = Actor->posx;
	for(int R = 0; R <= Radius; R += 1){
		int CirclePoints = Circle[R].Count;
		for(int Point = 0; Point < CirclePoints; Point += 1){
			int FieldX = ActorX + Circle[R].x[Point];
			int FieldY = ActorY + Circle[R].y[Point];
			int FieldZ = ActorZ;

			if(!ThrowPossible(ActorX, ActorY, ActorZ, FieldX, FieldY, FieldZ, 0)){
				continue;
			}

			GraphicalEffect(FieldX, FieldY, FieldZ, EFFECT_MAGIC_BLUE);
			Object Obj = GetFirstSpecObject(FieldX, FieldY, FieldZ, TYPEID_CREATURE_CONTAINER);
			if(Obj != NONE){
				TCreature *Victim = GetCreature(Obj);
				if(Victim == NULL){
					error(Translate("Challenge: Ungültige Kreatur.\n",
									"Challenge: Invalid creature provided.\n"));
				}else if(Victim->Type == MONSTER){
					ChallengeMonster(Actor, Victim);
				}
			}
		}
	}
}

void FindPerson(TCreature *Actor, int ManaPoints, int SoulPoints, const char *TargetName){
	// TODO(fusion): And we're back.
	if(Actor == NULL){
		error(Translate("FindPerson: Ungültige Kreatur übergeben.\n",
						"FindPerson: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(TargetName == NULL){
		error(Translate("FindPerson: Ungültiger Name übergeben.\n",
						"FindPerson: Invalid name provided.\n"));
		throw ERROR;
	}

	if(Actor->Type != PLAYER){
		error(Translate("FindPerson: Zauberspruch kann nur von Spielern angewendet werden.\n",
						"FindPerson: Spell can only be used by players.\n"));
		throw ERROR;
	}

	TPlayer *Target;
	bool IgnoreGamemasters = !CheckRight(Actor->ID, READ_GAMEMASTER_CHANNEL);
	switch(IdentifyPlayer(TargetName, false, IgnoreGamemasters, &Target)){
		default:
		case  0:	break; // PLAYERFOUND ?
		case -1:	throw PLAYERNOTONLINE;
		case -2:	throw NAMEAMBIGUOUS;
	}

	if(Target == NULL){
		error(Translate("FindPerson: Opp ist NULL.\n",
						"FindPerson: Opp is NULL.\n"));
		throw ERROR;
	}

	CheckMana(Actor, ManaPoints, SoulPoints, 1000);

	int Distance = std::max<int>(
			std::abs(Actor->posx - Target->posx),
			std::abs(Actor->posy - Target->posy));
	if(Distance <= 4){
		if(Actor->posz > Target->posz){
			SendMessage(Actor->Connection, TALK_INFO_MESSAGE, "%s is above you.", Target->Name);
		}else if(Actor->posz < Target->posz){
			SendMessage(Actor->Connection, TALK_INFO_MESSAGE, "%s is below you.", Target->Name);
		}else{
			SendMessage(Actor->Connection, TALK_INFO_MESSAGE, "%s is standing next to you.", Target->Name);
		}
	}else{
		const char *Direction = "";
		switch(GetDirection(Target->posx - Actor->posx, Target->posy - Actor->posy)){
			case DIRECTION_NORTH:		Direction = "north"; break;
			case DIRECTION_EAST:		Direction = "east"; break;
			case DIRECTION_SOUTH:		Direction = "south"; break;
			case DIRECTION_WEST:		Direction = "west"; break;
			case DIRECTION_SOUTHWEST:	Direction = "south-west"; break;
			case DIRECTION_SOUTHEAST:	Direction = "south-east"; break;
			case DIRECTION_NORTHWEST:	Direction = "north-west"; break;
			case DIRECTION_NORTHEAST:	Direction = "north-east"; break;
			default:{
				error(Translate("FindPerson: Richtung ist Null.\n",
								"FindPerson: Direction is null.\n"));
				throw ERROR;
			}
		}

		if(Distance <= 99){
			if(Actor->posz > Target->posz){
				SendMessage(Actor->Connection, TALK_INFO_MESSAGE, "%s is on a higher level to the %s.", Target->Name, Direction);
			}else if(Actor->posz < Target->posz){
				SendMessage(Actor->Connection, TALK_INFO_MESSAGE, "%s is on a lower level to the %s.", Target->Name, Direction);
			}else{
				SendMessage(Actor->Connection, TALK_INFO_MESSAGE, "%s is to the %s.", Target->Name, Direction);
			}
		}else if(Distance <= 250){
			SendMessage(Actor->Connection, TALK_INFO_MESSAGE, "%s is far to the %s.", Target->Name, Direction);
		}else{
			SendMessage(Actor->Connection, TALK_INFO_MESSAGE, "%s is very far to the %s.", Target->Name, Direction);
		}
	}

	GraphicalEffect(Actor->posx, Actor->posy, Actor->posz, EFFECT_MAGIC_BLUE);
}

void GetPosition(TCreature *Actor){
	if(Actor == NULL){
		error(Translate("GetPosition: Ungültige Kreatur übergeben.\n",
						"GetPosition: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(Actor->Type != PLAYER){
		error(Translate("GetPosition: Zauberspruch kann nur von Spielern angewendet werden.\n",
						"GetPosition: Spell can only be used by players.\n"));
		throw ERROR;
	}

	if(CheckRight(Actor->ID, SHOW_COORDINATE)){
		SendMessage(Actor->Connection, TALK_EVENT_MESSAGE,
				"Your position is [%d,%d,%d].",
				Actor->posx, Actor->posy, Actor->posz);
	}
}

void GetQuestValue(TCreature *Actor, const char *Param){
	if(Actor == NULL){
		error(Translate("GetQuestValue: Ungültige Kreatur übergeben.\n",
						"GetQuestValue: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(Param == NULL){
		error(Translate("GetQuestValue: Param ist NULL.\n",
						"GetQuestValue: Param is NULL.\n"));
		return; // TODO(fusion): Why don't we throw here?
	}

	if(Actor->Type != PLAYER){
		error(Translate("GetQuestValue: Zauberspruch kann nur von Spielern angewendet werden.\n",
						"GetQuestValue: Spell can only be used by players.\n"));
		throw ERROR;
	}

	if(CheckRight(Actor->ID, CHANGE_SKILLS)){
		int QuestNumber = atoi(Param);
		if(QuestNumber >= 0 && QuestNumber < NARRAY(TPlayer::QuestValues)){
			SendMessage(Actor->Connection, TALK_INFO_MESSAGE, "Quest value %d is %d.",
					QuestNumber, ((TPlayer*)Actor)->GetQuestValue(QuestNumber));
		}else{
			SendMessage(Actor->Connection, TALK_FAILURE_MESSAGE, "Invalid quest number.");
		}
	}
}

void SetQuestValue(TCreature *Actor, const char *Param1, const char *Param2){
	if(Actor == NULL){
		error(Translate("SetQuestValue: Ungültige Kreatur übergeben.\n",
						"SetQuestValue: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(Param1 == NULL){
		error(Translate("SetQuestValue: Param1 ist NULL.\n",
						"SetQuestValue: Param1 is NULL.\n"));
		return; // TODO(fusion): Why don't we throw here?
	}

	if(Param2 == NULL){
		error(Translate("SetQuestValue: Param2 ist NULL.\n",
						"SetQuestValue: Param2 is NULL.\n"));
		return; // TODO(fusion): Why don't we throw here?
	}

	if(Actor->Type != PLAYER){
		error(Translate("SetQuestValue: Zauberspruch kann nur von Spielern angewendet werden.\n",
						"SetQuestValue: Spell can only be used by players.\n"));
		throw ERROR;
	}

	if(CheckRight(Actor->ID, CHANGE_SKILLS)){
		int QuestNumber = atoi(Param1);
		int QuestValue = atoi(Param2);
		if(QuestNumber >= 0 && QuestNumber < NARRAY(TPlayer::QuestValues)){
			SendMessage(Actor->Connection, TALK_INFO_MESSAGE,
					"Quest value %d set to %d.", QuestNumber, QuestValue);
		}else{
			SendMessage(Actor->Connection, TALK_FAILURE_MESSAGE, "Invalid quest number.");
		}
	}
}

void ClearQuestValues(TCreature *Actor){
	if(Actor == NULL){
		error(Translate("ClearQuestValues: Ungültige Kreatur übergeben.\n",
						"ClearQuestValues: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(Actor->Type != PLAYER){
		error(Translate("ClearQuestValues: Zauberspruch kann nur von Spielern angewendet werden.\n",
						"ClearQuestValues: Spell can only be used by players.\n"));
		throw ERROR;
	}

	if(CheckRight(Actor->ID, CHANGE_SKILLS)){
		for(int QuestNumber = 0;
				QuestNumber < NARRAY(TPlayer::QuestValues);
				QuestNumber += 1){
			((TPlayer*)Actor)->SetQuestValue(QuestNumber, 0);
		}
		SendMessage(Actor->Connection, TALK_INFO_MESSAGE, "All quest values deleted.");
	}
}

void CreateKnowledge(TCreature *Actor, const char *Param1, const char *Param2){
	if(Actor == NULL){
		error(Translate("CreateKnowledge: Ungültige Kreatur übergeben.\n",
						"CreateKnowledge: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(Param1 == NULL){
		error(Translate("CreateKnowledge: Ungültiger Parameter 1 übergeben.\n",
						"CreateKnowledge: Invalid parameter 1 provided.\n"));
		throw ERROR;
	}

	if(Actor->Type != PLAYER){
		error(Translate("CreateKnowledge: Zauberspruch kann nur von Spielern angewendet werden.\n",
						"CreateKnowledge: Spell can only be used by players.\n"));
		throw ERROR;
	}

	if(CheckRight(Actor->ID, CHANGE_SKILLS)){
		int Skill = (Param2 != NULL) ? atoi(Param1) : 0;
		int Amount = (Param2 != NULL) ? atoi(Param2) : atoi(Param1);
		if(Skill < 0 || Skill >= NARRAY(Actor->Skills) || Amount < 1){
			return;
		}
		Actor->Skills[Skill]->Increase(Amount);
		GraphicalEffect(Actor->posx, Actor->posy, Actor->posz, EFFECT_MAGIC_GREEN);
	}
}

void ChangeProfession(TCreature *Actor, const char *Param){
	if(Actor == NULL){
		error(Translate("ChangeProfession: Ungültige Kreatur übergeben.\n",
						"ChangeProfession: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(Param == NULL){
		error(Translate("ChangeProfession: Ungültigen Parameter übergeben.\n",
						"ChangeProfession: Invalid parameter provided.\n"));
		throw ERROR;
	}

	if(Actor->Type != PLAYER){
		error(Translate("ChangeProfession: Zauberspruch kann nur von Spielern angewendet werden.\n",
						"ChangeProfession: Spell can only be used by players.\n"));
		throw ERROR;
	}

	if(!CheckRight(Actor->ID, CHANGE_PROFESSION)){
		return;
	}

	if(stricmp(Param, "male") == 0){
		Actor->Sex = 1;
	}else if(stricmp(Param, "female") == 0){
		Actor->Sex = 2;
	}else if(stricmp(Param, "none") == 0){
		((TPlayer*)Actor)->ClearProfession();
	}else if(stricmp(Param, "knight") == 0){
		((TPlayer*)Actor)->ClearProfession();
		((TPlayer*)Actor)->SetProfession(PROFESSION_KNIGHT);
	}else if(stricmp(Param, "paladin") == 0){
		((TPlayer*)Actor)->ClearProfession();
		((TPlayer*)Actor)->SetProfession(PROFESSION_PALADIN);
	}else if(stricmp(Param, "sorcerer") == 0){
		((TPlayer*)Actor)->ClearProfession();
		((TPlayer*)Actor)->SetProfession(PROFESSION_SORCERER);
	}else if(stricmp(Param, "druid") == 0){
		((TPlayer*)Actor)->ClearProfession();
		((TPlayer*)Actor)->SetProfession(PROFESSION_DRUID);
	}else if(stricmp(Param, "promotion") == 0){
		// TODO(fusion): Probably some inlined function to check whether the
		// player is already promoted.
		uint8 RealProfession = ((TPlayer*)Actor)->GetRealProfession();
		if(RealProfession == 0 || RealProfession >= 10){
			return;
		}

		((TPlayer*)Actor)->SetProfession(PROFESSION_PROMOTION);
	}else{
		return;
	}

	GraphicalEffect(Actor->posx, Actor->posy, Actor->posz, EFFECT_MAGIC_BLUE);
}

void EditGuests(TCreature *Actor){
	if(Actor == NULL){
		error(Translate("EditGuests: Ungültige Kreatur übergeben.\n",
						"EditGuests: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(Actor->Type != PLAYER){
		error(Translate("EditGuests: Zauberspruch kann nur von Spielern angewendet werden.\n",
						"EditGuests: Spell can only be used by players.\n"));
		throw ERROR;
	}

	uint16 HouseID = GetHouseID(Actor->posx, Actor->posy, Actor->posz);
	if(HouseID == 0){
		throw NOTACCESSIBLE;
	}

	// TODO(fusion): Now this can be problematic. If we don't sanitize house
	// lists to be under 4KB, we can easily get a buffer overflow here.
	char GuestList[4096];
	ShowGuestList(HouseID, (TPlayer*)Actor, GuestList);
	SendEditList(Actor->Connection, GUESTLIST, HouseID, GuestList);
}

void EditSubowners(TCreature *Actor){
	if(Actor == NULL){
		error(Translate("EditSubowners: Ungültige Kreatur übergeben.\n",
						"EditSubowners: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(Actor->Type != PLAYER){
		error(Translate("EditSubowners: Zauberspruch kann nur von Spielern angewendet werden.\n",
						"EditSubowners: Spell can only be used by players.\n"));
		throw ERROR;
	}

	uint16 HouseID = GetHouseID(Actor->posx, Actor->posy, Actor->posz);
	if(HouseID == 0){
		throw NOTACCESSIBLE;
	}

	// TODO(fusion): Now this can be problematic. If we don't sanitize house
	// lists to be under 4KB, we can easily get a buffer overflow here.
	char SubOwnerList[4096];
	ShowSubownerList(HouseID, (TPlayer*)Actor, SubOwnerList);
	SendEditList(Actor->Connection, SUBOWNERLIST, HouseID, SubOwnerList);
}

void EditNameDoor(TCreature *Actor){
	if(Actor == NULL){
		error(Translate("EditNameDoor: Ungültige Kreatur übergeben.\n",
						"EditNameDoor: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(Actor->Type != PLAYER){
		error(Translate("EditNameDoor: Zauberspruch kann nur von Spielern angewendet werden.\n",
						"EditNameDoor: Spell can only be used by players.\n"));
		throw ERROR;
	}

	int DoorX = Actor->posx;
	int DoorY = Actor->posy;
	int DoorZ = Actor->posz;
	Object Obj = GetFirstObject(DoorX, DoorY, DoorZ);
	while(Obj != NONE){
		if(Obj.getObjectType().getFlag(NAMEDOOR)){
			break;
		}
		Obj = Obj.getNextObject();
	}

	if(Obj == NONE){
		// TODO(fusion): This is probably an inlined function `TCreature::GetForwardPosition`.
		switch(Actor->Direction){
			case DIRECTION_NORTH:	DoorY -= 1; break;
			case DIRECTION_EAST:	DoorX += 1; break;
			case DIRECTION_SOUTH:	DoorY += 1; break;
			case DIRECTION_WEST:	DoorX -= 1; break;
		}

		// TODO(fusion): This could be some inlined function to retrieve the
		// first object with a given flag.
		Obj = GetFirstObject(DoorX, DoorY, DoorZ);
		while(Obj != NONE){
			if(Obj.getObjectType().getFlag(NAMEDOOR)){
				break;
			}
			Obj = Obj.getNextObject();
		}
	}

	if(Obj == NONE){
		print(3, Translate("Keine NameDoor gefunden.\n",
							"No NameDoor found.\n"));
		throw NOTACCESSIBLE;
	}

	char DoorList[4096];
	ShowNameDoor(Obj, (TPlayer*)Actor, DoorList);
	SendEditList(Actor->Connection, DOORLIST, Obj.ObjectID, DoorList);
}

void KickGuest(TCreature *Actor, const char *GuestName){
	if(Actor == NULL){
		error(Translate("KickGuest(magic): Ungültige Kreatur übergeben.\n",
						"KickGuest(magic): Invalid creature provided.\n"));
		throw ERROR;
	}

	if(GuestName == NULL){
		error(Translate("KickGuest(magic): Ungültigen Gast übergeben.\n",
						"KickGuest(magic): Invaliden Gast provided.\n"));
		throw ERROR;
	}

	if(Actor->Type != PLAYER){
		error(Translate("KickGuest(magic): Zauberspruch kann nur von Spielern angewendet werden.\n",
						"KickGuest(magic): Spell can only be used by players.\n"));
		throw ERROR;
	}

	TPlayer *Guest;
	bool IgnoreGamemasters = !CheckRight(Actor->ID, READ_GAMEMASTER_CHANNEL);
	switch(IdentifyPlayer(GuestName, false, IgnoreGamemasters, &Guest)){
		default:
		case  0:	break; // PLAYERFOUND ?
		case -1:	throw PLAYERNOTONLINE;
		case -2:	throw NAMEAMBIGUOUS;
	}

	uint16 HouseID = GetHouseID(Guest->posx, Guest->posy, Guest->posz);
	if(HouseID == 0){
		throw NOTACCESSIBLE;
	}

	KickGuest(HouseID, (TPlayer*)Actor, Guest);
}

void Notation(TCreature *Actor, const char *Name, const char *Comment){
	if(Actor == NULL){
		error(Translate("Notation: Ungültige Kreatur übergeben.\n",
						"Notation: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(Name == NULL){
		error(Translate("Notation: Ungültiger Name übergeben.\n",
						"Notation: Invalid name provided.\n"));
		throw ERROR;
	}

	if(Comment == NULL){
		error(Translate("Notation: Ungültige Bemerkung übergeben.\n",
						"Notation: Invalid note provided.\n"));
		throw ERROR;
	}

	if(Actor->Type != PLAYER){
		error(Translate("Notation: Zauberspruch kann nur von Spielern angewendet werden.\n",
						"Notation: Spell can only be used by players.\n"));
		throw ERROR;
	}

	SendMessage(Actor->Connection, TALK_FAILURE_MESSAGE,
			"Spell is not available any more. Press Ctrl+Y for the dialog.");
}

void NameLock(TCreature *Actor, const char *Name){
	if(Actor == NULL){
		error(Translate("NameLock: Ungültige Kreatur übergeben.\n",
						"NameLock: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(Name == NULL){
		error(Translate("NameLock: Ungültiger Name übergeben.\n",
						"NameLock: Invalid name provided.\n"));
		throw ERROR;
	}

	if(Actor->Type != PLAYER){
		error(Translate("NameLock: Zauberspruch kann nur von Spielern angewendet werden.\n",
						"NameLock: Spell can only be used by players.\n"));
		throw ERROR;
	}

	SendMessage(Actor->Connection, TALK_FAILURE_MESSAGE,
			"Spell is not available any more. Press Ctrl+Y for the dialog.");
}

void BanishAccount(TCreature *Actor, const char *Name, int Duration, const char *Reason){
	if(Actor == NULL){
		error(Translate("BanishAccount: Ungültige Kreatur übergeben.\n",
						"BanishAccount: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(Name == NULL){
		error(Translate("BanishAccount: Ungültiger Name übergeben.\n",
						"BanishAccount: Invalid name provided.\n"));
		throw ERROR;
	}

	if(Reason == NULL){
		error(Translate("BanishAccount: Ungültiger Grund übergeben.\n",
						"BanishAccount: Invalid reason provided.\n"));
		throw ERROR;
	}

	if(Actor->Type != PLAYER){
		error(Translate("BanishAccount: Zauberspruch kann nur von Spielern angewendet werden.\n",
						"BanishAccount: Spell can only be used by players.\n"));
		throw ERROR;
	}

	SendMessage(Actor->Connection, TALK_FAILURE_MESSAGE,
			"Spell is not available any more. Press Ctrl+Y for the dialog.");
}

void DeleteAccount(TCreature *Actor, const char *Name, const char *Reason){
	if(Actor == NULL){
		error(Translate("DeleteAccount: Ungültige Kreatur übergeben.\n",
						"DeleteAccount: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(Name == NULL){
		error(Translate("DeleteAccount: Ungültiger Name übergeben.\n",
						"DeleteAccount: Invalid name provided.\n"));
		throw ERROR;
	}

	if(Reason == NULL){
		error(Translate("DeleteAccount: Ungültiger Grund übergeben.\n",
						"DeleteAccount: Invalid reason provided.\n"));
		throw ERROR;
	}

	if(Actor->Type != PLAYER){
		error(Translate("DeleteAccount: Zauberspruch kann nur von Spielern angewendet werden.\n",
						"DeleteAccount: Spell can only be used by players.\n"));
		throw ERROR;
	}

	SendMessage(Actor->Connection, TALK_FAILURE_MESSAGE,
			"Spell is not available any more. Press Ctrl+Y for the dialog.");
}

void BanishCharacter(TCreature *Actor, const char *Name, int Duration, const char *Reason){
	if(Actor == NULL){
		error(Translate("BanishCharacter: Ungültige Kreatur übergeben.\n",
						"BanishCharacter: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(Name == NULL){
		error(Translate("BanishCharacter: Ungültiger Name übergeben.\n",
						"BanishCharacter: Invalid name provided.\n"));
		throw ERROR;
	}

	if(Reason == NULL){
		error(Translate("BanishCharacter: Ungültiger Grund übergeben.\n",
						"BanishCharacter: Invalid reason provided.\n"));
		throw ERROR;
	}

	if(Actor->Type != PLAYER){
		error(Translate("BanishCharacter: Zauberspruch kann nur von Spielern angewendet werden.\n",
						"BanishCharacter: Spell can only be used by players.\n"));
		throw ERROR;
	}

	SendMessage(Actor->Connection, TALK_FAILURE_MESSAGE,
			"Spell is not available any more. Press Ctrl+Y for the dialog.");
}

void DeleteCharacter(TCreature *Actor, const char *Name, const char *Reason){
	if(Actor == NULL){
		error(Translate("DeleteCharacter: Ungültige Kreatur übergeben.\n",
						"DeleteCharacter: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(Name == NULL){
		error(Translate("DeleteCharacter: Ungültiger Name übergeben.\n",
						"DeleteCharacter: Invalid name provided.\n"));
		throw ERROR;
	}

	if(Reason == NULL){
		error(Translate("DeleteCharacter: Ungültiger Grund übergeben.\n",
						"DeleteCharacter: Invalid reason provided.\n"));
		throw ERROR;
	}

	if(Actor->Type != PLAYER){
		error(Translate("DeleteCharacter: Zauberspruch kann nur von Spielern angewendet werden.\n",
						"DeleteCharacter: Spell can only be used by players.\n"));
		throw ERROR;
	}

	SendMessage(Actor->Connection, TALK_FAILURE_MESSAGE,
			"Spell is not available any more. Press Ctrl+Y for the dialog.");
}

void IPBanishment(TCreature *Actor, const char *Name, const char *Reason){
	if(Actor == NULL){
		error(Translate("IPBanishment: Ungültige Kreatur übergeben.\n",
						"IPBanishment: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(Name == NULL){
		error(Translate("IPBanishment: Ungültiger Name übergeben.\n",
						"IPBanishment: Invalid name provided.\n"));
		throw ERROR;
	}

	if(Reason == NULL){
		error(Translate("IPBanishment: Ungültiger Grund übergeben.\n",
						"IPBanishment: Invalid reason provided.\n"));
		throw ERROR;
	}

	if(Actor->Type != PLAYER){
		error(Translate("IPBanishment: Zauberspruch kann nur von Spielern angewendet werden.\n",
						"IPBanishment: Spell can only be used by players.\n"));
		throw ERROR;
	}

	SendMessage(Actor->Connection, TALK_FAILURE_MESSAGE,
			"Spell is not available any more. Press Ctrl+Y for the dialog.");
}

void SetNameRule(TCreature *Actor, const char *Name){
	if(Actor == NULL){
		error(Translate("SetNameRule: Ungültige Kreatur übergeben.\n",
						"SetNameRule: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(Name == NULL){
		error(Translate("SetNameRule: Ungültiger Name übergeben.\n",
						"SetNameRule: Invalid name provided.\n"));
		throw ERROR;
	}

	if(Actor->Type != PLAYER){
		error(Translate("SetNameRule: Zauberspruch kann nur von Spieler angewendet werden.\n",
						"SetNameRule: Spell can only be used by players.\n"));
		throw ERROR;
	}

	SendMessage(Actor->Connection, TALK_FAILURE_MESSAGE,
			"Spell is not available any more.");
}

void KickPlayer(TCreature *Actor, const char *Name){
	if(Actor == NULL){
		error(Translate("KickPlayer: Ungültige Kreatur übergeben.\n",
						"KickPlayer: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(Name == NULL){
		error(Translate("KickPlayer: Ungültiger Name übergeben.\n",
						"KickPlayer: Invalid name provided.\n"));
		throw ERROR;
	}

	if(Actor->Type != PLAYER){
		error(Translate("KickPlayer: Zauberspruch kann nur von Spielern angewendet werden.\n",
						"KickPlayer: Spell can only be used by players.\n"));
		throw ERROR;
	}

	if(!CheckRight(Actor->ID, KICK)){
		return;
	}

	TPlayer *Player;
	bool IgnoreGamemasters = !CheckRight(Actor->ID, READ_GAMEMASTER_CHANNEL);
	switch(IdentifyPlayer(Name, false, IgnoreGamemasters, &Player)){
		default:
		case  0:	break; // PLAYERFOUND ?
		case -1:	throw PLAYERNOTONLINE;
		case -2:	throw NAMEAMBIGUOUS;
	}

	GraphicalEffect(Player->posx, Player->posy, Player->posz, EFFECT_MAGIC_GREEN);
	Player->StartLogout(true, true);
	SendMessage(Actor->Connection, TALK_INFO_MESSAGE,
			"Player %s kicked out of the game.", Player->Name);
	Log("banish", Translate("%s kickt %s.\n",
							"%s kicks %s.\n"), Actor->Name, Player->Name);
}

void HomeTeleport(TCreature *Actor, const char *Name){
	if(Actor == NULL){
		error(Translate("HomeTeleport: Ungültige Kreatur übergeben.\n",
						"HomeTeleport: Invalid creature provided.\n"));
		throw ERROR;
	}

	if(Name == NULL || Name[0] == 0){
		error(Translate("HomeTeleport: Name existiert nicht.\n",
						"HomeTeleport: Name does not exist.\n"));
		throw ERROR;
	}

	if(Actor->Type != PLAYER){
		error(Translate("HomeTeleport: Zauberspruch kann nur von Spielern angewendet werden.\n",
						"HomeTeleport: Spell can only be used by players.\n"));
		throw ERROR;
	}

	if(!CheckRight(Actor->ID, HOME_TELEPORT)){
		return;
	}

	TPlayer *Player;
	bool IgnoreGamemasters = !CheckRight(Actor->ID, READ_GAMEMASTER_CHANNEL);
	switch(IdentifyPlayer(Name, false, IgnoreGamemasters, &Player)){
		default:
		case  0:	break; // PLAYERFOUND ?
		case -1:	throw PLAYERNOTONLINE;
		case -2:	throw NAMEAMBIGUOUS;
	}

	Object Dest = GetMapContainer(Player->startx, Player->starty, Player->startz);
	GraphicalEffect(Player->posx, Player->posy, Player->posz, EFFECT_POFF);
	Move(0, Player->CrObject, Dest, -1, false, NONE);
	GraphicalEffect(Player->posx, Player->posy, Player->posz, EFFECT_ENERGY);

	SendMessage(Actor->Connection, TALK_INFO_MESSAGE,
			"Player %s has been moved to the temple.", Player->Name);
	Log("banish", Translate("%s teleportiert %s zum Tempel.\n",
							"%s teleports %s to the temple.\n"), Actor->Name, Player->Name);
}

// Spell Casting
// =============================================================================
static void CharacterRightSpell(uint32 CreatureID, int SpellNr, const char (*SpellStr)[512]){
	TCreature *Actor = GetCreature(CreatureID);
	if(Actor == NULL){
		error(Translate("CharacterRightSpell: Kreatur existiert nicht.\n",
						"CharacterRightSpell: Creature does not exist.\n"));
		throw ERROR;
	}

	if(Actor->Type != PLAYER){
		return;
	}

	switch(SpellNr) {
		case 34: CreateThing(Actor, SpellStr[2], SpellStr[3]); 		break;
		case 35: CreateThing(Actor, SpellStr[2], NULL); 			break;
		case 37: Teleport(Actor, SpellStr[2]); 						break;
		case 40: CreateKnowledge(Actor, SpellStr[3], NULL); 		break;
		case 41: ChangeData(Actor, SpellStr[2]); 					break;
		case 46: CreateKnowledge(Actor, SpellStr[3], SpellStr[4]); 	break;
		case 47: TeleportToCreature(Actor, SpellStr[3]); 			break;
		case 52: TeleportPlayerToMe(Actor, SpellStr[3]); 			break;
		case 53: SummonCreature(Actor, 0, SpellStr[3], true); 		break;
		case 58: GetPosition(Actor); 								break;
		case 63: CreateMoney(Actor, SpellStr[3]); 					break;
		case 64: ChangeProfession(Actor, SpellStr[3]); 				break;
		case 71: EditGuests(Actor); 								break;
		case 72: EditSubowners(Actor); 								break;
		case 73: KickGuest(Actor, SpellStr[3]); 					break;
		case 74: EditNameDoor(Actor); 								break;
		case 96: GetQuestValue(Actor, SpellStr[3]); 				break;
		case 97: SetQuestValue(Actor, SpellStr[3], SpellStr[4]); 	break;
		case 98: CleanupField(Actor); 								break;
		case 99: MagicClimbing(Actor, SpellStr[3]); 				break;
		case 100: ClearQuestValues(Actor); 							break;
		case 101: KillAllMonsters(Actor, EFFECT_DEATH, 2); 			break;
		case 102: StartMonsterraid(Actor, SpellStr[4]); 			break;
	}
}

static void AccountRightSpell(uint32 CreatureID, int SpellNr, const char (*SpellStr)[512]){
	TCreature *Actor = GetCreature(CreatureID);
	if(Actor == NULL){
		error(Translate("AccountRightSpell: Kreatur existiert nicht.\n",
						"AccountRightSpell: Creature does not exist.\n"));
		throw ERROR;
	}

	if(Actor->Type != PLAYER){
		return;
	}

	switch(SpellNr) {
		case 57: BanishAccount(Actor,  SpellStr[3], atoi(SpellStr[4]), SpellStr[5]);	break;
		case 60: HomeTeleport(Actor, SpellStr[2]);										break;
		case 61: DeleteAccount(Actor, SpellStr[4], SpellStr[5]);						break;
		case 62: SetNameRule(Actor, SpellStr[2]);										break;
		case 65: Notation(Actor, SpellStr[2], SpellStr[3]);								break;
		case 66: NameLock(Actor, SpellStr[3]);											break;
		case 67: KickPlayer(Actor, SpellStr[2]);										break;
		case 68: DeleteCharacter(Actor, SpellStr[4], SpellStr[5]);						break;
		case 69: IPBanishment(Actor, SpellStr[3], SpellStr[4]);							break;
		case 70: BanishCharacter(Actor, SpellStr[3], atoi(SpellStr[4]), SpellStr[5]);	break;
	}
}

static void CastSpell(uint32 CreatureID, int SpellNr, const char (*SpellStr)[512]){
	TCreature *Actor = GetCreature(CreatureID);
	if(Actor == NULL){
		error(Translate("CastSpell: Kreatur existiert nicht.\n",
						"CastSpell: Creature does not exist.\n"));
		throw ERROR;
	}

	CheckSpellbook(Actor, SpellNr);
	CheckAccount(Actor, SpellNr);
	CheckLevel(Actor, SpellNr);
	CheckRing(Actor, SpellNr);

	if(Actor->EarliestSpellTime > ServerMilliseconds){
		throw EXHAUSTED;
	}

	if(IsAggressiveSpell(SpellNr) && Actor->Type == PLAYER
			&& !CheckRight(Actor->ID, ATTACK_EVERYWHERE)
			&& IsProtectionZone(Actor->posx, Actor->posy, Actor->posz)){
		throw PROTECTIONZONE;
	}

	int ManaPoints = SpellList[SpellNr].Mana;
	int SoulPoints = SpellList[SpellNr].SoulPoints;
	switch(SpellNr) {
		case 1:{
			int Amount = ComputeDamage(Actor, SpellNr, 20, 10);
			Heal(Actor, ManaPoints, SoulPoints, Amount);
			break;
		}

		case 2:{
			int Amount = ComputeDamage(Actor, SpellNr, 40, 20);
			Heal(Actor, ManaPoints, SoulPoints, Amount);
			break;
		}

		case 3:{
			int Amount = ComputeDamage(Actor, SpellNr, 250, 50);
			Heal(Actor, ManaPoints, SoulPoints, Amount);
			break;
		}

		case 6:{
			MagicGoStrength(Actor, Actor, ManaPoints, SoulPoints, 30, 3);
			break;
		}

		case 9:{
			SummonCreature(Actor, ManaPoints, SpellStr[3], false);
			break;
		}

		case 10:{
			Enlight(Actor, ManaPoints, SoulPoints, 6, 500);
			break;
		}

		case 11:{
			Enlight(Actor, ManaPoints, SoulPoints, 8, 1000);
			break;
		}

		case 13:{
			int Damage = ComputeDamage(Actor, SpellNr, 150, 50);
			AngleCombat(Actor, ManaPoints, SoulPoints, Damage,
					EFFECT_ENERGY, 5, 30, DAMAGE_ENERGY);
			break;
		}

		case 19:{
			int Damage = ComputeDamage(Actor, SpellNr, 30, 10);
			AngleCombat(Actor, ManaPoints, SoulPoints, Damage,
					EFFECT_FIRE, 4, 45, DAMAGE_FIRE);
			break;
		}

		case 20:{
			FindPerson(Actor, ManaPoints, SoulPoints, SpellStr[2]);
			break;
		}

		case 22:{
			int Damage = ComputeDamage(Actor, SpellNr, 60, 20);
			AngleCombat(Actor, ManaPoints, SoulPoints, Damage,
					EFFECT_FIRE_BURST, 5, 0, DAMAGE_ENERGY);
			break;
		}

		case 23:{
			int Damage = ComputeDamage(Actor, SpellNr, 120, 80);
			AngleCombat(Actor, ManaPoints, SoulPoints, Damage,
					EFFECT_FIRE_BURST, 8, 0, DAMAGE_ENERGY);
			break;
		}

		case 24:{
			int Damage = ComputeDamage(Actor, SpellNr, 250, 50);
			MassCombat(Actor, Actor->CrObject, ManaPoints, SoulPoints, Damage,
					EFFECT_FIRE_EXPLOSION, 6, DAMAGE_PHYSICAL, ANIMATION_FIRE);
			break;
		}

		case 29:{
			NegatePoison(Actor, Actor, ManaPoints, SoulPoints);
			break;
		}

		case 38:{
			CreatureIllusion(Actor, ManaPoints, SoulPoints, SpellStr[4], 200);
			break;
		}

		case 39:{
			MagicGoStrength(Actor, Actor, ManaPoints, SoulPoints, 70, 2);
			break;
		}

		case 42:{
			CreateFood(Actor, ManaPoints, SoulPoints);
			break;
		}

		case 44:{
			Shielding(Actor, ManaPoints, SoulPoints, 200);
			break;
		}

		case 45:{
			Invisibility(Actor, ManaPoints, SoulPoints, 200);
			break;
		}

		case 48:{
			CreateArrows(Actor, ManaPoints, SoulPoints, 1, 5);
			break;
		}

		case 49:{
			CreateArrows(Actor, ManaPoints, SoulPoints, 2, 3);
			break;
		}

		case 51:{
			CreateArrows(Actor, ManaPoints, SoulPoints, 0, 10);
			break;
		}

		case 56:{
			int Damage = ComputeDamage(Actor, SpellNr, 200, 50);
			MassCombat(Actor, Actor->CrObject, ManaPoints, SoulPoints, Damage,
					EFFECT_POISON, 8, DAMAGE_POISON_PERIODIC, ANIMATION_NONE);
			break;
		}

		case 75:{
			Enlight(Actor, ManaPoints, SoulPoints, 9, 2000);
			break;
		}

		case 76:{
			MagicRope(Actor, ManaPoints, SoulPoints);
			break;
		}

		case 79:{
			CreateArrows(Actor, ManaPoints, SoulPoints, 3, 5);
			break;
		}

		case 80:{
			int Level = Actor->Skills[SKILL_LEVEL]->Get();
			int Damage = (Level * ComputeDamage(Actor, SpellNr, 80, 20)) / 25;
			MassCombat(Actor, Actor->CrObject, Level * 4, 0, Damage,
					EFFECT_BONE_HIT, 2, DAMAGE_PHYSICAL, ANIMATION_NONE);
			break;
		}

		case 81:{
			MagicClimbing(Actor, ManaPoints, SoulPoints, SpellStr[3]);
			break;
		}

		case 82:{
			int Amount = ComputeDamage(Actor, SpellNr, 200, 40);
			MassHeal(Actor, Actor->CrObject, ManaPoints, SoulPoints, Amount, 4);
			break;
		}

		case 84:{
			int Amount = ComputeDamage(Actor,SpellNr,120,40);
			HealFriend(Actor, SpellStr[3], ManaPoints, SoulPoints, Amount);
			break;
		}

		case 85:{
			MassRaiseDead(Actor, Actor->CrObject, ManaPoints, SoulPoints, 4);
			break;
		}

		case 87:{
			int Damage = ComputeDamage(Actor, SpellNr, 45, 10);
			AngleCombat(Actor, ManaPoints, SoulPoints, Damage,
					EFFECT_DEATH, 1, 0, DAMAGE_PHYSICAL);
			break;
		}

		case 88:{
			int Damage = ComputeDamage(Actor, SpellNr, 45, 10);
			AngleCombat(Actor, ManaPoints, SoulPoints, Damage,
					EFFECT_ENERGY, 1, 0, DAMAGE_ENERGY);
			break;
		}

		case 89:{
			int Damage = ComputeDamage(Actor, SpellNr, 45, 10);
			AngleCombat(Actor, ManaPoints, SoulPoints, Damage,
					EFFECT_FIRE, 1, 0, DAMAGE_FIRE);
			break;
		}

		case 90:{
			CancelInvisibility(Actor, Actor->CrObject, ManaPoints, SoulPoints, 4);
			break;
		}

		case 92:{
			ObjectType OldType = GetNewObjectType(90, 25);
			ObjectType NewType = GetNewObjectType(90, 57);
			EnchantObject(Actor, ManaPoints, SoulPoints, OldType, NewType);
			break;
		}

		case 93:{
			Challenge(Actor, ManaPoints, SoulPoints, 2);
			break;
		}

		case 94:{
			CreateField(Actor, ManaPoints, SoulPoints, FIELD_TYPE_WILDGROWTH);
			break;
		}

		case 95:{
			CreateArrows(Actor, ManaPoints, SoulPoints, 4, 1);
			break;
		}
	}

	if(IsAggressiveSpell(SpellNr)){
		Actor->BlockLogout(60, false);
	}
}

static void RuneSpell(uint32 CreatureID, int SpellNr){
	TCreature *Actor = GetCreature(CreatureID);
	if(Actor == NULL){
		error(Translate("RuneSpell: Kreatur existiert nicht.\n",
						"RuneSpell: Creature does not exist.\n"));
		throw ERROR;
	}

	uint8 RuneGr = SpellList[SpellNr].RuneGr;
	uint8 RuneNr = SpellList[SpellNr].RuneNr;
	int ManaPoints = SpellList[SpellNr].Mana;
	int SoulPoints = SpellList[SpellNr].SoulPoints;
	int Amount = SpellList[SpellNr].Amount;

	if(RuneGr == 0){
		error(Translate("RuneSpell: Spell %d ist Runenspruch, hat aber keine Rune.\n",
						"RuneSpell: Spell %d is a rune spell, but has no rune.\n"), SpellNr);
		throw ERROR;
	}

	CheckSpellbook(Actor, SpellNr);
	CheckAccount(Actor, SpellNr);
	CheckLevel(Actor, SpellNr);
	CheckRing(Actor, SpellNr);
	if(Actor->EarliestSpellTime > ServerMilliseconds){
		throw EXHAUSTED;
	}

	bool RuneCreated = false;
	ObjectType BlankType = GetSpecialObject(RUNE_BLANK);

	Object RightHand = GetBodyObject(Actor->ID, INVENTORY_RIGHTHAND);
	if(RightHand.exists() && RightHand.getObjectType() == BlankType){
		CheckMana(Actor, ManaPoints, SoulPoints, 1000);
		ObjectType RuneType = GetNewObjectType(RuneGr, RuneNr);
		Change(RightHand, RuneType, Amount);
		RuneCreated = true;
	}

	Object LeftHand = GetBodyObject(Actor->ID, INVENTORY_LEFTHAND);
	if(LeftHand.exists() && LeftHand.getObjectType() == BlankType){
		// TODO(fusion): Ughh... I'm not sure why we're trying to cast the spell
		// twice but we need to make so errors from the second cast are only
		// relevant if there wasn't a first cast.
		try{
			CheckMana(Actor, ManaPoints, SoulPoints, 1000);
			ObjectType RuneType = GetNewObjectType(RuneGr, RuneNr);
			Change(LeftHand, RuneType, Amount);
			RuneCreated = true;
		}catch(RESULT r){
			if(!RuneCreated){
				throw;
			}
		}
	}

	if(!RuneCreated){
		throw MAGICITEM;
	}

	GraphicalEffect(Actor->posx, Actor->posy, Actor->posz, EFFECT_MAGIC_RED);
}

static int TypeOfSpell(const char *Text){
	// NOTE(fusion): We're checking `Text` against the first 5 spell syllables
	// which are "al", "ad", "ex", "ut", "om". They determine the type of the
	// spell. See `CheckForSpell`.
	int SpellType = 0;
	if(Text != NULL){
		for(int SyllableNr = 1;
				SyllableNr < 6;
				SyllableNr += 1){
			if(stricmp(Text, SpellSyllable[SyllableNr], 2) == 0){
				SpellType = SyllableNr;
				break;
			}
		}
	}
	return SpellType;
}

static int FindSpell(const uint8 *Syllable){
	int BestMatch = 0;
	int MinParams = 100;
	for(int SpellNr = 0;
			SpellNr < NARRAY(SpellList);
			SpellNr += 1){
		int Params = 0;
		TSpellList *Spell = &SpellList[SpellNr];
		for(int i = 0;
				i < NARRAY(Spell->Syllable);
				i += 1){
			// NOTE(fusion): SpellSyllable[6] is "para" which refers to a spell
			// parameter.
			if(Spell->Syllable[i] == 6){
				if(Syllable[i] == 0)
					break;
				Params += 1;
			}else if(Spell->Syllable[i] != Syllable[i]){
				break;
			}

			if(Spell->Syllable[i] == 0){
				if(Params < MinParams){
					MinParams = Params;
					BestMatch = SpellNr;
				}
				break;
			}
		}
	}

	return BestMatch;
}

static int FindSpell(Object Obj){
	if(!Obj.exists()){
		error(Translate("FindSpell: Übergebenes Objekt existiert nicht.\n",
						"FindSpell: Passed object does not exist.\n"));
		return 0;
	}

	ObjectType ObjType = Obj.getObjectType();
	if(!ObjType.getFlag(RUNE)){
		error(Translate("FindSpell: Übergebenes Objekt ist nicht magisch.\n",
						"FindSpell: Passed object is not magical.\n"));
		return 0;
	}

	int Result = 0;
	for(int SpellNr = 0;
			SpellNr < NARRAY(SpellList);
			SpellNr += 1){
		TSpellList *Spell = &SpellList[SpellNr];
		ObjectType RuneType = GetNewObjectType(Spell->RuneGr, Spell->RuneNr);
		if(ObjType == RuneType){
			Result = SpellNr;
			break;
		}
	}
	return Result;
}

static void GetSpellString(int SpellNr, char *Text){
	Text[0] = 0;

	if(SpellNr < 1 || SpellNr >= NARRAY(SpellList)){
		error(Translate("GetSpellString: Ungültige Zauberspruchnummer %d.\n",
						"GetSpellString: Invalid spell number %d.\n"), SpellNr);
		return;
	}

	TSpellList *Spell = &SpellList[SpellNr];
	for(int i = 0; i < NARRAY(Spell->Syllable); i += 1){
		const char *Syllable = SpellSyllable[Spell->Syllable[i]];
		if(Syllable[0] == 0){
			break;
		}

		// TODO(fusion): Review. I think we don't add a space between the first
		// and second syllable and it's probably correct.
		if(i >= 2){
			strcat(Text, " ");
		}

		strcat(Text, Syllable);
	}
}

void GetMagicItemDescription(Object Obj, char *SpellString, int *MagicLevel){
	SpellString[0] = 0;
	*MagicLevel = 0;

	if(!Obj.exists()){
		error(Translate("GetMagicItemDescription: Übergebenes Objekt existiert nicht.\n",
						"GetMagicItemDescription: Passed object does not exist.\n"));
		return;
	}

	ObjectType ObjType = Obj.getObjectType();
	if(!ObjType.getFlag(RUNE)){
		error(Translate("GetMagicItemDescription: Übergebenes Objekt ist nicht magisch.\n",
						"GetMagicItemDescription: Passed object is not magical.\n"));
		return;
	}

	int SpellNr = FindSpell(Obj);
	if(SpellNr == 0){
		error(Translate("GetMagicItemDescription: Objekt %d hat keinen Zauberspruch.\n",
						"GetMagicItemDescription: Object %d has no spell.\n"), ObjType.TypeID);
		return;
	}

	GetSpellString(SpellNr, SpellString);
	*MagicLevel = (int)SpellList[SpellNr].RuneLevel;
}

void GetSpellbook(uint32 CharacterID, char *Buffer){
	TPlayer *Player = GetPlayer(CharacterID);
	if(Player == NULL){
		error(Translate("GetSpellbook: Spieler existiert nicht.\n",
						"GetSpellbook: Player does not exist.\n"));
		return;
	}

	if(Buffer == NULL){
		error(Translate("GetSpellbook: Übergebener Puffer existiert nicht.\n",
						"GetSpellbook: Passed buffer does not exist.\n"));
		return;
	}

	// TODO(fusion): We're iterating over all spells for each level. This is bad
	// because we could have a list of spell numbers sorted by level and achieve
	// the same thing. We also had a hard coded max level but since we're already
	// iterating so many times over the spell list, what's another one?
	//	 Also, the overuse of `strcat` doesn't help.

	int MaxLevel = 1;
	for(int SpellNr = 0;
			SpellNr < NARRAY(SpellList);
			SpellNr += 1){
		int Level = (int)SpellList[SpellNr].Level;
		if(MaxLevel < Level){
			MaxLevel = Level;
		}
	}

	char Help[256];
	for(int Level = 1; Level <= MaxLevel; Level += 1){
		bool First = true;
		for(int SpellNr = 0;
				SpellNr < NARRAY(SpellList);
				SpellNr += 1){
			TSpellList *Spell = &SpellList[SpellNr];
			if((int)Spell->Level != Level || !Player->SpellKnown(SpellNr)){
				continue;
			}

			if(First){
				sprintf(Help, "Spells for Level %d\n", Level);
				strcat(Buffer, Help);
				First = false;
			}

			GetSpellString(SpellNr, Help);
			if(Help[0] == 0){
				error(Translate("GetSpellbook: Zauberspruch %d hat keine Zauberformel.\n",
								"GetSpellbook: Spell %d has no incantation.\n"), SpellNr);
				continue;
			}

			strcat(Buffer, "  ");
			strcat(Buffer, Help);
			if(Spell->Comment != NULL && Spell->Comment[0] != 0){
				strcat(Buffer, " - ");
				strcat(Buffer, Spell->Comment);
				if(SpellNr == 9 || SpellNr == 12){
					strcpy(Help, ": var");
				}else if(SpellNr == 80){
					strcpy(Help, ": 4*Level");
				}else{
					sprintf(Help, ": %d", Spell->Mana);
				}
				strcat(Buffer, Help);
			}
			strcat(Buffer, "\n");
		}

		if(!First){
			strcat(Buffer, "\n");
		}
	}
}

int GetSpellLevel(int SpellNr){
	if(SpellNr < 1 || SpellNr >= NARRAY(SpellList)){
		error(Translate("GetSpellLevel: Ungültige Spruchnummer %d.\n",
						"GetSpellLevel: Invalid spell number %d.\n"), SpellNr);
		return 1;
	}

	return (int)SpellList[SpellNr].Level;
}

int CheckForSpell(uint32 CreatureID, const char *Text){
	// IMPORTANT(fusion): The first syllable also determines the type of the spell
	// which is why we can quickly rule out a spell cast if it is unknown.
	//	It is also handled separately in the parsing loop below to allow for the
	// second syllable to be glued together (or not).
	int SpellType = TypeOfSpell(Text);
	if(SpellType == 0){
		return 0;
	}

	// TODO(fusion): Keeping syllables in text form doesn't make sense. `SpellStr`
	// should ideally only contain parameters.

	// TODO(fusion): Did we really need to use `stringstream` here? It allocates
	// an internal `std::string` from the input string which is just wasteful,
	// specially for the minimal processing we're doing here.

	int SyllableCount = 1;
	uint8 Syllable[10] = { (uint8)SpellType };
	char SpellStr[10][512] = {};
	strcpy(SpellStr[0], SpellSyllable[SpellType]);

	std::istringstream IS(Text);
	IS.get();
	IS.get();
	while(!IS.eof()){
		while(isSpace(IS.peek())){
			IS.get();
		}

		int Index = SyllableCount;
		if(IS.peek() == '"'){
			IS.get();
			IS.get(SpellStr[Index], sizeof(SpellStr[0]), '"');
			IS.get();
		}else{
			IS.get(SpellStr[Index], sizeof(SpellStr[0]), ' ');
		}

		// TODO(fusion): This could be a problem if there is a "" parameter?
		if(SpellStr[Index][0] == 0){
			break;
		}

		for(int SyllableNr = 0;
				SyllableNr < NARRAY(SpellSyllable);
				SyllableNr += 1){
			if(stricmp(SpellStr[Index], SpellSyllable[SyllableNr]) == 0){
				Syllable[Index] = (uint8)SyllableNr;
				break;
			}
		}

		// NOTE(fusion): SpellSyllable[6] is "para" which refers to a spell
		// parameter. This is setting up `Syllable` to be used by `FindSpell`.
		if(Syllable[Index] == 0){
			Syllable[Index] = 6;
		}

		SyllableCount += 1;
	}

	int SpellNr = FindSpell(Syllable);
	if(SpellNr == 0){
		return 0;
	}

	try{
		switch(SpellType){
			case 1: CharacterRightSpell(CreatureID, SpellNr, SpellStr); break;
			case 2: RuneSpell(CreatureID, SpellNr); break;
			case 3: CastSpell(CreatureID, SpellNr, SpellStr); break;
			case 4: CastSpell(CreatureID, SpellNr, SpellStr); break;
			case 5: AccountRightSpell(CreatureID, SpellNr, SpellStr); break;
			default:{
				error(Translate("CheckForSpell: Spruchklasse %d existiert nicht.\n",
								"CheckForSpell: Spell class %d does not exist.\n"), SpellType);
				break;
			}
		}

		return SpellType;
	}catch(RESULT r){
		// TODO(fusion): `SpellFailed` is inlined in here but I think it's
		// cleaner to keep it this way.
		TCreature *Actor = GetCreature(CreatureID);
		if(Actor == NULL){
			error(Translate("SpellFailed: Kreatur existiert nicht.\n",
							"SpellFailed: Creature does not exist.\n"));
		}else{
			if(r != ERROR){
				GraphicalEffect(Actor->posx, Actor->posy, Actor->posz, EFFECT_POFF);
			}

			if(Actor->Type == PLAYER){
				SendResult(Actor->Connection, r);
			}
		}

		return -SpellType;
	}
}

static void DeleteRune(Object Obj){
	if(!Obj.exists()){
		error(Translate("DeleteRune: Übergebenes Objekt existiert nicht.\n",
						"DeleteRune: Passed object does not exist.\n"));
		throw ERROR;
	}

	// TODO(fusion): Should probably check if object is a rune?
	uint32 Charges = Obj.getAttribute(CHARGES);
	if(Charges > 1){
		Change(Obj, CHARGES, Charges - 1);
	}else{
		Delete(Obj, -1);
	}
}

void UseMagicItem(uint32 CreatureID, Object Obj, Object Dest){
	TPlayer *Actor = GetPlayer(CreatureID);
	if(Actor == NULL){
		error(Translate("UseMagicItem: Kreatur existiert nicht.\n",
						"UseMagicItem: Creature does not exist.\n"));
		throw ERROR;
	}

	if(!Obj.exists()){
		error(Translate("UseMagicItem: Übergebenes Objekt existiert nicht.\n",
						"UseMagicItem: Passed object does not exist.\n"));
		throw ERROR;
	}

	if(!Dest.exists()){
		error(Translate("UseMagicItem: Übergebenes Ziel existiert nicht (Objekt %d).\n",
						"UseMagicItem: Passed target does not exist (object %d).\n"),
				Obj.getObjectType().TypeID);
		throw ERROR;
	}

	if(CheckRight(Actor->ID, NO_RUNES)){
		throw NOTUSABLE;
	}

	int SpellNr = FindSpell(Obj);
	if(SpellNr == 0){
		error(Translate("UseMagicItem: Für Objekt %d existiert kein Spruch.\n",
						"UseMagicItem: No spell exists for object %d.\n"),
				Obj.getObjectType().TypeID);
		throw ERROR;
	}


	// NOTE(fusion): This target picking logic is probably to avoid picking
	// obviously wrong candidates when there are multiple creatures on a single
	// field.
	TCreature *Target = NULL;
	bool Aggressive = IsAggressiveSpell(SpellNr);
	{
		if(Dest.getObjectType().isCreatureContainer()){
			Target = GetCreature(Dest);
		}

		Object Other = GetFirstContainerObject(Dest.getContainer());
		while(Other != NONE){
			if(Other.getObjectType().isCreatureContainer()){
				uint32 OtherID = Other.getCreatureID();
				if(Target == NULL
						|| (Aggressive && OtherID != Actor->ID && OtherID != Target->ID)
						|| (!Aggressive && OtherID == Actor->ID && OtherID != Target->ID)){
					Target = GetCreature(OtherID);
					Dest = Other;
				}
			}
			Other = Other.getNextObject();
		}
	}

	// NOTE(fusion): Rune check.
	CheckRuneLevel(Actor, SpellNr);

	if(Actor->EarliestSpellTime > ServerMilliseconds){
		throw EXHAUSTED;
	}

	if(Aggressive){
		int DestX, DestY, DestZ;
		GetObjectCoordinates(Dest, &DestX, &DestY, &DestZ);
		if(!CheckRight(Actor->ID, ATTACK_EVERYWHERE)
				&& (IsProtectionZone(Actor->posx, Actor->posy, Actor->posz)
					|| IsProtectionZone(DestX, DestY, DestZ))){
			throw PROTECTIONZONE;
		}

		if(!Dest.getContainer().getObjectType().isMapContainer()){
			throw NOROOM;
		}
	}

	try{
		switch(SpellNr){
			case 4:{
				if(Target == NULL){
					throw NOCREATURE;
				}

				int Amount = ComputeDamage(Actor, SpellNr, 70, 30);
				Heal(Target, -1, 0, Amount); // -1 ?
				break;
			}

			case 5:{
				if(Target == NULL){
					throw NOCREATURE;
				}

				int Amount = ComputeDamage(Actor, SpellNr, 250, 30);
				Heal(Target, -1, 0, Amount); // -1 ?
				break;
			}

			case 7:{
				int Damage = ComputeDamage(Actor, SpellNr, 15, 5);
				Combat(Actor, Dest, 0, 0, Damage, EFFECT_FIRE_BURST,
						ANIMATION_FIRE, DAMAGE_ENERGY);
				break;
			}

			case 8:{
				int Damage = ComputeDamage(Actor, SpellNr, 30, 10);
				Combat(Actor, Dest, 0, 0, Damage, EFFECT_FIRE_BURST,
						ANIMATION_FIRE, DAMAGE_ENERGY);
				break;
			}

			case 12:{
				if(Target == NULL){
					throw NOCREATURE;
				}

				Convince(Actor, Target);
				break;
			}

			case 14:{
				ObjectIllusion(Actor, 0, 0, Dest, 200);
				break;
			}

			case 15:{
				int Damage = ComputeDamage(Actor, SpellNr, 20, 5);
				MassCombat(Actor, Dest, 0, 0, Damage, EFFECT_FIRE,
						3, DAMAGE_FIRE, ANIMATION_FIRE);
				break;
			}

			case 16:{
				int Damage = ComputeDamage(Actor, SpellNr, 50, 15);
				MassCombat(Actor, Dest, 0, 0, Damage, EFFECT_FIRE,
						4, DAMAGE_FIRE, ANIMATION_FIRE);
				break;
			}

			case 17:{
				MassCreateField(Actor, Dest, 0, 0, FIELD_TYPE_FIRE, 2);
				break;
			}

			case 18:{
				int Damage = ComputeDamage(Actor, SpellNr, 60, 40);
				MassCombat(Actor, Dest, 0, 0, Damage, EFFECT_FIRE_EXPLOSION,
						1, DAMAGE_PHYSICAL, ANIMATION_FIRE);
				break;
			}

			case 21:{
				int Damage = ComputeDamage(Actor, SpellNr, 150, 20);
				Combat(Actor, Dest, 0, 0, Damage, EFFECT_DEATH,
						ANIMATION_DEATH, DAMAGE_PHYSICAL);
				break;
			}

			case 25:{
				CreateField(Actor, Dest, 0, 0, FIELD_TYPE_FIRE);
				break;
			}

			case 26:{
				CreateField(Actor, Dest, 0, 0, FIELD_TYPE_POISON);
				break;
			}

			case 27:{
				CreateField(Actor, Dest, 0, 0, FIELD_TYPE_ENERGY);
				break;
			}

			case 28:{
				CreateFieldWall(Actor, Dest, 0, 0, FIELD_TYPE_FIRE, 2);
				break;
			}

			case 30:{
				DeleteField(Actor, Dest, 0, 0);
				break;
			}

			case 31:{
				if(Target == NULL){
					throw NOCREATURE;
				}

				NegatePoison(Actor, Target, 0, 0);
				break;
			}

			case 32:{
				CreateFieldWall(Actor, Dest, 0, 0, FIELD_TYPE_POISON, 2);
				break;
			}

			case 33:{
				CreateFieldWall(Actor, Dest, 0, 0, FIELD_TYPE_ENERGY, 3);
				break;
			}

			case 50:{
				int Damage = ComputeDamage(Actor, SpellNr, 120, 20);
				Combat(Actor, Dest, 0, 0, Damage, EFFECT_FIRE_HIT,
						ANIMATION_FIRE, DAMAGE_FIRE_PERIODIC);
				break;
			}

			case 54:{
				if(Target == NULL){
					throw NOCREATURE;
				}

				if(Actor->GetEffectiveProfession() != PROFESSION_DRUID){
					throw NOTUSABLE;
				}

				int ManaPoints = SpellList[SpellNr].Mana;
				int SoulPoints = SpellList[SpellNr].SoulPoints;
				MagicGoStrength(Actor, Target, ManaPoints, SoulPoints, -101, 1);
				break;
			}

			case 55:{
				MassCreateField(Actor, Dest, 0, 0, FIELD_TYPE_ENERGY, 2);
				break;
			}

			case 77:{
				int Damage = ComputeDamage(Actor, SpellNr, 70, 20);
				Combat(Actor, Dest, 0, 0, Damage, EFFECT_POISON_HIT,
						ANIMATION_ENERGY, DAMAGE_POISON_PERIODIC);
				break;
			}

			case 78:{
				CleanupField(Actor, Dest, 0, 0);
				break;
			}

			case 83:{
				RaiseDead(Actor, Dest, 0, 0);
				break;
			}

			case 86:{
				CreateField(Actor, Dest, 0, 0, FIELD_TYPE_MAGICWALL);
				break;
			}

			case 91:{
				MassCreateField(Actor, Dest, 0, 0, FIELD_TYPE_POISON, 2);
				break;
			}

			default:{
				error(Translate("UseMagicItem: Spell %d noch nicht implementiert.\n",
								"UseMagicItem: Spell %d not yet implemented.\n"), SpellNr);
				throw ERROR;
			}
		}
	}catch(RESULT r){
		if(r != ERROR){
			GraphicalEffect(Actor->posx, Actor->posy, Actor->posz, EFFECT_POFF);
		}

		if(Actor->Type == PLAYER){
			SendResult(Actor->Connection, r);
		}
		return;
	}

	DeleteRune(Obj);

	if(Aggressive){
		Actor->BlockLogout(60, false);
	}
}

void DrinkPotion(uint32 CreatureID, Object Obj){
	TPlayer *Player = GetPlayer(CreatureID);
	if(Player == NULL){
		error(Translate("DrinkPotion: Kreatur existiert nicht.\n",
						"DrinkPotion: Creature does not exist.\n"));
		throw ERROR;
	}

	if(!Obj.exists()){
		error(Translate("DrinkPotion: Übergebenes Objekt existiert nicht.\n",
						"DrinkPotion: Passed object does not exist.\n"));
		throw ERROR;
	}

	ObjectType ObjType = Obj.getObjectType();
	if(!ObjType.getFlag(LIQUIDCONTAINER)){
		error(Translate("DrinkPotion: Übergebenes Objekt ist kein Flüssigkeitscontainer.\n",
						"DrinkPotion: Passed object is not a liquid container.\n"));
		throw ERROR;
	}

	int LiquidType = (int)Obj.getAttribute(CONTAINERLIQUIDTYPE);
	if(LiquidType == LIQUID_MANA){
		int Amount = ComputeDamage(NULL, 0, 100, 50);
		RefreshMana(Player, 0, 0, Amount);
	}else if(LiquidType == LIQUID_LIFE){
		int Amount = ComputeDamage(NULL, 0, 50, 25);
		Heal(Player, 0, 0, Amount);
	}else{
		error(Translate("DrinkPotion: Objekt enthält keinen Zaubertrank.\n",
						"DrinkPotion: Object contains no potion.\n"));
		throw ERROR;
	}

	Change(Obj, CONTAINERLIQUIDTYPE, LIQUID_NONE);
}

// Magic Init Functions
// =============================================================================
static void InitCircles(void){
	char FileName[4096];
	snprintf(FileName, sizeof(FileName), "%s/circles.dat", DATAPATH);
	std::ifstream IN(FileName, std::ios_base::in);
	if(IN.fail()){
		error(Translate("InitCircles: Kann Datei %s nicht öffnen.\n",
						"InitCircles: Cannot open file %s.\n"), FileName);
		throw "Cannot open \"circles.dat\"";
	}

	// TODO(fusion): This doesn't make a lot of sense because we have `Width`
	// and `Height` but not `CenterX` and `CenterY`.
	int Width, Height, Center;
	IN >> Width >> Height >> Center;

	if(Width < 0 || Width > 21 || Height < 0 || Height > 21){
		error(Translate("InitCircles: Ungültiges Dateiformat.\n",
						"InitCircles: Invalid file format.\n"));
		throw "Cannot process \"circles.dat\"";
	}

	if(Center > 0){
		for(int Y = 0; Y < Height; Y += 1)
		for(int X = 0; X < Width; X += 1){
			int Radius;
			IN >> Radius;
			if(Radius < NARRAY(Circle)){
				int PointIndex = Circle[Radius].Count;
				ASSERT(PointIndex < 32);
				Circle[Radius].x[PointIndex] = X - Center;
				Circle[Radius].y[PointIndex] = Y - Center;
				Circle[Radius].Count += 1;
			}
		}
	}

	// TODO(fusion): Probably check if we parsed the file successfully?
	//if(IN.fail()) { throw "..."; }
}

static TSpellList *CreateSpell(int SpellNr, ...){
	ASSERT(SpellNr < NARRAY(SpellList));
	int SyllableCount = 0;
	TSpellList *Spell = &SpellList[SpellNr];

	va_list ap;
	va_start(ap, SpellNr);
	while(true){
		const char *Syllable = va_arg(ap, const char*);
		if(!Syllable || Syllable[0] == 0){
			break;
		}

		for(int SyllableNr = 0;
				SyllableNr < NARRAY(SpellSyllable);
				SyllableNr += 1){
			if(strcmp(SpellSyllable[SyllableNr], Syllable) == 0){
				// TODO(fusion): I'm not sure it is a good idea to throw an
				// exception between `va_start` and `va_end`.
				if(SyllableCount > NARRAY(Spell->Syllable)){
					error(Translate("CreateSpell: Silbenzahl überschritten bei Spell %d.\n",
									"CreateSpell: Syllable count exceeded for spell %d.\n"), SpellNr);
					throw "Spell has too many syllables";
				}

				Spell->Syllable[SyllableCount] = (uint8)SyllableNr;
				SyllableCount += 1;
				break;
			}
		}
	}
	va_end(ap);
	return Spell;
}

static void InitSpells(void){
	TSpellList *Spell;

	Spell = CreateSpell(1, "ex", "ura", "");
	Spell->Mana = 25;
	Spell->Level = 9;
	Spell->Flags = 8;
	Spell->Comment = "Light Healing";

	Spell = CreateSpell(2, "ex", "ura", "gran", "");
	Spell->Mana = 40;
	Spell->Level = 11;
	Spell->Flags = 8;
	Spell->Comment = "Intense Healing";

	Spell = CreateSpell(3, "ex", "ura", "vita", "");
	Spell->Mana = 160;
	Spell->Level = 20;
	Spell->Flags = 8;
	Spell->Comment = "Ultimate Healing";

	Spell = CreateSpell(4, "ad", "ura", "gran", "");
	Spell->Mana = 240;
	Spell->Level = 15;
	Spell->RuneGr = 79;
	Spell->RuneNr = 5;
	Spell->Flags = 8;
	Spell->Amount = 1;
	Spell->RuneLevel = 1;
	Spell->SoulPoints = 2;
	Spell->Comment = "Intense Healing Rune";

	Spell = CreateSpell(5, "ad", "ura", "vita", "");
	Spell->Mana = 400;
	Spell->Level = 24;
	Spell->RuneGr = 79;
	Spell->RuneNr = 13;
	Spell->Flags = 8;
	Spell->Amount = 1;
	Spell->RuneLevel = 4;
	Spell->SoulPoints = 3;
	Spell->Comment = "Ultimate Healing Rune";

	Spell = CreateSpell(6, "ut", "ani", "hur", "");
	Spell->Mana = 60;
	Spell->Level = 14;
	Spell->Flags = 2;
	Spell->Comment = "Haste";

	Spell = CreateSpell(7, "ad", "ori", "");
	Spell->Mana = 120;
	Spell->Level = 15;
	Spell->RuneGr = 79;
	Spell->RuneNr = 27;
	Spell->Flags = 9;
	Spell->Amount = 5;
	Spell->RuneLevel = 0;
	Spell->SoulPoints = 1;
	Spell->Comment = "Light Magic Missile";

	Spell = CreateSpell(8, "ad", "ori", "gran", "");
	Spell->Mana = 280;
	Spell->Level = 25;
	Spell->RuneGr = 79;
	Spell->RuneNr = 51;
	Spell->Flags = 9;
	Spell->Amount = 5;
	Spell->RuneLevel = 4;
	Spell->SoulPoints = 2;
	Spell->Comment = "Heavy Magic Missile";

	Spell = CreateSpell(9, "ut", "evo", "res", "para", "");
	Spell->Mana = 0;
	Spell->Level = 25;
	Spell->Flags = 1;
	Spell->Comment = "Summon Creature";

	Spell = CreateSpell(10, "ut", "evo", "lux", "");
	Spell->Mana = 20;
	Spell->Level = 8;
	Spell->Flags = 0;
	Spell->Comment = "Light";

	Spell = CreateSpell(11, "ut", "evo", "gran", "lux", "");
	Spell->Mana = 60;
	Spell->Level = 13;
	Spell->Flags = 0;
	Spell->Comment = "Great Light";

	Spell = CreateSpell(12, "ad", "eta", "sio", "");
	Spell->Mana = 200;
	Spell->Level = 16;
	Spell->RuneGr = 79;
	Spell->RuneNr = 30;
	Spell->Flags = 1;
	Spell->Amount = 1;
	Spell->RuneLevel = 5;
	Spell->SoulPoints = 3;
	Spell->Comment = "Convince Creature";

	Spell = CreateSpell(13, "ex", "evo", "mort", "hur", "");
	Spell->Mana = 250;
	Spell->Level = 38;
	Spell->Flags = 1;
	Spell->Comment = "Energy Wave";

	Spell = CreateSpell(14, "ad", "evo", "ina", "");
	Spell->Mana = 600;
	Spell->Level = 27;
	Spell->RuneGr = 79;
	Spell->RuneNr = 31;
	Spell->Flags = 0;
	Spell->Amount = 1;
	Spell->RuneLevel = 4;
	Spell->SoulPoints = 2;
	Spell->Comment = "Chameleon";

	Spell = CreateSpell(15, "ad", "ori", "flam", "");
	Spell->Mana = 160;
	Spell->Level = 17;
	Spell->RuneGr = 79;
	Spell->RuneNr = 42;
	Spell->Flags = 9;
	Spell->Amount = 2;
	Spell->RuneLevel = 2;
	Spell->SoulPoints = 2;
	Spell->Comment = "Fireball";

	Spell = CreateSpell(16, "ad", "ori", "gran", "flam", "");
	Spell->Mana = 480;
	Spell->Level = 23;
	Spell->RuneGr = 79;
	Spell->RuneNr = 44;
	Spell->Flags = 9;
	Spell->Amount = 2;
	Spell->RuneLevel = 4;
	Spell->SoulPoints = 3;
	Spell->Comment = "Great Fireball";

	Spell = CreateSpell(17, "ad", "evo", "mas", "flam", "");
	Spell->Mana = 600;
	Spell->Level = 27;
	Spell->RuneGr = 79;
	Spell->RuneNr = 45;
	Spell->Flags = 1;
	Spell->Amount = 2;
	Spell->RuneLevel = 5;
	Spell->SoulPoints = 4;
	Spell->Comment = "Firebomb";

	Spell = CreateSpell(18, "ad", "evo", "mas", "hur", "");
	Spell->Mana = 720;
	Spell->Level = 31;
	Spell->RuneGr = 79;
	Spell->RuneNr = 53;
	Spell->Flags = 1;
	Spell->Amount = 3;
	Spell->RuneLevel = 6;
	Spell->SoulPoints = 4;
	Spell->Comment = "Explosion";

	Spell = CreateSpell(19, "ex", "evo", "flam", "hur", "");
	Spell->Mana = 80;
	Spell->Level = 18;
	Spell->Flags = 9;
	Spell->Comment = "Fire Wave";

	Spell = CreateSpell(20, "ex", "iva", "para", "");
	Spell->Mana = 20;
	Spell->Level = 8;
	Spell->Flags = 0;
	Spell->Comment = "Find Person";

	Spell = CreateSpell(21, "ad", "ori", "vita", "vis", "");
	Spell->Mana = 880;
	Spell->Level = 45;
	Spell->RuneGr = 79;
	Spell->RuneNr = 8;
	Spell->Flags = 1;
	Spell->Amount = 1;
	Spell->RuneLevel = 15;
	Spell->SoulPoints = 5;
	Spell->Comment = "Sudden Death";

	Spell = CreateSpell(22, "ex", "evo", "vis", "lux", "");
	Spell->Mana = 100;
	Spell->Level = 23;
	Spell->Flags = 1;
	Spell->Comment = "Energy Beam";

	Spell = CreateSpell(23, "ex", "evo", "gran", "vis", "lux", "");
	Spell->Mana = 200;
	Spell->Level = 29;
	Spell->Flags = 1;
	Spell->Comment = "Great Energy Beam";

	Spell = CreateSpell(24, "ex", "evo", "gran", "mas", "vis", "");
	Spell->Mana = 1200;
	Spell->Level = 60;
	Spell->Flags = 3;
	Spell->Comment = "Ultimate Explosion";

	Spell = CreateSpell(25, "ad", "evo", "grav", "flam", "");
	Spell->Mana = 240;
	Spell->Level = 15;
	Spell->RuneGr = 79;
	Spell->RuneNr = 41;
	Spell->Flags = 1;
	Spell->Amount = 3;
	Spell->RuneLevel = 1;
	Spell->SoulPoints = 1;
	Spell->Comment = "Fire Field";

	Spell = CreateSpell(26, "ad", "evo", "grav", "pox", "");
	Spell->Mana = 200;
	Spell->Level = 14;
	Spell->RuneGr = 79;
	Spell->RuneNr = 25;
	Spell->Flags = 1;
	Spell->Amount = 3;
	Spell->RuneLevel = 0;
	Spell->SoulPoints = 1;
	Spell->Comment = "Poison Field";

	Spell = CreateSpell(27, "ad", "evo", "grav", "vis", "");
	Spell->Mana = 320;
	Spell->Level = 18;
	Spell->RuneGr = 79;
	Spell->RuneNr = 17;
	Spell->Flags = 1;
	Spell->Amount = 3;
	Spell->RuneLevel = 3;
	Spell->SoulPoints = 2;
	Spell->Comment = "Energy Field";

	Spell = CreateSpell(28, "ad", "evo", "mas", "grav", "flam", "");
	Spell->Mana = 780;
	Spell->Level = 33;
	Spell->RuneGr = 79;
	Spell->RuneNr = 43;
	Spell->Flags = 1;
	Spell->Amount = 4;
	Spell->RuneLevel = 6;
	Spell->SoulPoints = 4;
	Spell->Comment = "Fire Wall";

	Spell = CreateSpell(29, "ex", "ana", "pox", "");
	Spell->Mana = 30;
	Spell->Level = 10;
	Spell->Flags = 0;
	Spell->Comment = "Antidote";

	Spell = CreateSpell(30, "ad", "ito", "grav", "");
	Spell->Mana = 120;
	Spell->Level = 17;
	Spell->RuneGr = 79;
	Spell->RuneNr = 1;
	Spell->Flags = 0;
	Spell->Amount = 3;
	Spell->RuneLevel = 3;
	Spell->SoulPoints = 2;
	Spell->Comment = "Destroy Field";

	Spell = CreateSpell(31, "ad", "ana", "pox", "");
	Spell->Mana = 200;
	Spell->Level = 15;
	Spell->RuneGr = 79;
	Spell->RuneNr = 6;
	Spell->Flags = 0;
	Spell->Amount = 1;
	Spell->RuneLevel = 0;
	Spell->SoulPoints = 1;
	Spell->Comment = "Antidote Rune";

	Spell = CreateSpell(32, "ad", "evo", "mas", "grav", "pox", "");
	Spell->Mana = 640;
	Spell->Level = 29;
	Spell->RuneGr = 79;
	Spell->RuneNr = 29;
	Spell->Flags = 1;
	Spell->Amount = 4;
	Spell->RuneLevel = 5;
	Spell->SoulPoints = 3;
	Spell->Comment = "Poison Wall";

	Spell = CreateSpell(33, "ad", "evo", "mas", "grav", "vis", "");
	Spell->Mana = 1000;
	Spell->Level = 41;
	Spell->RuneGr = 79;
	Spell->RuneNr = 19;
	Spell->Flags = 1;
	Spell->Amount = 4;
	Spell->RuneLevel = 9;
	Spell->SoulPoints = 5;
	Spell->Comment = "Energy Wall";

	Spell = CreateSpell(34, "al", "evo", "para", "para", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Get Item";

	Spell = CreateSpell(35, "al", "evo", "para", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Get Item";

	Spell = CreateSpell(37, "al", "ani", "para", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Move";

	Spell = CreateSpell(38, "ut", "evo", "res", "ina", "para", "");
	Spell->Mana = 100;
	Spell->Level = 23;
	Spell->Flags = 0;
	Spell->Comment = "Creature Illusion";

	Spell = CreateSpell(39, "ut", "ani", "gran", "hur", "");
	Spell->Mana = 100;
	Spell->Level = 20;
	Spell->Flags = 2;
	Spell->Comment = "Strong Haste";

	Spell = CreateSpell(40, "al", "evo", "cogni", "para", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Get Experience";

	Spell = CreateSpell(41, "al", "eta", "para", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Change Data";

	Spell = CreateSpell(42, "ex", "evo", "pan", "");
	Spell->Mana = 120;
	Spell->Level = 14;
	Spell->SoulPoints = 1;
	Spell->Flags = 0;
	Spell->Comment = "Food";

	Spell = CreateSpell(44, "ut", "amo", "vita", "");
	Spell->Mana = 50;
	Spell->Level = 14;
	Spell->Flags = 0;
	Spell->Comment = "Magic Shield";

	Spell = CreateSpell(45, "ut", "ana", "vid", "");
	Spell->Mana = 440;
	Spell->Level = 35;
	Spell->Flags = 0;
	Spell->Comment = "Invisible";

	Spell = CreateSpell(46, "al", "evo", "cogni", "para", "para", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Get Skill Experience";

	Spell = CreateSpell(47, "al", "ani", "sio", "para", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Teleport to Friend";

	Spell = CreateSpell(48, "ex", "evo", "con", "pox", "");
	Spell->Mana = 130;
	Spell->Level = 16;
	Spell->SoulPoints = 2;
	Spell->Flags = 0;
	Spell->Comment = "Poisoned Arrow";

	Spell = CreateSpell(49, "ex", "evo", "con", "flam", "");
	Spell->Mana = 290;
	Spell->Level = 25;
	Spell->SoulPoints = 3;
	Spell->Flags = 0;
	Spell->Comment = "Explosive Arrow";

	Spell = CreateSpell(50, "ad", "evo", "res", "flam", "");
	Spell->Mana = 600;
	Spell->Level = 27;
	Spell->RuneGr = 79;
	Spell->RuneNr = 48;
	Spell->Flags = 3;
	Spell->Amount = 2;
	Spell->RuneLevel = 7;
	Spell->SoulPoints = 3;
	Spell->Comment = "Soulfire";

	Spell = CreateSpell(51, "ex", "evo", "con", "");
	Spell->Mana = 100;
	Spell->Level = 13;
	Spell->SoulPoints = 1;
	Spell->Flags = 0;
	Spell->Comment = "Conjure Arrow";

	Spell = CreateSpell(52, "al", "liber", "sio", "para", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Retrieve Friend";

	Spell = CreateSpell(53, "al", "evo", "res", "para", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Summon Wild Creature";

	Spell = CreateSpell(54, "ad", "ana", "ani", "");
	Spell->Mana = 1400;
	Spell->Level = 54;
	Spell->RuneGr = 79;
	Spell->RuneNr = 18;
	Spell->Flags = 3;
	Spell->Amount = 1;
	Spell->RuneLevel = 18;
	Spell->SoulPoints = 3;
	Spell->Comment = "Paralyze";

	Spell = CreateSpell(55, "ad", "evo", "mas", "vis", "");
	Spell->Mana = 880;
	Spell->Level = 37;
	Spell->RuneGr = 79;
	Spell->RuneNr = 2;
	Spell->Flags = 3;
	Spell->Amount = 2;
	Spell->RuneLevel = 10;
	Spell->SoulPoints = 5;
	Spell->Comment = "Energybomb";

	Spell = CreateSpell(56, "ex", "evo", "gran", "mas", "pox", "");
	Spell->Mana = 600;
	Spell->Level = 50;
	Spell->Flags = 3;
	Spell->Comment = "Poison Storm";

	Spell = CreateSpell(57, "om", "ana", "liber", "para", "para", "para", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Banish Account";

	Spell = CreateSpell(58, "al", "iva", "tera", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Get Position";

	Spell = CreateSpell(60, "om", "ani", "para", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Temple Teleport";

	Spell = CreateSpell(61, "om", "ana", "gran", "liber", "para", "para", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Delete Account";

	Spell = CreateSpell(62, "om", "amo", "para", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Set Namerule";

	Spell = CreateSpell(63, "al", "evo", "vis", "para", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Create Gold";

	Spell = CreateSpell(64, "al", "eta", "vita", "para", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Change Profession or Sex";

	Spell = CreateSpell(65, "om", "isa", "para", "para", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Entry in Criminal Record";

	Spell = CreateSpell(66, "om", "ana", "hora", "para", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Namelock";

	Spell = CreateSpell(67, "om", "ana", "para", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Kick Player";

	Spell = CreateSpell(68, "om", "ana", "gran", "res", "para", "para", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Delete Character";

	Spell = CreateSpell(69, "om", "ana", "vis", "para", "para", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Banish IP Address";

	Spell = CreateSpell(70, "om", "ana", "res", "para", "para", "para", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Banish Character";

	Spell = CreateSpell(71, "al", "eta", "sio", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Invite Guests";

	Spell = CreateSpell(72, "al", "eta", "som", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Invite Subowners";

	Spell = CreateSpell(73, "al", "ana", "sio", "para", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Kick Guest";

	Spell = CreateSpell(74, "al", "eta", "grav", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Edit Door";

	Spell = CreateSpell(75, "ut", "evo", "vis", "lux", "");
	Spell->Mana = 140;
	Spell->Level = 26;
	Spell->Flags = 2;
	Spell->Comment = "Ultimate Light";

	Spell = CreateSpell(76, "ex", "ani", "tera", "");
	Spell->Mana = 20;
	Spell->Level = 9;
	Spell->Flags = 2;
	Spell->Comment = "Magic Rope";

	Spell = CreateSpell(77, "ad", "evo", "res", "pox", "");
	Spell->Mana = 400;
	Spell->Level = 21;
	Spell->RuneGr = 79;
	Spell->RuneNr = 32;
	Spell->Flags = 3;
	Spell->Amount = 1;
	Spell->RuneLevel = 4;
	Spell->SoulPoints = 2;
	Spell->Comment = "Envenom";

	Spell = CreateSpell(78, "ad", "ito", "tera", "");
	Spell->Mana = 200;
	Spell->Level = 21;
	Spell->RuneGr = 79;
	Spell->RuneNr = 50;
	Spell->Amount = 3;
	Spell->RuneLevel = 4;
	Spell->SoulPoints = 3;
	Spell->Flags = 3;
	Spell->Comment = "Desintegrate";

	Spell = CreateSpell(79, "ex", "evo", "con", "mort", "");
	Spell->Mana = 140;
	Spell->Level = 17;
	Spell->SoulPoints = 2;
	Spell->Flags = 2;
	Spell->Comment = "Conjure Bolt";

	Spell = CreateSpell(80, "ex", "ori", "");
	Spell->Mana = 0;
	Spell->Level = 35;
	Spell->Flags = 7;
	Spell->Comment = "Berserk";

	Spell = CreateSpell(81, "ex", "ani", "hur", "para", "");
	Spell->Mana = 50;
	Spell->Level = 12;
	Spell->Flags = 2;
	Spell->Comment = "Levitate";

	Spell = CreateSpell(82, "ex", "ura", "gran", "mas", "res", "");
	Spell->Mana = 150;
	Spell->Level = 36;
	Spell->Flags = 10;
	Spell->Comment = "Mass Healing";

	Spell = CreateSpell(83, "ad", "ana", "mort", "");
	Spell->Mana = 600;
	Spell->Level = 27;
	Spell->RuneGr = 79;
	Spell->RuneNr = 56;
	Spell->Amount = 1;
	Spell->RuneLevel = 4;
	Spell->SoulPoints = 5;
	Spell->Flags = 3;
	Spell->Comment = "Animate Dead";

	Spell = CreateSpell(84, "ex", "ura", "sio", "para", "");
	Spell->Mana = 70;
	Spell->Level = 18;
	Spell->Flags = 10;
	Spell->Comment = "Heal Friend";

	Spell = CreateSpell(85, "ex", "ana", "mas", "mort", "");
	Spell->Mana = 500;
	Spell->Level = 30;
	Spell->Flags = 3;
	Spell->Comment = "Undead Legion";

	Spell = CreateSpell(86, "ad", "evo", "grav", "tera", "");
	Spell->Mana = 750;
	Spell->Level = 32;
	Spell->RuneGr = 79;
	Spell->RuneNr = 33;
	Spell->Amount = 3;
	Spell->RuneLevel = 9;
	Spell->SoulPoints = 5;
	Spell->Flags = 3;
	Spell->Comment = "Magic Wall";

	Spell = CreateSpell(87, "ex", "ori", "mort", "");
	Spell->Mana = 20;
	Spell->Level = 11;
	Spell->Flags = 3;
	Spell->Comment = "Force Strike";

	Spell = CreateSpell(88, "ex", "ori", "vis", "");
	Spell->Mana = 20;
	Spell->Level = 12;
	Spell->Flags = 3;
	Spell->Comment = "Energy Strike";

	Spell = CreateSpell(89, "ex", "ori", "flam", "");
	Spell->Mana = 20;
	Spell->Level = 12;
	Spell->Flags = 3;
	Spell->Comment = "Flame Strike";

	Spell = CreateSpell(90, "ex", "ana", "ina", "");
	Spell->Mana = 200;
	Spell->Level = 26;
	Spell->Flags = 2;
	Spell->Comment = "Cancel Invisibility";

	Spell = CreateSpell(91, "ad", "evo", "mas", "pox", "");
	Spell->Mana = 520;
	Spell->Level = 25;
	Spell->RuneGr = 79;
	Spell->RuneNr = 26;
	Spell->Flags = 3;
	Spell->Amount = 2;
	Spell->RuneLevel = 4;
	Spell->SoulPoints = 2;
	Spell->Comment = "Poisonbomb";

	Spell = CreateSpell(92, "ex", "eta", "vis", "");
	Spell->Mana = 80;
	Spell->Level = 41;
	Spell->Flags = 2;
	Spell->Comment = "Enchant Staff";

	Spell = CreateSpell(93, "ex", "eta", "res", "");
	Spell->Mana = 30;
	Spell->Level = 20;
	Spell->Flags = 3;
	Spell->Comment = "Challenge";

	Spell = CreateSpell(94, "ex", "evo", "grav", "vita", "");
	Spell->Mana = 220;
	Spell->Level = 27;
	Spell->Flags = 3;
	Spell->Comment = "Wild Growth";

	Spell = CreateSpell(95, "ex", "evo", "con", "vis", "");
	Spell->Mana = 800;
	Spell->Level = 59;
	Spell->SoulPoints = 3;
	Spell->Flags = 2;
	Spell->Comment = "Power Bolt";

	Spell = CreateSpell(96, "al", "iva", "cogni", "para", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Get Quest Value";

	Spell = CreateSpell(97, "al", "eta", "cogni", "para", "para", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Set Quest Value";

	Spell = CreateSpell(98, "al", "ito", "tera", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Desintegrate Spell";

	Spell = CreateSpell(99, "al", "ani", "hur", "para", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Levitate Gamemaster";

	Spell = CreateSpell(100, "al", "ana", "cogni", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Clear Quest Values";

	Spell = CreateSpell(101, "al", "ito", "mas", "res", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Kill All Creatures";

	Spell = CreateSpell(102, "al", "evo", "mas", "res", "para", "");
	Spell->Mana = 0;
	Spell->Level = 0;
	Spell->Flags = 0;
	Spell->Comment = "Start Monsterraid";
}

void InitMagic(void){
	InitCircles();
	InitSpells();
	InitLog("banish");
}

void ExitMagic(void){
	// no-op
}
