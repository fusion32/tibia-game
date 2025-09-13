#include "common.hh"
#include "containers.hh"

struct TStaticStringTableBlock {
	int TotalTextLength;
	char Text[65536];
};

struct TDynamicStringTableBlock {
	int FreeEntries;
	int TotalTextLength;
	bool Dirty;
	uint8 EntryType[256];
	uint16 StringOffset[256];
	char Text[32768];
};

constexpr int StaticBlockSize = NARRAY(TStaticStringTableBlock::Text);
constexpr int DynamicBlockSize = NARRAY(TDynamicStringTableBlock::Text);
constexpr int DynamicBlockEntries = NARRAY(TDynamicStringTableBlock::EntryType);

enum : uint8 {
	DYNAMIC_STRING_FREE = 0,
	DYNAMIC_STRING_ALLOCATED = 1,
	DYNAMIC_STRING_DELETED = 2,
};

static list<TStaticStringTableBlock> StaticStringTable;
static list<TDynamicStringTableBlock> DynamicStringTable;

const char *AddStaticString(const char *String){
	int StringLen = (int)strlen(String);
	if((StringLen + 1) > StaticBlockSize){
		error(Translate("AddStaticString: String zu lang (%d).\n",
						"AddStaticString: String too long (%d)\n"), StringLen);
		return NULL;
	}

	if(StringLen == 0){
		return "";
	}

	listnode<TStaticStringTableBlock> *Node = StaticStringTable.firstNode;
	while(Node != NULL){
		if((Node->data.TotalTextLength + StringLen + 1) <= StaticBlockSize){
			break;
		}
		Node = Node->next;
	}

	if(Node == NULL){
		Node = StaticStringTable.append();
		memset(&Node->data, 0, sizeof(TStaticStringTableBlock));
	}

	TStaticStringTableBlock *Block = &Node->data;
	char *Result = &Block->Text[Block->TotalTextLength];
	memcpy(Result, String, StringLen + 1);
	Block->TotalTextLength += StringLen + 1;
	return Result;
}

uint32 AddDynamicString(const char *String){
	int StringLen = (int)strlen(String);
	if((StringLen + 1) > DynamicBlockSize){
		error(Translate("AddDynamicString: String zu lang (%d)\n",
						"AddDynamicString: String too long (%d)\n"), StringLen);
		return 0;
	}

	if(StringLen == 0){
		return 0;
	}

	int BlockIndex = 0;
	listnode<TDynamicStringTableBlock> *Node = DynamicStringTable.firstNode;
	while(Node != NULL){
		if(Node->data.FreeEntries > 0){
			if((Node->data.TotalTextLength + StringLen + 1) <= DynamicBlockSize){
				break;
			}
		}
		Node = Node->next;
		BlockIndex += 1;
	}

	if(Node == NULL){
		Node = DynamicStringTable.append();
		memset(&Node->data, 0, sizeof(TDynamicStringTableBlock));
		Node->data.FreeEntries = DynamicBlockEntries;
	}

	TDynamicStringTableBlock *Block = &Node->data;
	int EntryIndex = -1;
	for(int i = 0; i < DynamicBlockEntries; i += 1){
		if(Block->EntryType[i] == DYNAMIC_STRING_FREE){
			EntryIndex = i;
			break;
		}
	}

	if(EntryIndex == -1){
		error(Translate("AddDynamicString: Keinen freien Platz gefunden.\n",
						"AddDynamicString: No free space found.\n"));
		return 0;
	}

	int StringOffset = Block->TotalTextLength;
	Block->FreeEntries -= 1;
	Block->TotalTextLength += StringLen + 1;
	Block->EntryType[EntryIndex] = DYNAMIC_STRING_ALLOCATED;
	Block->StringOffset[EntryIndex] = (uint16)StringOffset;
	memcpy(&Block->Text[StringOffset], String, StringLen + 1);

	return (uint32)(1 + BlockIndex * DynamicBlockEntries + EntryIndex);
}

