#include "cr.hh"
#include "config.hh"
#include "containers.hh"
#include "info.hh"
#include "magic.hh"
#include "operate.hh"
#include "writer.hh"

#include <dirent.h>

static vector<TNonplayer*> NonplayerList(0, 10000, 1000, NULL);
static int FirstFreeNonplayer;

static vector<TMonsterhome> Monsterhome(1, 5000, 1000);
static int Monsterhomes;

static store<TBehaviourNode, 256> BehaviourNodeTable;

// Behaviour Database
// =============================================================================
static TBehaviourNode *NewBehaviourNode(int Type, TBehaviourNode *Left, TBehaviourNode *Right){
	TBehaviourNode *Node = BehaviourNodeTable.getFreeItem();
	Node->Type = Type;
	Node->Data = 0;
	Node->Left = Left;
	Node->Right = Right;
	return Node;
}

static TBehaviourNode *NewBehaviourNode(int Type, int Data){
	TBehaviourNode *Node = BehaviourNodeTable.getFreeItem();
	Node->Type = Type;
	Node->Data = Data;
	Node->Left = NULL;
	Node->Right = NULL;
	return Node;
}

static void DeleteBehaviourNode(TBehaviourNode *Node){
	if(Node != NULL){
		DeleteBehaviourNode(Node->Left);
		DeleteBehaviourNode(Node->Right);
		BehaviourNodeTable.putFreeItem(Node);
	}
}

bool TBehaviourCondition::set(int Type, void *Data){
	if(Type != BEHAVIOUR_CONDITION_SHORTCIRCUIT && Data == NULL){
		error("TBehaviourCondition::set: Data ist NULL.\n");
		this->Type = BEHAVIOUR_CONDITION_NONE;
		return false;
	}

	this->Type = Type;
	switch(Type){
		case BEHAVIOUR_CONDITION_TEXT:{
			this->Text = AddDynamicString((const char*)Data);
			break;
		}

		case BEHAVIOUR_CONDITION_PROPERTY:{
			const char *Property = (const char*)Data;
			this->Property = BEHAVIOUR_PROPERTY_NONE;
			if(strcmp(Property, "address") == 0){
				this->Property = BEHAVIOUR_PROPERTY_ADDRESS;
			}else if(strcmp(Property, "busy") == 0){
				this->Property = BEHAVIOUR_PROPERTY_BUSY;
			}else if(strcmp(Property, "vanish") == 0){
				this->Property = BEHAVIOUR_PROPERTY_VANISH;
			}else if(strcmp(Property, "male") == 0){
				this->Property = BEHAVIOUR_PROPERTY_MALE;
			}else if(strcmp(Property, "female") == 0){
				this->Property = BEHAVIOUR_PROPERTY_FEMALE;
			}else if(strcmp(Property, "knight") == 0){
				this->Property = BEHAVIOUR_PROPERTY_KNIGHT;
			}else if(strcmp(Property, "paladin") == 0){
				this->Property = BEHAVIOUR_PROPERTY_PALADIN;
			}else if(strcmp(Property, "sorcerer") == 0){
				this->Property = BEHAVIOUR_PROPERTY_SORCERER;
			}else if(strcmp(Property, "druid") == 0){
				this->Property = BEHAVIOUR_PROPERTY_DRUID;
			}else if(strcmp(Property, "premium") == 0){
				this->Property = BEHAVIOUR_PROPERTY_PREMIUM;
			}else if(strcmp(Property, "promoted") == 0){
				this->Property = BEHAVIOUR_PROPERTY_PROMOTED;
			}else if(strcmp(Property, "pzblock") == 0){
				this->Property = BEHAVIOUR_PROPERTY_PZBLOCK;
			}else if(strcmp(Property, "nonpvp") == 0){
				this->Property = BEHAVIOUR_PROPERTY_NONPVP;
			}else if(strcmp(Property, "pvpenforced") == 0){
				this->Property = BEHAVIOUR_PROPERTY_PVPENFORCED;
			}

			if(this->Property == BEHAVIOUR_PROPERTY_NONE){
				return false;
			}
			break;
		}

		case BEHAVIOUR_CONDITION_PARAMETER:{
			this->Number = *(int*)Data;
			break;
		}

		case BEHAVIOUR_CONDITION_EXPRESSION:{
			this->Expression = (TBehaviourNode*)Data;
			break;
		}

		case BEHAVIOUR_CONDITION_SHORTCIRCUIT:{
			// no-op
			break;
		}

		default:{
			error("TBehaviourCondition::set: Ungültiger Bedingungstyp %d\n", Type);
			return false;
		}
	}

	return true;
}

void TBehaviourCondition::clear(void){
	if(this->Type == BEHAVIOUR_CONDITION_TEXT){
		DeleteDynamicString(this->Text);
	}else if(this->Type == BEHAVIOUR_CONDITION_EXPRESSION){
		DeleteBehaviourNode(this->Expression);
	}

	this->Type = BEHAVIOUR_CONDITION_NONE;
}

bool TBehaviourAction::set(int Type, void *Data, void *Data2, void *Data3, void *Data4){
	this->Type = Type;
	switch(Type){
		case BEHAVIOUR_ACTION_REPLY:{
			this->Text = AddDynamicString((const char*)Data);
			break;
		}

		case BEHAVIOUR_ACTION_SET_VARIABLE:
		case BEHAVIOUR_ACTION_SET_SKILL:
		case BEHAVIOUR_ACTION_FUNCTION1:{
			this->Number = *(int*)Data;
			this->Expression = (TBehaviourNode*)Data2;
			break;
		}

		case BEHAVIOUR_ACTION_FUNCTION2:
		case BEHAVIOUR_ACTION_SET_SKILL_TIMER:{
			this->Number = *(int*)Data;
			this->Expression = (TBehaviourNode*)Data2;
			this->Expression2 = (TBehaviourNode*)Data3;
			break;
		}

		case BEHAVIOUR_ACTION_FUNCTION0:
		case BEHAVIOUR_ACTION_CHANGESTATE:{
			this->Number = *(int*)Data;
			break;
		}

		case BEHAVIOUR_ACTION_REPEAT:{
			// no-op
			break;
		}

		case BEHAVIOUR_ACTION_FUNCTION3:{
			this->Number = *(int*)Data;
			this->Expression = (TBehaviourNode*)Data2;
			this->Expression2 = (TBehaviourNode*)Data3;
			this->Expression3 = (TBehaviourNode*)Data4;
			break;
		}

		default:{
			error("TAction::set: Ungültiger Aktionstyp %d\n", Type);
			return false;
		}
	}

	return true;
}

void TBehaviourAction::clear(void){
	switch(this->Type){
		case BEHAVIOUR_ACTION_REPLY:{
			DeleteDynamicString(this->Text);
			break;
		}

		case BEHAVIOUR_ACTION_SET_VARIABLE:
		case BEHAVIOUR_ACTION_SET_SKILL:
		case BEHAVIOUR_ACTION_FUNCTION1:{
			DeleteBehaviourNode(this->Expression);
			break;
		}

		case BEHAVIOUR_ACTION_FUNCTION2:
		case BEHAVIOUR_ACTION_SET_SKILL_TIMER:{
			DeleteBehaviourNode(this->Expression);
			DeleteBehaviourNode(this->Expression2);
			break;
		}

		case BEHAVIOUR_ACTION_FUNCTION3:{
			DeleteBehaviourNode(this->Expression);
			DeleteBehaviourNode(this->Expression2);
			DeleteBehaviourNode(this->Expression3);
			break;
		}

		default:{
			break;
		}
	}

	this->Type = BEHAVIOUR_ACTION_NONE;
}

TBehaviour::TBehaviour(void) :
		Condition(0, 5, 5),
		Action(0, 5, 5)
{
	this->Conditions = 0;
	this->Actions = 0;
}

TBehaviour::~TBehaviour(void){
	for(int i = 0; i < this->Conditions; i += 1){
		this->Condition.at(i)->clear();
	}

	for(int i = 0; i < this->Actions; i += 1){
		this->Action.at(i)->clear();
	}
}

bool TBehaviour::addCondition(int Type, void *Data){
	bool Result = this->Condition.at(this->Conditions)->set(Type, Data);
	if(Result){
		this->Conditions += 1;
	}
	return Result;
}

bool TBehaviour::addAction(int Type, void *Data, void *Data2, void *Data3, void *Data4){
	bool Result = this->Action.at(this->Actions)->set(Type, Data, Data2, Data3, Data4);
	if(Result){
		this->Actions += 1;
	}
	return Result;
}

TBehaviour::TBehaviour(const TBehaviour &Other) :
		TBehaviour()
{
	this->operator=(Other);
}

void TBehaviour::operator=(const TBehaviour &Other){
	this->Conditions = Other.Conditions;
	for(int i = 0; i < Other.Conditions; i += 1){
		*this->Condition.at(i) = Other.Condition.copyAt(i);
	}

	this->Actions = Other.Actions;
	for(int i = 0; i < Other.Actions; i += 1){
		*this->Action.at(i) = Other.Action.copyAt(i);
	}
}


