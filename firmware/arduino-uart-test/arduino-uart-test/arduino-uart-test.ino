#include <SoftwareSerial.h>
#include <string.h>
#define TX_PIN 18 // TX=A4
#define RX_PIN 19 // RX=A5
#define LED 4
#define BUFFER_LEN 32
#define STX 0x02
#define ETX 0x03
#define FS 0x1c

// Uncomment to read commands from Serial (USB, via send_cmd.py) instead of
// Serial1 (SoftwareSerial from the ESP32 bridge) -- standalone debug mode.
// #define DEBUG_STANDALONE_USB

#ifdef DEBUG_STANDALONE_USB
  #define CMD_SERIAL Serial
#else
  #define CMD_SERIAL Serial1
#endif

typedef enum {
  WAIT_STX,
  READING_DATA,
  WAIT_LRC,
}uart_state_e;




uart_state_e uart_state = WAIT_STX;
SoftwareSerial Serial1(RX_PIN, TX_PIN); // RX, TX
char rcvByte = 0;
char rcvBuffer[BUFFER_LEN] = {0};
uint32_t rcvBufferOffset = 0;
char xorValue = 0;

void setup() {
  pinMode(LED, OUTPUT);      // set LED pin as output
  digitalWrite(LED, LOW);    // switch off LED pin
  Serial1.begin(9600);              // initialize serial communication at 9600 bits per second
  Serial.begin(9600); // initialize serial monitor
  Serial.println("Setup done!");
  Serial.print("Build: "); Serial.println(__DATE__ " " __TIME__);
}

void loop() {
  while (CMD_SERIAL.available() > 0) {
    rcvByte = CMD_SERIAL.read();
    switch(uart_state){
      case WAIT_STX: {
        if(rcvByte == STX){
          if(!saveDataInRcvBuffer(rcvByte)){
            resetFSM(); break;
          } else {
            uart_state = READING_DATA;
          }
        } else {
          resetFSM();
        }
        break;
      }
      case READING_DATA: {
        if (rcvByte == STX) {
          resetRcvBuffer();
          saveDataInRcvBuffer(rcvByte);
        }
        else if(!saveDataInRcvBuffer(rcvByte)){
          resetFSM(); break;
        } else {
          xorValue ^= rcvByte;
          if(rcvByte == ETX){
            uart_state = WAIT_LRC;
          }
        }
        break;
      }
      case WAIT_LRC: {
        if(checkLrc(rcvByte)){
          Serial.println("FULL_PKG!");
          processRcvMsg();
          resetFSM();
        } else {
          resetRcvBuffer();
          uart_state = WAIT_STX;
        }
        break;
      }
      default:{
        Serial.println("Error");
        break;
      }
    }
  }
  delay(100); // delay para no usar el 100% del cpu.
}


bool saveDataInRcvBuffer(char newByte){
  rcvBuffer[rcvBufferOffset] = newByte;
  rcvBufferOffset++;
  if ( rcvBufferOffset >= BUFFER_LEN) {
    Serial.println("Buffer lleno!");
    return false;
  }
  return true;
}

void resetRcvBuffer(){
  for (int i = 0; i < BUFFER_LEN ; i++){
    rcvBuffer[i] = 0;
  }
  rcvBufferOffset = 0;
  xorValue = 0;
}

void resetFSM(){
  resetRcvBuffer();
  uart_state = WAIT_STX;
}

bool checkLrc(char rcvLrc){
  return xorValue == rcvLrc;
}

void processRcvMsg(){
  char msg[BUFFER_LEN] = {0};
  for(int i=1 ; i < BUFFER_LEN; i++){
    if(rcvBuffer[i]==ETX) break;
    msg[i-1] = rcvBuffer[i];
  }
  Serial.print("RX: "); Serial.println(msg);
  
  processCommand(msg);
}

void processCommand(const char* msg){
  if(strcmp(msg, "ON")==0) {
    Serial.println("LED ON.");
    digitalWrite(LED, HIGH);
    Serial1.println("ACK:ON");
  } else if (strcmp(msg, "OFF")==0){
    Serial.println("LED OFF.");
    digitalWrite(LED, LOW);
    Serial1.println("ACK:OFF");
  } else {
    Serial.println("Codigo incorrecto. Reintente.");
    Serial1.println("NACK:UNKNOWN");
  }
}