const char *GetDynamicString(uint32 Number){
	if(Number == 0){
		return NULL;
	}

	int BlockIndex = (int)(Number - 1) / DynamicBlockEntries;
	int EntryIndex = (int)(Number - 1) % DynamicBlockEntries;
	listnode<TDynamicStringTableBlock> *Node = DynamicStringTable.firstNode;
	while(BlockIndex > 0 && Node != NULL){
		Node = Node->next;
		BlockIndex -= 1;
	}

	if(Node == NULL){
		error(Translate("GetDynamicString: Block für String %u existiert nicht\n",
						"GetDynamicString: Block for string %u does not exist\n"), Number);
		return NULL;
	}

	TDynamicStringTableBlock *Block = &Node->data;
	if(Block->EntryType[EntryIndex] != DYNAMIC_STRING_ALLOCATED){
		error(Translate("GetDynamicString: Eintrag für String %u existiert nicht\n",
						"GetDynamicString: Entry for string %u does not exist\n"), Number);
		return NULL;
	}

	int StringOffset = (int)Block->StringOffset[EntryIndex];
	return &Block->Text[StringOffset];
}

void DeleteDynamicString(uint32 Number){
	if(Number == 0){
		return;
	}

	int BlockIndex = (int)(Number - 1) / DynamicBlockEntries;
	int EntryIndex = (int)(Number - 1) % DynamicBlockEntries;
	listnode<TDynamicStringTableBlock> *Node = DynamicStringTable.firstNode;
	while(BlockIndex > 0 && Node != NULL){
		Node = Node->next;
		BlockIndex -= 1;
	}

	if(Node == NULL){
		error(Translate("DeleteDynamicString: Block für String %u existiert nicht\n",
						"DeleteDynamicString: Block for string %u does not exist\n"), Number);
		return;
	}

	TDynamicStringTableBlock *Block = &Node->data;
	if(Block->EntryType[EntryIndex] != DYNAMIC_STRING_ALLOCATED){
		error(Translate("DeleteDynamicString: Eintrag für String %u existiert nicht\n",
						"DeleteDynamicString: Entry for string %u does not exist\n"), Number);
		return;
	}

	Block->EntryType[EntryIndex] = DYNAMIC_STRING_DELETED;
	Block->Dirty = true;
}

void CleanupDynamicStrings(void){
	// IMPORTANT(fusion): The way we manage dynamic strings also mean pointers
	// returned from `GetDynamicString` aren't stable.
	for(listnode<TDynamicStringTableBlock> *Node = DynamicStringTable.firstNode;
			Node != NULL;
			Node = Node->next){
		TDynamicStringTableBlock *Block = &Node->data;
		if(!Block->Dirty){
			continue;
		}

		for(int i = 0; i < DynamicBlockEntries; i += 1){
			if(Block->EntryType[i] == DYNAMIC_STRING_DELETED){
				int StringOffset = Block->StringOffset[i];
				char *String = &Block->Text[StringOffset];
				int StringSize = (int)strlen(String) + 1;
				int StringEnd = StringOffset + StringSize;
				Block->EntryType[i] = DYNAMIC_STRING_FREE;

				if(StringEnd > DynamicBlockSize){
					error(Translate("CleanupDynamicStrings: Stringende fehlt\n",
									"CleanupDynamicStrings: StringEnd is missing\n"));
					continue;
				}

				if(StringEnd < DynamicBlockSize){
					memmove(String, String + StringSize, DynamicBlockSize - StringEnd);
				}

				for(int j = 0; j < DynamicBlockEntries; j += 1){
					if(Block->EntryType[j] != DYNAMIC_STRING_FREE
							&& Block->StringOffset[j] > StringOffset){
						ASSERT(Block->StringOffset[j] >= StringEnd);
						Block->StringOffset[j] -= StringSize;
					}
				}
			}
		}
	}
}

void InitStrings(void){
	// no-op
}

void ExitStrings(void){
	// no-op
}

// String Utility
// =============================================================================
// TODO(fusion): I'm not sure why we have these separate from the other string
// utility defined in `utils.cc`. We should probably just move them all here.