TBehaviourDatabase::TBehaviourDatabase(TReadScriptFile *Script) :
		Behaviour(0, 50, 25)
{
	this->Behaviours = 0;
	Script->readSymbol('{');
	Script->nextToken();
	while(Script->Token != SPECIAL || Script->getSpecial() != '}'){
		TBehaviour *Behaviour = this->Behaviour.at(this->Behaviours);

		// NOTE(fusion): Optional conditions.
		if(Script->Token != SPECIAL || Script->getSpecial() != 'I'){
			while(true){
				bool Ok = false;
				if(Script->Token == STRING){
					Ok = Behaviour->addCondition(BEHAVIOUR_CONDITION_TEXT, Script->getString());
				}else if(Script->Token == IDENTIFIER){
					Ok = Behaviour->addCondition(BEHAVIOUR_CONDITION_PROPERTY, Script->getIdentifier());
				}else if(Script->Token == SPECIAL){
					if(Script->getSpecial() == '!'){
						Ok = Behaviour->addCondition(BEHAVIOUR_CONDITION_SHORTCIRCUIT, NULL);
					}else if(Script->getSpecial() == '%'){
						int Parameter = Script->readNumber();
						if(Parameter != 1 && Parameter != 2){
							Script->error("illegal ordinal number");
						}
						Ok = Behaviour->addCondition(BEHAVIOUR_CONDITION_PARAMETER, &Parameter);
					}
				}

				if(!Ok){
					TBehaviourNode *Left = this->readTerm(Script);
					if(Script->Token != SPECIAL){
						Script->error("relational operator expected");
					}

					int Operator = BEHAVIOUR_NODE_NONE;
					switch(Script->getSpecial()){
						case '<': Operator = BEHAVIOUR_NODE_CMP_LT; break;
						case '>': Operator = BEHAVIOUR_NODE_CMP_GT; break;
						case '=': Operator = BEHAVIOUR_NODE_CMP_EQ; break;
						case 'N': Operator = BEHAVIOUR_NODE_CMP_NEQ; break;
						case 'L': Operator = BEHAVIOUR_NODE_CMP_LE; break;
						case 'G': Operator = BEHAVIOUR_NODE_CMP_GE; break;
						default:{
							Script->error("relational operator expected");
							break;
						}
					}

					Script->nextToken();
					TBehaviourNode *Right = this->readTerm(Script);
					Behaviour->addCondition(BEHAVIOUR_CONDITION_EXPRESSION,
							NewBehaviourNode(Operator, Left, Right));
				}else{
					Script->nextToken();
				}

				if(Script->Token == SPECIAL && Script->getSpecial() == ','){
					Script->nextToken();
				}else{
					break;
				}
			}
		}

		if(Script->Token != SPECIAL || Script->getSpecial() != 'I'){
			Script->error("'->' expected");
		}

		// NOTE(fusion): Required Actions.
		Script->nextToken();
		while(true){
			if(Script->Token == STRING){
				Behaviour->addAction(BEHAVIOUR_ACTION_REPLY, Script->getString(), NULL, NULL, NULL);
				Script->nextToken();
			}else if(Script->Token == IDENTIFIER){
				int Type = -1;
				int Data = 0;
				const char *Identifier = Script->getIdentifier();
				if(strcmp(Identifier, "topic") == 0){
					Type = BEHAVIOUR_ACTION_SET_VARIABLE;
					Data = 1; // BEHAVIOUR_VARIABLE_TOPIC ?
				}else if(strcmp(Identifier, "price") == 0){
					Type = BEHAVIOUR_ACTION_SET_VARIABLE;
					Data = 2; // BEHAVIOUR_VARIABLE_PRICE ?
				}else if(strcmp(Identifier, "amount") == 0){
					Type = BEHAVIOUR_ACTION_SET_VARIABLE;
					Data = 3; // BEHAVIOUR_VARIABLE_AMOUNT ?
				}else if(strcmp(Identifier, "type") == 0){
					Type = BEHAVIOUR_ACTION_SET_VARIABLE;
					Data = 4; // BEHAVIOUR_VARIABLE_TYPE
				}else if(strcmp(Identifier, "data") == 0){
					Type = BEHAVIOUR_ACTION_SET_VARIABLE;
					Data = 6; // BEHAVIOUR_VARIABLE_DATA
				}else if(strcmp(Identifier, "hp") == 0){
					Type = BEHAVIOUR_ACTION_SET_SKILL;
					Data = SKILL_HITPOINTS;
				}else if(strcmp(Identifier, "poison") == 0){
					Type = BEHAVIOUR_ACTION_SET_SKILL_TIMER;
					Data = SKILL_POISON;
				}else if(strcmp(Identifier, "burning") == 0){
					Type = BEHAVIOUR_ACTION_SET_SKILL_TIMER;
					Data = SKILL_BURNING;
				}else if(strcmp(Identifier, "setquestvalue") == 0){
					Type = BEHAVIOUR_ACTION_FUNCTION2;
					Data = 3; // BEHAVIOUR_FUNCTION2_SETQUESTVALUE ?
				}else if(strcmp(Identifier, "effectme") == 0){
					Type = BEHAVIOUR_ACTION_FUNCTION1;
					Data = 1; // BEHAVIOUR_FUNCTION1_EFFECTME ?
				}else if(strcmp(Identifier, "effectopp") == 0){
					Type = BEHAVIOUR_ACTION_FUNCTION1;
					Data = 2; // BEHAVIOUR_FUNCTION1_EFFECTOPP ?
				}else if(strcmp(Identifier, "profession") == 0){
					Type = BEHAVIOUR_ACTION_FUNCTION1;
					Data = 3; // BEHAVIOUR_FUNCTION1_SETPROFESSION ?
				}else if(strcmp(Identifier, "teachspell") == 0){
					Type = BEHAVIOUR_ACTION_FUNCTION1;
					Data = 4; // BEHAVIOUR_FUNCTION1_TEACHSPELL ?
				}else if(strcmp(Identifier, "summon") == 0){
					Type = BEHAVIOUR_ACTION_FUNCTION1;
					Data = 5; // BEHAVIOUR_FUNCTION1_SUMMON ?
				}else if(strcmp(Identifier, "create") == 0){
					Type = BEHAVIOUR_ACTION_FUNCTION1;
					Data = 6; // BEHAVIOUR_FUNCTION1_CREATE ?
				}else if(strcmp(Identifier, "delete") == 0){
					Type = BEHAVIOUR_ACTION_FUNCTION1;
					Data = 7; // BEHAVIOUR_FUNCTION1_DELETE ?
				}else if(strcmp(Identifier, "createmoney") == 0){
					Type = BEHAVIOUR_ACTION_FUNCTION0;
					Data = 1; // BEHAVIOUR_FUNCTION0_CREATEMONEY ?
				}else if(strcmp(Identifier, "deletemoney") == 0){
					Type = BEHAVIOUR_ACTION_FUNCTION0;
					Data = 2; // BEHAVIOUR_FUNCTION0_DELETEMONEY ?
				}else if(strcmp(Identifier, "queue") == 0){
					Type = BEHAVIOUR_ACTION_FUNCTION0;
					Data = 3; // BEHAVIOUR_FUNCTION0_ENQUEUE ?
				}else if(strcmp(Identifier, "teleport") == 0){
					Type = BEHAVIOUR_ACTION_FUNCTION3;
					Data = 1; // BEHAVIOUR_FUNCTION3_TELEPORT ?
				}else if(strcmp(Identifier, "startposition") == 0){
					Type = BEHAVIOUR_ACTION_FUNCTION3;
					Data = 2; // BEHAVIOUR_FUNCTION3_SETSTART ?
				}else if(strcmp(Identifier, "idle") == 0){
					Type = BEHAVIOUR_ACTION_CHANGESTATE;
					Data = IDLE;
				}else if(strcmp(Identifier, "nop") == 0){
					Type = BEHAVIOUR_ACTION_NONE;
					Data = 0;
				}

				switch(Type){
					case BEHAVIOUR_ACTION_NONE:{
						Script->nextToken();
						break;
					}

					case BEHAVIOUR_ACTION_SET_VARIABLE:
					case BEHAVIOUR_ACTION_SET_SKILL:{
						Script->readSymbol('=');
						Script->nextToken();
						TBehaviourNode *Value = this->readTerm(Script);
						Behaviour->addAction(Type, &Data, Value, NULL, NULL);
						break;
					}

					case BEHAVIOUR_ACTION_FUNCTION1:{
						Script->readSymbol('(');
						Script->nextToken();
						TBehaviourNode *Param = this->readTerm(Script);
						if(Script->Token != SPECIAL || Script->getSpecial() != ')'){
							Script->error(") expected");
						}
						Script->nextToken();
						Behaviour->addAction(Type, &Data, Param, NULL, NULL);
						break;
					}

					case BEHAVIOUR_ACTION_FUNCTION2:
					case BEHAVIOUR_ACTION_SET_SKILL_TIMER:{
						Script->readSymbol('(');
						Script->nextToken();
						TBehaviourNode *Param1 = this->readTerm(Script);
						if(Script->Token != SPECIAL || Script->getSpecial() != ','){
							Script->error(", expected");
						}
						Script->nextToken();
						TBehaviourNode *Param2 = this->readTerm(Script);
						if(Script->Token != SPECIAL || Script->getSpecial() != ')'){
							Script->error(") expected");
						}
						Script->nextToken();
						Behaviour->addAction(Type, &Data, Param1, Param2, NULL);
						break;
					}

					case BEHAVIOUR_ACTION_FUNCTION0:
					case BEHAVIOUR_ACTION_CHANGESTATE:{
						Script->nextToken();
						Behaviour->addAction(Type, &Data, NULL, NULL, NULL);
						break;
					}

					case BEHAVIOUR_ACTION_FUNCTION3:{
						Script->readSymbol('(');
						Script->nextToken();
						TBehaviourNode *Param1 = this->readTerm(Script);
						if(Script->Token != SPECIAL || Script->getSpecial() != ','){
							Script->error(", expected");
						}
						Script->nextToken();
						TBehaviourNode *Param2 = this->readTerm(Script);
						if(Script->Token != SPECIAL || Script->getSpecial() != ','){
							Script->error(", expected");
						}
						Script->nextToken();
						TBehaviourNode *Param3 = this->readTerm(Script);
						if(Script->Token != SPECIAL || Script->getSpecial() != ')'){
							Script->error(") expected");
						}
						Script->nextToken();
						Behaviour->addAction(Type, &Data, Param1, Param2, Param3);
						break;
					}

					default:{
						Script->error("unknown identifier");
						break;
					}
				}

			}else if(Script->Token == SPECIAL && Script->getSpecial() == '*'){
				if(this->Behaviours == 0){
					Script->error("no previous pattern");
				}
				Script->nextToken();
				Behaviour->addAction(BEHAVIOUR_ACTION_REPEAT, NULL, NULL, NULL, NULL);
			}else{
				Script->error("illegal action");
			}

			if(Script->Token == SPECIAL && Script->getSpecial() == ','){
				Script->nextToken();
			}else{
				break;
			}
		}

		this->Behaviours += 1;
	}
}

TBehaviourNode *TBehaviourDatabase::readValue(TReadScriptFile *Script){
	TBehaviourNode *Node = NULL;
	if(Script->Token == NUMBER){
		Node = NewBehaviourNode(BEHAVIOUR_NODE_NUMBER, Script->getNumber());
	}else if(Script->Token == IDENTIFIER){
		const char *Identifier = Script->getIdentifier();
		if(strcmp(Identifier, "topic") == 0){
			Node = NewBehaviourNode(BEHAVIOUR_NODE_TOPIC, 0);
		}else if(strcmp(Identifier, "price") == 0){
			Node = NewBehaviourNode(BEHAVIOUR_NODE_PRICE, 0);
		}else if(strcmp(Identifier, "amount") == 0){
			Node = NewBehaviourNode(BEHAVIOUR_NODE_AMOUNT, 0);
		}else if(strcmp(Identifier, "level") == 0){
			Node = NewBehaviourNode(BEHAVIOUR_NODE_SKILL, SKILL_LEVEL);
		}else if(strcmp(Identifier, "magiclevel") == 0){
			Node = NewBehaviourNode(BEHAVIOUR_NODE_SKILL, SKILL_MAGIC_LEVEL);
		}else if(strcmp(Identifier, "hp") == 0){
			Node = NewBehaviourNode(BEHAVIOUR_NODE_SKILL, SKILL_HITPOINTS);
		}else if(strcmp(Identifier, "poison") == 0){
			Node = NewBehaviourNode(BEHAVIOUR_NODE_SKILL, SKILL_POISON);
		}else if(strcmp(Identifier, "burning") == 0){
			Node = NewBehaviourNode(BEHAVIOUR_NODE_SKILL, SKILL_BURNING);
		}else if(strcmp(Identifier, "count") == 0){
			Script->readSymbol('(');
			Script->nextToken();
			TBehaviourNode *Left = this->readTerm(Script);
			if(Script->Token != SPECIAL || Script->getSpecial() != ')'){
				Script->error(") expected");
			}

			Node = NewBehaviourNode(BEHAVIOUR_NODE_COUNT, Left, NULL);
		}else if(strcmp(Identifier, "countmoney") == 0){
			Node = NewBehaviourNode(BEHAVIOUR_NODE_COUNTMONEY, 0);
		}else if(strcmp(Identifier, "type") == 0){
			Node = NewBehaviourNode(BEHAVIOUR_NODE_TYPE, 0);
		}else if(strcmp(Identifier, "data") == 0){
			Node = NewBehaviourNode(BEHAVIOUR_NODE_DATA, 0);
		}else if(strcmp(Identifier, "spellknown") == 0){
			Script->readSymbol('(');
			Script->nextToken();
			TBehaviourNode *Left = this->readTerm(Script);
			if(Script->Token != SPECIAL || Script->getSpecial() != ')'){
				Script->error(") expected");
			}

			Node = NewBehaviourNode(BEHAVIOUR_NODE_SPELLKNOWN, Left, NULL);
		}else if(strcmp(Identifier, "spelllevel") == 0){
			Script->readSymbol('(');
			Script->nextToken();
			TBehaviourNode *Left = this->readTerm(Script);
			if(Script->Token != SPECIAL || Script->getSpecial() != ')'){
				Script->error(") expected");
			}

			Node = NewBehaviourNode(BEHAVIOUR_NODE_SPELLLEVEL, Left, NULL);
		}else if(strcmp(Identifier, "random") == 0){
			Script->readSymbol('(');

			Script->nextToken();
			TBehaviourNode *Left = this->readTerm(Script);
			if(Script->Token != SPECIAL || Script->getSpecial() != ','){
				Script->error(", expected");
			}

			Script->nextToken();
			TBehaviourNode *Right = this->readTerm(Script);
			if(Script->Token != SPECIAL || Script->getSpecial() != ')'){
				Script->error(") expected");
			}

			Node = NewBehaviourNode(BEHAVIOUR_NODE_RANDOM, Left, Right);
		}else if(strcmp(Identifier, "questvalue") == 0){
			Script->readSymbol('(');
			Script->nextToken();
			TBehaviourNode *Left = this->readTerm(Script);
			if(Script->Token != SPECIAL || Script->getSpecial() != ')'){
				Script->error(") expected");
			}

			Node = NewBehaviourNode(BEHAVIOUR_NODE_QUESTVALUE, Left, NULL);
		}
	}else if(Script->Token == SPECIAL){
		if(Script->getSpecial() != '%'){
			Script->error("illegal character");
		}
		Node = NewBehaviourNode(BEHAVIOUR_NODE_PARAMETER, Script->readNumber());
	}else{
		Script->error("illegal value");
	}

	if(Node == NULL){
		Script->error("unknown value");
	}

	Script->nextToken();
	return Node;
}

TBehaviourNode *TBehaviourDatabase::readFactor(TReadScriptFile *Script){
	TBehaviourNode *Node = this->readValue(Script);
	while(Script->Token == SPECIAL && Script->getSpecial() == '*'){
		Script->nextToken();
		Node = NewBehaviourNode(BEHAVIOUR_NODE_MUL, Node, this->readValue(Script));
	}
	return Node;
}

TBehaviourNode *TBehaviourDatabase::readTerm(TReadScriptFile *Script){
	TBehaviourNode *Node = this->readFactor(Script);
	while(Script->Token == SPECIAL){
		int Type = BEHAVIOUR_NODE_NONE;
		if(Script->getSpecial() == '+'){
			Type = BEHAVIOUR_NODE_ADD;
		}else if(Script->getSpecial() == '-'){
			Type = BEHAVIOUR_NODE_SUB;
		}else{
			break;
		}

		Script->nextToken();
		Node = NewBehaviourNode(Type, Node, this->readFactor(Script));
	}
	return Node;
}

