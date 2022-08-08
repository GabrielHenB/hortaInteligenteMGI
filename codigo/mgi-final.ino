#if CONFIG_FREERTOS_UNICORE
#define ARDUINO_RUNNING_CORE 0
#else
#define ARDUINO_RUNNING_CORE 1
#endif

// Includes Iniciais
#include <arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_err.h>
#include <esp_log.h>
#include <Servo_ESP32.h>
// VARIAVEIS DE OBJETOS GLOBAIS
xSemaphoreHandle xmutex;
// ANALOGICOS ESP32 ( de 25 a 27 e 32 a 35 ok )
const int PINO_UMIDADE = 35; // Sensor umidade
const int PINO_RELE_BOMBA =  32; // Pino da bomba que rega
// DIGITAIS ESP32
const int PINO_VALVULA = 22; // Pino da Valvula que Rega
const int PINO_NIVEL = 39;
const int servoPin = 14;
const int PINO_CHUVA = 36;
Servo_ESP32 servo1;

//TOPICOS MQTT
#define TOPICO_PUBLISH_UMIDADE "se_mgi_sensorUmidade"
#define TOPICO_PUBLISH_VALVULA "se_mgi_sensorValvula"
#define TOPICO_PUBLISH_RESERVATORIO "se_mgi_reservatorio"
#define TOPICO_PUBLISH_REGANDO "se_mgi_regando"
#define ID_MQTT  "se_mgi_trabalho_2022"     //id mqtt (para identificar sessao)

//definicoes wifi
#define WIFINOME "#"
#define WIFISENHA "#"

const char* SSID     = WIFINOME; // SSID / nome da rede WI-FI que deseja se conectar
const char* PASSWORD = WIFISENHA; // Senha da rede WI-FI que deseja se conectar

const char* BROKER_MQTT = "test.mosquitto.org";
int BROKER_PORT = 1883; // Porta do Broker MQTT

// OBJETOS GLOBAIS
WiFiClient espClient; // Cria o objeto espClient
PubSubClient MQTT(espClient); // Instancia o Cliente MQTT passando o objeto espClient
const int tempoRega = 5; //tempo em segundos de delay para regar
const int limiarSeco = 70; //limiar de umidade para regar
int umidade = 0; //recebe umidade
int chuva =0;
int nivel = 0;
int grau = 0;// grau do servo
int leitura = 0;// leitura do grau do servo

/* Prototypes */
void initWiFi(void);
void initMQTT(void);
void mqtt_callback(char* topic, byte* payload, unsigned int length);
void reconnectMQTT(void);
void reconnectWiFi(void);
void VerificaConexoesWiFIEMQTT(void);


// define two tasks for Blink & AnalogRead
void TaskHorta( void *pvParameters );
void TaskComporta( void *pvParameters );


// the setup function runs once when you press reset or power the board
void setup() {
    //Serial.begin(9600);
  Serial.begin(115200);
  // inicializa valvula
  pinMode(PINO_VALVULA, OUTPUT); // configurar valvula
  digitalWrite(PINO_VALVULA, HIGH); // desliga valvula
  
  // inicializa umidade
  pinMode(PINO_UMIDADE, INPUT);
  
  //inicializa sensor de chuva
   pinMode(PINO_CHUVA, INPUT);

    // inicializa nivel de agua
  pinMode(PINO_NIVEL, INPUT);
  
  //inicializa bomba
  pinMode(PINO_RELE_BOMBA, OUTPUT );
  digitalWrite(PINO_VALVULA, HIGH);

  //inicializa o servo motor
  servo1.attach(servoPin);

  //Inicializa a conexao wi-fi
  initWiFi();

  //Inicializa a conexao ao broker MQTT
  initMQTT();
  xmutex = xSemaphoreCreateMutex();
  // Now set up two tasks to run independently.
  xTaskCreatePinnedToCore(
    TaskHorta
    ,  "TaskBlink"   // A name just for humans
    ,  4096  // This stack size can be checked & adjusted by reading the Stack Highwater
    ,  NULL
    ,  2  // Priority, with 3 (configMAX_PRIORITIES - 1) being the highest, and 0 being the lowest.
    ,  NULL 
    ,  ARDUINO_RUNNING_CORE);

  xTaskCreatePinnedToCore(
    TaskComporta
    ,  "AnalogReadA3"
    ,  2048  // Stack size
    ,  NULL
    ,  1  // Priority
    ,  NULL 
    ,  ARDUINO_RUNNING_CORE);

  // Now the task scheduler, which takes over control of scheduling individual tasks, is automatically started.
}