bool IsCountable(const char *s){
	if(strncmp(s, "some ", 5) == 0){
		return false;
	}

	return strncmp(s, "a ", 2) == 0
		|| strncmp(s, "an ", 3) == 0;
}

const char *Plural(const char *s, int Count){
	static char ObjectNameString[50];

	ObjectNameString[0] = 0;
	if(s == NULL){
		error(Translate("Plural: Übergebener String existiert nicht.\n",
						"Plural: Passed string does not exist.\n"));
		return ObjectNameString;
	}

	strcpy(ObjectNameString, s);
	if(Count == 1 || !IsCountable(ObjectNameString)){
		return ObjectNameString;
	}

	// NOTE(fusion): `IsCountable` will only return true for `a XXXX` or `an XXXX`.
	if(ObjectNameString[1] == 'n'){
		memmove(&ObjectNameString[0],
				&ObjectNameString[3],
				sizeof(ObjectNameString) - 3);
	}else{
		memmove(&ObjectNameString[0],
				&ObjectNameString[2],
				sizeof(ObjectNameString) - 2);
	}

	if(Count < 0){
		return ObjectNameString;
	}

	char Help[sizeof(ObjectNameString)];
	if(char *Suffix = strstr(ObjectNameString, " of ")){
		strcpy(Help, Suffix);
		Suffix[0] = 0;
	}else{
		Help[0] = 0;
	}

	// TODO(fusion): This method for pluralization is limited and incomplete but
	// it probably works with the current set of object names.
	if(strcmp(ObjectNameString, "knife") == 0){
		strcpy(ObjectNameString, "knives");
	}else if(strcmp(ObjectNameString, "throwing knife") == 0){
		strcpy(ObjectNameString, "throwing knives");
	}else if(strcmp(ObjectNameString, "orc spearman") == 0){
		strcpy(ObjectNameString, "orc spearmen");
	}else if(strcmp(ObjectNameString, "deer") != 0
			&& strcmp(ObjectNameString, "dead deer") != 0
			&& strcmp(ObjectNameString, "sheep") != 0
			&& strcmp(ObjectNameString, "black sheep") != 0
			&& strcmp(ObjectNameString, "fish") != 0){
		int NameLen = (int)strlen(ObjectNameString);
		if(NameLen > 0){
			int Last = toLower(ObjectNameString[NameLen - 1]);
			int Prev = 0;
			if(NameLen > 1){
				Prev = toLower(ObjectNameString[NameLen - 2]);
			}

			if(Last == 's' || Last == 'x'
					|| (Last == 'h' && (Prev == 's' || Prev == 'c'))){
				strcat(ObjectNameString, "es");
			}else if(Last == 'y' && Prev != 'a' && Prev != 'e'
					&& Prev != 'i' && Prev != 'o' && Prev != 'u'){
				ObjectNameString[NameLen - 1] = 'i';
				strcat(ObjectNameString, "es");
			}else if(Last == 'f' && Prev == 'l'){
				ObjectNameString[NameLen - 1] = 'v';
				strcat(ObjectNameString, "es");
			}else if(Last == 'o'){
				strcat(ObjectNameString, "es");
			}else{
				strcat(ObjectNameString, "s");
			}
		}
	}

	strcat(ObjectNameString, Help);
	if(Count != 0){
		strcpy(Help, ObjectNameString);
		snprintf(ObjectNameString, sizeof(ObjectNameString), "%d %s", Count, Help);
	}

	return ObjectNameString;
}