int TBehaviourDatabase::evaluate(TNPC *Npc, TBehaviourNode *Node, int *Parameters){
	if(Npc == NULL){
		error("TBehaviourDatabase::evaluate: NPC existiert nicht.\n");
		return 0;
	}

	if(Node == NULL){
		error("TBehaviourDatabase::evaluate: Knoten existiert nicht.\n");
		return 0;
	}

	if(Parameters == NULL){
		error("TBehaviourDatabase::evaluate: Zahlen existieren nicht.\n");
		return 0;
	}

	uint32 InterlocutorID = Npc->Interlocutor;
	TPlayer *Interlocutor = GetPlayer(InterlocutorID);
	if(Interlocutor == NULL){
		error("TBehaviourDatabase::evaluate: Gesprächspartner existiert nicht.\n");
		return 0;
	}

	int Result = 0;
	switch(Node->Type){
		case BEHAVIOUR_NODE_CMP_LT:{
			int Left = this->evaluate(Npc, Node->Left, Parameters);
			int Right = this->evaluate(Npc, Node->Right, Parameters);
			Result = Left < Right;
			break;
		}

		case BEHAVIOUR_NODE_CMP_GT:{
			int Left = this->evaluate(Npc, Node->Left, Parameters);
			int Right = this->evaluate(Npc, Node->Right, Parameters);
			Result = Left > Right;
			break;
		}

		case BEHAVIOUR_NODE_CMP_EQ:{
			int Left = this->evaluate(Npc, Node->Left, Parameters);
			int Right = this->evaluate(Npc, Node->Right, Parameters);
			Result = Left == Right;
			break;
		}

		case BEHAVIOUR_NODE_CMP_NEQ:{
			int Left = this->evaluate(Npc, Node->Left, Parameters);
			int Right = this->evaluate(Npc, Node->Right, Parameters);
			Result = Left != Right;
			break;
		}

		case BEHAVIOUR_NODE_CMP_LE:{
			int Left = this->evaluate(Npc, Node->Left, Parameters);
			int Right = this->evaluate(Npc, Node->Right, Parameters);
			Result = Left <= Right;
			break;
		}

		case BEHAVIOUR_NODE_CMP_GE:{
			int Left = this->evaluate(Npc, Node->Left, Parameters);
			int Right = this->evaluate(Npc, Node->Right, Parameters);
			Result = Left >= Right;
			break;
		}

		case BEHAVIOUR_NODE_ADD:{
			int Left = this->evaluate(Npc, Node->Left, Parameters);
			int Right = this->evaluate(Npc, Node->Right, Parameters);
			Result = Left + Right;
			break;
		}

		case BEHAVIOUR_NODE_SUB:{
			int Left = this->evaluate(Npc, Node->Left, Parameters);
			int Right = this->evaluate(Npc, Node->Right, Parameters);
			Result = Left - Right;
			break;
		}

		case BEHAVIOUR_NODE_MUL:{
			int Left = this->evaluate(Npc, Node->Left, Parameters);
			int Right = this->evaluate(Npc, Node->Right, Parameters);
			Result = Left * Right;
			break;
		}

		case BEHAVIOUR_NODE_PARAMETER:{
			if(Node->Data != 1 && Node->Data != 2){
				error("TBehaviourDatabase::evaluate: Ungültiger Zahl-Parameter %d.\n", Node->Data);
			}else if(Parameters[Node->Data - 1] < 0){
				error("TBehaviourDatabase::evaluate: Zahl-Parameter %d nicht belegt.\n", Node->Data);
			}else{
				Result = Parameters[Node->Data - 1];
			}
			break;
		}

		case BEHAVIOUR_NODE_NUMBER:{
			Result = Node->Data;
			break;
		}

		case BEHAVIOUR_NODE_TOPIC:{
			Result = Npc->Topic;
			break;
		}

		case BEHAVIOUR_NODE_PRICE:{
			Result = Npc->Price;
			break;
		}

		case BEHAVIOUR_NODE_AMOUNT:{
			Result = Npc->Amount;
			break;
		}

		case BEHAVIOUR_NODE_SKILL:{
			int SkillNr = Node->Data;
			if(SkillNr == SKILL_LEVEL
					|| SkillNr == SKILL_MAGIC_LEVEL
					|| SkillNr == SKILL_HITPOINTS){
				Result = Interlocutor->Skills[SkillNr]->Get();
			}else if(SkillNr == SKILL_POISON
					|| SkillNr == SKILL_BURNING){
				Result = Interlocutor->Skills[SkillNr]->TimerValue();
			}else{
				error("TBehaviourDatabase::evaluate: Ungültiger Skill %d.\n", SkillNr);
			}
			break;
		}

		case BEHAVIOUR_NODE_COUNT:{
			int TypeID = this->evaluate(Npc, Node->Left, Parameters);
			Result = CountInventoryObjects(InterlocutorID, TypeID, Npc->Data);
			break;
		}

		case BEHAVIOUR_NODE_COUNTMONEY:{
			Result = CountInventoryMoney(InterlocutorID);
			break;
		}

		case BEHAVIOUR_NODE_TYPE:{
			Result = Npc->TypeID;
			break;
		}

		case BEHAVIOUR_NODE_DATA:{
			Result = Npc->Data;
			break;
		}

		case BEHAVIOUR_NODE_SPELLKNOWN:{
			int SpellNr = this->evaluate(Npc, Node->Left, Parameters);
			Result = Interlocutor->SpellKnown(SpellNr);
			break;
		}

		case BEHAVIOUR_NODE_SPELLLEVEL:{
			int SpellNr = this->evaluate(Npc, Node->Left, Parameters);
			Result = GetSpellLevel(SpellNr);
			break;
		}

		case BEHAVIOUR_NODE_RANDOM:{
			int Left = this->evaluate(Npc, Node->Left, Parameters);
			int Right = this->evaluate(Npc, Node->Right, Parameters);
			Result = random(Left, Right);
			break;
		}

		case BEHAVIOUR_NODE_QUESTVALUE:{
			int QuestNr = this->evaluate(Npc, Node->Left, Parameters);
			Result = Interlocutor->GetQuestValue(QuestNr);
			break;
		}

		default:{
			error("TBehaviourDatabase::evaluate: Ungültiger Knotentyp %d.\n", Node->Type);
			break;
		}
	}
	return Result;
}

// NOTE(fusion): These smaller functions were inside `TBehaviourDatabase::react`
// but I figured it would be better to pull them out for readability.
static bool CheckBehaviourProperty(int Property, SITUATION Situation, TPlayer *Interlocutor){
	if(Interlocutor == NULL){
		error("CheckBehaviourProperty: Interlocutor ist NULL.");
		return false;
	}

	bool Result = false;
	switch(Property){
		case BEHAVIOUR_PROPERTY_ADDRESS:
			Result = (Situation == ADDRESS || Situation == ADDRESSQUEUE);
			break;
		case BEHAVIOUR_PROPERTY_BUSY:
			Result = (Situation == BUSY);
			break;
		case BEHAVIOUR_PROPERTY_VANISH:
			Result = (Situation == VANISH);
			break;
		case BEHAVIOUR_PROPERTY_MALE:
			Result = (Interlocutor->Sex == 1);
			break;
		case BEHAVIOUR_PROPERTY_FEMALE:
			Result = (Interlocutor->Sex == 2);
			break;
		case BEHAVIOUR_PROPERTY_KNIGHT:
			Result = (Interlocutor->GetEffectiveProfession() == PROFESSION_KNIGHT);
			break;
		case BEHAVIOUR_PROPERTY_PALADIN:
			Result = (Interlocutor->GetEffectiveProfession() == PROFESSION_PALADIN);
			break;
		case BEHAVIOUR_PROPERTY_SORCERER:
			Result = (Interlocutor->GetEffectiveProfession() == PROFESSION_SORCERER);
			break;
		case BEHAVIOUR_PROPERTY_DRUID:
			Result = (Interlocutor->GetEffectiveProfession() == PROFESSION_DRUID);
			break;
		case BEHAVIOUR_PROPERTY_PREMIUM:
			Result = CheckRight(Interlocutor->ID, PREMIUM_ACCOUNT);
			break;
		case BEHAVIOUR_PROPERTY_PROMOTED:
			Result = Interlocutor->GetActivePromotion();
			break;
		case BEHAVIOUR_PROPERTY_PZBLOCK:
			Result = (Interlocutor->EarliestProtectionZoneRound > RoundNr);
			break;
		case BEHAVIOUR_PROPERTY_NONPVP:
			Result = (WorldType == NON_PVP);
			break;
		case BEHAVIOUR_PROPERTY_PVPENFORCED:
			Result = (WorldType == PVP_ENFORCED);
			break;
	}

	return Result;
}

static bool FormatNpcResponse(char *Buffer, int BufferSize,
		const char *Template, TNPC *Npc, TPlayer *Interlocutor){
	if(Buffer == NULL || BufferSize <= 0){
		error("FormatNpcResponse: Invalid response buffer.");
		return false;
	}

	if(Template == NULL || Template[0] == 0){
		error("FormatNpcResponse: Template is NULL or empty.");
		return false;
	}

	if(Npc == NULL){
		error("FormatNpcResponse: Npc is NULL.\n");
		return false;
	}

	if(Interlocutor == NULL){
		error("FormatNpcResponse: Interlocutor is NULL.\n");
		return false;
	}

	int WritePos = 0;
	int ReadPos = 0;
	while(Template[ReadPos] != 0){
		if(Template[ReadPos] == '%' && Template[ReadPos + 1] != 0){
			char Help[50] = {};
			switch(Template[ReadPos + 1]){
				case 'N': strcpy(Help, Interlocutor->Name); break;
				case 'A': sprintf(Help, "%d", Npc->Amount); break;
				case 'P': sprintf(Help, "%d", Npc->Price); break;
				case 'T':{
					int Hour, Minute;
					GetTime(&Hour, &Minute);
					if(Hour < 12){
						sprintf(Help, "%d:%.2d am", Hour, Minute);
					}else{
						sprintf(Help, "%d:%.2d pm", (Hour - 12), Minute);
					}
					break;
				}
			}

			// NOTE(fusion): Make sure we don't overflow the buffer.
			int HelpLen = strlen(Help);
			if(HelpLen > 0){
				if((WritePos + HelpLen) > BufferSize){
					HelpLen = BufferSize - WritePos;
				}

				if(HelpLen > 0){
					memcpy(&Buffer[WritePos], Help, HelpLen);
					WritePos += HelpLen;
				}
			}

			ReadPos += 2;
		}else{
			// NOTE(fusion): Make sure we don't overflow the buffer.
			if(WritePos < BufferSize){
				Buffer[WritePos] = Template[ReadPos];
				WritePos += 1;
			}

			ReadPos += 1;
		}
	}

	if(WritePos < BufferSize){
		Buffer[WritePos] = 0;
		return true;
	}else{
		Buffer[BufferSize - 1] = 0;
		return false;
	}
}

