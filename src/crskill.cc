#include "cr.hh"
#include "operate.hh"

#include <math.h>

// TSkill REGULAR FUNCTIONS
//==============================================================================
TSkill::TSkill(int SkNr, TCreature *Master){
	// TODO(fusion): I'm not sure we're calling `Reset` here but the decompiled
	// function sets the same values as `TSkill::Reset` which makes me wonder
	// whether it is inlined. It's probably using `TSkill::Reset()` directly
	// instead of virtual dispatch, if that is the case.
	this->Reset();

	this->SkNr = (uint16)SkNr;
	this->Master = Master;
}

int TSkill::Get(void){
	int Value = this->Act;
	if(Value < this->Min)
		Value = this->Min;
	Value += this->MDAct + this->DAct;
	return Value;
}

int TSkill::GetProgress(void){
	int Result = 0;
	if(this->NextLevel > this->LastLevel){
		// NOTE(fusion): Use 64-bit integers to avoid overflows.
		int64 Result64 = (int64)(this->Exp - this->LastLevel) * 100
							/ (int64)(this->NextLevel - this->LastLevel);

		Result = (int)Result64;

		// TODO(fusion): This feels too much for reporting a mostly *impossible* error.
		if(Result < 0 || Result > 100){
			error(Translate("TSkill::GetProgress: Berechnungsfehler Exp %d, Last %d, Next %d, Prozent %d.\n",
							"TSkill::GetProgress: Calculation error Exp %d, Last %d, Next %d, Percent %d.\n"),
					this->Exp, this->LastLevel, this->NextLevel, Result);

			const char *MasterName = "(Unknown";
			if(this->Master != NULL){
				MasterName = this->Master->Name;
			}
			error(Translate("# Spieler %s - Skill %d\n",
							"# Player %s - Skill %d\n"), MasterName, this->SkNr);
			Result = 0;
		}
	}
	return Result;
}

void TSkill::Check(void){
	if(this->Act > this->Max){
		this->Act = this->Max;
	}
}

void TSkill::Change(int Amount){
	this->Set(this->Act + Amount);

	// TODO(fusion): Probably `TSkill::Check` inlined? It doesn't make a whole
	// lot of sense because `TSkill::Set` also checks the value. Maybe a custom
	// version of it does something differently?
	if(this->Act > this->Max){
		this->Act = this->Max;
	}
}

void TSkill::SetMax(void){
	this->Set(this->Max);
}

void TSkill::DecreasePercent(int Percent){
	// NOTE(fusion): Use 64-bit integers to avoid overflows.
	int64 Amount64 = ((int64)this->Exp * (int64)Percent) / 100;
	this->Decrease((int)Amount64);
}

void TSkill::SetMDAct(int MDAct){
	this->MDAct = MDAct;
	if(this->SkNr == SKILL_GO_STRENGTH && this->Master && this->Master->Type == PLAYER){
		// TODO(fusion): Same as `TSkill::Process`.
		((TPlayer*)this->Master)->CheckState();
	}
}

void TSkill::Load(int Act, int Max, int Min, int DAct, int MDAct,
		int Cycle, int MaxCycle, int Count, int MaxCount, int AddLevel,
		int Exp, int FactorPercent, int NextLevel, int Delta){
	this->Act = Act;
	this->Max = Max;
	this->Min = Min;
	this->DAct = DAct;
	this->MDAct = MDAct;
	this->Cycle = Cycle;
	this->MaxCycle = MaxCycle;
	this->Count = Count;
	this->MaxCount = MaxCount;
	this->AddLevel = AddLevel;
	this->Exp = Exp;
	this->FactorPercent = FactorPercent;
	this->NextLevel = NextLevel;
	this->Delta = Delta;

	// TODO(fusion): Shouldn't we also do the same for `NextLevel`?
	this->LastLevel = this->GetExpForLevel(Act);

	TCreature *Master = this->Master;
	if(Master && Cycle != 0){
		// NOTE(fusion): It seems we had `TSkillBase::SetTimer` inlined here.
		// For whatever reason I hadn't noticed the error message referencing
		// it, LOL.
		Master->SetTimer(this->SkNr, Cycle, Count, MaxCount, FactorPercent);
	}
}


void TSkill::Save(int *Act, int *Max, int *Min, int *DAct, int *MDAct,
		int *Cycle, int *MaxCycle, int *Count, int *MaxCount, int *AddLevel,
		int *Exp, int *FactorPercent, int *NextLevel, int *Delta){
	*Act = this->Act;
	*Max = this->Max;
	*Min = this->Min;
	*DAct = this->DAct;
	*MDAct = this->MDAct;
	*Cycle = this->Cycle;
	*MaxCycle = this->MaxCycle;
	*Count = this->Count;
	*MaxCount = this->MaxCount;
	*AddLevel = this->AddLevel;
	*Exp = this->Exp;
	*FactorPercent = this->FactorPercent;
	*NextLevel = this->NextLevel;
	*Delta = this->Delta;
}

// TSkill VIRTUAL FUNCTIONS
//==============================================================================
TSkill::~TSkill(void){
	if(this->Master != NULL){
		this->Master->DelTimer(this->SkNr);
	}
}

void TSkill::Set(int Value){
	if(Value > this->Max)
		Value = this->Max;
	this->Act = Value;
}

void TSkill::Increase(int Amount){
	// no-op
}

void TSkill::Decrease(int Amount){
	// no-op
}

int TSkill::GetExpForLevel(int Level){
	return 0;
}

void TSkill::Advance(int Range){
	// no-op
}

void TSkill::ChangeSkill(int FactorPercent, int Delta){
	// no-op
}

int TSkill::ProbeValue(int Max, bool Increase){
	return 0;
}

bool TSkill::Probe(int Diff, int Prob, bool Increase){
	return false;
}

