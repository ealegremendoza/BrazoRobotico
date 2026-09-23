// Echo por serial
#define LED 4// 13-BOARDLED 4-YELLOWLED

String input = "";
void setup() {
  Serial.begin(115200);
  pinMode(LED,OUTPUT);
  digitalWrite(LED,LOW);
}

void loop() {
  while (Serial.available() > 0) {
    char c = Serial.read();

    if (c == '\n') {
      if( input == "ON") {
        digitalWrite(LED,HIGH);
      } else if (input == "OFF") {
        digitalWrite(LED,LOW);
      }
      
      // Línea completa recibida
      Serial.println(input);
      input = "";// limpiar para la próxima
    } else {
      input += c;
    }
  }
}
