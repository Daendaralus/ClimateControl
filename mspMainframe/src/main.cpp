#include <energia.h>
#include <Wire.h>
#define TXD BIT2
#define RXD BIT1
// #include "i2c.h"
#define AM2320_address 0xB8 
#define Wire_Address (AM2320_address>>1)
unsigned int CRC16(byte *ptr, byte length) 
{ 
      unsigned int crc = 0xFFFF; 
      uint8_t s = 0x00; 

      while(length--) {
        crc ^= *ptr++; 
        for(s = 0; s < 8; s++) {
          if((crc & 0x01) != 0) {
            crc >>= 1; 
            crc ^= 0xA001; 
          } else crc >>= 1; 
        } 
      } 
      return crc; 
} 


class AM2320
{
	public:
        TwoWire* comm;

		AM2320(TwoWire* c){comm = c;}
		float t;
		float h;
		int Read(int address)
        {
            byte buf[8];
            for(int s = 0; s < 8; s++) buf[s] = 0x00; 

            comm->beginTransmission(address);
            int ret = comm->endTransmission();
            delayMicroseconds(1000);          // TODO tune

            // запрос 4 байт (температуры и влажности)
            comm->beginTransmission(address);
            comm->write(0x03);// запрос
            comm->write(0x00); // с 0-го адреса
            comm->write(0x04); // 4 байта
            ret = comm->endTransmission(1);

            if (ret < 0) return 1;
            delayMicroseconds(1600); //>1.5ms
            // считываем результаты запроса
            ret = comm->requestFrom(address, 0x08); 

            for (int i = 0; i < 0x08; i++) {buf[i] = comm->read();}
            // CRC check
            unsigned int Rcrc = buf[7] << 8;
            Rcrc += buf[6];

            //Serial.print("Rcrc: ");Serial.println(buf[7]);
            unsigned int crc = CRC16(buf, 6);
            if (Rcrc==crc) {
                unsigned int temperature = ((buf[4] & 0x7F) << 8) + buf[5];
                t = temperature / 10.0;
                t = ((buf[4] & 0x80) >> 7) == 1 ? t * (-1) : t;

                unsigned int humidity = (buf[2] << 8) + buf[3];
                h = humidity / 10.0;
                return 0;
            }
            //Serial.print("CRC16: ");Serial.println(crc);

            return 2;
        } 
};
AM2320* th;



struct ComfyOut
{
    ComfyOut operator<<(String s)
    {
        Serial.print(s);
        return *this;
    }
};
ComfyOut cout;
bool meme=false;

//COMMAND PROTOCOL
/*
command start character:  $
command end charater: ~
command args delimiter: &
                        first byte    second
format: command start|command number|argcount|data(numbers, string w/e)[& data...]|command end
$11&250~
Have command buffer, in case not everything has arrived from serial yet. 
command buffer holds all data between any two command characters
*/
enum Command{
    NONE = 1,
    STATUS_RETURN,
    SET_TARGET_TEMP,
    SET_TARGET_HUM,
    GET_TEMP,
    GET_HUM,
    START_FLASH_LED,
    STOP_FLASH_LED
};

enum StatusReturns
{
    SUCCESS = 1,
    FAILURE
};

enum ArgType{
    STRING = 1,
    NUMBER
};

struct CommandArgument
{
    ArgType type;
    union{
        char* str;
        long number;
    }data;
    size_t datalength;

    CommandArgument()
    {
        type = NUMBER;
        data.number=0;
        datalength=sizeof(data.number);
    }

    CommandArgument(char* value, size_t char_c)
    {
        datalength=char_c;
        type = STRING;
        data.str = (char*) malloc(sizeof(char)*char_c);//new char[char_c];
        memcpy(data.str, value, char_c);
    }

    CommandArgument(long value)
    {
        datalength=sizeof(data.number);
        type = NUMBER;
        data.number = value;
    }

    CommandArgument(const CommandArgument& ca)
    {
        type = ca.type;
        datalength=ca.datalength;
        switch(type)
        {
            case STRING:
                data.str = (char*) malloc(sizeof(char)*datalength);//new char[char_c];
                memcpy(data.str, ca.data.str, datalength);    
                break;
            case NUMBER:
                data.number=ca.data.number;
                break;
        }
    }
    template <class T>
    void swap(T& lhs, T& rhs)
    {
        T tmp(lhs);
        lhs = rhs;
        rhs = tmp;
    }

    CommandArgument& operator=(CommandArgument other)
    {
        CommandArgument tmp(*this);
        swap(type, other.type);
        swap(datalength, other.datalength);
        swap(data, other.data);
        return *this;
    }

    ~CommandArgument()
    {
        if(type==STRING && data.str!=NULL)
            free(data.str);
    };


   
};
int serialbuffersize = 10;
char*  serialbuffer = (char*)malloc(sizeof(char)*serialbuffersize);
size_t head=0, tail=0;

Command currentCommand;
size_t argCount=0, argExpected=0;
CommandArgument* args;
bool startRecording = false;
const char cmdStartChar='$';
const char cmdEndChar='~';
const char cmdArgsDelim='&';

char lastControlChar = '-';
char newControlChar = '-';


void resetCommandStructures()
{
    currentCommand = NONE;
    lastControlChar='-';
    newControlChar ='-';
    argCount=0;
    argExpected=0;
    free(args);
    head=0;
    tail=0;
}

void sendError(String s)
{
    startRecording=false;

    Serial.print("$");Serial.print(STATUS_RETURN);Serial.print(2);Serial.print("&");Serial.print(FAILURE);Serial.print("&");Serial.print(s);Serial.print("~");//serialbuffer overflow return error to host
}

