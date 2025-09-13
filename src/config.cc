#include "config.hh"
#include "script.hh"

char BINPATH[4096];
char DATAPATH[4096];
char LOGPATH[4096];
char MAPPATH[4096];
char MONSTERPATH[4096];
char NPCPATH[4096];
char ORIGMAPPATH[4096];
char SAVEPATH[4096];
char USERPATH[4096];

int SHMKey;
int AdminPort;
int GamePort;
int QueryManagerPort;

char AdminAddress[16];
char GameAddress[16];
char QueryManagerAddress[16];
char QueryManagerAdminPW[9];
char QueryManagerGamePW[9];
char QueryManagerWebPW[9];

int DebugLevel;
TLanguage Language = TLanguage::GERMAN;
bool PrivateWorld;
TWorldType WorldType;
char WorldName[30];
int MaxPlayers;
int MaxNewbies;
int PremiumPlayerBuffer;
int PremiumNewbieBuffer;
int Beat;
int RebootTime;

TDatabaseSettings ADMIN_DATABASE;
TDatabaseSettings VOLATILE_DATABASE;
TDatabaseSettings WEB_DATABASE;
TDatabaseSettings FORUM_DATABASE;
TDatabaseSettings MANAGER_DATABASE;

int NumberOfQueryManagers;
TQueryManagerSettings QUERY_MANAGER[10];

static char PasswordKey[9] = "Pm-,o%yD";

static void DisguisePassword(char *Password, char *Key){
	if(Password == NULL){
		throw "password is null";
	}

	if(Key == NULL){
		throw "key for disguising password is null";
	}

	usize KeyLen = strlen(Key);
	usize PasswordLen = strlen(Password);
	if(KeyLen < PasswordLen){
		throw "key for disguising password is too short";
	}

	for(usize Index = 0; Index < PasswordLen; Index += 1){
		Password[Index] = (Key[Index] - Password[Index] + 0x5E) % 0x5E + 0x21;
	}
}

