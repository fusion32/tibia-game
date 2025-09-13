#include "common.hh"
#include "config.hh"

#include <sys/stat.h>

static TErrorFunction *ErrorFunction;
static TPrintFunction *PrintFunction;
static char StandardLogFile[4096];

const char *Translate(const char *German, const char *English){
	return (Language == TLanguage::ENGLISH) ? English : German;
}

void SetErrorFunction(TErrorFunction *Function){
	ErrorFunction = Function;
}

void SetPrintFunction(TPrintFunction *Function){
	PrintFunction = Function;
}

void SetStandardLogFile(const char *FileName){
	strcpy(StandardLogFile, FileName);
}

void SilentHandler(const char *Text){
	// no-op
}

void SilentHandler(int Level, const char *Text){
	// no-op
}

void LogFileHandler(const char *Text){
	if(StandardLogFile[0] == 0){
		return;
	}

	FILE *File = fopen(StandardLogFile, "at");
	if(File != NULL){
		fprintf(File, "%s", Text);
		if(fclose(File) != 0){
			error(Translate("LogfileHandler: Fehler %d beim Schließen der Datei.\n",
							"LogfileHandler: Error %d closing file.\n"), errno);
		}
	}
}

void LogFileHandler(int Level, const char *Text){
	LogFileHandler(Text);
}

void error(const char *Text, ...){
	char s[1024];

	va_list ap;
	va_start(ap, Text);
	vsnprintf(s, sizeof(s), Text, ap);
	va_end(ap);

	if(ErrorFunction){
		ErrorFunction(s);
	}else{
		printf("%s", s);
	}
}

void print(int Level, const char *Text, ...){
	char s[1024];

	va_list ap;
	va_start(ap, Text);
	vsnprintf(s, sizeof(s), Text, ap);
	va_end(ap);

	if(PrintFunction){
		PrintFunction(Level, s);
	}else{
		printf("%s", s);
	}
}

int random(int Min, int Max){
	int Range = (Max - Min) + 1;
	int Result = Min;
	if(Range > 0){
		Result += rand() % Range;
	}
	return Result;
}

bool FileExists(const char *FileName){
	struct stat Buffer;
	bool Result = true;
	if(stat(FileName, &Buffer) != 0){
		if(errno != ENOENT){
			error(Translate("FileExists: Unerwarteter Fehlercode %d.\n",
							"FileExists: Unexpected error code %d.\n"), errno);
		}
		Result = false;
	}
	return Result;
}

// String Utility
// =============================================================================
bool isSpace(int c){
	return c == ' '
		|| c == '\t'
		|| c == '\n'
		|| c == '\r'
		|| c == '\v'
		|| c == '\f';
}

bool isAlpha(int c){
	// TODO(fusion): This is most likely wrong! We're assuming a direct conversion
	// from `char` to `int` which will cause sign extension for negative values. This
	// wouldn't be a problem if we expected to parse only streams of `char[]` but can
	// be problematic for the output of `getc` which returns bytes as `unsigned char`
	// converted to `int`.
	//	TLDR: The parameter `c` should be `uint8`.
	return ('A' <= c && c <= 'Z')
		|| ('a' <= c && c <= 'z')
		|| c == -0x1C	// E4 => ä
		|| c == -0x0A	// F6 => ö
		|| c == -0x04	// FC => ü
		|| c == -0x3C	// C4 => Ä
		|| c == -0x2A	// D6 => Ö
		|| c == -0x24	// DC => Ü
		|| c == -0x21;	// DF => ß
}

bool isEngAlpha(int c){
	return ('A' <= c && c <= 'Z')
		|| ('a' <= c && c <= 'z');
}

bool isDigit(int c){
	return ('0' <= c && c <= '9');
}

int toLower(int c){
	// TODO(fusion): Same problem as `isAlpha`.
	if(('A' <= c && c <= 'Z') || (0xC0 <= c && c <= 0xDE && c != 0xD7)){
		c += 32;
	}
	return c;
}