void sendSuccess(int args)
{
    Serial.print(cmdStartChar);Serial.print((char)STATUS_RETURN);Serial.print((char)args+2);Serial.print("&");Serial.print((char)SUCCESS);
}

void executeCommand()
{
    switch(currentCommand)
    {
        case STATUS_RETURN:
            break;
        case SET_TARGET_TEMP:
            break;
        case SET_TARGET_HUM:
            break;
        case GET_TEMP:
            sendSuccess(1);
            Serial.print(th->t); Serial.println("~");
            break;
        case GET_HUM:
            sendSuccess(1);
            Serial.print(th->h); Serial.println("~");
            break;
        case START_FLASH_LED:
             Serial.println("START FLASHING");
            meme=true;
            break;
        case STOP_FLASH_LED:
             Serial.println("STOP FLASHING");
            meme=false;
            break;
    }
}

void HandleCommandChar()
{
    if(newControlChar==cmdStartChar)
    {
        resetCommandStructures();
        startRecording=true;
    }
    else if(newControlChar== cmdEndChar)
    {
        if(lastControlChar == cmdStartChar)
        {
            if( tail <2)
                argExpected=-1;
            currentCommand = Command((int)serialbuffer[head++]);

            argExpected = (int)serialbuffer[head]-1;
            args = (CommandArgument*) malloc(sizeof(CommandArgument)*argExpected);
            tail = 0;
        }
        else if(lastControlChar == cmdArgsDelim)
        {
            args[argCount++] = CommandArgument(serialbuffer, tail);
            tail=0;
        }
        if(argCount==argExpected)
        {
            //success i guess?
            executeCommand();
        }
        startRecording=false;
    }
    else if(newControlChar== cmdArgsDelim)
    {
        if(lastControlChar == cmdStartChar && tail == 2)
        {
            //first argsdelim meaning I have the two bytes denoting the current command and the argscount
            currentCommand = Command((int)serialbuffer[head++]);
            argExpected = (int)serialbuffer[head]-1;
            args = (CommandArgument*) malloc(sizeof(CommandArgument)*argExpected);
            tail = 0;
        }
        else if (lastControlChar == cmdArgsDelim)
        {
            args[argCount++] = CommandArgument(serialbuffer, tail);
            tail=0;
        }
        else if(lastControlChar == cmdEndChar || lastControlChar == '-')
        {
            //garbage
            resetCommandStructures();
            sendError("Unexpected Argument end. Error.");
        }
    }
}

void HandleSerialInput() 
{
    int charc =  Serial.available();
    for(int i = 0; i<charc;++i)
    {
        int currv = Serial.read();
        if(currv <=0xFF)
        {
            char currc = (char)currv;
            if(currc == '\n' || currc == '\r') //Characters to ignore
                continue;
            if(currc == cmdStartChar || currc == cmdEndChar || currc == cmdArgsDelim)
            {
                newControlChar= currc;
                HandleCommandChar();
                lastControlChar = currc;
            }
            else
            {
                if(tail < serialbuffersize)
                {
                    if(startRecording)
                        serialbuffer[tail++] = currc;
                }
                else
                {   
                    resetCommandStructures();
                    sendError("Buffer overflow. Argument too long.");
                }
            }
        }
        delay(0);
    }
}

int teststate= LOW;
int ledState = LOW;         // the current state of the output pin
int buttonState = LOW;             // the current reading from the input pin
int lastButtonState = HIGH;   // the previous reading from the input pin
unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 50;  
unsigned long lastwrite = 0;
unsigned long lastwrite2 = 0;
unsigned long lastBlink = 0;

//TO SEND >255 INTS SHIFT INTS DOWN TO INDIVIDUAL BYTES. TELL OTHER SIDE HOW MANY BYTES ON NUMBER?
//OTHERWISE PARSE INTS....
void setup()
{
    pinMode(RED_LED, OUTPUT);
    pinMode(P2_5, INPUT_PULLDOWN);

    Serial.begin(9600);
    Wire.setModule(0);
    Wire.begin();
    th = new AM2320(&Wire);
    lastwrite = millis();
    lastwrite2 = lastwrite;
    lastDebounceTime = lastwrite;
    lastBlink = lastwrite;
};

void onButtonDown()
{
    ledState = ledState?LOW:HIGH;
    Serial.print("Knopf gedrückt! :) : "); Serial.print(NONE); Serial.println(START_FLASH_LED);
    //sendSuccess(1);
    Serial.print(th->h); Serial.println("~");

}
void onButtonUp()
{

}
void loop()
{
    //meme=!meme;
    int reading = digitalRead(P2_5);
    unsigned long milli = millis();
    if(reading!=lastButtonState)
    {
        lastDebounceTime = milli;
    }
    if((milli - lastDebounceTime) > debounceDelay)
    {
        if(reading != buttonState)
        {
            buttonState=reading;
            if(buttonState==HIGH)
            {
                onButtonDown();
            }
            else
            {
                onButtonUp();
            }
        }
    }
    digitalWrite(RED_LED, ledState);
   
    if(meme&&(milli-lastBlink)>1000)
    {
        ledState = ledState?LOW:HIGH;
        lastBlink= milli;
    }

    if((milli-lastwrite2)>15000)
        {
            int ret = th->Read(Wire_Address);
            switch(ret)
            {
                case 2:
                    //digitalWrite(P1_4, LOW);

                    cout <<"crc failed\n";
                    break;
                case 1:
                    //digitalWrite(P1_4, LOW);

                    cout << "it's not on? OMEGALUL\n";
                break;
                case 0:
                    cout<<"h: "; Serial.print(th->h); cout<<"% | t: "; Serial.print(th->t); cout<<"C\n";

                break;
            }
            lastwrite2 = milli;

        }
    lastButtonState = reading;
    //delay(5);
    HandleSerialInput();
}