void TBehaviourDatabase::react(TNPC *Npc, const char *Text, SITUATION Situation){
	if(Npc == NULL){
		error("TBehaviourDatabase::react: NPC existiert nicht.\n");
		return;
	}

	if(Text == NULL){
		error("TBehaviourDatabase::react: Übergebener Text existiert nicht.\n");
		return;
	}

	uint32 InterlocutorID = Npc->Interlocutor;
	TPlayer *Interlocutor = GetPlayer(InterlocutorID);
	if(Interlocutor == NULL){
		error("TBehaviourDatabase::react: Gesprächspartner existiert nicht"
				" (Text=%s, Situation=%d).\n", Text, Situation);
		return;
	}

	int Parameters[2] = {-1, -1};
	int BestMatch = -1;
	int MaxConditions = -1;
	for(int BehaviourNr = 0;
			BehaviourNr < this->Behaviours;
			BehaviourNr += 1){
		TBehaviour *Behaviour = this->Behaviour.at(BehaviourNr);
		bool Match = true;
		bool ShortCircuit = false;
		const char *TextPtr = Text;
		for(int ConditionNr = 0;
				ConditionNr < Behaviour->Conditions && Match;
				ConditionNr += 1){
			TBehaviourCondition *Condition = Behaviour->Condition.at(ConditionNr);
			if(Condition->Type == BEHAVIOUR_CONDITION_SHORTCIRCUIT){
				ShortCircuit = true;
				break;
			}

			switch(Condition->Type){
				case BEHAVIOUR_CONDITION_TEXT:{
					const char *Pattern = GetDynamicString(Condition->Text);
					const char *Word = SearchForWord(Pattern, TextPtr);
					if(Word != NULL){
						TextPtr = Word + strlen(Pattern);
					}else{
						Match = false;
					}
					break;
				}

				case BEHAVIOUR_CONDITION_PROPERTY:{
					if(!CheckBehaviourProperty(Condition->Property, Situation, Interlocutor)){
						Match = false;
					}
					break;
				}

				case BEHAVIOUR_CONDITION_PARAMETER:{
					// TODO(fusion): The original function wouldn't check before
					// writing to `Parameters` which could be a problem.
					const char *Parameter = SearchForNumber(Condition->Number, TextPtr);
					if(Parameter != NULL
							&& Condition->Number >= 1
							&& Condition->Number <= NARRAY(Parameters)){
						Parameters[Condition->Number - 1] = atoi(Parameter);
						if(Parameters[Condition->Number - 1] > 500){
							Parameters[Condition->Number - 1] = 500;
						}

						// TODO(fusion): This could be problematic if the number
						// has more than one digit.
						TextPtr = Parameter + 1;
					}else{
						Match = false;
					}
					break;
				}

				case BEHAVIOUR_CONDITION_EXPRESSION:{
					if(this->evaluate(Npc, Condition->Expression, Parameters) == 0){
						Match = false;
					}
					break;
				}

				case BEHAVIOUR_CONDITION_SHORTCIRCUIT:{
					// TODO(fusion): Select current and stop loop.
					break;
				}
			}
		}

		if(ShortCircuit || (Match && Behaviour->Conditions > MaxConditions)){
			BestMatch = BehaviourNr;
			MaxConditions = Behaviour->Conditions;
			if(ShortCircuit){
				break;
			}
		}
	}

	if(BestMatch == -1){
		return;
	}

	if(Situation != BUSY){
		Npc->Topic = 0;
	}

	bool Repeat = false;
	bool StartToDo = false;
	int TalkDelay = 1000;
	int BehaviourNr = BestMatch;
	do{
		Repeat = false;
		TBehaviour *Behaviour = this->Behaviour.at(BehaviourNr);
		for(int ActionNr = 0;
				ActionNr < Behaviour->Actions;
				ActionNr += 1){
			TBehaviourAction *Action = Behaviour->Action.at(ActionNr);
			if(Action->Type == BEHAVIOUR_ACTION_REPEAT){
				if(BehaviourNr > 0){
					BehaviourNr -= 1;
					Repeat = true;
				}else{
					error("TBehaviourDatabase::react (9): Kein vorheriges Muster.\n");
				}
				break;
			}

			switch(Action->Type){
				case BEHAVIOUR_ACTION_REPLY:{
					char Response[256] = {};
					const char *Template = GetDynamicString(Action->Text);
					if(FormatNpcResponse(Response, sizeof(Response), Template, Npc, Interlocutor)){
						Npc->ToDoWait(TalkDelay);
						Npc->ToDoTalk(TALK_SAY, NULL, Response, false);
						TalkDelay += 3100 + (int)(strlen(Response) / 2) * 100;
						StartToDo = true;
					}else{
						Response[20] = 0;
						error("TBehaviourDatabase::react: Text von NPC %s wird zu lang (%s...).\n",
								Npc->Name, Response);
					}
					break;
				}

				case BEHAVIOUR_ACTION_SET_VARIABLE:{
					int Variable = Action->Number;
					int Value = this->evaluate(Npc, Action->Expression, Parameters);
					switch(Variable){
						case 1: Npc->Topic = Value; break;
						case 2: Npc->Price = Value; break;
						case 3: Npc->Amount = Value; break;
						case 4: Npc->TypeID = Value; break;
						case 6: Npc->Data = Value; break;
						default:{
							error("TBehaviourDatabase::react: Ungültige Variable.\n");
							break;
						}
					}
					break;
				}

				case BEHAVIOUR_ACTION_SET_SKILL:{
					int SkillNr = Action->Number;
					int Value = this->evaluate(Npc, Action->Expression, Parameters);
					if(SkillNr == SKILL_HITPOINTS){
						Interlocutor->Skills[SKILL_HITPOINTS]->Set(Value);
						if(Interlocutor->Skills[SKILL_HITPOINTS]->Get() <= 0){
							error("TBehaviourDatabase::react: NPC %s tötet Spieler.\n", Npc->Name);
						}
					}else{
						error("TBehaviourDatabase::react: Ungültiger Skill.\n");
					}
					break;
				}

				case BEHAVIOUR_ACTION_FUNCTION2:{
					int FunctionNr = Action->Number;
					int Param1 = this->evaluate(Npc, Action->Expression, Parameters);
					int Param2 = this->evaluate(Npc, Action->Expression2, Parameters);
					switch(FunctionNr){
						case 3: Interlocutor->SetQuestValue(Param1, Param2); break;
						default:{
							error("TBehaviourDatabase::react (4): Ungültige Funktionsnummer.\n");
							break;
						}
					}
					break;
				}

				case BEHAVIOUR_ACTION_FUNCTION1:{
					int FunctionNr = Action->Number;
					int Param = this->evaluate(Npc, Action->Expression, Parameters);
					switch(FunctionNr){
						case 1: GraphicalEffect(Npc->CrObject, Param); break;
						case 2: GraphicalEffect(Interlocutor->CrObject, Param); break;
						case 3: Interlocutor->SetProfession(Param); break;
						case 4: Interlocutor->LearnSpell(Param); break;
						case 5: CreateMonster(Param, Npc->posx, Npc->posy, Npc->posz, 0, 0, true); break;
						case 6: Npc->GiveTo(Param, Npc->Amount); break;
						case 7: Npc->GetFrom(Param, Npc->Amount); break;
						default:{
							error("TBehaviourDatabase::react (5): Ungültige Funktionsnummer.\n");
							break;
						}
					}
					break;
				}

				case BEHAVIOUR_ACTION_FUNCTION0:{
					int FunctionNr = Action->Number;
					switch(FunctionNr){
						case 1: Npc->GiveMoney(Npc->Price); break;
						case 2: Npc->GetMoney(Npc->Price); break;
						case 3:{
							if(Situation == BUSY){
								Npc->Enqueue(InterlocutorID, Text);
							}else{
								error("TBehaviourDatabase::react (6): falsche Situation für Aktion \"Queue\".\n");
							}
							break;
						}
						default:{
							error("TBehaviourDatabase::react (6): Ungültige Funktionsnummer.\n");
							break;
						}
					}
					break;
				}

				case BEHAVIOUR_ACTION_CHANGESTATE:{
					int NewState = Action->Number;
					if(!StartToDo){
						Npc->ChangeState((STATE)NewState, false);
						if(Situation != ADDRESSQUEUE){
							StartToDo = true;
						}else{
							error("TBehaviourDatabase::react: NPC %s reagiert nicht auf Anrede %s.\n",
									Npc->Name, Text);
						}
					}else{
						if(NewState == IDLE){
							Npc->ChangeState(LEAVING, false);
						}
						Npc->ToDoChangeState(NewState);
					}
					break;
				}

				case BEHAVIOUR_ACTION_SET_SKILL_TIMER:{
					int SkillNr = Action->Number;
					int Cycle = this->evaluate(Npc, Action->Expression, Parameters);
					int MaxCount = this->evaluate(Npc, Action->Expression2, Parameters);
					if(SkillNr == SKILL_POISON || SkillNr == SKILL_BURNING){
						if(Cycle == 0 || Interlocutor->Skills[SkillNr]->TimerValue() < Cycle){
							Interlocutor->SetTimer(SkillNr, Cycle, MaxCount, MaxCount, -1);
							if(SkillNr == SKILL_POISON){
								Interlocutor->PoisonDamageOrigin = 0;
							}else{
								Interlocutor->FireDamageOrigin = 0;
							}
						}
					}else{
						error("TBehaviourDatabase::react (8): Ungültiger Skill.\n");
					}
					break;
				}

				case BEHAVIOUR_ACTION_FUNCTION3:{
					int FunctionNr = Action->Number;
					int Param1 = this->evaluate(Npc, Action->Expression, Parameters);
					int Param2 = this->evaluate(Npc, Action->Expression2, Parameters);
					int Param3 = this->evaluate(Npc, Action->Expression3, Parameters);
					switch(FunctionNr){
						case 1:{
							print(3, "NPC teleportiert Gesprächspartner nach [%d,%d,%d].\n",
									Param1, Param2, Param3);
							try{
								Object Dest = GetMapContainer(Param1, Param2, Param3);
								Move(0, Interlocutor->CrObject, Dest, -1, false, NONE);
							}catch(RESULT r){
								error("TBehaviourDatabase::react (10): Exception %d.\n", r);
							}
							break;
						}

						case 2:{
							print(3, "NPC setzt Startkoordinate für Gesprächspartner auf [%d,%d,%d].\n",
									Param1, Param2, Param3);
							Interlocutor->startx = Param1;
							Interlocutor->starty = Param2;
							Interlocutor->startz = Param3;
							Interlocutor->SaveData();
							break;
						}

						default:{
							error("TBehaviourDatabase::react (10): Ungültige Unternummer.\n");
							break;
						}
					}
					break;
				}
			}
		}
	}while(Repeat);

	if(StartToDo){
		Npc->ToDoWait(TalkDelay);
		Npc->ToDoStart();
		if(Situation != BUSY){
			Npc->LastTalk = (TalkDelay / 1000) + RoundNr;
		}
	}
}

// Monster Homes
// =============================================================================
void StartMonsterhomeTimer(int Nr){
	if(Nr < 1 || Nr > Monsterhomes){
		error("StartMonsterhomeTimer: Ungültige Monsterhome-Nummer %d.\n", Nr);
		return;
	}

	TMonsterhome *MH = Monsterhome.at(Nr);
	if(MH->Timer > 0){
		error("StartMonsterhomeTimer: Zähler läuft schon.\n");
		return;
	}

	if(MH->ActMonsters >= MH->MaxMonsters){
		error("StartMonsterhomeTimer: Maximale Monsterzahl schon erreicht.\n");
		error("# Monsterhome mit Rasse %d an [%d,%d,%d]\n", MH->Race, MH->x, MH->y, MH->z);
		return;
	}

	int MaxTimer = MH->RegenerationTime;
	int NumPlayers = GetNumberOfPlayers();
	if(NumPlayers > 800){
		MaxTimer = (MaxTimer * 2) / 5;
	}else if(NumPlayers > 200){
		MaxTimer = (MaxTimer * 200) / ((NumPlayers / 2) + 100);
	}

	MH->Timer = random(MaxTimer / 2, MaxTimer);
}

void LoadMonsterhomes(void){
	print(1, "Initialisiere Monsterhomes ...\n");

	char FileName[4096];
	snprintf(FileName, sizeof(FileName), "%s/monster.db", DATAPATH);

	TReadScriptFile Script;
	Script.open(FileName);

	Monsterhomes = 0;
	while(true){
		int Race = Script.readNumber();
		if(Race == 0){
			break;
		}

		Monsterhomes += 1;
		TMonsterhome *MH = Monsterhome.at(Monsterhomes);
		MH->Race = Race;
		MH->x = Script.readNumber();
		MH->y = Script.readNumber();
		MH->z = Script.readNumber();
		MH->Radius = Script.readNumber();
		MH->MaxMonsters = Script.readNumber();
		MH->ActMonsters = 0;
		MH->RegenerationTime = Script.readNumber();
		MH->Timer = 0;

		if(!IsOnMap(MH->x, MH->y, MH->z)){
			print(1, "WARNUNG: Monsterhome [%d,%d,%d] befindet sich außerhalb der Karte.\n", MH->x, MH->y, MH->z);
		}
	}

	print(1, "%d Monsterhomes geladen.\n", Monsterhomes);
	Script.close();

	for(int i = 0; i < Monsterhomes; i += 1){
		TMonsterhome *MH = Monsterhome.at(i);
		for (int j = 0; j < MH->MaxMonsters; j += 1){
			int SpawnX = MH->x;
			int SpawnY = MH->y;
			int SpawnZ = MH->z;
			int SpawnRadius = MH->Radius;

			if(j == 0){
				if(SpawnRadius > 1){
					SpawnRadius = 1;
				}
			}else{
				if(SpawnRadius > 10){
					SpawnRadius = 10;
				}

				// NOTE(fusion): `SearchSpawnField` performs an extended search
				// if the radius is negative.
				SpawnRadius = -SpawnRadius;
			}

			if(SearchSpawnField(&SpawnX, &SpawnY, &SpawnZ, SpawnRadius, false)){
				CreateMonster(MH->Race, SpawnX, SpawnY, SpawnZ, i, 0, false);
				MH->ActMonsters += 1;
			}
		}

		if(MH->Timer > 0){
			error("LoadMonsterhomes: Timer läuft schon (Rasse %d an [%d,%d,%d]).\n",
					MH->Race, MH->x, MH->y, MH->z);
		}else if(MH->ActMonsters < MH->MaxMonsters){
			StartMonsterhomeTimer(i);
		}
	}
}

void ProcessMonsterhomes(void){
	for(int i = 1; i <= Monsterhomes; i += 1){
		TMonsterhome *MH = Monsterhome.at(i);

		ASSERT(MH->Timer >= 0);
		if(MH->Timer == 0){
			continue;
		}

		MH->Timer -= 1;
		if(MH->Timer > 0){
			continue;
		}

		int MaxRadius = MH->Radius;
		if(MaxRadius > 10){
			MaxRadius = 10;
		}

		TFindCreatures Search(MaxRadius + 9, MaxRadius + 7, MH->x, MH->y, FIND_PLAYERS);
		while(true){
			uint32 CharacterID = Search.getNext();
			if(CharacterID == 0){
				break;
			}

			TPlayer *Player = GetPlayer(CharacterID);
			if(Player == NULL){
				error("ProcessMonsterhomes: Kreatur existiert nicht.\n");
				break;
			}

			if(!Player->CanSeeFloor(MH->z)){
				continue;
			}

			int DistanceX = std::abs(Player->posx - MH->x);
			int DistanceY = std::abs(Player->posy - MH->y);
			int Radius = std::max<int>(DistanceX - 9, DistanceY - 7);
			if(Radius < MaxRadius){
				MaxRadius = Radius;
			}
		}

		if(MaxRadius >= 0){
			int SpawnX = MH->x;
			int SpawnY = MH->y;
			int SpawnZ = MH->z;
			int SpawnRadius = MaxRadius;

			if(MH->ActMonsters == 0){
				if(SpawnRadius > 1){
					SpawnRadius = 1;
				}
			}else{
				// NOTE(fusion): `SearchSpawnField` performs an extended search
				// if the radius is negative.
				SpawnRadius = -SpawnRadius;
			}

			if(SearchSpawnField(&SpawnX, &SpawnY, &SpawnZ, SpawnRadius, false)){
				CreateMonster(MH->Race, SpawnX, SpawnY, SpawnZ, i, 0, false);
				MH->ActMonsters += 1;

				// TODO(fusion): Not sure why this check is here.
				if(MH->Timer > 0){
					error("ProcessMonsterhomes: Timer läuft schon (Rasse %d an [%d,%d,%d]).\n",
							MH->Race, SpawnX, SpawnY, SpawnZ);
				}
			}
		}

		if(MH->ActMonsters < MH->MaxMonsters){
			StartMonsterhomeTimer(i);
		}
	}
}

void NotifyMonsterhomeOfDeath(int Nr){
	if(Nr < 1 || Nr > Monsterhomes){
		error("NotifyMonsterhomeOfDeath: Ungültige Monsterhome-Nummer %d.\n", Nr);
		return;
	}

	TMonsterhome *MH = Monsterhome.at(Nr);
	if(MH->ActMonsters < 1){
		error("NotifyMonsterhomeOfDeath: Monsterhome hat keine lebenden Kreaturen.\n");
		return;
	}

	MH->ActMonsters -= 1;
	if(MH->ActMonsters >= MH->MaxMonsters){
		error("NotifyMonsterhomeOfDeath: Monsterhome %d hatte zu viele Monster (%d statt %d).\n",
				Nr, (MH->ActMonsters + 1), MH->MaxMonsters);
		return;
	}

	if(MH->Timer == 0){
		StartMonsterhomeTimer(Nr);
	}
}