int toUpper(int c){
	// TODO(fusion): Same problem as `isAlpha`.
	if(('A' <= c && c <= 'Z') || (0xE0 <= c && c <= 0xFE && c != 0xF7)){
		c -= 32;
	}
	return c;
}

char *strLower(char *s){
	for(int i = 0; s[i] != 0; i += 1){
		s[i] = (char)toLower(s[i]);
	}
	return s;
}

char *strUpper(char *s){
	for(int i = 0; s[i] != 0; i += 1){
		s[i] = (char)toUpper(s[i]);
	}
	return s;
}

int stricmp(const char *s1, const char *s2, int Max /*= INT_MAX*/){
	for(int i = 0; i < Max; i += 1){
		int c1 = toLower(s1[i]);
		int c2 = toLower(s2[i]);
		if(c1 > c2){
			return 1;
		}else if(c1 < c2){
			return -1;
		}else{
			ASSERT(c1 == c2);
			if(c1 == 0){
				break;
			}
		}
	}
	return 0;
}

char *findFirst(char *s, char c){
	return strchr(s, (int)c);
}

char *findLast(char *s, char c){
	char *Current = s;
	char *Last = NULL;
	while(true){
		Current = strchr(Current, (int)c);
		if(Current == NULL)
			break;
		Last = Current;
		Current += 1; // skip character
	}
	return Last;
}

// BitSet Utility
// =============================================================================
bool CheckBitIndex(int BitSetBytes, int Index){
	return Index >= 0 && Index < (BitSetBytes * 8);
}

bool CheckBit(uint8 *BitSet, int Index){
	int ByteIndex = (int)(Index / 8);
	uint8 BitMask = (uint8)(1 << (Index % 8));
	return (BitSet[ByteIndex] & BitMask) != 0;
}

void SetBit(uint8 *BitSet, int Index){
	int ByteIndex = (int)(Index / 8);
	uint8 BitMask = (uint8)(1 << (Index % 8));
	BitSet[ByteIndex] |= BitMask;
}

void ClearBit(uint8 *BitSet, int Index){
	int ByteIndex = (int)(Index / 8);
	uint8 BitMask = (uint8)(1 << (Index % 8));
	BitSet[ByteIndex] &= ~BitMask;
}


// TReadStream
// =============================================================================
bool TReadStream::readFlag(void){
	return this->readByte() != 0;
}

uint16 TReadStream::readWord(void){
	// NOTE(fusion): Data is encoded in little endian.
	uint8 Byte0 = this->readByte();
	uint8 Byte1 = this->readByte();
	return ((uint16)Byte1 << 8) | (uint16)Byte0;
}

uint32 TReadStream::readQuad(void){
	// NOTE(fusion): Data is encoded in little endian.
	uint8 Byte0 = this->readByte();
	uint8 Byte1 = this->readByte();
	uint8 Byte2 = this->readByte();
	uint8 Byte3 = this->readByte();
	return ((uint32)Byte3 << 24) | ((uint32)Byte2 << 16)
			| ((uint32)Byte1 << 8) | (uint32)Byte0;
}

void TReadStream::readString(char *Buffer, int MaxLength){
	if(Buffer == NULL || MaxLength == 0){
		error(Translate("TReadStream::readString: Übergebener Puffer existiert nicht.\n",
						"TReadStream::readString: Passed buffer does not exist.\n"));
		throw "internal error";
	}

	int Length = (int)this->readWord();
	if(Length == 0xFFFF){
		Length = (int)this->readQuad();
	}

	if(Length > 0){
		if(MaxLength < 0 || MaxLength > Length){
			this->readBytes((uint8*)Buffer, Length);
			Buffer[Length] = 0;
		}else{
			this->readBytes((uint8*)Buffer, MaxLength - 1);
			this->skip(Length - MaxLength + 1);
			Buffer[MaxLength - 1] = 0;
		}
	}else{
		Buffer[0] = 0;
	}
}

void TReadStream::readBytes(uint8 *Buffer, int Count){
	if(Buffer == NULL){
		error(Translate("TReadStream::readBytes: Übergebener Puffer existiert nicht.\n",
						"TReadStream::readBytes: Passed buffer does not exist.\n"));
		throw "internal error";
	}

	for(int i = 0; i < Count; i += 1){
		Buffer[i] = this->readByte();
	}
}