bool TSkill::Process(void){
	bool Result = this->Cycle == 0;
	if(Result){
		if(this->Master && this->Master->Type == PLAYER){
			// TODO(fusion): Verify if a simple pointer cast works. Maybe `dynamic_cast`?
			((TPlayer*)this->Master)->CheckState();
		}
	}else if(this->Count <= 0){
		this->Count = this->MaxCount;
		// NOTE(fusion): `Range` should move `Cycle` towards ZERO.
		int Range = (this->Cycle < 0) ? +1 : -1;
		this->Cycle += Range;
		this->Event(Range);
	}else{
		this->Count -= 1;
	}
	return Result;
}

bool TSkill::SetTimer(int Cycle, int Count, int MaxCount, int AdditionalValue){
	this->Cycle = Cycle;
	this->Count = Count;
	this->MaxCount = MaxCount;
	if(this->Master && this->Master->Type == PLAYER){
		// TODO(fusion): Same as `TSkill::Process`.
		((TPlayer*)this->Master)->CheckState();
	}
	return true;
}

bool TSkill::DelTimer(void){
	this->Cycle = 0;
	this->Count = 0;
	this->MaxCount = 0;
	if(this->Master && this->Master->Type == PLAYER){
		// TODO(fusion): Same as `TSkill::Process`.
		((TPlayer*)this->Master)->CheckState();
	}
	return true;
}

int TSkill::TimerValue(void){
	return this->Cycle;
}

bool TSkill::Jump(int Range){
	return true;
}

void TSkill::Event(int Range){
	// no-op
}

void TSkill::Reset(void){
	this->Act = 0;
	this->Max = INT_MAX;
	this->Min = 0;
	this->DAct = 0;
	this->MDAct = 0;
	this->FactorPercent = 1000;
	this->LastLevel = 0;
	this->NextLevel = INT_MAX;
	this->Exp = 0;
	this->Delta = INT_MAX;
	this->Cycle = 0;
	this->MaxCycle = 0;
	this->Count = 0;
	this->MaxCount = 0;
	this->AddLevel = 0;
}

// TSkillLevel
//==============================================================================
void TSkillLevel::Increase(int Amount){
	if(Amount < 0){
		error("TSkillLevel::Increase: Amount negativ (%d).\n", Amount);
		return;
	}

	// BUG(fusion): No bounds check as in `TSkillLevel::Decrease`?

	// TODO(fusion): This could probably be an oversight but the decompiled
	// function was calling `GetExpForLevel` twice instead of using `NextLevel`
	// which makes me wonder whether `NextLevel` is properly initialized.
	int Range = 0;
	this->Exp += Amount;
	while(this->Exp >= this->NextLevel){
		this->Act += 1;
		this->LastLevel = this->NextLevel;
		this->NextLevel = this->GetExpForLevel(this->Act + 1);
		if(this->NextLevel < 0){
			// BUG(fusion): We don't check if `Master` is valid here?
			error(Translate("TSkillLevel::Increase: Skill vor Überlauf (%s, Skill %d).\n",
							"TSkillLevel::Increase: Skill before overflow (%s, Skill %d).\n"), this->Master->Name, this->SkNr);
			this->NextLevel = this->Exp;
			this->Exp -= 1000000;
			break;
		}
		Range += 1;
	}

	if(Range != 0){
		this->Jump(Range);
	}

	if(this->Master == NULL){
		error(Translate("TSkillLevel::Increase: GetMaster liefert NULL zurueck.\n",
						"TSkillLevel::Increase: GetMaster returns NULL.\n"));
		return;
	}

	if(this->Master->Type == PLAYER){
		SendPlayerData(this->Master->Connection);
	}
}

void TSkillLevel::Decrease(int Amount){
	if(Amount < 0){
		error("TSkillLevel::Decrease: Amount negativ (%d).\n", Amount);
		return;
	}

	// TODO(fusion): This is some weird ass comparison.
	if(Amount > this->Exp && this->Exp > 100000){
		error(Translate("TSkillLevel::Decrease: Amount zu gross(%d).\n",
						"TSkillLevel::Decrease: Amount too large(%d).\n"), Amount);
		return;
	}

	if(Amount < this->Exp){
		this->Exp -= Amount;
	}else{
		this->Exp = 0;
	}

	// TODO(fusion): This could probably be an oversight but the decompiled
	// function was calling `GetExpForLevel` twice instead of using `LastLevel`
	// which makes me wonder whether `LastLevel` is properly initialized.
	int Range = 0;
	while(this->Exp < this->LastLevel){
		this->Act -= 1;
		this->NextLevel = this->LastLevel;
		this->LastLevel = this->GetExpForLevel(this->Act);
		Range -= 1;
	}

	if(Range != 0){
		this->Jump(Range);
	}

	if(this->Master == NULL){
		error(Translate("TSkillLevel::Decrease: GetMaster liefert NULL zurueck.\n",
						"TSkillLevel::Decrease: GetMaster returns NULL.\n"));
		return;
	}

	if(this->Master->Type == PLAYER){
		SendPlayerData(this->Master->Connection);
	}
}

int TSkillLevel::GetExpForLevel(int Level){
	if(Level < 1){
		error(Translate("TSkillLevel::GetExpForLevel: Ungültiger Level %d.\n",
						"TSkillLevel::GetExpForLevel: Invalid Level %d.\n"), Level);
		return 0;
	}

	if(this->Delta <= 0){
		error(Translate("TSkillLevel::GetExpForLevel: Ungültiger Delta-Wert %d.\n",
						"TSkillLevel::GetExpForLevel: Invalid Delta value %d.\n"), this->Delta);
		return 0;
	}

	if(Level > 500){
		error(Translate("TSkillLevel::GetExpForLevel: Level=%d; Formel gegen Überlauf sichern.\n",
						"TSkillLevel::GetExpForLevel: Level=%d; Protect formula against overflow.\n"), Level);
		return -1; // TODO(fusion): Shouldn't this be 0?
	}

	return ((((Level - 6) * Level + 17) * Level - 12) / 6) * this->Delta;
}