const char *SearchForWord(const char *Pattern, const char *Text){
	if(Pattern == NULL || Pattern[0] == 0){
		error(Translate("SearchForWord: Übergebenes Suchwort existiert nicht.\n",
						"SearchForWord: Passed search term does not exist.\n"));
		return NULL;
	}

	if(Text == NULL){
		error(Translate("SearchForWord: Übergebener Text existiert nicht.\n",
						"SearchForWord: Passed text does not exist.\n"));
		return NULL;
	}

	int PatternLength = (int)strlen(Pattern);
	bool WholeWord = false;
	if(Pattern[PatternLength - 1] == '$'){
		PatternLength -= 1;
		WholeWord = true;
	}

	bool WordStart = true;
	const char *Match = NULL;
	for(int i = 0; Text[i] != 0; i += 1){
		// TODO(fusion): The original function would only check for spaces to
		// determine the beginning of a word which can be problematic for sentences
		// with punctuation.
		//if(isSpace(Text[i])){
		if(!isAlpha(Text[i]) && !isDigit(Text[i])){
			WordStart = true;
		}else if(WordStart){
			int j = 0;
			while(j < PatternLength){
				if(toLower(Pattern[j]) != toLower(Text[i + j])){
					break;
				}
				j += 1;
			}

			if(j == PatternLength){
				if(!WholeWord || (!isAlpha(Text[i + j]) && !isDigit(Text[i + j]))){
					Match = &Text[i];
					break;
				}
			}

			WordStart = false;
		}
	}

	return Match;
}

const char *SearchForNumber(int Count, const char *Text){
	if(Count < 1){
		error(Translate("SearchForNumber: Illegale Suchnummer %d.\n",
						"SearchForNumber: Illegal search number %d.\n"), Count);
		return NULL;
	}

	if(Text == NULL){
		error(Translate("SearchForNumber: Übergebener Text existiert nicht.\n",
						"SearchForNumber: Passed text does not exist.\n"));
		return NULL;
	}

	bool WordStart = true;
	const char *Match = NULL;
	for(int i = 0; Text[i] != 0; i += 1){
		// TODO(fusion): Same as `SearchForWord`.
		//if(isSpace(Text[i])){
		if(!isAlpha(Text[i]) && !isDigit(Text[i])){
			WordStart = true;
		}else if(WordStart){
			int j = 0;
			while(isDigit(Text[i + j])){
				j += 1;
			}

			// TODO(fusion): Same as `SearchForWord`.
			//if(j > 0 && (Text[i + j] == 0 || isSpace(Text[i + j]))){
			if(j > 0 && !isAlpha(Text[i + j])){
				Count -= 1;
				if(Count == 0){
					Match = &Text[i];
					break;
				}
			}

			WordStart = false;
		}
	}

	return Match;
}

bool MatchString(const char *Pattern, const char *String){
	if(Pattern == NULL){
		error(Translate("MatchString: Pattern ist NULL.\n",
						"MatchString: Pattern is NULL.\n"));
		return false;
	}

	if(String == NULL){
		error(Translate("MatchString: String ist NULL.\n",
						"MatchString: String is NULL.\n"));
		return false;
	}

	// NOTE(fusion): The original function was allocating an integer table on
	// the stack, with dimensions (StringLength + 1) x (PatternLength + 1) to
	// compute the edit distance. This made the decompiled version difficult
	// to read and could potentially blow up the stack for large inputs.
	//	We'll use a fixed buffer instead, that will store the distances for the
	// last and current table rows which is all is needed to compute the same
	// edit distance.

	int StringLength = (int)strlen(String);
	int PatternLength = (int)strlen(Pattern);

	constexpr int MaxStringLength = 255;
	if(StringLength > MaxStringLength){
		error("MatchString: String is too large (Length = %d, MaxLength = %d)\n",
				StringLength, MaxStringLength);
		return false;
	}

	int DistanceBuffer[2 * (MaxStringLength + 1)] = {};
	int *PrevRow = &DistanceBuffer[0];
	int *CurRow = &DistanceBuffer[StringLength + 1];
	for(int i = 0; i < (StringLength + 1); i += 1){
		PrevRow[i] = i;
	}

	for(int i = 0; i < PatternLength; i += 1){
		int Ins = (Pattern[i] != '*');
		int Del = (Pattern[i] != '*');
		CurRow[0] = PrevRow[0] + Del;
		for(int j = 0; j < StringLength; j += 1){
			int Edit = (Pattern[i] != '?'
				&& toLower(Pattern[i]) != toLower(String[j]));

			int Min = PrevRow[j + 1] + Ins;
			if(Min > (PrevRow[j] + Edit)){
				Min = PrevRow[j] + Edit;
			}
			if(Min > (CurRow[j] + Del)){
				Min = CurRow[j] + Del;
			}

			CurRow[j + 1] = Min;
		}

		std::swap(PrevRow, CurRow);
	}

	return PrevRow[StringLength] == 0;
}

