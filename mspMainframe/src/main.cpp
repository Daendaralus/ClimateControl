#include <energia.h>
#include <Wire.h>

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
            Serial.print("CRC16: ");Serial.println(crc);

            return 2;
        } 
};
AM2320* th;

void setup()
{
    pinMode(RED_LED, OUTPUT);
    pinMode(P1_3, INPUT);
    pinMode(P1_4, OUTPUT);
    Serial.begin(9600);
    Wire.setModule(0);
    Wire.begin();
    th = new AM2320(&Wire);
};

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
void loop()
{
    if(digitalRead(P1_3)==0)
    {
        digitalWrite(P1_4, LOW);
        digitalWrite(RED_LED, LOW);
    }
    else
    {
        digitalWrite(P1_4, HIGH);
        digitalWrite(RED_LED, HIGH);
    }
    meme=!meme;
    switch(th->Read(Wire_Address))
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
        //digitalWrite(P1_4, HIGH);

        cout<<"h: "; Serial.print(th->h); cout<<"% | t: "; Serial.print(th->t); cout<<"C\n";
        break;
    }
    delay(1000);
}