bool TSkillLevel::Jump(int Range){
	if(this->Master == NULL){
		error(Translate("TSkillLevel::Jump: GetMaster liefert NULL zurueck!\n",
						"TSkillLevel::Jump: GetMaster returns NULL!\n"));
		return false;
	}

	this->Master->Skills[SKILL_HITPOINTS     ]->Advance(Range);
	this->Master->Skills[SKILL_MANA          ]->Advance(Range);
	this->Master->Skills[SKILL_GO_STRENGTH   ]->Advance(Range);
	this->Master->Skills[SKILL_CARRY_STRENGTH]->Advance(Range);

	AnnounceChangedCreature(this->Master->ID, CREATURE_SPEED_CHANGED);
	this->Master->Combat.CheckCombatValues();

	if(this->Master->Type == PLAYER){
		int ToLevel = this->Get();
		int FromLevel = ToLevel - Range;
		if(Range > 0){
			SendMessage(this->Master->Connection, TALK_EVENT_MESSAGE,
					"You advanced from Level %d to Level %d.", FromLevel, ToLevel);
		}else if(Range < 0){
			SendMessage(this->Master->Connection, TALK_EVENT_MESSAGE,
					"You were downgraded from Level %d to Level %d.", FromLevel, ToLevel);
		}
	}

	return true;
}

// TSkillProbe
//==============================================================================
void TSkillProbe::Increase(int Amount){
	if(Amount < 0){
		error("TSkillProbe::Increase: Amount negativ (%d).\n", Amount);
		return;
	}

	int OldProgress = this->GetProgress();

	// TODO(fusion): This could probably be an oversight but the decompiled
	// function was calling `GetExpForLevel` twice instead of using `NextLevel`
	// which makes me wonder whether `NextLevel` is properly initialized.
	int Range = 0;
	this->Exp += Amount;
	while(this->Exp >= this->NextLevel){
		this->Act += 1;
		this->LastLevel = this->NextLevel;
		this->NextLevel = this->GetExpForLevel(this->Act + 1);
		if(this->NextLevel < 0){
			// BUG(fusion): We don't check if `Master` is valid here?
			error(Translate("TSkillProbe::Increase: Skill vor Überlauf (%s, Skill %d).\n",
							"TSkillProbe::Increase: Skill before overflow (%s, Skill %d).\n"), this->Master->Name, this->SkNr);
			this->NextLevel = this->Exp;
			this->Exp -= 1000;
			break;
		}
		Range += 1;
	}

	if(Range != 0){
		this->Jump(Range);
	}else{
		TCreature *Master = this->Master;
		if(Master && Master->Type == PLAYER){
			int NewProgress = this->GetProgress();
			if(NewProgress != OldProgress){
				if(this->SkNr == SKILL_MAGIC_LEVEL){
					SendPlayerData(Master->Connection);
				}else{
					SendPlayerSkills(Master->Connection);
				}
			}
		}
	}
}

void TSkillProbe::Decrease(int Amount){
	if(Amount < 0){
		error("TSkillProbe::Decrease: Amount negativ (%d).\n", Amount);
		return;
	}

	int OldProgress = this->GetProgress();

	if(Amount < this->Exp){
		this->Exp -= Amount;
	}else{
		this->Exp = 0;
	}

	// TODO(fusion): This could probably be an oversight but the decompiled
	// function was calling `GetExpForLevel` twice instead of using `LastLevel`
	// which makes me wonder whether `LastLevel` is properly initialized.
	int Range = 0;
	while(this->Exp < this->LastLevel && this->Act > this->Min){
		this->Act -= 1;
		this->NextLevel = this->LastLevel;
		this->LastLevel = this->GetExpForLevel(this->Act);
		Range -= 1;
	}

	if(Range != 0){
		this->Jump(Range);
	}else{
		TCreature *Master = this->Master;
		if(Master && Master->Type == PLAYER){
			int NewProgress = this->GetProgress();
			if(NewProgress != OldProgress){
				if(this->SkNr == SKILL_MAGIC_LEVEL){
					SendPlayerData(Master->Connection);
				}else{
					SendPlayerSkills(Master->Connection);
				}
			}
		}
	}
}

int TSkillProbe::GetExpForLevel(int Level){
	if(Level < 0 || Level < this->Min){
		error(Translate("TSkillProbe::GetExpForLevel: Ungültiger Level %d.\n",
						"TSkillProbe::GetExpForLevel: Invalid Level %d.\n"), Level);
		return 0;
	}

	if(this->Delta <= 0){
		error(Translate("TSkillProbe::GetExpForLevel: Ungültiger Delta-Wert %d.\n",
						"TSkillProbe::GetExpForLevel: Invalider Delta value %d.\n"), this->Delta);
		return 0;
	}

	int FactorPercent = this->FactorPercent;
	if(FactorPercent < 1050){
		if(FactorPercent != 1000){
			const char *MasterName = (this->Master != NULL ? this->Master->Name : "---");
			error(Translate("TSkillProbe::GetExpForLevel: Ungültiger FactorPercent-Wert %d bei %s. Rechne mit 1000 weiter.\n",
							"TSkillProbe::GetExpForLevel: Invalid FactorPercent value %d at %s. Continue calculating with 1000.\n"), FactorPercent, MasterName);
		}
		return (Level - this->Min) * this->Delta;
	}

	double Base = (double)FactorPercent / 1000.0;
	double Power = pow(Base, (double)(Level - this->Min));
	double Result = (double)this->Delta * ((Power - 1.0) / (Base - 1.0));
	return (int)Result;
}