// TODO(fusion): This function is unsafe like `strcpy`.
void AddSlashes(char *Destination, const char *Source){
	if(Source == NULL){
		error(Translate("AddSlashes: Source ist NULL.\n",
						"AddSlashes: Source is NULL.\n"));
		return;
	}

	if(Destination == NULL){
		error(Translate("AddSlashes: Destination ist NULL.\n",
						"AddSlashes: Destination is NULL.\n"));
		return;
	}

	int ReadIndex = 0;
	int WriteIndex = 0;
	while(Source[ReadIndex] != 0){
		if(Source[ReadIndex] == '"' || Source[ReadIndex] == '#'
		|| Source[ReadIndex] == '\'' || Source[ReadIndex] == '\\'){
			Destination[WriteIndex] = '\\';
			WriteIndex += 1;
		}

		Destination[WriteIndex] = Source[ReadIndex];
		WriteIndex += 1;
		ReadIndex += 1;
	}

	Destination[WriteIndex] = 0;
	WriteIndex += 1;
}

void Trim(char *Text){
	if(Text == NULL){
		error(Translate("Trim: Text ist NULL.\n",
						"Trim: Text is NULL.\n"));
		return;
	}

	// NOTE(fusion): `Start` is inclusive.
	int Start = 0;
	while(Text[Start] != 0 && isSpace(Text[Start])){
		Start += 1;
	}

	int OldLength = Start;
	while(Text[OldLength] != 0){
		OldLength += 1;
	}

	if(Start < OldLength){
		// NOTE(fusion): `End` is inclusive.
		int End = OldLength - 1;
		while(End > Start && isSpace(Text[End])){
			End -= 1;
		}

		int NewLength = End - Start + 1;
		memmove(&Text[0], &Text[Start], NewLength);
		Text[NewLength] = 0;
	}else{
		Text[0] = 0;
	}
}

// TODO(fusion): This is a copy of the function above, except that we write to
// `Destination` instead of modifying the original string. It is unsafe as `strcpy`.
void Trim(char *Destination, const char *Source){
	if(Source == NULL){
		error(Translate("Trim: Source ist NULL.\n",
						"Trim: Source is NULL.\n"));
		return;
	}

	if(Destination == NULL){
		error(Translate("Trim: Destination ist NULL.\n",
						"Trim: Destination is NULL.\n"));
		return;
	}

	int Start = 0;
	while(Source[Start] != 0 && isSpace(Source[Start])){
		Start += 1;
	}

	int OldLength = Start;
	while(Source[OldLength] != 0){
		OldLength += 1;
	}

	if(Start < OldLength){
		// NOTE(fusion): `End` is inclusive.
		int End = OldLength - 1;
		while(End > Start && isSpace(Source[End])){
			End -= 1;
		}

		int NewLength = End - Start + 1;
		memcpy(&Destination[0], &Source[Start], NewLength);
		Destination[NewLength] = 0;
	}else{
		Destination[0] = 0;
	}
}

void ChangeWildcards(char *String){
	if(String == NULL){
		error(Translate("ChangeWildcards: String ist NULL.\n",
						"ChangeWildcards: String is NULL.\n"));
		return;
	}

	for(int i = 0; String[i] != 0; i += 1){
		if(String[i] == '*'){
			String[i] = '%';
		}else if(String[i] == '?'){
			String[i] = '_';
		}
	}
}

char *Capitals(char *Text){
	bool WordStart = true;
	for(int i = 0; Text[i] != 0; i += 1){
		// TODO(fusion): The original function wouldn't consider digits which
		// I'd argue is a mistake if there is a number prefixing a word.
		if(!isAlpha(Text[i]) && !isDigit(Text[i])){
			WordStart = true;
		}else if(WordStart){
			if(isAlpha(Text[i])){
				Text[i] = toUpper(Text[i]);
			}
			WordStart = false;
		}
	}
	return Text;
}