bool MonsterhomeInRange(int Nr, int x, int y, int z){
	if(Nr == 0){
		return true;
	}

	if(Nr < 1 || Nr > Monsterhomes){
		error("MonsterhomeInRange: Ungültige Monsterhome-Nummer %d.\n", Nr);
		return false;
	}

	TMonsterhome *MH = Monsterhome.at(Nr);
	return std::abs(z - MH->z) <= 2
		&& std::abs(x - MH->x) <= MH->Radius
		&& std::abs(y - MH->y) <= MH->Radius;
}

// TNonplayer
// =============================================================================
TNonplayer::TNonplayer(void) : TCreature() {
	this->State = SLEEPING;
}

TNonplayer::~TNonplayer(void){
	this->DelInList();
}

void TNonplayer::SetInList(void){
	TCreature::SetInCrList();
	*NonplayerList.at(FirstFreeNonplayer) = this;
	FirstFreeNonplayer += 1;
}

void TNonplayer::DelInList(void){
	bool Removed = false;
	for(int i = 0; i < FirstFreeNonplayer; i += 1){
		if(*NonplayerList.at(i) == this){
			FirstFreeNonplayer -= 1;
			*NonplayerList.at(i) = *NonplayerList.at(FirstFreeNonplayer);
			*NonplayerList.at(FirstFreeNonplayer) = NULL;
			Removed = true;
			break;
		}
	}

	if(!Removed){
		error("TNonplayer::DelInList: Kreatur nicht gefunden.\n");
	}
}

// TNPC
// =============================================================================
TNPC::TNPC(const char *FileName) :
		TNonplayer(),
		QueuedPlayers(0, 9, 10),
		QueuedAddresses(0, 9, 10)
{
	this->Type = NPC;
	this->posz = 255;
	this->Interlocutor = 0;
	this->Topic = 0;
	this->Price = 0;
	this->Amount = 1;
	this->TypeID = 0;
	this->Data = 0;
	this->LastTalk = 0;
	this->QueueLength = 0;
	this->Behaviour = NULL;

	TReadScriptFile Script;
	Script.open(FileName);
	while(true){
		Script.nextToken();
		if(Script.Token == ENDOFFILE){
			Script.close();
			break;
		}

		if(Script.Token != IDENTIFIER){
			Script.error("identifier expected");
		}

		char Identifier[MAX_IDENT_LENGTH];
		strcpy(Identifier, Script.getIdentifier());
		Script.readSymbol('=');

		if(strcmp(Identifier, "name") == 0){
			strcpy(this->Name, Script.readString());
		}else if(strcmp(Identifier, "sex") == 0){
			const char *Sex = Script.readIdentifier();
			if(strcmp(Sex, "male") == 0){
				this->Sex = 1;
			}else if(strcmp(Sex, "female") == 0){
				this->Sex = 2;
			}else{
				Script.error("unknown constant");
			}
		}else if(strcmp(Identifier, "race") == 0){
			this->Race = Script.readNumber();
			this->SetSkills(this->Race);
		}else if(strcmp(Identifier, "outfit") == 0){
			this->OrgOutfit = ReadOutfit(&Script);
			this->Outfit = this->OrgOutfit;
		}else if(strcmp(Identifier, "home") == 0){
			Script.readCoordinate(&this->startx, &this->starty, &this->startz);
			this->posx = this->startx;
			this->posy = this->starty;
			this->posz = this->startz;
		}else if(strcmp(Identifier, "radius") == 0){
			this->Radius = Script.readNumber();
		}else if(strcmp(Identifier, "gostrength") == 0){
			if(this->Race == 0){
				Script.error("gostrength before race in npc-script-file");
			}
			int GoStrength = Script.readNumber();
			this->Skills[SKILL_GO_STRENGTH]->Act = GoStrength;
			this->Skills[SKILL_GO_STRENGTH]->Max = GoStrength;
		}else if(strcmp(Identifier, "behaviour") == 0){
			if(this->Behaviour != NULL){
				Script.error("behaviour database specified twice for NPC");
			}
			this->Behaviour = new TBehaviourDatabase(&Script);
		}else{
			Script.error("unknown npc property");
		}
	}

	if(this->Name[0] == 0){
		throw "no name specified for NPC";
	}

	if(this->posz == 255){
		throw "no startpoint specified for NPC";
	}

	if(this->Behaviour == NULL){
		throw "no behaviour database specified for NPC";
	}

	if(!IsOnMap(this->posx, this->posy, this->posz)){
		print(1, "WARNUNG: NPC '%s' steht außerhalb der Karte.\n", this->Name);
		return;
	}

	this->SetID(0);
	this->SetInList();
	if(!this->SetOnMap()){
		print(1, "WARNUNG: Kann NPC \'%s\' nicht setzen.\n", this->Name);
		return;
	}

	this->ToDoYield();
}

TNPC::~TNPC(void){
	delete this->Behaviour;
}

bool TNPC::MovePossible(int x, int y, int z, bool Execute, bool Jump){
	return CoordinateFlag(x, y, z, BANK)
		&& !CoordinateFlag(x, y, z, UNPASS)
		&& !CoordinateFlag(x, y, z, AVOID)
		&& z == this->startz
		&& std::abs(x - this->startx) <= this->Radius
		&& std::abs(y - this->starty) <= this->Radius
		&& !IsHouse(x, y, z);
}

void TNPC::TalkStimulus(uint32 SpeakerID, const char *Text){
	if(SpeakerID == this->ID){
		return;
	}

	if(Text == NULL){
		error("TNPC::TalkStimulus: Übergebener Text existiert nicht.\n");
		return;
	}

	if(this->State == TALKING || this->QueueLength != 0){
		uint32 InterlocutorID = this->Interlocutor;
		if(SpeakerID == InterlocutorID){
			this->LastTalk = RoundNr;
			this->Behaviour->react(this, Text, DEFAULT);
		}else{
			this->Interlocutor = SpeakerID;
			this->Behaviour->react(this, Text, BUSY);
			this->Interlocutor = InterlocutorID;
		}
	}else{
		this->ToDoClear();
		this->ChangeState(TALKING, false);
		this->Interlocutor = SpeakerID;
		this->LastTalk = RoundNr;
		this->Behaviour->react(this, Text, ADDRESS);
		if(this->State == TALKING){
			this->TurnToInterlocutor();
		}
	}
}

void TNPC::DamageStimulus(uint32 AttackerID, int Damage, int DamageType){
	// no-op
}

void TNPC::IdleStimulus(void){
	if(this->State == TALKING){
		if((this->LastTalk + 30) > RoundNr){
			this->ToDoWait(2000);
			this->ToDoStart();
			return;
		}

		this->Behaviour->react(this, "", VANISH);
		this->ChangeState(IDLE, false);
	}

	if(this->State == IDLE){
		while(this->QueueLength > 0){
			uint32 InterlocutorID = *this->QueuedPlayers.at(0);
			uint32 Text = *this->QueuedAddresses.at(0);

			// NOTE(fusion): Ordered removal.
			this->QueueLength -= 1;
			for(int i = 0; i < this->QueueLength; i += 1){
				*this->QueuedPlayers.at(i) = *this->QueuedPlayers.at(i + 1);
				*this->QueuedAddresses.at(i) = *this->QueuedAddresses.at(i + 1);
			}

			this->ToDoClear();

			TCreature *Interlocutor = GetCreature(InterlocutorID);
			if(Interlocutor != NULL){
				this->ChangeState(TALKING, false);
				this->Interlocutor = InterlocutorID;
				this->Behaviour->react(this, GetDynamicString(Text), ADDRESSQUEUE);
			}else{
				error("TNPC::IdleStimulus: Gesprächspartner existiert nicht.\n");
			}

			DeleteDynamicString(Text);
			if(this->State == TALKING){
				this->TurnToInterlocutor();
				this->LastTalk = RoundNr;
				return;
			}
		}

		if(!this->LockToDo){
			TFindCreatures Search(10, 10, this->ID, FIND_PLAYERS);
			if(Search.getNext() == 0){
				this->ChangeState(SLEEPING, false);
				return;
			}

			bool DestFound = false;
			int DestX, DestY, DestZ;
			for(int i = 0; i < 10; i += 1){
				DestX = this->posx;
				DestY = this->posy;
				DestZ = this->posz;
				switch(rand() % 4){
					case 0: DestX -= 1; break;
					case 1: DestX += 1; break;
					case 2: DestY -= 1; break;
					case 3: DestY += 1; break;
				}

				if(this->MovePossible(DestX, DestY, DestZ, true, false)){
					DestFound = true;
					break;
				}
			}

			if(DestFound){
				try{
					this->ToDoGo(DestX, DestY, DestZ, true, INT_MAX);
					this->ToDoWait(2000);
					this->ToDoStart();
				}catch(RESULT r){
					error("TNPC::IdleStimulus: Exception %d!\n", r);
				}
			}else{
				this->ToDoWait(2000);
				this->ToDoStart();
			}
		}
	}
}

void TNPC::CreatureMoveStimulus(uint32 CreatureID, int Type){
	for(int i = 0; i < this->QueueLength; i += 1){
		uint32 QueuedPlayerID = *this->QueuedPlayers.at(i);
		if(CreatureID != QueuedPlayerID && CreatureID != this->ID){
			continue;
		}

		TCreature *QueuedPlayer = GetCreature(QueuedPlayerID);
		if(Type != OBJECT_DELETED
				&& QueuedPlayer->posz == this->posz
				&& std::abs(QueuedPlayer->posx - this->posx) < 5
				&& std::abs(QueuedPlayer->posy - this->posy) < 4){
			continue;
		}

		// NOTE(fusion): Ordered removal.
		this->QueueLength -= 1;
		for(int j = i; j < this->QueueLength; j += 1){
			*this->QueuedPlayers.at(j) = *this->QueuedPlayers.at(j + 1);
			*this->QueuedAddresses.at(j) = *this->QueuedAddresses.at(j + 1);
		}

		// NOTE(fusion): The ordered removal has shifted queued players back one
		// place so we need to check the current queue position again.
		i -= 1;
	}

	if(this->State == TALKING || this->State == LEAVING){
		if(CreatureID == this->Interlocutor || CreatureID == this->ID){
			TCreature *Interlocutor = GetCreature(this->Interlocutor);
			if(Interlocutor == NULL){
				error("TNPC::CreatureMoveStimulus: Gesprächspartner existiert nicht.\n");
				this->ChangeState(IDLE, true);
				return;
			}

			if(Type != OBJECT_DELETED){
				this->Rotate(Interlocutor);
			}

			if(this->State == TALKING){
				if(Type == OBJECT_DELETED
						|| Interlocutor->posz != this->posz
						|| std::abs(Interlocutor->posx - this->posx) >= 5
						|| std::abs(Interlocutor->posy - this->posy) >= 4){
					this->Behaviour->react(this, "", VANISH);
					this->ChangeState(IDLE, true);
				}
			}
		}
	}

	if(CreatureID != this->ID
			&& this->State == SLEEPING
			&& Type != OBJECT_DELETED){
		this->ChangeState(IDLE, true);
	}
}

void TNPC::GiveTo(ObjectType Type, int Amount){
	if(Amount == 0){
		return;
	}

	if(Type.isMapContainer() || Type.getName(1) == NULL){
		error("TNPC::GiveTo: %s will Objekte vom Typ %d erschaffen.\n",
				this->Name, Type.TypeID);
		return;
	}

	Log("npc", "%s -> %u: %d %s\n", this->Name, this->Interlocutor, Amount, Type.getName(1));

	if(Type.getFlag(CUMULATIVE)){
		while(Amount > 0){
			int StackAmount = std::min<int>(Amount, 100);
			CreateAtCreature(this->Interlocutor, Type, StackAmount);
			Amount -= StackAmount;
		}
	}else{
		while(Amount > 0){
			CreateAtCreature(this->Interlocutor, Type, this->Data);
			Amount -= 1;
		}
	}
}

void TNPC::GetFrom(ObjectType Type, int Amount){
	if(Amount > 0){
		Log("npc", "%s <- %u: %d %s\n", this->Name, this->Interlocutor, Amount, Type.getName(1));
		DeleteAtCreature(this->Interlocutor, Type, Amount, this->Data);
	}
}

void TNPC::GiveMoney(int Amount){
	int Crystal  = (Amount / 10000);
	int Platinum = (Amount % 10000) / 100;
	int Gold     = (Amount % 10000) % 100;
	this->GiveTo(GetSpecialObject(MONEY_TENTHOUSAND), Crystal);
	this->GiveTo(GetSpecialObject(MONEY_HUNDRED), Platinum);
	this->GiveTo(GetSpecialObject(MONEY_ONE), Gold);
}

void TNPC::GetMoney(int Amount){
	int Crystal  = CountInventoryObjects(this->Interlocutor, GetSpecialObject(MONEY_TENTHOUSAND), 0);
	int Platinum = CountInventoryObjects(this->Interlocutor, GetSpecialObject(MONEY_HUNDRED), 0);
	int Gold     = CountInventoryObjects(this->Interlocutor, GetSpecialObject(MONEY_ONE), 0);
	CalculateChange(Amount, &Gold, &Platinum, &Crystal);

	if(Gold > 0){
		this->GetFrom(GetSpecialObject(MONEY_ONE), Gold);
	}else if(Gold < 0){
		this->GiveTo(GetSpecialObject(MONEY_ONE), -Gold);
	}

	if(Platinum > 0){
		this->GetFrom(GetSpecialObject(MONEY_HUNDRED), Platinum);
	}else if(Platinum < 0){
		this->GiveTo(GetSpecialObject(MONEY_HUNDRED), -Platinum);
	}

	ASSERT(Crystal >= 0);
	if(Crystal > 0){
		this->GetFrom(GetSpecialObject(MONEY_TENTHOUSAND), Crystal);
	}
}