void loop()
{
  // Empty. Things are done in Tasks.
}

/*--------------------------------------------------*/
/*---------------------- Tasks ---------------------*/
/*--------------------------------------------------*/

void TaskHorta(void *pvParameters)  // This is a task.
{
  (void) pvParameters;

  for (;;) // A Task shall never return or exit.
  {
      //garante funcionamento do WiFi e ao broker MQTT
  if(xSemaphoreTake(xmutex,1500)){
    VerificaConexoesWiFIEMQTT();
    }
    else{
      Serial.print("não conectou");
      }
  

  //recebe os valores em string dos sensores.
  char umidade_str[10] = {0};
  char nivel_str[10] = {0};
  char chuva_str[10] = {0};
  
  // ler umidade
  umidade = analogRead(PINO_UMIDADE);
  // ler nivel
  nivel = analogRead(PINO_NIVEL);

  //ler chuva (usado como um nivel 2)
  chuva = analogRead(PINO_CHUVA);
  
  //sprintf(umidade_str, "%d", umidade);
  
  Serial.print("UMIDADE:");
   umidade = map(umidade, 4095, 978, 0, 100);
  Serial.print(umidade);
  Serial.println("%");

  //sprintf(chuva_str, "%d", chuva);
  sprintf(chuva_str, "%d", chuva);
  Serial.print("CHUVA:");
  chuva = map(chuva, 4095, 978, 0, 100);
  Serial.print(chuva);
  Serial.println("%");
  
  sprintf(nivel_str, "%d", nivel);
   nivel = map(nivel, 4095, 978, 0, 100);
  Serial.print("NIVEL DE ÁGUA:");
  Serial.print(nivel);
  Serial.println("%");
  
  // formata como string, buga no simulador
  //transforma o valor umidade em string enviavel pelo mqtt
  sprintf(umidade_str, "%d", umidade);
  // formata como string
  sprintf(chuva_str, "%d", chuva);
  // formata como string
 sprintf(nivel_str, "%d", nivel);
  

  /* ANALISE TEMPERATURA E UMIDADE */
  if(umidade < limiarSeco) {
    if(nivel > 40){
        //liga a bomba de agua
        digitalWrite(PINO_RELE_BOMBA, LOW); //liga a bomba
        digitalWrite(PINO_VALVULA, HIGH); //desliga a valvula
        Serial.println("REGANDO COM A BOMBA....");
        MQTT.publish(TOPICO_PUBLISH_REGANDO, "BOMBA");
      }
     else{
      //liga a valvula
        digitalWrite(PINO_VALVULA, LOW); //liga a valvula
        digitalWrite(PINO_RELE_BOMBA, HIGH); //desliga a bomba
        Serial.println("REGANDO COM A VALVULA....");
        MQTT.publish(TOPICO_PUBLISH_REGANDO, "VALVULA"); //publica no topico que a valvula foi aberta
      }   
  }
  else {
    digitalWrite(PINO_VALVULA, HIGH); //desliga a valvula
    digitalWrite(PINO_RELE_BOMBA, HIGH); //desliga a bomba
    Serial.println("JÁ EXISTE MUITA ÁGUA NO SOLO....");
    MQTT.publish(TOPICO_PUBLISH_REGANDO, "NÃO"); //publica no topico que o solo encharcado
    // Espera o tempo estipulado
  }

  //Envia os dados para o MQTT
  if(nivel > 40 || chuva > 40){
    if(chuva > 40){
       MQTT.publish(TOPICO_PUBLISH_RESERVATORIO, "CHEIO");
      }
    else{
      MQTT.publish(TOPICO_PUBLISH_RESERVATORIO, "MÉDIO");
      }
    }
  else{
    MQTT.publish(TOPICO_PUBLISH_RESERVATORIO, "VAZIO");
     }
  
  MQTT.publish(TOPICO_PUBLISH_UMIDADE, umidade_str,"%");
  //keep-alive da comunicacao com broker MQTT
  MQTT.loop();
    vTaskDelay(1000);  // one tick delay (15ms) in between reads for stability
  }
}

