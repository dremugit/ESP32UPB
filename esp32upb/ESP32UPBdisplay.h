#define ESP32UPBdisplayversion 8

// these are functions used at various levels in the UPB emulation
// split out from the main UPB core, either to simplify that code
// or so that simpler code, like the EEPROM write, can use them directly
// without incurring the complexity of the entire UPBcore

// prototypes

bool isprintable(char thing);
void displayMAC(uint8_t address[]);
void printableprint(char thing[], uint16_t arraylength, bool newline);
uint8_t hexcharstobyte(char thing1, char thing2);
uint8_t hexchartobyte(char thing);
char bytetohexchar(uint8_t hexdata, uint8_t nibble);
uint8_t binStringToInt(String binstring);
uint8_t hexStringToInt(String hexstring);
String makeNiceHexString(uint8_t intvalue);

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

// returns true for 0-9, a-z, A-Z, false for other values that are presumably unprintable

bool isprintable(char thing)
{
  return (
  (thing==' ')
	||
  ((thing>='a') && (thing<='z'))
  ||
  ((thing>='A') && (thing<='Z'))
  ||
  ((thing>='0') && (thing<='9'))
  ||
  ((thing>=0x21) && (thing<=0x40))
    );
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

// as above for bytes

bool isprintablebyte(uint8_t thing)
{
  return (
  (thing==' ')
	||
  ((thing>='a') && (thing<='z'))
  ||
  ((thing>='A') && (thing<='Z'))
  ||
  ((thing>='0') && (thing<='9'))
    );
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void displayMAC(uint8_t address[])
{
  for (uint8_t i=0; i<6; i++)
  {
		Serial.printf("%02X",address[i]);
    if (i<5) {Serial.print(":");}
  }
  Serial.println();
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

// print a zero-terminated string, but use [%02X] for unprintable chars
// since the array could contain zeros, must pass its intended length
// as sizeof(array) is fixed

void printableprint(char thing[], uint16_t arraylength, bool newline)
{
  for (uint16_t i=0; i<arraylength; i++)
  {
    if (isprintable(thing[i]))
    {
      Serial.print(thing[i]);
    }
    else
    {
			Serial.printf("[%02X]",thing[i]);
    }
  }
  if (newline) {Serial.println();}
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

// as above but for uint8_t arrays

void printableprintbytes(uint8_t thing[], uint16_t arraylength, bool newline)
{
  for (uint16_t i=0; i<arraylength; i++)
  {
    if (isprintablebyte(thing[i] & 0xFF))
    {
      Serial.print(thing[i]);
    }
    else
    {
			Serial.printf("[%02X]",thing[i]);
	  }
  }
  if (newline) {Serial.println();}
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

// takes two ASCII hex characters and returns the uint8_t equivalent
// e.g. {'A','F'} returns 0xAF;

uint8_t hexcharstobyte(char thing1, char thing2)
{
  if (isLowerCase(thing1)) {thing1-=0x20;}
  if (isLowerCase(thing2)) {thing2-=0x20;}
  uint8_t a=thing1-48; if (a>9) {a-=7;}
  uint8_t b=thing2-48; if (b>9) {b-=7;}
  return a*16+b;
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
// takes one ASCII hex character and returns the uint8_t equivalent
// e.g. 'A' returns 0xA;

uint8_t hexchartobyte(char thing)
{
  if (isLowerCase(thing)) {thing-=0x20;}

  uint8_t a=thing-48; if (a>9) {a-=7;}
  return a;
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

// takes a byte and writes its ASCII equivalent into a string
// return a char, either high or low nibble of byte
// call via bytetohexchar(0xAF, HIGH) to get 'A' or bytetohexchar(0x17, LOW) to get '7'

char bytetohexchar(uint8_t hexdata, uint8_t nibble)
{
  char retval;

  if (nibble == HIGH)
  {
    retval=hexdata>>4;
  }
  else
  {
    retval=hexdata&0x0F;
  }

  retval+=48; 
  if (retval > 0x39) {retval+=7;}
  return retval;
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

// returns int of String of binary (eg 42 for "00101010")

uint8_t binStringToInt(String binstring)
{
  uint8_t bitvalues[8]={128,64,32,16,8,4,2,1};

  uint8_t retval=0;
  for (uint8_t i=0; i<8; i++)
  {
    if (binstring.substring(i,i+1).equals("1"))
    {
      retval=retval+bitvalues[i];
    }
  }

  return retval;
}
// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

// returns String of binary (eg "00101010" for 42)
// opposite of above 

String intToBinString(uint8_t binaryvalue)
{
  String retval;
  retval="";
  uint8_t bitvalues[8]={1,2,4,8,16,32,64,128};

  for (uint8_t i=0; i<8; i++)
  {
    if ((binaryvalue & bitvalues[i])==0)
    {
      retval="0"+retval;
    }
    else
    {
      retval="1"+retval;
    }
  }

//  Serial.printf("Binary %d becomes ",binaryvalue);
//  Serial.println(retval);
  return retval;
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

// returns int of String of hex (eg 42 for "2A")

uint8_t hexStringToInt(String hexstring)
{
  if (hexstring.length() < 2)
  {
    hexstring="0"+hexstring;
  }

  char highnybble=hexstring.charAt(0);
  char lownybble=hexstring.charAt(1);

  uint8_t retval=0;

  if (isDigit(highnybble))
  {
    retval=retval+16*(highnybble-48);
  }
  else if (isxdigit(highnybble))
  {
    if (isupper(highnybble))
    {
      retval=retval+16*(highnybble-55);
    }
    if (islower(highnybble))
    {
      retval=retval+16*(highnybble-87);
    }
  }
  else
  {
    Serial.printf("WTF is %d?\n",highnybble);
  }

  if (isDigit(lownybble))
  {
    retval=retval+(lownybble-48);
  }
  else if (isxdigit(lownybble))
  {
    if (isupper(lownybble))
    {
      retval=retval+(lownybble-55);
    }
    if (islower(lownybble))
    {
      retval=retval+(lownybble-87);
    }
  }
  else
  {
    Serial.printf("WTF is %c?\n",lownybble);
  }

  return retval;
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

String makeNiceHexString(uint8_t intvalue)
{
  String returnstring=String(intvalue,HEX);
  returnstring.toUpperCase();
  if (returnstring.length() < 2)
  {
    returnstring="0"+returnstring;
  }

  return returnstring;
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