void TNPC::Enqueue(uint32 InterlocutorID, const char *Text){
	if(InterlocutorID == 0){
		error("TNPC::Enqueue: Gesprächspartner ist Null.\n");
		return;
	}

	if(Text == NULL){
		error("TNPC::Enqueue: Text ist NULL.\n");
		return;
	}

	bool AlreadyQueued = false;
	for(int i = 0; i < this->QueueLength; i += 1){
		if(*this->QueuedPlayers.at(i) == InterlocutorID){
			AlreadyQueued = true;
			break;
		}
	}

	if(!AlreadyQueued){
		*this->QueuedPlayers.at(this->QueueLength) = InterlocutorID;
		*this->QueuedAddresses.at(this->QueueLength) = AddDynamicString(Text);
		this->QueueLength += 1;
	}
}

void TNPC::TurnToInterlocutor(void){
	TCreature *Interlocutor = GetCreature(this->Interlocutor);
	if(Interlocutor == NULL){
		error("TNPC::TurnToInterlocutor: Gesprächspartner existiert nicht.\n");
		return;
	}

	this->Rotate(Interlocutor);
}

void TNPC::ChangeState(STATE NewState, bool Stimulus){
	this->State = NewState;
	if(Stimulus){
		this->ToDoYield();
	}
}

void ChangeNPCState(TCreature *Npc, int NewState, bool Stimulus){
	if(Npc == NULL){
		error("ChangeNPCState: npc ist NULL.\n");
		return;
	}

	if(Npc->Type != NPC){
		error("ChangeNPCState: npc ist kein NPC.\n");
		return;
	}

	((TNPC*)Npc)->ChangeState((STATE)NewState, Stimulus);
}

// TMonster
// =============================================================================
TMonster::TMonster(int Race, int x, int y, int z, int Home, uint32 MasterID) :
		TNonplayer()
{
	this->Type = MONSTER;
	this->Race = Race;
	this->startx = x;
	this->starty = y;
	this->startz = z;
	this->posx = x;
	this->posx = y;
	this->posx = z;
	this->State = IDLE;
	this->Home = Home;
	this->Master = MasterID;
	this->Target = 0;

	while(this->Master != 0){
		TCreature *Master = GetCreature(this->Master);
		// TODO(fusion): Do we actually want to return here?
		if(Master == NULL){
			error("TMonster::TMonster: Master existiert nicht.\n");
			return;
		}

		if(Master->Type != MONSTER || ((TMonster*)Master)->Master == 0){
			this->LifeEndRound = Master->LifeEndRound;
			Master->SummonedCreatures += 1;
			break;
		}

		error("TMonster::TMonster: Kinder dürfen keine eigenen Kinder erschaffen.\n");
		this->Master = ((TMonster*)Master)->Master;
	}

	if(RaceData[Race].Article[0] == 0){
		strcpy(this->Name, RaceData[Race].Name);
	}else{
		snprintf(this->Name, sizeof(this->Name), "%s %s",
				RaceData[Race].Article, RaceData[Race].Name);
	}

	this->SetSkills(Race);
	this->OrgOutfit = RaceData[Race].Outfit;
	this->Outfit = RaceData[Race].Outfit;
	this->SetID(0);
	this->SetInList();
	if(!this->SetOnMap()){
		error("TMonster::TMonster: Kann Monster nicht auf die Karte setzen.\n");
		return;
	}

	// TODO(fusion): This part is very similar to `ProcessMonsterRaids` when
	// adding extra items to spawned monsters.
	try{
		if(this->Master == 0 && RaceData[Race].Items > 0){
			Object Bag = Create(GetBodyContainer(this->ID, INVENTORY_BAG),
								GetSpecialObject(DEFAULT_CONTAINER),
								0);
			for(int i = 1; i <= RaceData[Race].Items; i += 1){
				TItemData *ItemData = RaceData[Race].Item.at(i);
				if(random(0, 999) > ItemData->Probability){
					continue;
				}

				ObjectType ItemType = ItemData->Type;
				int Amount = random(1, ItemData->Maximum);
				int Repeat = 1;
				if(!ItemType.getFlag(CUMULATIVE)){
					Repeat = Amount;
					Amount = 0;
				}

				for(int j = 0; j < Repeat; j += 1){
					Object Item = NONE;
					try{
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
							Item = CreateAtCreature(this->ID, ItemType, Amount);
						}
					}catch(RESULT r){
						error("TMonster::TMonster: Exception %d bei Rasse %d, ggf."
								" CarryStrength erhöhen.\n", r, Race);
						break;
					}

					// NOTE(fusion): Prevent items from being dropped onto the map.
					if(Item.getContainer().getObjectType().isMapContainer()){
						error("TMonster::TMonster: Objekt fällt auf die Karte."
								" CarryStrength für Rasse %d erhöhen.\n", Race);
						Delete(Item, -1);
					}
				}
			}

			// NOTE(fusion): `Bag` could be empty if we failed to add any
			// items to it in the loop above.
			if(GetFirstContainerObject(Bag) == NONE){
				Delete(Bag, -1);
			}
		}
	}catch(RESULT r){
		error("TMonster::TMonster: Exception %d bei Rasse %d.\n", r, Race);
	}

	this->Combat.CheckCombatValues();
	this->Combat.SetAttackMode(ATTACK_MODE_BALANCED);
	this->ToDoYield();
}

TMonster::~TMonster(void){
	if(!this->IsDead){
		GraphicalEffect(this->posx, this->posy, this->posz, EFFECT_POFF);
	}else if(this->Master == 0){
		this->Combat.DistributeExperiencePoints(RaceData[this->Race].ExperiencePoints);
	}

	if(this->Master != 0){
		TCreature *Master = GetCreature(this->Master);
		if(Master != NULL && Master->SummonedCreatures > 0){
			Master->SummonedCreatures -= 1;
		}
	}

	if(this->Home != 0){
		NotifyMonsterhomeOfDeath(this->Home);
	}
}

bool TMonster::MovePossible(int x, int y, int z, bool Execute, bool Jump){
	if(this->posz != z){
		error("TMonster::MovePossible: Prüfung über Ebenen hinweg ([%d,%d,%d] -> [%d,%d,%d]).\n",
				this->posx, this->posy, this->posz, x, y, z);
		return false;
	}

	if(this->State != ATTACKING && this->State != PANIC){
		if(!MonsterhomeInRange(this->Home, x, y, z)){
			return false;
		}

		// NOTE(fusion): Max distance.
		int Distance = std::max<int>(
				std::abs(x - this->posx),
				std::abs(y - this->posy));
		if(Distance > this->Radius){
			return false;
		}
	}

	if(this->Skills[SKILL_GO_STRENGTH]->Act < 0){
		error("TMonster::MovePossible: Monster %s an [%d,%d,%d] darf sich nicht bewegen.\n",
				this->Name, this->posx, this->posy, this->posz);
		return false;
	}

	if(IsProtectionZone(x, y, z) || IsHouse(x, y, z)){
		return false;
	}

	if(Execute && this->Master != 0 && this->State != ATTACKING && this->State != PANIC){
		TCreature *Master = GetCreature(this->Master);
		if(Master != NULL && Master->posz == this->posz){
			// NOTE(fusion): Manhattan distance.
			if((std::abs(Master->posx - this->posx) + std::abs(Master->posy - this->posy)) >  1
			&& (std::abs(Master->posx -          x) + std::abs(Master->posy -          y)) <= 1){
				return false;
			}
		}
	}

	// NOTE(fusion): Check destination and retry while we keep kicking blocking
	// objects away.
	for(int Attempt = 0; Attempt < 100; Attempt += 1){
		Object Obj = GetFirstObject(x, y, z);
		if(Obj == NONE || !Obj.getObjectType().getFlag(BANK)){
			return false;
		}

		while(Obj != NONE){
			ObjectType ObjType = Obj.getObjectType();
			if(ObjType.isCreatureContainer()){
				if(this->State != ATTACKING && this->State != PANIC){
					return false;
				}

				if(this->Target == 0){
					return false;
				}

				if(!RaceData[this->Race].KickCreatures){
					return false;
				}

				TCreature *Creature = GetCreature(Obj);
				if(Creature == NULL){
					error("TMonster::MovePossible: Kann Hindernis-Kreatur nicht identifizieren.\n");
					return false;
				}

				if(Creature->ID == this->Target || Creature->ID == this->Master){
					return false;
				}

				if(RaceData[Creature->Race].Unpushable){
					return false;
				}

				// TODO(fusion): Why?
				if(!RaceData[this->Race].SeeInvisible && Creature->IsInvisible()){
					return false;
				}

				if(Creature->Type == NPC){
					return false;
				}

				if(Creature->Type == PLAYER){
					if(this->Master != 0 || CheckRight(Creature->ID, IGNORED_BY_MONSTERS)){
						return false;
					}
				}

				if(Execute){
					if(Creature->Type == PLAYER){
						this->Target = 0;
						throw EXHAUSTED;
					}

					if(!this->KickCreature(Creature)){
						throw EXHAUSTED;
					}

					// NOTE(fusion): Break from the inner loop and retry.
					break;
				}
			}else{
				if(ObjType.getFlag(UNPASS)){
					if(ObjType.getFlag(UNMOVE) || !this->CanKickBoxes()){
						return false;
					}

					if(Execute){
						this->KickBoxes(Obj);

						// NOTE(fusion): Break from the inner loop and retry.
						break;
					}
				}

				if(ObjType.getFlag(AVOID)){
					int AvoidDamageTypes = (int)ObjType.getAttribute(AVOIDDAMAGETYPES);
					bool IgnoreHazard = (this->State == PANIC && AvoidDamageTypes != 0)
						|| (RaceData[this->Race].NoPoison  && AvoidDamageTypes == DAMAGE_POISON)
						|| (RaceData[this->Race].NoBurning && AvoidDamageTypes == DAMAGE_FIRE)
						|| (RaceData[this->Race].NoEnergy  && AvoidDamageTypes == DAMAGE_ENERGY);
					if(!IgnoreHazard){
						if(ObjType.getFlag(UNMOVE) || !this->CanKickBoxes()){
							return false;
						}

						if(Execute){
							this->KickBoxes(Obj);

							// NOTE(fusion): Break from the inner loop and retry.
							break;
						}
					}
				}
			}
			Obj = Obj.getNextObject();
		}

		if(Obj == NONE){
			return true;
		}
	}

	error("TMonster::MovePossible: Endlosschleife vermutet für %s an [%d,%d,%d].\n",
			this->Name, x, y, z);
	return false;
}

bool TMonster::IsPeaceful(void){
	return this->Master != 0
		&& IsCreaturePlayer(this->Master);
}

uint32 TMonster::GetMaster(void){
	return this->Master;
}

void TMonster::DamageStimulus(uint32 AttackerID, int Damage, int DamageType){
	if(AttackerID != 0 && Damage != 0){
		if(this->State == SLEEPING){
			if(this->Target == 0){
				this->State = PANIC;
			}else{
				this->State = UNDERATTACK;
			}
			this->ToDoYield();
		}else{
			if(this->Target == 0){
				this->State = PANIC;
			}else if(this->State == IDLE){
				this->State = UNDERATTACK;
			}
		}
	}
}