void TSkillProbe::ChangeSkill(int FactorPercent, int Delta){
	double Progress = 0.0;
	if(this->LastLevel < this->NextLevel){
		Progress = (double)(this->Exp - this->LastLevel)
				/ (double)(this->NextLevel - this->LastLevel);
	}

	double Base = (double)FactorPercent / 1000.0;

	// NOTE(fusion): Similar to `this->GetExpForLevel(this->Act)` but inlined?
	double LastLevelPower = pow(Base, (double)(this->Act - this->Min));
	double LastLevel = (double)Delta * ((LastLevelPower - 1.0) / (Base - 1.0));

	// NOTE(fusion): Similar to `this->GetExpForLevel(this->Act + 1)` but inlined?
	double NextLevelPower = pow(Base, (double)(this->Act - this->Min + 1));
	double NextLevel = (double)Delta * ((NextLevelPower - 1.0) / (Base - 1.0));

	// NOTE(fusion): Renormalize experience.
	double Exp = LastLevel + Progress * (NextLevel - LastLevel);

	this->FactorPercent = FactorPercent;
	this->LastLevel = (int)LastLevel;
	this->NextLevel = (int)NextLevel;
	this->Delta = Delta;
	this->Exp = (int)Exp;

	TCreature *Master = this->Master;
	if(Master != NULL && Master->Type == PLAYER){
		if(this->SkNr == SKILL_MAGIC_LEVEL){
			SendPlayerData(Master->Connection);
		}else{
			SendPlayerSkills(Master->Connection);
		}
	}
}

int TSkillProbe::ProbeValue(int Max, bool Increase){
	if(Increase){
		this->Increase(1);
	}

	// TODO(fusion): Some optimizations made the decompilation output for this
	// `RandomFactor` look very weird. It looks correct but we should come back
	// to it.
	int RandomFactor = ((rand() % 100) + (rand() % 100)) / 2;
	int MaxValue = Max * (this->Get() * 5 + 50);
	int Result = (RandomFactor * MaxValue) / 10000;
	return Result;
}

bool TSkillProbe::Probe(int Diff, int Prob, bool Increase){
	if(Increase){
		this->Increase(1);
	}

	// NOTE(fusion): `Diff` is a difficulty threshold and determines how challenging
	// an action is based on the current skill level `this->Act`. The higher the
	// skill level, the less difficult the action.
	//	I've only seen it used with SKILL_DISTANCE to determine whether a ranged
	// attack should hit.
	bool Result = true;
	if(Diff != 0){
		if(this->Act >= (rand() % Diff)){
			Result = (rand() % 100) <= Prob;
		}else{
			Result = false;
		}
	}
	return Result;
}

bool TSkillProbe::SetTimer(int Cycle, int Count, int MaxCount, int AdditionalValue){
	this->Cycle = Cycle;
	this->Count = Count;
	this->MaxCount = MaxCount;

	if(this->Master == NULL){
		error(Translate("TSkillProbe::SetTimer: GetMaster liefert NULL zurueck!\n",
						"TSkillProbe::SetTimer: GetMaster returns NULL!\n"));
		return false;
	}

	if(this->Master->Type == PLAYER){
		((TPlayer*)this->Master)->CheckState();
		SendPlayerData(this->Master->Connection);
	}

	return true;
}

bool TSkillProbe::Jump(int Range){
	TCreature *Master = this->Master;
	if(Master == NULL){
		error(Translate("TSkillProbe::Jump: GetMaster liefert NULL zurueck!\n",
						"TSkillProbe::Jump: GetMaster returns NULL!\n"));
		return false;
	}

	if(Master->Type == PLAYER){
		if(this->SkNr == SKILL_MAGIC_LEVEL){
			SendPlayerData(Master->Connection);
		}else{
			SendPlayerSkills(Master->Connection);
		}

		if(Range > 0){
			switch(this->SkNr){
				case SKILL_MAGIC_LEVEL:{
					SendMessage(Master->Connection, TALK_EVENT_MESSAGE,
							"You advanced to magic level %d.", this->Get());
					break;
				}
				case SKILL_SHIELDING:{
					SendMessage(Master->Connection, TALK_EVENT_MESSAGE, "You advanced in shielding.");
					break;
				}
				case SKILL_DISTANCE:{
					SendMessage(Master->Connection, TALK_EVENT_MESSAGE, "You advanced in distance fighting.");
					break;
				}
				case SKILL_SWORD:{
					SendMessage(Master->Connection, TALK_EVENT_MESSAGE, "You advanced in sword fighting.");
					break;
				}
				case SKILL_CLUB:{
					SendMessage(Master->Connection, TALK_EVENT_MESSAGE, "You advanced in club fighting.");
					break;
				}
				case SKILL_AXE:{
					SendMessage(Master->Connection, TALK_EVENT_MESSAGE, "You advanced in axe fighting.");
					break;
				}
				case SKILL_FIST:{
					SendMessage(Master->Connection, TALK_EVENT_MESSAGE, "You advanced in fist fighting.");
					break;
				}
				case SKILL_FISHING:{
					SendMessage(Master->Connection, TALK_EVENT_MESSAGE, "You advanced in fishing.");
					break;
				}
			}
		}
	}

	return true;
}

void TSkillProbe::Event(int Range){
	if(this->Cycle == 0){
		TCreature *Master = this->Master;
		if(Master == NULL){
			error(Translate("TSkillProbe::Event: GetMaster liefert NULL zurueck!\n",
							"TSkillProbe::Event: GetMaster returns NULL!\n"));
			return;
		}

		this->MDAct = 0;
		if(Master->Type == PLAYER){
			// TODO(fusion): This is weird because `SKILL_GO_STRENGTH` should
			// be TSkillGoStrength, instead of TSkillProbe.
			if(this->SkNr == SKILL_GO_STRENGTH){
				((TPlayer*)Master)->CheckState();
			}

			SendPlayerSkills(Master->Connection);
		}
	}
}

// TSkillAdd
//==============================================================================
void TSkillAdd::Advance(int Range){
	int Increment = Range * this->AddLevel;
	int Max = this->Max + Increment;
	int Act = this->Act + Increment;

	// TODO(fusion): I'm not sure this is right. Do we fill health and mana when
	// the player levels up?
	if(Act < Max){
		Act = Max;
	}

	this->Act = Act;
	this->Max = Max;
}