void TaskComporta(void *pvParameters)  // This is a task.
{
  (void) pvParameters;

  for (;;)
  {
  char chuva_str[10] = {0};
  chuva = analogRead(PINO_CHUVA);
  //sprintf(chuva_str, "%d", chuva);
  sprintf(chuva_str, "%d", chuva);
  chuva = map(chuva, 4095, 978, 0, 100);
    /* ANALISE DO SERVO*/
  //caso o reservatório esteja cheio
  leitura = servo1.read();
  if(chuva > 40){
      if(leitura != 90) {
       servo1.write(90);
     }
   }
   //caso o reservatório não esteja cheio
   else{
    if(leitura != 180){
        servo1.write(180);
      }
    }
    vTaskDelay(1000);  // one tick delay (15ms) in between reads for stability
  }
}
/*####################################################################################################################################################################################################*/
//CONECTAR A REDE WIFI
void initWiFi(void)
{
  delay(10);
  Serial.println("------Conexao WI-FI------");
  Serial.print("Conectando-se na rede: ");
  Serial.println(SSID);
  Serial.println("Aguarde");

  reconnectWiFi();
}
//INICIA COISAS DO MQTT
void initMQTT(void)
{
  MQTT.setServer(BROKER_MQTT, BROKER_PORT);   //informa qual broker e porta deve ser conectado
  MQTT.setCallback(mqtt_callback);            //atribui função de callback (função chamada quando qualquer informação de um dos tópicos subescritos chega)
}
//FUNCAO CHAMADA TODA VEZ QUE ALGO CHEGA CALLBACK
void mqtt_callback(char* topic, byte* payload, unsigned int length)
{
  String msg;
  /* obtem a string do payload recebido */
  for (int i = 0; i < length; i++)
  {
    char c = (char)payload[i];
    msg += c;
  }
  Serial.print("Chegou a seguinte string via MQTT: ");
  Serial.println(msg);
  /* toma ação dependendo da string recebida */
  if (msg.equals("L"))
  {
    Serial.println("Se isso aparecer deu algum erro nao era pra aparecer");
  }

  if (msg.equals("D"))
  {
    Serial.println("Se isso aparecer deu algum erro nao era pra aparecer");
  }
}
//FUNCAO DE RECONEXAO AO MQTT
void reconnectMQTT(void)
{
  while (!MQTT.connected())
  {
    Serial.print("* Tentando se conectar ao Broker MQTT: ");
    Serial.println(BROKER_MQTT);
    if (MQTT.connect(ID_MQTT))
    {
      Serial.println("Conectado com sucesso ao broker MQTT!");
      
    }
    else
    {
      Serial.println("Falha ao reconectar no broker.");
      Serial.println("Havera nova tentativa de conexao em 2s");
      delay(2000);
    }
  }  
}
//FUNCAO QUE VERIFICA ESTADO DE CONEXAO E RECONECTA SE NECESSARIO
void VerificaConexoesWiFIEMQTT(void)
{
  if (!MQTT.connected())
    reconnectMQTT(); //se n h conexão com o Broker, a conexo refeita

  reconnectWiFi(); //se no h conexo com o WiFI, a conexo refeita
}
//FUNCAO DE RECONEXAO COM O WIFI
void reconnectWiFi(void)
{
  //se ja esta conectado a rede WI-FI, nada feito.
  //Caso contrário, sao efetuadas tentativas de conexao
  if (WiFi.status() == WL_CONNECTED)
    return;

  WiFi.begin(SSID, PASSWORD); // Conecta na rede WI-FI

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(100);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Conectado com sucesso na rede ");
  Serial.print(SSID);
  Serial.println("\nIP obtido: ");
  Serial.println(WiFi.localIP());
}