void ReadConfig(void){
	// TODO(fusion): We're not properly initializing these values, specially the
	// `TDatabaseSettings` ones. It is probably not that big of a deal anyway since
	// we only call this function once at startup and all this memory should be zero
	// initialized.
	PrivateWorld = false;
	strncpy(GameAddress, "0.0.0.0", 8);
	SHMKey = 0;
	QueryManagerPort = 0;
	strncpy(QueryManagerAddress, "0.0.0.0", 8);
	AdminPort = 0;
	GamePort = 0;
	strncpy(AdminAddress, "0.0.0.0", 8);
	MaxPlayers = 0;
	DebugLevel = 1;
	WorldType = NORMAL;
	BINPATH[0] = 0;
	DATAPATH[0] = 0;
	LOGPATH[0] = 0;
	MAPPATH[0] = 0;
	MONSTERPATH[0] = 0;
	NPCPATH[0] = 0;
	ORIGMAPPATH[0] = 0;
	SAVEPATH[0] = 0;
	USERPATH[0] = 0;
	QueryManagerAdminPW[0] = 0;
	QueryManagerGamePW[0] = 0;
	QueryManagerWebPW[0] = 0;
	MaxNewbies = 0;
	PremiumPlayerBuffer = 0;
	PremiumNewbieBuffer = 0;
	NumberOfQueryManagers = 0;
	Beat = 200;
	RebootTime = 540;
	ADMIN_DATABASE.Database[0] = 0;
	VOLATILE_DATABASE.Database[0] = 0;
	WEB_DATABASE.Database[0] = 0;
	FORUM_DATABASE.Database[0] = 0;
	MANAGER_DATABASE.Database[0] = 0;

	char FileName[4096];
#if 0
	if(const char *Home = getenv("home")){
		snprintf(FileName, sizeof(FileName), "%s/.tibia", Home);
	}else if(const char *Home = getenv("HOME")){
		snprintf(FileName, sizeof(FileName), "%s/.tibia", Home);
	}else{
		snprintf(FileName, sizeof(FileName), ".tibia");
	}
#else
	snprintf(FileName, sizeof(FileName), ".tibia");
#endif

	if(!FileExists(FileName)){
		throw "cannot find config-file";
	}

	TReadScriptFile Script;
	Script.open(FileName);
	while(true){
		Script.nextToken();
		if(Script.Token == ENDOFFILE){
			// NOTE(fusion): This is the only graceful exit point of the function.
			// Errors are transmitted back through exceptions.
			Script.close();
			return;
		}

		char Identifier[MAX_IDENT_LENGTH];
		strcpy(Identifier, Script.getIdentifier());
		Script.readSymbol('=');

		// TODO(fusion): Ughh... Get rid of all `strcpy`s. A malicious configuration
		// file could do a lot of damage.
		if(strcmp(Identifier, "binpath") == 0){
			strcpy(BINPATH, Script.readString());
		}else if(strcmp(Identifier, "mappath") == 0){
			strcpy(MAPPATH, Script.readString());
		}else if(strcmp(Identifier, "origmappath") == 0){
			strcpy(ORIGMAPPATH, Script.readString());
		}else if(strcmp(Identifier, "datapath") == 0){
			strcpy(DATAPATH, Script.readString());
		}else if(strcmp(Identifier, "monsterpath") == 0){
			strcpy(MONSTERPATH, Script.readString());
		}else if(strcmp(Identifier, "npcpath") == 0){
			strcpy(NPCPATH, Script.readString());
		}else if(strcmp(Identifier, "userpath") == 0){
			strcpy(USERPATH, Script.readString());
		}else if(strcmp(Identifier, "logpath") == 0){
			strcpy(LOGPATH, Script.readString());
		}else if(strcmp(Identifier, "savepath") == 0){
			strcpy(SAVEPATH, Script.readString());
		}else if(strcmp(Identifier, "shm") == 0){
			SHMKey = Script.readNumber();
		}else if(strcmp(Identifier, "adminport") == 0){
			AdminPort = Script.readNumber();
		}else if(strcmp(Identifier, "adminaddress") == 0){
			strcpy(AdminAddress, Script.readString());
		}else if(strcmp(Identifier, "querymanagerport") == 0){
			QueryManagerPort = Script.readNumber();
		}else if(strcmp(Identifier, "querymanageraddress") == 0){
			strcpy(QueryManagerAddress, Script.readString());
		}else if(strcmp(Identifier, "querymanageradminpw") == 0){
			strcpy(QueryManagerAdminPW, Script.readString());
		}else if(strcmp(Identifier, "querymanagergamepw") == 0){
			strcpy(QueryManagerGamePW, Script.readString());
		}else if(strcmp(Identifier, "querymanagerwebpw") == 0){
			strcpy(QueryManagerWebPW, Script.readString());
		}else if(strcmp(Identifier, "debuglevel") == 0){
			DebugLevel = Script.readNumber();
		}else if(strcmp(Identifier, "state") == 0){
			PrivateWorld = (strcmp(Script.readIdentifier(), "private") == 0);
		}else if(strcmp(Identifier, "world") == 0){
			strcpy(WorldName, Script.readString());
		}else if(strcmp(Identifier, "beat") == 0){
			Beat = Script.readNumber();
		}else if(strcmp(Identifier, "admindatabase") == 0){
			Script.readSymbol('(');
			strcpy(ADMIN_DATABASE.Product, Script.readIdentifier());
			Script.readSymbol(',');
			strcpy(ADMIN_DATABASE.Database, Script.readString());
			Script.readSymbol(',');
			strcpy(ADMIN_DATABASE.Login, Script.readString());
			Script.readSymbol(',');
			strcpy(ADMIN_DATABASE.Password, Script.readString());
			DisguisePassword(ADMIN_DATABASE.Password, PasswordKey);
			Script.readSymbol(',');
			strcpy(ADMIN_DATABASE.Host, Script.readString());
			Script.readSymbol(',');
			strcpy(ADMIN_DATABASE.Port, Script.readString());
			Script.readSymbol(')');
		}else if(strcmp(Identifier, "volatiledatabase") == 0){
			Script.readSymbol('(');
			strcpy(VOLATILE_DATABASE.Product, Script.readIdentifier());
			Script.readSymbol(',');
			strcpy(VOLATILE_DATABASE.Database, Script.readString());
			Script.readSymbol(',');
			strcpy(VOLATILE_DATABASE.Login, Script.readString());
			Script.readSymbol(',');
			strcpy(VOLATILE_DATABASE.Password, Script.readString());
			DisguisePassword(VOLATILE_DATABASE.Password, PasswordKey);
			Script.readSymbol(',');
			strcpy(VOLATILE_DATABASE.Host, Script.readString());
			Script.readSymbol(',');
			strcpy(VOLATILE_DATABASE.Port, Script.readString());
			Script.readSymbol(')');
		}else if(strcmp(Identifier, "webdatabase") == 0){
			Script.readSymbol('(');
			strcpy(WEB_DATABASE.Product, Script.readIdentifier());
			Script.readSymbol(',');
			strcpy(WEB_DATABASE.Database, Script.readString());
			Script.readSymbol(',');
			strcpy(WEB_DATABASE.Login, Script.readString());
			Script.readSymbol(',');
			strcpy(WEB_DATABASE.Password, Script.readString());
			DisguisePassword(WEB_DATABASE.Password, PasswordKey);
			Script.readSymbol(',');
			strcpy(WEB_DATABASE.Host, Script.readString());
			Script.readSymbol(',');
			strcpy(WEB_DATABASE.Port, Script.readString());
			Script.readSymbol(')');
		}else if(strcmp(Identifier, "forumdatabase") == 0){
			Script.readSymbol('(');
			strcpy(FORUM_DATABASE.Product, Script.readIdentifier());
			Script.readSymbol(',');
			strcpy(FORUM_DATABASE.Database, Script.readString());
			Script.readSymbol(',');
			strcpy(FORUM_DATABASE.Login, Script.readString());
			Script.readSymbol(',');
			strcpy(FORUM_DATABASE.Password, Script.readString());
			DisguisePassword(FORUM_DATABASE.Password, PasswordKey);
			Script.readSymbol(',');
			strcpy(FORUM_DATABASE.Host, Script.readString());
			Script.readSymbol(',');
			strcpy(FORUM_DATABASE.Port, Script.readString());
			Script.readSymbol(')');
		}else if(strcmp(Identifier, "managerdatabase") == 0){
			Script.readSymbol('(');
			strcpy(MANAGER_DATABASE.Product, Script.readIdentifier());
			Script.readSymbol(',');
			strcpy(MANAGER_DATABASE.Database, Script.readString());
			Script.readSymbol(',');
			strcpy(MANAGER_DATABASE.Login, Script.readString());
			Script.readSymbol(',');
			strcpy(MANAGER_DATABASE.Password, Script.readString());
			DisguisePassword(MANAGER_DATABASE.Password, PasswordKey);
			Script.readSymbol(',');
			strcpy(MANAGER_DATABASE.Host, Script.readString());
			Script.readSymbol(',');
			strcpy(MANAGER_DATABASE.Port, Script.readString());
			Script.readSymbol(')');
		}else if(strcmp(Identifier, "querymanager") == 0){
			Script.readSymbol('{');
			do{
				if(NumberOfQueryManagers >= NARRAY(QUERY_MANAGER)){
					Script.error("Cannot handle more query managers");
				}
				Script.readSymbol('(');
				strcpy(QUERY_MANAGER[NumberOfQueryManagers].Host, Script.readString());
				Script.readSymbol(',');
				QUERY_MANAGER[NumberOfQueryManagers].Port = Script.readNumber();
				Script.readSymbol(',');
				strcpy(QUERY_MANAGER[NumberOfQueryManagers].Password, Script.readString());
				DisguisePassword(QUERY_MANAGER[NumberOfQueryManagers].Password, PasswordKey);
				Script.readSymbol(')');
				NumberOfQueryManagers += 1;
			}while(Script.readSpecial() != '}');
		}else{
			// TODO(fusion):
			//error("Unknown configuration key \"%s\"", Identifier);
		}
	}
}