void TMonster::IdleStimulus(void){
	if(this->LockToDo || this->LoggingOut){
		return;
	}

	if(this->LifeEndRound != 0 && this->LifeEndRound <= RoundNr){
		print(3, "Lebenszeit für %s abgelaufen.\n", this->Name);
		this->StartLogout(true, true);
		this->State = SLEEPING;
		return;
	}

	if(this->Master != 0){
		TCreature *Master = GetCreature(this->Master);
		bool MasterIsPlayer = IsCreaturePlayer(this->Master);
		bool ShouldDespawn = false;
		if(Master == NULL){
			if(MasterIsPlayer){
				print(3, "Kreatur %s verliert ihren Spieler-Master.\n", this->Name);
			}else{
				print(3, "Kreatur %s verliert ihr Mutter-Monster.\n", this->Name);
			}
			ShouldDespawn = true;
		}else if(MasterIsPlayer && Master->SummonedCreatures == 0){
			print(3, "Spieler-Master hat sich eben wieder neu eingeloggt.\n");
			ShouldDespawn = true;
		}else if(!MasterIsPlayer && Master->posz != this->posz){
			print(3, "Mutter-Monster ist zu weit entfernt.\n");
			ShouldDespawn = true;
		}else if(std::abs(Master->posz - this->posz) > 1
				|| std::abs(Master->posx - this->posx) > 30
				|| std::abs(Master->posy - this->posy) > 30){
			if(MasterIsPlayer){
				print(3, "Spieler-Master ist zu weit entfernt.\n");
			}else{
				print(3, "Mutter-Monster ist zu weit entfernt.\n");
			}
			ShouldDespawn = true;
		}

		if(ShouldDespawn){
			if(MasterIsPlayer){
				this->StartLogout(true, true);
			}else{
				this->Kill();
			}
			this->State = SLEEPING;
			return;
		}

		if(Master->Combat.Following){
			this->Target = 0;
		}else{
			this->Target = Master->Combat.AttackDest;
		}

		if(this->Target == 0 || this->Target == this->ID){
			this->Target = this->Master;
		}

	}else{
		if(!MonsterhomeInRange(this->Home, this->posx, this->posy, this->posz)){
			TMonsterhome *MH = Monsterhome.at(this->Home);
			print(3, "%s an [%d,%d,%d] zu weit von [%d,%d,%d] entfernt.\n",
					this->Name, this->posx, this->posy, this->posz, MH->x, MH->y, MH->z);
			this->StartLogout(true, true);
			this->State = SLEEPING;
			return;
		}
	}

	if(this->Target != 0){
		TCreature *Target = GetCreature(this->Target);
		bool LoseTarget = (Target == NULL)
			// RANGE CHECK
			|| Target->posz != this->posz
			|| std::abs(Target->posx - this->posx) > 10
			|| std::abs(Target->posy - this->posy) > 10
			// FIELD CHECK
			|| IsProtectionZone(Target->posx, Target->posy, Target->posz)
			|| IsHouse(Target->posx, Target->posy, Target->posz)
			// INVISIBILITY CHECK
			|| (Target->IsInvisible() && !RaceData[this->Race].SeeInvisible)
			// LOSETARGET CHECK
			|| (this->Master == 0 && random(0, 99) < RaceData[this->Race].LoseTarget);
		if(LoseTarget){
			this->Target = 0;
		}
	}

	if(this->State != PANIC && this->State != UNDERATTACK){
		this->State = IDLE;
	}

	// NOTE(fusion): TALKING.
	if(RaceData[this->Race].Talks > 0 && (rand() % 50) == 0){
		int TalkNr = random(1, RaceData[this->Race].Talks);
		const char *Text = GetDynamicString(*RaceData[this->Race].Talk.at(TalkNr));
		if(Text != 0 && Text[0] != 0){
			// TODO(fusion): We were only checking for a `#` but it could be
			// problematic for any two character nul terminated string, since
			// adding 3 would make it point out of bounds. Poor string management
			// is a recurring theme.
			int Mode = TALK_ANIMAL_LOW;
			if(Text[0] == '#' && (Text[1] == 'y' || Text[1] == 'Y') && Text[2] == ' '){
				Mode = TALK_ANIMAL_LOUD;
				Text += 3;
			}

			try{
				Talk(this->ID, Mode, NULL, Text, false);
			}catch(RESULT r){
				error("TMonster::IdleStimulus: Exception %d bei Talk.\n", r);
			}
		}else{
			// TODO(fusion): The original wouldn't check if `Text` is actually
			// valid which could also be a problem.
			error("TMonster::IdleStimulus: Race %d talk entry %d is invalid.",
					this->Race, TalkNr);
		}
	}

	// NOTE(fusion): TARGETING.
	if(this->Target == 0){
		bool ShouldSleep = true;
		if(this->Master == 0){
			// IMPORTANT(fusion): We don't iterate over the last strategy on purpose.
			int Strategy = 0;
			int StrategyRoll = random(0, 99);
			while(Strategy < (NARRAY(TRaceData::Strategy) - 1)){
				if(StrategyRoll < RaceData[this->Race].Strategy[Strategy]){
					break;
				}
				StrategyRoll -= RaceData[this->Race].Strategy[Strategy];
				Strategy += 1;
			}

			int BestStrategyParam = INT_MIN;
			int BestTieBreaker = 0;
			TFindCreatures Search(12, 12, this->ID, FIND_PLAYERS | FIND_MONSTERS);
			while(true){
				uint32 TargetID = Search.getNext();
				if(TargetID == 0){
					break;
				}

				TCreature *Target = GetCreature(TargetID);
				if(Target == NULL){
					error("TMonster::IdleStimulus: Kreatur existiert nicht.\n");
					continue;
				}

				if(Target->Type == MONSTER && !((TMonster*)Target)->IsPlayerControlled()){
					continue;
				}

				if(Target->CanSeeFloor(this->posz)){
					ShouldSleep = false;
				}

				// TODO(fusion): This is quite similar to the conditions for losing
				// the target. Perhaps there is some common inlined function.
				int DistanceX = std::abs(Target->posx - this->posx);
				int DistanceY = std::abs(Target->posy - this->posy);
				if((Target->posz != this->posz || DistanceX > 10 || DistanceY > 10)
						||(Target->Type == PLAYER && CheckRight(Target->ID, IGNORED_BY_MONSTERS))
						|| (Target->IsInvisible() && !RaceData[this->Race].SeeInvisible)
						|| IsProtectionZone(Target->posx, Target->posy, Target->posz)
						|| IsHouse(Target->posx, Target->posy, Target->posz)
						|| std::abs(Target->posx - this->posx) > 10
						|| std::abs(Target->posy - this->posy) > 10){
					continue;
				}

				int StrategyParam = 0;
				if(Strategy == 0){ // STRATEGY_NEAREST
					StrategyParam = -(DistanceX + DistanceY);
				}else if(Strategy == 1){ // STRATEGY_LOWEST_HEALTH
					StrategyParam = -Target->Skills[SKILL_HITPOINTS]->Get();
				}else if(Strategy == 2){ // STRATEGY_MOST_DAMAGE
					StrategyParam = this->Combat.GetDamageByCreature(TargetID);
				}else if(Strategy == 3){ // STRATEGY_RANDOM
					// no-op
				}else{
					error("TMonster::IdleStimulus: Unbekannte Strategie %d.\n", Strategy);
				}

				// NOTE(fusion): We're looking for the creature that maximizes
				// the strategy parameter.
				int TieBreaker = random(0, 99);
				if(StrategyParam >= BestStrategyParam
				||(StrategyParam == BestStrategyParam && TieBreaker > BestTieBreaker)){
					this->Target = TargetID;
					BestTieBreaker = TieBreaker;
					BestStrategyParam = StrategyParam;
				}
			}
		}

		if(ShouldSleep && this->Target == 0
				&& this->State != UNDERATTACK
				&& this->State != PANIC){
			if(this->Master == 0){
				this->State = SLEEPING;
			}else{
				this->ToDoWait(1000);
				this->ToDoStart();
			}
			return;
		}

		if(this->State == PANIC && this->Target == 0){
			this->State = IDLE;
		}

		if(this->State == UNDERATTACK){
			this->State = IDLE;
		}
	}

	// NOTE(fusion): CASTING.
	// TODO(fusion): It is highly unlikely but we could cast all spells at once.
	// I'm not sure this is correct but I can see it happening.
	if(RaceData[this->Race].Spells > 0){
		TCreature *Target = GetCreature(this->Target);
		for(int SpellNr = 1;
				SpellNr <= RaceData[this->Race].Spells;
				SpellNr += 1){
			TSpellData *SpellData = RaceData[this->Race].Spell.at(SpellNr);
			if((rand() % SpellData->Delay) != 0){
				continue;
			}

			if(this->IsFleeing() && random(1, 3) != 1){
				continue;
			}

			TImpact *Impact = NULL;
			switch(SpellData->Impact){
				case IMPACT_DAMAGE:{
					int DamageType = SpellData->ImpactParam1;
					int Damage = SpellData->ImpactParam2;
					int Variation = SpellData->ImpactParam3;
					Damage = ComputeDamage(this, 0, Damage, Variation);
					Impact = new TDamageImpact(this, DamageType, Damage, true);
					break;
				}

				case IMPACT_FIELD:{
					int FieldType = SpellData->ImpactParam1;
					Impact = new TFieldImpact(this, FieldType);
					break;
				}

				case IMPACT_HEALING:{
					int Amount = SpellData->ImpactParam1;
					int Variation = SpellData->ImpactParam2;
					Amount = ComputeDamage(this, 0, Amount, Variation);
					Impact = new THealingImpact(this, Amount);
					break;
				}

				case IMPACT_SPEED:{
					int Percent = SpellData->ImpactParam1;
					int Variation = SpellData->ImpactParam2;
					int Duration = SpellData->ImpactParam3;
					Percent = ComputeDamage(this, 0, Percent, Variation);
					Impact = new TSpeedImpact(this, Percent, Duration);
					break;
				}

				case IMPACT_DRUNKEN:{
					int Strength = SpellData->ImpactParam1;
					int Variation = SpellData->ImpactParam2;
					int Duration = SpellData->ImpactParam3;
					Strength = ComputeDamage(this, 0, Strength, Variation);
					Impact = new TDrunkenImpact(this, Strength, Duration);
					break;
				}

				case IMPACT_STRENGTH:{
					int Skills = SpellData->ImpactParam1;
					int Percent = SpellData->ImpactParam2;
					int Variation = SpellData->ImpactParam3;
					int Duration = SpellData->ImpactParam4;
					Percent = ComputeDamage(this, 0, Percent, Variation);
					Impact = new TStrengthImpact(this, Skills, Percent, Duration);
					break;
				}

				case IMPACT_OUTFIT:{
					TOutfit Outfit = {};
					Outfit.OutfitID = SpellData->ImpactParam1;
					Outfit.ObjectType = SpellData->ImpactParam2;
					int Duration = SpellData->ImpactParam3;
					Impact = new TOutfitImpact(this, Outfit, Duration);
					break;
				}

				case IMPACT_SUMMON:{
					if(this->Master == 0){
						int SummonRace = SpellData->ImpactParam1;
						int MaxSummons = SpellData->ImpactParam2;
						Impact = new TSummonImpact(this, SummonRace, MaxSummons);
					}
					break;
				}
			}

			if(Impact != NULL){
				if(!Impact->isAggressive() || (this->Target != 0 && this->Target != this->Master)){
					switch(SpellData->Shape){
						case SHAPE_ACTOR:{
							int Effect = SpellData->ShapeParam1;
							ActorShapeSpell(this, Impact, Effect);
							break;
						}

						case SHAPE_VICTIM:{
							if(Target != NULL){
								this->Rotate(Target);

								int Range = SpellData->ShapeParam1;
								int Animation = SpellData->ShapeParam2;
								int Effect = SpellData->ShapeParam3;
								VictimShapeSpell(this, Target, Range,
										Animation, Impact, Effect);
							}
							break;
						}

						case SHAPE_ORIGIN:{
							int Radius = SpellData->ShapeParam1;
							int Effect = SpellData->ShapeParam2;
							OriginShapeSpell(this, Radius, Impact, Effect);
							break;
						}

						case SHAPE_DESTINATION:{
							if(Target != NULL){
								this->Rotate(Target);

								int Range = SpellData->ShapeParam1;
								int Animation = SpellData->ShapeParam2;
								int Radius = SpellData->ShapeParam3;
								int Effect  = SpellData->ShapeParam4;
								DestinationShapeSpell(this, Target, Range,
										Animation, Radius, Impact, Effect);
							}
							break;
						}

						case SHAPE_ANGLE:{
							if(Target != NULL){
								this->Rotate(Target);
							}

							int Angle = SpellData->ShapeParam1;
							int Range = SpellData->ShapeParam2;
							int Effect = SpellData->ShapeParam3;
							AngleShapeSpell(this, Angle, Range, Impact, Effect);
							break;
						}
					}
				}

				delete Impact;
			}
		}
	}

	// NOTE(fusion): WALKING. What was already bad got even worse.
	TCreature *Target = GetCreature(this->Target);
	if(this->Target != 0 && Target == NULL){
		error("TMonster::IdleStimulus: Kreatur existiert nicht.\n");
		this->Target = 0;
	}

	try{
		if(Target != NULL){
			if(this->IsFleeing()){
				int DestX, DestY, DestZ;
				if(SearchFlightField(this->ID, this->Target, &DestX, &DestY, &DestZ)){
					this->ToDoGo(DestX, DestY, DestZ, true, INT_MAX);
					this->ToDoStart();
					return;
				}
			}else if(this->Target == this->Master){
				// NOTE(fusion): Manhattan distance.
				int DistanceX = std::abs(Target->posx - this->posx);
				int DistanceY = std::abs(Target->posy - this->posy);
				int Distance = DistanceX + DistanceY;
				if(Distance > 1){
					if(Distance == 2){
						this->ToDoWait(1000);
					}else{
						if(Distance == 3){
							this->ToDoWait(1000);
						}
						this->ToDoGo(Target->posx, Target->posy, Target->posz, false, 3);
					}
					this->ToDoStart();
					return;
				}
			}else{
				if(this->Skills[SKILL_FIST]->Get() > 0 && this->State != PANIC){
					this->State = ATTACKING;
				}

				if(this->State == ATTACKING || this->State == PANIC){
					this->Combat.SetAttackDest(this->Target, false);
					this->Combat.SetChaseMode(CHASE_MODE_NONE);
				}

				// TODO(fusion): We're doing something similar to `TCombat::CanToDoAttack`
				// with added random steps around the attacking position. This function is
				// too convoluted and we should definitely split it into smaller ones. I
				// don't have a problem with large functions but this it too much.

				// NOTE(fusion): Max distance.
				int Distance = std::max<int>(
						std::abs(Target->posx - this->posx),
						std::abs(Target->posy - this->posy));
				if(!RaceData[this->Race].DistanceFighting
						|| !ThrowPossible(this->posx, this->posy, this->posz,
								Target->posx, Target->posy, Target->posz, 0)){
					this->Combat.SetChaseMode(CHASE_MODE_CLOSE);
					if(Distance > 1){
						// TODO(fusion): Not sure what is going on here. I think
						// it's because `ToDoAttack` with `CHASE_MODE_CLOSE` will
						// already take care of walking to the target?
						if(this->State != ATTACKING && this->State != PANIC){
							this->ToDoGo(Target->posx, Target->posy, Target->posz, false, 3);
						}
					}else{
						int DestX = this->posx;
						int DestY = this->posy;
						int DestZ = this->posz;
						switch(rand() % 5){
							case 0: DestX -= 1; break;
							case 1: DestX += 1; break;
							case 2: DestY -= 1; break;
							case 3: DestY += 1; break;
							case 4: break; // no step
						}

						int DestDistance = std::max<int>(
								std::abs(Target->posx - DestX),
								std::abs(Target->posy - DestY));
						if(DestDistance == 1 && this->MovePossible(DestX, DestY, DestZ, true, false)){
							this->ToDoGo(DestX, DestY, DestZ, true, INT_MAX);
						}

						if(this->State == PANIC){
							this->State = ATTACKING;
						}
					}
				}else{
					if(Distance < 4){
						int DestX, DestY, DestZ;
						if(SearchFlightField(this->ID, this->Target, &DestX, &DestY, &DestZ)){
							this->ToDoGo(DestX, DestY, DestZ, true, INT_MAX);
						}else{
							this->ToDoWait(1000);
						}
					}else if(Distance > 4){
						this->ToDoGo(Target->posx, Target->posy, Target->posz, false, Distance - 4);
					}else{
						int DestX = this->posx;
						int DestY = this->posy;
						int DestZ = this->posz;
						switch(rand() % 5){
							case 0: DestX -= 1; break;
							case 1: DestX += 1; break;
							case 2: DestY -= 1; break;
							case 3: DestY += 1; break;
							case 4: break; // no step
						}

						int DestDistance = std::max<int>(
								std::abs(Target->posx - DestX),
								std::abs(Target->posy - DestY));
						if(DestDistance == 4 && this->MovePossible(DestX, DestY, DestZ, true, false)){
							this->ToDoGo(DestX, DestY, DestZ, true, INT_MAX);
						}

						this->ToDoWait(1000);
					}
				}

				if(this->State == ATTACKING || this->State == PANIC){
					this->Rotate(Target);
					if(this->Master != this->Target){
						this->ToDoAttack();
					}else{
						error("TMonster::IdleStimulus: %s greift Master %u an (St=%d, T: %u).\n",
								this->Name, this->Master, this->State, this->Target);
					}
				}else{
					this->ToDoWait(1000);
				}

				this->ToDoStart();
				return;
			}
		}
	}catch(RESULT r){
		this->Target = 0;
		this->ToDoClear();
		if(r == EXHAUSTED){
			this->ToDoWait(1000);
			this->ToDoStart();
			return;
		}
	}

	// TODO(fusion): This part is similar to `TNPC::IdleStimulus`. Perhaps there
	// is a common function in `TNonplayer` that does this?
	bool FoundDest = false;
	int DestX, DestY, DestZ;
	for(int i = 0; i < 10; i += 1){
		DestX = this->posx;
		DestY = this->posy;
		DestZ = this->posz;
		switch(rand() % 4){
			case 0: DestX -= 1; break;
			case 1: DestX += 1; break;
			case 2: DestY -= 1; break;
			case 3: DestY += 1; break;
		}

		try{
			if(this->MovePossible(DestX, DestY, DestZ, true, false)){
				FoundDest = true;
				break;
			}
		}catch(RESULT r){
			// no-op
		}
	}

	if(FoundDest){
		try{
			this->ToDoGo(DestX, DestY, DestZ, true, INT_MAX);
			this->ToDoWait(1000);
			this->ToDoStart();
		}catch(RESULT r){
			error("TMonster::IdleStimulus: Exception %d.\n", r);
		}
	}else{
		this->ToDoWait(1000);
		this->ToDoStart();
	}
}