// TSkillHitpoints
//==============================================================================
void TSkillHitpoints::Set(int Value){
	TCreature *Master = this->Master;
	if(Master != NULL && Master->IsDead && Value > 0){
		error(Translate("TSkillHitpoints::Set: HP von toter Kreatur sollen erhöht werden.\n",
						"TSkillHitpoints::Set: HP of dead creature should be increased.\n"));
		return;
	}

	if(Value > this->Max){
		Value = this->Max;
	}
	this->Act = Value;

	if(Master != NULL){
		if(Master->Type == PLAYER){
			SendPlayerData(Master->Connection);
		}
		AnnounceChangedCreature(Master->ID, CREATURE_HEALTH_CHANGED);
	}
}

// TSkillMana
//==============================================================================
void TSkillMana::Set(int Value){
	if(Value > this->Max){
		Value = this->Max;
	}
	this->Act = Value;

	TCreature *Master = this->Master;
	if(Master != NULL && Master->Type == PLAYER){
		SendPlayerData(Master->Connection);
	}
}

// TSkillGoStrength
//==============================================================================
bool TSkillGoStrength::SetTimer(int Cycle, int Count, int MaxCount, int AdditionalValue){
	// TODO(fusion): The decompiled version of this function was kind of confusing
	// with `Master` being checked multiple times and `CheckState` being called up
	// to two times. I've done some cleanup that should keep the same behavior but
	// we should always keep an eye out for bugs.

	this->Cycle = Cycle;
	this->Count = Count;
	this->MaxCount = MaxCount;

	if(Cycle == 0){
		this->MDAct = 0;
	}

	TCreature *Master = this->Master;
	if(Master == NULL){
		error(Translate("TSkillGoStrength::SetTimer: GetMaster liefert NULL zurueck!\n",
						"TSkillGoStrength::SetTimer: GetMaster returns NULL!\n"));
		return false;
	}

	if(Master->Type == PLAYER){
		((TPlayer*)Master)->CheckState();
	}
	AnnounceChangedCreature(Master->ID, CREATURE_SPEED_CHANGED);
	return true;
}

void TSkillGoStrength::Event(int Range){
	if(this->Cycle != 0){
		return;
	}

	TCreature *Master = this->Master;
	if(Master == NULL){
		error(Translate("TSkillGoStrength::Event: GetMaster liefert NULL zurueck!\n",
						"TSkillGoStrength::Event: GetMaster returns NULL!\n"));
		return;
	}

	// TODO(fusion): Again, shouldn't `SkNr` always refer to `GO_STRENGTH` here?
	this->MDAct = 0;
	if(this->SkNr == SKILL_GO_STRENGTH && Master->Type == PLAYER){
		((TPlayer*)Master)->CheckState();
	}
	AnnounceChangedCreature(Master->ID, CREATURE_SPEED_CHANGED);
}

// TSkillCarryStrength
//==============================================================================
void TSkillCarryStrength::Set(int Value){
	if(Value > this->Max){
		Value = this->Max;
	}
	this->Act = Value;

	TCreature *Master = this->Master;
	if(Master != NULL && Master->Type == PLAYER){
		SendPlayerData(Master->Connection);
	}
}

// TSkillSoulpoints
//==============================================================================
void TSkillSoulpoints::Set(int Value){
	if(Value > this->Max){
		Value = this->Max;
	}
	this->Act = Value;

	TCreature *Master = this->Master;
	if(Master != NULL && Master->Type == PLAYER){
		SendPlayerData(Master->Connection);
	}
}

int TSkillSoulpoints::TimerValue(void){
	return (this->Cycle - 1) * this->MaxCount + this->Count;
}

void TSkillSoulpoints::Event(int Range){
	TCreature *Master = this->Master;
	if(Master == NULL){
		error(Translate("TSkillSoulpoints::Event: GetMaster liefert NULL zurueck!\n",
						"TSkillSoulpoints::Event: GetMaster returns NULL!\n"));
		return;
	}

	// TODO(fusion): Shouldn't this be the same as `Master->Skills[SKILL_SOUL]`?
	// Not sure what's going on here.
	if(!Master->IsDead){
		Master->Skills[SKILL_SOUL]->Change(1);
	}
}

// TSkillFed
//==============================================================================
void TSkillFed::Event(int Range){
	TCreature *Master = this->Master;
	if(Master == NULL){
		error(Translate("TSkillFed::Event: GetMaster liefert NULL zurueck!\n",
						"TSkillFed::Event: GetMaster returns NULL!\n"));
		return;
	}

	if(Master->IsDead || IsProtectionZone(Master->posx, Master->posy, Master->posz)){
		return;
	}

	uint8 Profession = 0;
	if(Master->Type == PLAYER){
		Profession = ((TPlayer*)Master)->GetActiveProfession();
	}

	int SecsPerHP = 12;
	int SecsPerMana = 6;
	switch(Profession){
		case PROFESSION_NONE:
		case PROFESSION_KNIGHT:{
			SecsPerHP = 6;
			SecsPerMana = 6;
			break;
		}

		case PROFESSION_PALADIN:{
			SecsPerHP = 8;
			SecsPerMana = 4;
			break;
		}

		case PROFESSION_SORCERER:
		case PROFESSION_DRUID:{
			SecsPerHP = 12;
			SecsPerMana = 3;
			break;
		}

		case PROFESSION_ELITE_KNIGHT:{
			SecsPerHP = 4;
			SecsPerMana = 6;
			break;
		}

		case PROFESSION_ROYAL_PALADIN:{
			SecsPerHP = 6;
			SecsPerMana = 3;
			break;
		}

		case PROFESSION_MASTER_SORCERER:
		case PROFESSION_ELDER_DRUID:{
			SecsPerHP = 12;
			SecsPerMana = 2;
			break;
		}

		default:{
			error(Translate("TSkillFed::Event: Unbekannter Beruf %d.\n",
							"TSkillFed::Event: Unknown Profession %d.\n"), Profession);
			break;
		}
	}

	// TODO(fusion): Not sure about this.
	int Timer = this->TimerValue();

	if(Timer % SecsPerHP == 0){
		Master->Skills[SKILL_HITPOINTS]->Change(1);
	}

	if(Timer % SecsPerMana == 0){
		Master->Skills[SKILL_MANA]->Change(2);
	}
}