// TReadBuffer
// =============================================================================
TReadBuffer::TReadBuffer(const uint8 *Data, int Size){
	if(Data == NULL){
		error(Translate("TReadBuffer::TReadBuffer: data ist NULL.\n",
						"TReadBuffer::TReadBuffer: data is NULL.\n"));
		Size = 0;
	}else if(Size < 0){
		error(Translate("TReadBuffer::TReadBuffer: Ungültige Datengröße %d.\n",
						"TReadBuffer::TReadBuffer: Invalid data size %d.\n"), Size);
		Size = 0;
	}

	this->Data = Data;
	this->Size = Size;
	this->Position = 0;
}

uint8 TReadBuffer::readByte(void){
	if((this->Size - this->Position) < 1){
		throw "buffer empty";
	}

	uint8 Byte = this->Data[this->Position];
	this->Position += 1;
	return Byte;
}

uint16 TReadBuffer::readWord(void){
	if((this->Size - this->Position) < 2){
		throw "buffer empty";
	}

	uint8 Byte0 = this->Data[this->Position];
	uint8 Byte1 = this->Data[this->Position + 1];
	this->Position += 2;
	return ((uint16)Byte1 << 8) | (uint16)Byte0;
}

uint32 TReadBuffer::readQuad(void){
	if((this->Size - this->Position) < 4){
		throw "buffer empty";
	}

	uint8 Byte0 = this->Data[this->Position];
	uint8 Byte1 = this->Data[this->Position + 1];
	uint8 Byte2 = this->Data[this->Position + 2];
	uint8 Byte3 = this->Data[this->Position + 3];
	this->Position += 4;
	return ((uint32)Byte3 << 24) | ((uint32)Byte2 << 16)
			| ((uint32)Byte1 << 8) | (uint32)Byte0;
}

void TReadBuffer::readBytes(uint8 *Buffer, int Count){
	if(Buffer == NULL || Count <= 0){
		error(Translate("TReadBuffer::readBytes: Übergebener Puffer existiert nicht.\n",
						"TReadBuffer::readBytes: Passed buffer does not exist.\n"));
		throw "buffer not existing";
	}

	if((this->Size - this->Position) < Count){
		throw "buffer empty";
	}

	memcpy(Buffer, &this->Data[this->Position], Count);
	this->Position += Count;
}

bool TReadBuffer::eof(void){
	return this->Size <= this->Position;
}

void TReadBuffer::skip(int Count){
	if((this->Size - this->Position) < Count){
		throw "buffer empty";
	}

	this->Position += Count;
}

// TWriteStream
// =============================================================================
void TWriteStream::writeFlag(bool Flag){
	this->writeByte((uint8)Flag);
}

void TWriteStream::writeWord(uint16 Word){
	this->writeByte((uint8)(Word));
	this->writeByte((uint8)(Word >> 8));
}

void TWriteStream::writeQuad(uint32 Quad){
	this->writeByte((uint8)(Quad));
	this->writeByte((uint8)(Quad >> 8));
	this->writeByte((uint8)(Quad >> 16));
	this->writeByte((uint8)(Quad >> 24));
}

void TWriteStream::writeString(const char *String){
	if(String == NULL){
		this->writeWord(0);
		return;
	}

	int StringLength = (int)strlen(String);
	ASSERT(StringLength >= 0);
	if(StringLength < 0xFFFF){
		this->writeWord((uint16)StringLength);
	}else{
		this->writeWord(0xFFFF);
		this->writeQuad((uint32)StringLength);
	}

	if(StringLength > 0){
		this->writeBytes((const uint8*)String, StringLength);
	}
}

void TWriteStream::writeBytes(const uint8 *Buffer, int Count){
	if(Buffer == NULL){
		error(Translate("TWriteStream::writeBytes: Übergebener Puffer existiert nicht.\n",
						"TWriteStream::writeBytes: Passed buffer does not exist.\n"));
		throw "internal error";
	}

	for(int i = 0; i < Count; i += 1){
		this->writeByte(Buffer[i]);
	}
}

