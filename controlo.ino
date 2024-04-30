#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WebSerial.h>
#include <HTTPClient.h>
#include <MFRC522.h>
#include <SPI.h>
#include <Keypad.h>
#include <freertos/task.h>

#define RST_PIN     0     // Configuração do pino RST do leitor RFID
#define SS_PIN      5     // Configuração do pino SS do leitor RFID

MFRC522 mfrc522(SS_PIN, RST_PIN);  

#define ROW_NUM     4 // quatro linhas
#define COLUMN_NUM  4 // quatro colunas

char pass[] = {'1', '5', '5'};

char keys[ROW_NUM][COLUMN_NUM] = { 
  {'D','#','0','*'},
  {'C','9','8','7'},
  {'B','6','5','4'},
  {'A','3','2','1'}
};


 byte pin_rows[ROW_NUM] = {13, 12, 14, 27}; //26, 25, 33, 32
 byte pin_column[COLUMN_NUM] = {26, 25, 33, 32}; //13, 12, 14, 27

 Keypad keypad = Keypad( makeKeymap(keys), pin_rows, pin_column, ROW_NUM, COLUMN_NUM );

AsyncWebServer server(80);

bool waitingForRFID = false; 
bool waitingForName = false; 
bool registroAtivo = false; 
bool autenticacao = true; 

String rfid;
String name;

String serverName= "endpoint";

const char* ssid = "";        
const char* password = "";  

unsigned long lastCardReadTime = 0; // Variável para pegar o tempo da leitura do cartão RFID (pra impedir crash, o rfid parava de ler depois de um tempo )
#define CARD_READ_INTERVAL 7000 // tempo para reiniciar a leitura do cartão RFID
const int rele = 4;

void recvMsg(uint8_t *data, size_t len){
  WebSerial.println("Dados Recebidos...");
  String d = "";
  for(int i=0; i < len; i++){
    d += char(data[i]);
  }
  WebSerial.println(d);
  
  if (d == "registo"){
    Serial.println("Realizando registro...");
    WebSerial.println("Por favor, insira o exemplar de tag RFID:");
    waitingForRFID = true;
    registroAtivo = true; 
  }
  else if (d == "autenticacao") {
    Serial.println("Modo de autenticação ativado...");
    registroAtivo = false; 
  }
  else if (waitingForRFID) {
    rfid = d;
    waitingForRFID = false;
    WebSerial.println("Tag RFID recebida: " + rfid);
    WebSerial.println("Agora, por favor, insira o nome:");
    waitingForName = true;
  }
  else if (waitingForName) {
    name = d;
    waitingForName = false;
    Serial.println("RFID: " + rfid + ", Nome: " + name);

    String logData = name + ":" + rfid;
    Serial.println("Log Data: " + logData);

    if (registroAtivo) {
      WebSerial.println("Registro concluído para RFID: " + rfid + ", Nome: " + name);
      sendPostRequest(logData);
      registroAtivo = false; // Retorna automaticamente ao modo de autenticação após o registro
    } else {
      Serial.println("msms");
    }

    rfid = "";
    name = "";
  }
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  SPI.begin();          
  mfrc522.PCD_Init();  

  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.printf("WiFi Falhou!\n");
    return;
  }
  Serial.print("Endereço IP: ");
  Serial.println(WiFi.localIP());
  WebSerial.begin(&server);
  WebSerial.msgCallback(recvMsg);
  server.begin();
  pinMode(rele, OUTPUT);
}

void loop() {

  unsigned long currentMillis = millis(); // Declara a variável currentMillis
   // Verifica se passou tempo suficiente desde a última leitura do cartão RFID
  if (currentMillis - lastCardReadTime >= CARD_READ_INTERVAL) {
    
    // Reinicia a leitura do cartão RFID
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    mfrc522.PCD_Init();
    lastCardReadTime = currentMillis; // Atualiza o tempo da leitura
  }

  if (registroAtivo && waitingForRFID) {
    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
      Serial.println("Nova tag RFID detectada!");
      String rfidTag = "";
      for (byte i = 0; i < mfrc522.uid.size; i++) {
        rfidTag.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " "));
        rfidTag.concat(String(mfrc522.uid.uidByte[i], HEX));
      }
      rfidTag.toUpperCase();  

      Serial.print("Tag RFID: ");
      Serial.println(rfidTag);

      rfid = rfidTag;
      waitingForRFID = false;
      waitingForName = true;

      WebSerial.println("Agora, por favor, insira o nome:");
    }
  } else {
    if (autenticacao) {
      if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
        HTTPClient http;
        String serverPath = serverName;
        http.begin(serverPath.c_str());
        int httpResponseCode = http.GET();
        
        if (httpResponseCode > 0) {
          Serial.print("Resposta HTTP: ");
          Serial.println(httpResponseCode);
          String payload = http.getString();
          Serial.println("Payload recebido da API: " + payload);
          
          // Mostra o UID na serial
          Serial.print("UID da tag RFID: ");
          String tagUID = "";
          for (byte i = 0; i < mfrc522.uid.size; i++) {
            tagUID.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " "));
            tagUID.concat(String(mfrc522.uid.uidByte[i], HEX));
          }
          tagUID.toUpperCase();
          Serial.println(tagUID);

          // Verifica se o UID da tag está presente no payload
          if (payload.indexOf(tagUID) != -1) {
            Serial.println("Correspondido");
            
            digitalWrite(rele, HIGH);
            delay(6000);
            digitalWrite(rele, LOW);
          } else {
            Serial.println("Não correspondido");
          }
        } else {
          Serial.print("Erro HTTP: ");
          Serial.println(httpResponseCode);
        }
        
        http.end();
      }
    }
  }
}

void sendPostRequest(String logData) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin("exec do sheets");
    http.addHeader("Authorization", "Bearer [token]");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    String data = "text=" + logData; 
    int httpResponseCode = http.POST(data);
    if (httpResponseCode > 0) {
      Serial.print("Código de resposta: ");
      Serial.println(httpResponseCode);
      String payload = http.getString();
      Serial.println(payload);
    } else {
      Serial.print("Erro na solicitação: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  }
}