// TSkillLight
//==============================================================================
bool TSkillLight::SetTimer(int Cycle, int Count, int MaxCount, int AdditionalValue){
	// TODO(fusion): I think the decompiled version of most `SetTimer` functions
	// look weird because they're calling the base class method `TSkill::SetTimer()`
	// which is getting inlined.
	this->Cycle = Cycle;
	this->Count = Count;
	this->MaxCount = MaxCount;

	TCreature *Master = this->Master;
	if(Master == NULL){
		error(Translate("TSkillLight::SetTimer: GetMaster liefert NULL zurueck!\n",
						"TSkillLight::SetTimer: GetMaster returns NULL!\n"));
		return false;
	}

	if(Master->Type == PLAYER){
		((TPlayer*)Master)->CheckState();
	}

	// TODO(fusion): The decompiled function had this logic, but WTF.
	//	if(GetCreature(Master->ID) != NULL){
	//		AnnounceChangedCreature(Master->ID, CREATURE_LIGHT_CHANGED);
	//	}
	AnnounceChangedCreature(Master->ID, CREATURE_LIGHT_CHANGED);
	return true;
}

void TSkillLight::Event(int Range){
	TCreature *Master = this->Master;
	if(Master == NULL){
		error(Translate("TSkillLight::Event: GetMaster liefert NULL zurueck!\n",
						"TSkillLight::Event: GetMaster returns NULL!\n"));
		return;
	}

	AnnounceChangedCreature(Master->ID, CREATURE_LIGHT_CHANGED);
}

// TSkillIllusion
//==============================================================================
bool TSkillIllusion::SetTimer(int Cycle, int Count, int MaxCount, int AdditionalValue){
	this->Cycle = Cycle;
	this->Count = Count;
	this->MaxCount = MaxCount;

	TCreature *Master = this->Master;
	if(Master == NULL){
		error(Translate("TSkillIllusion::SetTimer: GetMaster liefert NULL zurueck!\n",
						"TSkillIllusion::SetTimer: GetMaster returns NULL!\n"));
		return false;
	}

	if(Master->Type == PLAYER){
		((TPlayer*)Master)->CheckState();
	}

	// TODO(fusion): The decompiled function had this logic, but WTF.
	//	if(GetCreature(Master->ID) != NULL){
	//		AnnounceChangedCreature(Master->ID, CREATURE_OUTFIT_CHANGED);
	//	}
	AnnounceChangedCreature(Master->ID, CREATURE_OUTFIT_CHANGED);
	return true;
}

void TSkillIllusion::Event(int Range){
	if(this->Cycle == 0){
		TCreature *Master = this->Master;
		if(Master == NULL){
			error(Translate("TSkillIllusion::Event: GetMaster liefert NULL zurueck!\n",
							"TSkillIllusion::Event: GetMaster returns NULL!\n"));
			return;
		}

		if(this->Get() <= 0){
			Master->Outfit = Master->OrgOutfit;
			AnnounceChangedCreature(Master->ID, CREATURE_OUTFIT_CHANGED);
			NotifyAllCreatures(Master->CrObject, OBJECT_CHANGED, NONE);
		}
	}
}

// TSkillPoison
//==============================================================================
bool TSkillPoison::Process(void){
	bool Result = this->Cycle == 0;
	if(Result){
		TCreature *Master = this->Master;
		if(Master != NULL && Master->Type == PLAYER){
			((TPlayer*)Master)->CheckState();
		}
	}else if(this->Count <= 0){
		this->Count = this->MaxCount;

		// NOTE(fusion): Not sure what's going on here.
		int Range = (this->Cycle * this->FactorPercent) / 1000;
		if(Range == 0){
			// NOTE(fusion): This seems the opposite from `TSkill::Process`.
			Range = (this->Cycle > 0) ? + 1 : -1;
		}

		this->Cycle -= Range;
		this->Event(Range);
	}else{
		this->Count -= 1;
	}
	return Result;
}

bool TSkillPoison::SetTimer(int Cycle, int Count, int MaxCount, int AdditionalValue){
	this->Cycle = Cycle;
	this->Count = Count;
	this->MaxCount = MaxCount;

	TCreature *Master = this->Master;
	if(Master != NULL && Master->Type == PLAYER){
		((TPlayer*)Master)->CheckState();
	}

	if(AdditionalValue == -1){
		AdditionalValue = 50;
	}

	if(AdditionalValue < 10){
		AdditionalValue = 10;
	}

	if(AdditionalValue > 1000){
		AdditionalValue = 1000;
	}

	this->FactorPercent = AdditionalValue;
	return true;
}

void TSkillPoison::Event(int Range){
	TCreature *Master = this->Master;
	if(Master == NULL){
		error(Translate("TSkillPoison::Event: GetMaster liefert NULL zurueck!\n",
						"TSkillPoison::Event: GetMaster returns NULL!\n"));
		return;
	}

	if(Range < 0){
		Range = -Range;
	}

	Master->Damage(GetCreature(Master->PoisonDamageOrigin), Range, DAMAGE_POISON);

	// NOTE(fusion): I think this is checking whether `Master` is still upon some
	// poison field to determine whether we should extend the poison effect?
	Object Obj = GetFirstObject(Master->posx, Master->posy, Master->posz);
	while(Obj != NONE){
		// NOTE(fusion): I think `ObjectType` is analogous to `ItemType` in
		// OpenTibia terms.
		ObjectType ObjType = Obj.getObjectType();
		if(ObjType.getFlag(AVOID)){
			if(ObjType.getAttribute(AVOIDDAMAGETYPES) == DAMAGE_POISON){
				this->Cycle += 1;
			}
		}

		Obj = Obj.getNextObject();
	}
}