void TMonster::CreatureMoveStimulus(uint32 CreatureID, int Type){
	if(CreatureID == this->ID){
		if(this->State == SLEEPING && Type != OBJECT_DELETED){
			this->State = IDLE;
			this->ToDoYield();
		}
		return;
	}

	if(this->State == SLEEPING && Type != OBJECT_DELETED){
		TCreature *Creature = GetCreature(CreatureID);
		if(Creature == NULL){
			error("TMonster::CreatureMoveStimulus: Kreatur %u existiert nicht.\n", CreatureID);
			return;
		}

		if(Creature->Type == NPC){
			return;
		}

		if(Creature->Type == MONSTER && !((TMonster*)Creature)->IsPlayerControlled()){
			return;
		}

		this->State = IDLE;
		this->ToDoYield();
	}

	TCreature::CreatureMoveStimulus(CreatureID, Type);
}

bool TMonster::CanKickBoxes(void){
	bool Result = RaceData[this->Race].KickBoxes;
	if(!Result && this->Master != 0){
		TCreature *Master = GetCreature(this->Master);
		Result = (Master != NULL && Master->Type == MONSTER
					&& ((TMonster*)Master)->CanKickBoxes());
	}
	return Result;
}

void TMonster::KickBoxes(Object Obj){
	if(!Obj.exists()){
		error("TMonster::KickBoxes: Übergebenes Objekt existiert nicht.\n");
		return;
	}

	int ObjX, ObjY, ObjZ;
	GetObjectCoordinates(Obj, &ObjX, &ObjY, &ObjZ);

	try{
		bool ObjMoved = false;
		int OffsetX[4] = { 0,  0, -1,  1};
		int OffsetY[4] = {-1,  1,  0,  0};
		for(int i = 0; i < 4; i += 1){
			int DestX = ObjX + OffsetX[i];
			int DestY = ObjY + OffsetY[i];
			int DestZ = ObjZ;
			if(DestX == this->posx && DestY == this->posy){
				continue;
			}

			if(CoordinateFlag(DestX, DestY, DestZ, BANK)
			&& !CoordinateFlag(DestX, DestY, DestZ, UNPASS)){
				Object Dest = GetMapContainer(DestX, DestY, DestZ);
				::Move(this->ID, Obj, Dest, -1, false, NONE);
				ObjMoved = true;
				break;
			}
		}

		if(!ObjMoved){
			GraphicalEffect(Obj, EFFECT_BLOCK_HIT);
			Delete(Obj, -1);
		}
	}catch(RESULT r){
		error("TMonster::KickBoxes: Exception %d, Objekt %d.\n",
				r, Obj.getObjectType().TypeID);
		error("# eigene Position: [%d,%d,%d] - Objektposition: [%d,%d,%d]\n",
				this->posx, this->posy, this->posz, ObjX, ObjY, ObjZ);
	}
}

bool TMonster::KickCreature(TCreature *Creature){
	if(Creature == NULL){
		error("TMonster::KickCreature: Übergebene Kreatur existiert nicht.\n");
		return false;
	}

	if(Creature->Type != MONSTER){
		error("TMonster::KickCreature: Zu verschiebende Kreatur ist kein Monster.\n");
		return false;
	}

	print(3, "%s verschiebt %s.\n", this->Name, Creature->Name);

	// TODO(fusion): Declare these here so they can be used within the catch
	// block to print out what's on the last position we tried to move the
	// creature to.
	int DestX = Creature->posx;
	int DestY = Creature->posy;
	int DestZ = Creature->posz;
	bool CreatureMoved = false;
	try{
		int OffsetX[4] = { 0,  0, -1,  1};
		int OffsetY[4] = {-1,  1,  0,  0};
		for(int i = 0; i < 4; i += 1){
			DestX = Creature->posx + OffsetX[i];
			DestY = Creature->posy + OffsetY[i];
			if(DestX == this->posx && DestY == this->posy){
				continue;
			}

			if(Creature->MovePossible(DestX, DestY, DestZ, true, false)
					&& !CoordinateFlag(DestX, DestY, DestZ, AVOID)){
				Object Dest = GetMapContainer(DestX, DestY, DestZ);
				::Move(this->ID, Creature->CrObject, Dest, -1, false, NONE);
				CreatureMoved = true;
				break;
			}
		}

		if(!CreatureMoved){
			print(3, "Kein Platz zum Verschieben => Töten.\n");
			GraphicalEffect(Creature->CrObject, EFFECT_BLOCK_HIT);
			Creature->Combat.AddDamageToCombatList(this->ID,
					Creature->Skills[SKILL_HITPOINTS]->Get());
			Creature->Kill();
		}
	}catch(RESULT r){
		error("TMonster::KickCreature: Exception %d, Kreatur %s.\n",
				r, Creature->Name);
		error("# eigene Position: [%d,%d,%d] - Hindernisposition: [%d,%d,%d]\n",
				this->posx, this->posy, this->posz,
				Creature->posx, Creature->posy, Creature->posz);

		error("# Objekte auf Zielfeld [%d,%d,%d]:\n", DestX, DestY, DestZ);
		Object Obj = GetFirstObject(DestX, DestY, DestZ);
		while(Obj != NONE){
			error("# %d\n", Obj.getObjectType().TypeID);
			Obj = Obj.getNextObject();
		}
	}

	return CreatureMoved;
}

void TMonster::Convince(TCreature *NewMaster){
	if(NewMaster == NULL){
		error("TMonster::Convince: NewMaster ist NULL.");
		return;
	}

	if(this->Home != 0){
		NotifyMonsterhomeOfDeath(this->Home);
	}

	this->Home = 0;
	if(this->Master != 0){
		TCreature *OldMaster = GetCreature(this->Master);
		if(OldMaster != NULL){
			OldMaster->SummonedCreatures -= 1;
		}
	}

	this->Master = NewMaster->ID;
	NewMaster->SummonedCreatures += 1;

	this->ToDoClear();
	this->ToDoWait(100);
	this->ToDoStart();
}

void TMonster::SetTarget(TCreature *NewTarget){
	if(NewTarget == NULL){
		error("TMonster::SetTarget: NewTarget ist NULL.\n");
		return;
	}

	if(this->Master == 0){
		this->Target = NewTarget->ID;
		this->Rotate(NewTarget);
		this->ToDoYield();
	}
}

bool TMonster::IsPlayerControlled(void){
	bool Result = false;
	if(this->Master != 0){
		TCreature *Master = GetCreature(this->Master);
		Result = (Master && Master->Type == PLAYER);
	}
	return Result;
}

bool TMonster::IsFleeing(void){
	bool Result = false;
	if(this->Master == 0){
		int HitPoints = this->Skills[SKILL_HITPOINTS]->Get();
		int FleeThreshold = RaceData[this->Race].FleeThreshold;
		Result = HitPoints <= FleeThreshold;
	}
	return Result;
}

TCreature *CreateMonster(int Race, int x, int y, int z, int Home, uint32 MasterID, bool ShowEffect){
	if(!IsRaceValid(Race)){
		error("CreateMonster: Ungültige Rassennummer %d.\n", Race);
		return NULL;
	}

	if(RaceData[Race].Name[0] == 0){
		error("CreateMonster: Daten für Rasse %d nicht definiert.\n", Race);
		return NULL;
	}

	SearchFreeField(&x, &y, &z, 2, 0, false);
	TMonster *Monster = new TMonster(Race, x, y, z, Home, MasterID);
	if(ShowEffect){
		GraphicalEffect(x, y, z, EFFECT_ENERGY);
	}
	return Monster;
}

void ConvinceMonster(TCreature *Master, TCreature *Slave){
	if(Master == NULL){
		error("ConvinceMonster: Master existiert nicht.\n");
		return;
	}

	if(Slave == NULL){
		error("ConvinceMonster: Slave existiert nicht.\n");
		return;
	}

	if(Slave->Type != MONSTER){
		error("ConvinceMonster: Slave ist kein Monster.\n");
		return;
	}

	((TMonster*)Slave)->Convince(Master);
}

void ChallengeMonster(TCreature *Challenger, TCreature *Monster){
	if(Challenger == NULL){
		error("ChallengeMonster: Herausforderer existiert nicht.\n");
		return;
	}

	if(Monster == NULL){
		error("ChallengeMonster: Monster existiert nicht.\n");
		return;
	}

	if(Monster->Type != MONSTER){
		error("ChallengeMonster: Monster ist kein Monster.\n");
		return;
	}

	((TMonster*)Monster)->Target = Challenger->ID;
	Monster->Rotate(Challenger);
	Monster->ToDoYield();
}

// Initialization
// =============================================================================
void InitNPCs(void){
	DIR *NpcDir = opendir(NPCPATH);
	if(NpcDir == NULL){
		error("InitNPCs: Unterverzeichnis %s nicht gefunden\n", NPCPATH);
		throw "Cannot init NPCs";
	}

	char FileName[4096];
	while(dirent *DirEntry = readdir(NpcDir)){
		if(DirEntry->d_type != DT_REG){
			continue;
		}

		const char *FileExt = findLast(DirEntry->d_name, '.');
		if(FileExt == NULL || strcmp(FileExt, ".npc") != 0){
			continue;
		}

		snprintf(FileName, sizeof(FileName), "%s/%s", NPCPATH, DirEntry->d_name);
		new TNPC(FileName);
	}

	closedir(NpcDir);
}

void InitNonplayer(void){
	print(1, "Initializing Nonplayers ...\n");
	FirstFreeNonplayer = 0;
	InitNPCs();
	LoadMonsterhomes();
}

void ExitNonplayer(void){
	while(FirstFreeNonplayer > 0){
		// NOTE(fusion): Deleting a non player or any other creature will
		// automatically remove it from its related creature lists.
		TNonplayer *Nonplayer = *NonplayerList.at(0);
		delete Nonplayer;
	}

	// TODO(fusion): For any reason `BehaviourNodeTable` was originally a
	// pointer and it was deleted here. I doesn't really make a difference
	// because we'd usually exit after calling this function but if we wanted
	// to cleanup all memory, we'd need to add some `freeAll` method to the
	// `store` container.
}