// TWriteBuffer
// =============================================================================
TWriteBuffer::TWriteBuffer(uint8 *Data, int Size){
	if(Data == NULL){
		error(Translate("TWriteBuffer::TWriteBuffer: data ist NULL.\n",
						"TWriteBuffer::TWriteBuffer: data is NULL.\n"));
		Size = 0;
	}else if(Size < 0){
		error(Translate("TWriteBuffer::TWriteBuffer: Ungültige Datengröße %d.\n",
						"TWriteBuffer::TWriteBuffer: Invalid data size %d.\n"), Size);
		Size = 0;
	}

	this->Data = Data;
	this->Size = Size;
	this->Position = 0;
}

void TWriteBuffer::writeByte(uint8 Byte){
	if((this->Size - this->Position) < 1){
		throw "buffer full";
	}

	this->Data[this->Position] = Byte;
	this->Position += 1;
}

void TWriteBuffer::writeWord(uint16 Word){
	if((this->Size - this->Position) < 2){
		throw "buffer full";
	}

	this->Data[this->Position] = (uint8)(Word);
	this->Data[this->Position + 1] = (uint8)(Word >> 8);
	this->Position += 2;
}

void TWriteBuffer::writeQuad(uint32 Quad){
	if((this->Size - this->Position) < 4){
		throw "buffer full";
	}

	this->Data[this->Position] = (uint8)(Quad);
	this->Data[this->Position + 1] = (uint8)(Quad >> 8);
	this->Data[this->Position + 2] = (uint8)(Quad >> 16);
	this->Data[this->Position + 3] = (uint8)(Quad >> 24);
	this->Position += 4;
}

void TWriteBuffer::writeBytes(const uint8 *Buffer, int Count){
	if((this->Size - this->Position) < Count){
		throw "buffer full";
	}

	memcpy(&this->Data[this->Position], Buffer, Count);
	this->Position += Count;
}

// TDynamicWriteBuffer
// =============================================================================
TDynamicWriteBuffer::TDynamicWriteBuffer(int InitialSize)
	: TWriteBuffer(new uint8[InitialSize], InitialSize)
{
	// no-op
}

void TDynamicWriteBuffer::resizeBuffer(void){
	ASSERT(this->Size > 0);
	int Size = this->Size * 2;
	uint8 *Data = new uint8[Size];
	if(this->Data != NULL){
		memcpy(Data, this->Data, this->Size);
		delete[] this->Data;
	}

	this->Data = Data;
	this->Size = Size;
}

void TDynamicWriteBuffer::writeByte(uint8 Byte){
	while((this->Size - this->Position) < 1){
		this->resizeBuffer();
	}

	this->Data[this->Position] = Byte;
	this->Position += 1;
}

void TDynamicWriteBuffer::writeWord(uint16 Word){
	while((this->Size - this->Position) < 2){
		this->resizeBuffer();
	}

	this->Data[this->Position] = (uint8)(Word);
	this->Data[this->Position + 1] = (uint8)(Word >> 8);
	this->Position += 2;
}

void TDynamicWriteBuffer::writeQuad(uint32 Quad){
	while((this->Size - this->Position) < 4){
		this->resizeBuffer();
	}

	this->Data[this->Position] = (uint8)(Quad);
	this->Data[this->Position + 1] = (uint8)(Quad >> 8);
	this->Data[this->Position + 2] = (uint8)(Quad >> 16);
	this->Data[this->Position + 3] = (uint8)(Quad >> 24);
	this->Position += 4;
}

void TDynamicWriteBuffer::writeBytes(const uint8 *Buffer, int Count){
	while((this->Size - this->Position) < Count){
		this->resizeBuffer();
	}

	memcpy(&this->Data[this->Position], Buffer, Count);
	this->Position += Count;
}

TDynamicWriteBuffer::~TDynamicWriteBuffer(void){
	if(this->Data != NULL){
		delete[] this->Data;
	}
}