void TSkillPoison::Reset(void){
	TSkill::Reset();
	this->FactorPercent = 0x32;
}

// TSkillBurning
//==============================================================================
void TSkillBurning::Event(int Range){
	TCreature *Master = this->Master;
	if(Master == NULL){
		error(Translate("TSkillBurning::Event: GetMaster liefert NULL zurueck!\n",
						"TSkillBurning::Event: GetMaster returns NULL!\n"));
		return;
	}

	Master->Damage(GetCreature(Master->FireDamageOrigin), 10, DAMAGE_FIRE);

	// NOTE(Fusion): Something similar to `TSkillPoison::Event` except we're
	// looking for a different field type.
	Object Obj = GetFirstObject(Master->posx, Master->posy, Master->posz);
	while(Obj != NONE){
		ObjectType ObjType = Obj.getObjectType();
		if(ObjType.getFlag(AVOID)){
			if(ObjType.getAttribute(AVOIDDAMAGETYPES) == DAMAGE_FIRE){
				this->Cycle += 1;
			}
		}

		Obj = Obj.getNextObject();
	}
}

// TSkillEnergy
//==============================================================================
void TSkillEnergy::Event(int Range){
	TCreature *Master = this->Master;
	if(Master == NULL){
		error(Translate("TSkillEnergy::Event: GetMaster liefert NULL zurueck!\n",
						"TSkillEnergy::Event: GetMaster returns NULL!\n"));
		return;
	}

	Master->Damage(GetCreature(Master->EnergyDamageOrigin), 25, DAMAGE_ENERGY);

	// NOTE(Fusion): Something similar to `TSkillPoison::Event` except we're
	// looking for a different field type.
	Object Obj = GetFirstObject(Master->posx, Master->posy, Master->posz);
	while(Obj != NONE){
		ObjectType ObjType = Obj.getObjectType();
		if(ObjType.getFlag(AVOID)){
			if(ObjType.getAttribute(AVOIDDAMAGETYPES) == DAMAGE_ENERGY){
				this->Cycle += 1;
			}
		}

		Obj = Obj.getNextObject();
	}
}

// TSkillBase
//==============================================================================
TSkillBase::TSkillBase(void){
	STATIC_ASSERT(NARRAY(this->Skills) == NARRAY(this->TimerList));
	for(int i = 0; i < NARRAY(this->Skills); i += 1){
		this->Skills[i] = NULL;
		this->TimerList[i] = NULL;
	}
	this->FirstFreeTimer = 0;
}

TSkillBase::~TSkillBase(void){
	for(int i = 0; i < NARRAY(this->Skills); i += 1){
		delete this->Skills[i];
	}
}

bool TSkillBase::NewSkill(uint16 SkillNo, TCreature *Creature){
	if(SkillNo >= NARRAY(this->Skills)){
		error(Translate("TSkillBase::NewSkill: unbekannte SkillNummer %d\n",
						"TSkillBase::NewSkill: unknown SkillNummer %d\n"), SkillNo);
		return false;
	}

	if(this->Skills[SkillNo] != NULL){
		delete this->Skills[SkillNo];
		this->Skills[SkillNo] = NULL;
	}

	TSkill *Skill = NULL;
	switch(SkillNo){
		case SKILL_LEVEL:			Skill = new TSkillLevel(SkillNo, Creature); break;
		case SKILL_MAGIC_LEVEL:		ATTR_FALLTHROUGH;
		case SKILL_SHIELDING:		ATTR_FALLTHROUGH;
		case SKILL_DISTANCE:		ATTR_FALLTHROUGH;
		case SKILL_SWORD:			ATTR_FALLTHROUGH;
		case SKILL_CLUB:			ATTR_FALLTHROUGH;
		case SKILL_AXE:				ATTR_FALLTHROUGH;
		case SKILL_FIST:			ATTR_FALLTHROUGH;
		case SKILL_FISHING:			Skill = new TSkillProbe(SkillNo, Creature); break;
		case SKILL_HITPOINTS:		Skill = new TSkillHitpoints(SkillNo, Creature); break;
		case SKILL_MANA:			Skill = new TSkillMana(SkillNo, Creature); break;
		case SKILL_GO_STRENGTH:		Skill = new TSkillGoStrength(SkillNo, Creature); break;
		case SKILL_CARRY_STRENGTH:	Skill = new TSkillCarryStrength(SkillNo, Creature); break;
		case SKILL_FED:				Skill = new TSkillFed(SkillNo, Creature); break;
		case SKILL_LIGHT:			Skill = new TSkillLight(SkillNo, Creature); break;
		case SKILL_ILLUSION:		Skill = new TSkillIllusion(SkillNo, Creature); break;
		case SKILL_POISON:			Skill = new TSkillPoison(SkillNo, Creature); break;
		case SKILL_BURNING:			Skill = new TSkillBurning(SkillNo, Creature); break;
		case SKILL_ENERGY:			Skill = new TSkillEnergy(SkillNo, Creature); break;
		case SKILL_SOUL:			Skill = new TSkillSoulpoints(SkillNo, Creature); break;
		default:					Skill = new TSkill(SkillNo, Creature);	break;
	}

	ASSERT(Skill != NULL);
	this->Skills[SkillNo] = Skill;
	return true;
}

bool TSkillBase::SetSkills(int Race){
	if(!IsRaceValid(Race)){
		error(Translate("TSkillBase::SetSkills: Ungültige Rassennummer %d.\n",
						"TSkillBase::SetSkills: Invalid race number %d.\n"), Race);
		return false;
	}

	// NOTE(fusion): Skills are indexed from 1.
	for(int i = 1; i <= RaceData[Race].Skills; i += 1){
		TSkillData *SkillData = RaceData[Race].Skill.at(i);

		// BUG(fusion): We don't check if `Skill` is valid? Could be NULL. We
		// don't seem to set all `Skill` fields either so there is something
		// else going on, probably.
		TSkill *Skill = this->Skills[SkillData->Nr];
		Skill->Act = SkillData->Actual;
		Skill->Min = SkillData->Minimum;
		Skill->Max = SkillData->Maximum;
		Skill->LastLevel = 0;
		Skill->NextLevel = SkillData->NextLevel;
		Skill->Delta = SkillData->NextLevel;
		Skill->FactorPercent = SkillData->FactorPercent;
		Skill->MaxCount = 0;
		Skill->AddLevel = SkillData->AddLevel;
	}

	return true;
}

void TSkillBase::ProcessSkills(void){
	int Index = 0;
	while(Index < this->FirstFreeTimer){
		// TODO(fusion): Probably remove if `Skill == NULL`?
		TSkill *Skill = this->TimerList[Index];
		if(Skill != NULL && Skill->Process()){
			// NOTE(fusion): A little swap and pop action.
			this->FirstFreeTimer -= 1;
			this->TimerList[Index] = this->TimerList[this->FirstFreeTimer];
			this->TimerList[this->FirstFreeTimer] = NULL;
		}else{
			Index += 1;
		}
	}
}

bool TSkillBase::SetTimer(uint16 SkNr, int Cycle, int Count, int MaxCount, int AdditionalValue){
	if(SkNr >= NARRAY(this->Skills)){
		error(Translate("TSkillBase::SetTimer: Ungueltige SkNr: %d\n",
						"TSkillBase::SetTimer: Invalid SkNr: %d\n"), SkNr);
		return false;
	}

	this->DelTimer(SkNr);

	// BUG(fusion): We don't check if `Skill` is valid here.
	TSkill *Skill = this->Skills[SkNr];
	bool Result = Skill->SetTimer(Cycle, Count, MaxCount, AdditionalValue);
	if(Result){
		// NOTE(fusion): A little push back action. We don't check if there is
		// room in `TimerList` because it should be naturally bound by the max
		// number of skills.
		ASSERT(this->FirstFreeTimer < NARRAY(this->TimerList));
		this->TimerList[this->FirstFreeTimer] = Skill;
		this->FirstFreeTimer += 1;
	}

	return Result;
}

void TSkillBase::DelTimer(uint16 SkNr){
	if(SkNr >= NARRAY(this->Skills)){
		error(Translate("TSkillBase::DelTimer: Ungueltige SkNr: %d\n",
						"TSkillBase::DelTimer: Invalid SkNr: %d\n"), SkNr);
		return;
	}

	TSkill *Skill = this->Skills[SkNr];
	if(Skill != NULL){
		Skill->DelTimer();

		for(int Index = 0; Index < this->FirstFreeTimer; Index += 1){
			if(Skill == this->TimerList[Index]){
				// NOTE(fusion): A little swap and pop action.
				this->FirstFreeTimer -= 1;
				this->TimerList[Index] = this->TimerList[this->FirstFreeTimer];
				this->TimerList[this->FirstFreeTimer] = NULL;
				break;
			}
		}
	}
}

// Helpers
//==============================================================================
int GetSkillByName(const char *Name){
	int Result = -1;
	if(strcmp(Name, "level") == 0){
		Result = SKILL_LEVEL;
	}else if(strcmp(Name, "magiclevel") == 0){
		Result = SKILL_MAGIC_LEVEL;
	}else if(strcmp(Name, "hitpoints") == 0){
		Result = SKILL_HITPOINTS;
	}else if(strcmp(Name, "mana") == 0){
		Result = SKILL_MANA;
	}else if(strcmp(Name, "gostrength") == 0){
		Result = SKILL_GO_STRENGTH;
	}else if(strcmp(Name, "carrystrength") == 0){
		Result = SKILL_CARRY_STRENGTH;
	}else if(strcmp(Name, "shielding") == 0){
		Result = SKILL_SHIELDING;
	}else if(strcmp(Name, "distancefighting") == 0){
		Result = SKILL_DISTANCE;
	}else if(strcmp(Name, "swordfighting") == 0){
		Result = SKILL_SWORD;
	}else if(strcmp(Name, "clubfighting") == 0){
		Result = SKILL_CLUB;
	}else if(strcmp(Name, "axefighting") == 0){
		Result = SKILL_AXE;
	}else if(strcmp(Name, "fistfighting") == 0){
		Result = SKILL_FIST;
	}else if(strcmp(Name, "magicdefense") == 0){
		Result = SKILL_MAGIC_DEFENSE;
	}else if(strcmp(Name, "fishing") == 0){
		Result = SKILL_FISHING;
	}else if(strcmp(Name, "eating") == 0){
		Result = SKILL_FED;
	}else if(strcmp(Name, "shining") == 0){
		Result = SKILL_LIGHT;
	}else if(strcmp(Name, "illusion") == 0){
		Result = SKILL_ILLUSION;
	}else if(strcmp(Name, "poison") == 0){
		Result = SKILL_POISON;
	}else if(strcmp(Name, "burning") == 0){
		Result = SKILL_BURNING;
	}else if(strcmp(Name, "energy") == 0){
		Result = SKILL_ENERGY;
	}else if(strcmp(Name, "drunken") == 0){
		Result = SKILL_DRUNKEN;
	}else if(strcmp(Name, "manashield") == 0){
		Result = SKILL_MANASHIELD;
	}else if(strcmp(Name, "soulpoints") == 0){
		Result = SKILL_SOUL;
	}
	return Result;
}

// Initialization
//==============================================================================
void InitCrskill(void){
	// no-op
}

void ExitCrskill(void){
	// no-op
}
