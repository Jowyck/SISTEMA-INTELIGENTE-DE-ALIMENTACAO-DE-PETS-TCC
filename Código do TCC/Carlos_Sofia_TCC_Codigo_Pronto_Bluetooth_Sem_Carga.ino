#include <LiquidCrystal.h>
#include <RTClib.h>
#include <Wire.h>

// Configuração do LCD para o Keypad Shield
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);

// Definição dos pinos
const int motorR_direcao = 10;   // Motor da ração (direção) - DRV8825 #2
const int motorR_passo = A3;     // Motor da ração (passo) - DRV8825 #2
const int motorA_direcao = A2;   // Motor da água (direção) - DRV8825 #1
const int motorA_passo = A1;     // Motor da água (passo) - DRV8825 #1
const int sensor_agua = 3;       // Sensor de nível de água (boia)
const int botao_racao = 12;      // Botão para liberar ração (modo manual) - PULL-DOWN
const int buzzer = 2;            // Buzzer Ativo WJ
const int led = 13;              // LED Difuso Genérico

// Variáveis globais
int modo = 0;                    // 0=não selecionado, 1=Manual, 2=Timer
int vezes = 0;                   // CORREÇÃO: Número de alimentações por dia (0 = não configurado)
int hora_racao[3] = {0, 0, 0};   // CORREÇÃO: Horários zerados inicialmente
int minuto_racao[3] = {0, 0, 0}; // CORREÇÃO: Minutos zerados inicialmente
int segundo_racao[3] = {0, 0, 0}; // CORREÇÃO: Segundos zerados inicialmente
int tempoLiberacaoRacao = 0;     // CORREÇÃO: Tempo de liberação zerado inicialmente
bool modoConfigurado = false;    // Indica se o modo foi configurado
bool enchendoAgua = false;       // Flag para controle de enchimento de água
bool enchendoRacao = false;      // Flag para controle de enchimento de ração
bool horarioAcionado[3] = {false, false, false}; // Controle de acionamento por horário

// Objetos
RTC_DS3231 rtc;                  // RTC

// Variáveis de interface
int opcaoSelecionada = 1;        // 1=Manual, 2=Timer
int interfaceSelecionada = 0;    // 0=não selecionado, 1=Bluetooth, 2=LCD Keypad

// Variáveis para debounce
unsigned long ultimoTempoTeclado = 0;
unsigned long ultimoTempoNavegacao = 0;
unsigned long ultimoTempoBotaoRacao = 0;
unsigned long tempoInicioRacao = 0;
unsigned long tempoInicioAgua = 0; // CORREÇÃO: Variável para controle de tempo do enchimento de água
const int debounceDelay = 200;   // ms
const int navegacaoDelay = 500;  // ms entre navegação na agenda

// Flags de presença de periféricos
bool rtcPresent = false;

// Variáveis para controle de estado dos reservatórios
bool aguaCheia = false;

// Variáveis para controle do botão e motor
bool botaoPressionado = false;
bool motorAberto = false;
const int passos180Graus = 100;  // Ajuste este valor conforme necessário para 180 graus

// Variáveis Bluetooth
String inputString = "";         // String para armazenar dados recebidos
bool stringComplete = false;     // Flag se a string está completa (recebeu \n)
bool bluetoothConnected = false; // Flag para conexão Bluetooth

// Estados anteriores para detectar mudanças
bool aguaCheiaAnterior = false;
bool enchendoRacaoAnterior = false;

// Variável para controle de último minuto verificado
int ultimoMinutoVerificado = -1;

// ALTERAÇÃO: Variáveis para controle de exibição de mensagens
unsigned long tempoExibicaoMensagem = 0;
bool exibindoMensagem = false;
String mensagemAtual = "";

// ALTERAÇÃO: Estados do Bluetooth
int estadoBluetooth = 0; // 0=Aguardando conexão, 1=Conectado, 2=Ajuste RTC, 3=Seleção Modo, 4=Manual, 5=Timer

// ALTERAÇÃO: Variável para controle de exibição contínua de "Enchendo racao"
bool forcarExibicaoRacao = false;

// ALTERAÇÃO: Variável para controle de estado anterior da ração
bool estadoRacaoAnterior = false;

void setup() {
  Serial.begin(9600);
  
  // Inicialização do LCD
  lcd.begin(16, 2);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Iniciando...");
  delay(800);
  
  // Configuração dos pinos
  pinMode(motorR_direcao, OUTPUT);
  pinMode(motorR_passo, OUTPUT);
  pinMode(motorA_direcao, OUTPUT);
  pinMode(motorA_passo, OUTPUT);
  pinMode(sensor_agua, INPUT_PULLUP);
  pinMode(botao_racao, INPUT);
  pinMode(buzzer, OUTPUT);
  pinMode(led, OUTPUT);
  
  // Desligar buzzer e LED inicialmente
  digitalWrite(buzzer, LOW);
  digitalWrite(led, LOW);
  
  // Inicialização do RTC
  Wire.begin();
  if (!rtc.begin()) {
    rtcPresent = false;
    lcd.clear();
    lcd.print("RTC nao detect.");
    delay(1200);
    lcd.clear();
  } else {
    rtcPresent = true;
  }
  
  // Mensagem inicial
  mensagemInicial();

  // Seleção de interface
  selecionarInterface();

  // Ajuste da hora atual apenas se interface for LCD
  if (interfaceSelecionada == 2 && rtcPresent) {
    ajustarHoraAtual();
  } else if (interfaceSelecionada == 1) {
    // ALTERAÇÃO: Mensagem Bluetooth atualizada
    lcd.clear();
    lcd.print("BT:Aguard.Conex.");
    estadoBluetooth = 0;
  }
}

void loop() {
  // ALTERAÇÃO: Verifica se está exibindo mensagem temporária (apenas para mensagens que NÃO são "Enchendo racao")
  if (exibindoMensagem && !forcarExibicaoRacao && (millis() - tempoExibicaoMensagem > 1500)) {
    exibindoMensagem = false;
    lcd.clear();
  }
  
  // Atualiza hora atual
  DateTime now = rtcPresent ? rtc.now() : DateTime(F(__DATE__), F(__TIME__));
  
  // CORREÇÃO: Reset dos flags de horário quando o minuto muda
  if (now.minute() != ultimoMinutoVerificado) {
    for (int i = 0; i < 3; i++) {
      horarioAcionado[i] = false;
    }
    ultimoMinutoVerificado = now.minute();
  }
  
  // Verifica o estado do reservatório de água - LÓGICA INVERTIDA
  bool sensorAguaEstado = digitalRead(sensor_agua);
  bool aguaCheiaAtual = (sensorAguaEstado == LOW); // INVERTIDO: agora LOW = cheia, HIGH = vazia
  
  // Detecta mudança no sensor de boia e envia apenas quando mudar
  if (aguaCheiaAtual != aguaCheiaAnterior) {
    aguaCheia = aguaCheiaAtual;
    aguaCheiaAnterior = aguaCheiaAtual;
    if (bluetoothConnected) {
      enviarEstadoAgua();
    }
  }
  
  // No modo TIMER, detecta mudança no estado da ração
  if (modo == 2 && bluetoothConnected) {
    bool estadoRacaoAtual = (enchendoRacao || motorAberto);
    if (estadoRacaoAtual != enchendoRacaoAnterior) {
      enchendoRacaoAnterior = estadoRacaoAtual;
      enviarEstadoRacao();
    }
  }
  
  // Controla indicadores (LED e buzzer)
  controlarIndicadores();
  
  // Processa Bluetooth de forma NÃO-BLOQUEANTE
  processarBluetooth();
  
  if (interfaceSelecionada == 1) {
    // Modo Bluetooth
    // ALTERAÇÃO: Se estiver forçando exibição de ração, não mudar a tela
    if (!exibindoMensagem && !enchendoAgua && !forcarExibicaoRacao) {
      exibirModoBluetooth(now);
    }
    
    if (modo == 1) {
      controlarAgua();
    } else if (modo == 2) {
      if (!modoConfigurado) {
        modoConfigurado = true;
      }
      controlarAgua();
      controlarRacaoModo2(now);
    }
  } else {
    // Modo LCD Keypad
    if (modo == 0) {
      selecionarModo();
    } else if (modo == 1) {
      if (!exibindoMensagem && !enchendoAgua && !forcarExibicaoRacao) {
        exibirModoAtivo(1, now);
      }
      controlarAgua();
      controlarRacaoModo1();
      verificarTecladoModo1();
    } else if (modo == 2) {
      if (!exibindoMensagem && !enchendoAgua && !forcarExibicaoRacao) {
        exibirModoAtivo(2, now);
      }
      if (!modoConfigurado) {
        configurarTimer();
        modoConfigurado = true;
      }
      controlarAgua();
      controlarRacaoModo2(now);
      verificarTecladoModo2();
    }
  }
  
  delay(100);
}

void exibirModoBluetooth(DateTime now) {
  lcd.setCursor(0, 0);
  
  switch (estadoBluetooth) {
    case 0: // Aguardando conexão
      lcd.print("BT:Aguard.Conex.");
      break;
    case 1: // Conectado
      lcd.print("Conectado!      ");
      break;
    case 2: // Ajuste RTC
      lcd.print("BT:Ajuste RTC   ");
      break;
    case 3: // Seleção de modo
      lcd.print("BT:Sel.Modo     ");
      break;
    case 4: // Modo Manual
      lcd.print("BT:Manual       ");
      break;
    case 5: // Modo Timer
      lcd.print("BT:Timer        ");
      break;
    default:
      lcd.print("BT:Aguard.Conex.");
      break;
  }
  
  lcd.setCursor(0, 1);
  lcd.print("Hora: ");
  if (now.hour() < 10) lcd.print("0");
  lcd.print(now.hour());
  lcd.print(":");
  if (now.minute() < 10) lcd.print("0");
  lcd.print(now.minute());
  lcd.print(":");
  if (now.second() < 10) lcd.print("0");
  lcd.print(now.second());
  lcd.print(" ");
}

void selecionarInterface() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Sel. Interface: ");
  
  bool selecionado = false;
  int opcao = 1;
  
  while (!selecionado) {
    lcd.setCursor(0, 1);
    if (opcao == 1) {
      lcd.print(">Bluetooth     ");
    } else {
      lcd.print(">LCD Keypad    ");
    }
    
    int botao = lerTeclado();
    if (botao == 1) {
      opcao = 2;
    } else if (botao == 4) {
      opcao = 1;
    } else if (botao == 5) {
      interfaceSelecionada = opcao;
      selecionado = true;
    }
    
    delay(200);
  }
  
  lcd.clear();
  
  // CORREÇÃO: Adicionado delay de 2 segundos após seleção da interface
  if (interfaceSelecionada == 1) {
    lcd.print("Modo Bluetooth");
    lcd.setCursor(0, 1);
    lcd.print("Selecionado!");
    delay(2000);
  } else {
    lcd.print("Modo LCD Keypad");
    lcd.setCursor(0, 1);
    lcd.print("Selecionado!");
    delay(2000);
  }
  
  lcd.clear();
}

void processarBluetooth() {
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    
    if (inChar == '\n') {
      stringComplete = true;
    } else {
      if (inChar != '\r') {
        inputString += inChar;
      }
    }
  }

  if (stringComplete) {
    inputString.trim();
    
    if (inputString == "CONECTSCRN1") {
      bluetoothConnected = true;
      modo = 0;
      // ALTERAÇÃO: Exibir mensagem de conectado e mudar estado
      lcd.clear();
      lcd.print("Conectado!");
      exibindoMensagem = true;
      forcarExibicaoRacao = false;
      tempoExibicaoMensagem = millis();
      estadoBluetooth = 1;
      
      // Após conectar, vai para ajuste RTC
      delay(1500);
      lcd.clear();
      estadoBluetooth = 2; // Ajuste RTC
    }
    else if (inputString == "VOLTDISP") {
      bluetoothConnected = false;
      modo = 0;
      estadoBluetooth = 0; // Volta para aguardando conexão
      forcarExibicaoRacao = false;
      lcd.clear();
      lcd.print("BT:Aguard.Conex.");
    }
    else if (inputString == "VOLTRTC") {
      estadoBluetooth = 2; // Volta para ajuste RTC
      forcarExibicaoRacao = false;
      lcd.clear();
      lcd.print("BT:Ajuste RTC");
    }
    else if (inputString.startsWith("RTC:")) {
      ajustarHoraBluetooth(inputString);
      // ALTERAÇÃO: Após ajustar RTC, vai para seleção de modo
      estadoBluetooth = 3; // Seleção de modo
    }
    // CORREÇÃO CRÍTICA: VOLTMODOS deve ir para Seleção de Modo (estado 3), não para Ajuste RTC (estado 2)
    else if (inputString == "VOLTMODOS") {
      modo = 0;
      modoConfigurado = false;
      estadoBluetooth = 3; // CORREÇÃO: Volta para seleção de modo
      forcarExibicaoRacao = false;
      lcd.clear();
      lcd.print("BT:Sel.Modo");
    }
    else if (inputString == "MODSENS") {
      modo = 1;
      modoConfigurado = true;
      bluetoothConnected = true;
      estadoBluetooth = 4; // Modo Manual ativo
      forcarExibicaoRacao = false;
      lcd.clear();
      lcd.print("BT:Manual");
      enviarEstadoAgua();
    }
    else if (inputString == "MODTIMER") {
      modo = 2;
      modoConfigurado = true;
      bluetoothConnected = true;
      estadoBluetooth = 5; // Modo Timer ativo
      forcarExibicaoRacao = false;
      lcd.clear();
      lcd.print("BT:Timer");
      enviarEstadoAgua();
      enviarEstadoRacao();
    }
    else if (inputString.startsWith("PARAMETROSTM:")) {
      configurarTimerBluetooth(inputString);
    }
    // CORREÇÃO CRÍTICA: Usar RS1 e RS0 para controlar a exibição de "Enchendo racao" - COM CORREÇÃO DA EXIBIÇÃO
    else if (inputString == "RS1") {
      if (!enchendoRacao && !motorAberto) {
        enchendoRacao = true;
        motorAberto = true;
        // ALTERAÇÃO CRÍTICA: Exibição estável e contínua sem piscar
        lcd.clear();
        lcd.print("Enchendo racao");
        forcarExibicaoRacao = true;
        exibindoMensagem = false; // Garante que outras mensagens não interfiram
        girarMotorRacao(0, passos180Graus);
        if (modo == 2 && bluetoothConnected) {
          enviarEstadoRacao();
        }
      }
    }
    else if (inputString == "RS0") {
      if (enchendoRacao && motorAberto) {
        enchendoRacao = false;
        // CORREÇÃO CRÍTICA: Parar exibição forçada IMEDIATAMENTE - SEM DELAY
        forcarExibicaoRacao = false;
        exibindoMensagem = false;
        lcd.clear(); // Limpa a tela imediatamente
        girarMotorRacao(1, passos180Graus);
        motorAberto = false;
        if (modo == 2 && bluetoothConnected) {
          enviarEstadoRacao();
        }
      }
    }
    
    inputString = "";
    stringComplete = false;
  }
}

void enviarEstadoAgua() {
  if (bluetoothConnected) {
    if (aguaCheia) {
      Serial.println("A1");
    } else {
      Serial.println("A0");
    }
    delay(10);
  }
}

// CORREÇÃO CRÍTICA: Lógica invertida para o estado da ração
void enviarEstadoRacao() {
  if (bluetoothConnected && modo == 2) {
    if (enchendoRacao || motorAberto) {
      Serial.println("R0"); // CORREÇÃO: Envia R0 quando está enchendo
    } else {
      Serial.println("R1"); // CORREÇÃO: Envia R1 quando está cheio/parado
    }
    delay(10);
  }
}

void ajustarHoraBluetooth(String comando) {
  int hora = comando.substring(4, 6).toInt();
  int minuto = comando.substring(7, 9).toInt();
  
  if (rtcPresent) {
    DateTime now = rtc.now();
    rtc.adjust(DateTime(now.year(), now.month(), now.day(), hora, minuto, 0));
    
    lcd.clear();
    lcd.print("HORA AJUSTADA:");
    lcd.setCursor(0, 1);
    lcd.print("     ");
    if (hora < 10) lcd.print("0");
    lcd.print(hora);
    lcd.print(":");
    if (minuto < 10) lcd.print("0");
    lcd.print(minuto);
    delay(2000);
    
    lcd.clear();
    lcd.print("BT:Ajuste RTC");
  }
}

void configurarTimerBluetooth(String comando) {
  int primeiroSeparador = comando.indexOf(',');
  int segundoSeparador = comando.indexOf(',', primeiroSeparador + 1);
  
  int intervalo = comando.substring(13, primeiroSeparador).toInt();
  int agenda = comando.substring(primeiroSeparador + 1, segundoSeparador).toInt();
  
  String horariosStr = comando.substring(segundoSeparador + 1);
  int startPos = 0;
  
  for (int i = 0; i < 3; i++) {
    int doisPontos = horariosStr.indexOf(':', startPos);
    int virgula = horariosStr.indexOf(',', startPos);
    
    if (doisPontos != -1) {
      if (virgula == -1) virgula = horariosStr.length();
      
      hora_racao[i] = horariosStr.substring(startPos, doisPontos).toInt();
      minuto_racao[i] = horariosStr.substring(doisPontos + 1, virgula).toInt();
      segundo_racao[i] = 0; // Bluetooth sempre usa 0 segundos
      
      startPos = virgula + 1;
    }
  }
  
  tempoLiberacaoRacao = intervalo;
  vezes = agenda;
  modoConfigurado = true;
  
  lcd.clear();
  lcd.print("Agenda ajustada.");
  lcd.setCursor(0, 1);
  lcd.print("INT:");
  lcd.print(intervalo);
  lcd.print("s AG:");
  lcd.print(agenda);
  delay(2000);
  
  lcd.clear();
  lcd.print("BT:Timer");
}

void controlarIndicadores() {
  bool ledLigado = aguaCheia && !enchendoAgua && !enchendoRacao;
  digitalWrite(led, ledLigado ? HIGH : LOW);
  
  digitalWrite(buzzer, (enchendoAgua || enchendoRacao) ? HIGH : LOW);
}

void mensagemInicial() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Alimentador");
  lcd.setCursor(0, 1);
  lcd.print("Inteligente Pet");
  delay(1500);
  lcd.clear();
}

void selecionarModo() {
  lcd.setCursor(0, 0);
  lcd.print("Selecione:       ");
  lcd.setCursor(0, 1);
  if (opcaoSelecionada == 1) lcd.print(">Modo 1 Manual   ");
  else lcd.print(">Modo 2 Timer    ");

  int botao = lerTeclado();
  if (botao == 4) opcaoSelecionada = 1;
  else if (botao == 1) opcaoSelecionada = 2;
  else if (botao == 5) {
    modo = opcaoSelecionada;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Modo ");
    lcd.print(modo);
    lcd.print(": ");
    lcd.print(modo == 1 ? "Manual" : "Timer");
    lcd.setCursor(0, 1);
    lcd.print("   selecionado   ");
    delay(1000);
    lcd.clear();
  }
}

void exibirModoAtivo(int modoAtivo, DateTime now) {
  if (enchendoAgua || enchendoRacao) {
    return;
  }
  
  lcd.setCursor(0, 0);
  lcd.print("Modo ");
  lcd.print(modoAtivo);
  lcd.print(modoAtivo == 1 ? " Manual" : " Timer");
  
  if (modoAtivo == 1) {
    lcd.print(" Btn:Libera");
  } else {
    lcd.print("       ");
  }
  
  lcd.setCursor(0, 1);
  lcd.print("Hora: ");
  if (now.hour() < 10) lcd.print("0");
  lcd.print(now.hour());
  lcd.print(":");
  if (now.minute() < 10) lcd.print("0");
  lcd.print(now.minute());
  lcd.print(":");
  if (now.second() < 10) lcd.print("0");
  lcd.print(now.second());
  lcd.print("    ");
}

int lerTeclado() {
  if (millis() - ultimoTempoTeclado < debounceDelay) {
    return 0;
  }
  int valor = analogRead(A0);
  ultimoTempoTeclado = millis();
  if (valor < 50) return 1;
  else if (valor < 200) return 2;
  else if (valor < 400) return 3;
  else if (valor < 600) return 4;
  else if (valor < 800) return 5;
  else return 0;
}

// CORREÇÃO CRÍTICA: Função controlarAgua() modificada para não bloquear o loop principal
void controlarAgua() {
  // LÓGICA INVERTIDA: Agora verifica se sensor está HIGH (vazio) para encher
  if (digitalRead(sensor_agua) == HIGH && !enchendoAgua && !enchendoRacao) {
    enchendoAgua = true;
    tempoInicioAgua = millis(); // CORREÇÃO: Guarda o tempo de início
    lcd.clear();
    lcd.print("Enchendo agua...");
    girarMotorAgua(0, 100);
    
    // CORREÇÃO: Envia estado de água vazia (A0) imediatamente ao iniciar o enchimento
    if (bluetoothConnected) {
      Serial.println("A0");
    }
  }
  
  // CORREÇÃO: Controle não-bloqueante do enchimento de água
  if (enchendoAgua) {
    // Verifica se o sensor ainda está vazio (HIGH) e se não passou do tempo máximo
    if (digitalRead(sensor_agua) == HIGH && (millis() - tempoInicioAgua) < 30000) {
      // Continua enchendo - não faz nada, apenas mantém o processo
    } else {
      // Para o enchimento: sensor cheio (LOW) ou tempo esgotado
      girarMotorAgua(1, 100);
      enchendoAgua = false;
      lcd.clear();
      
      // CORREÇÃO: Envia estado atual da água após parar o enchimento
      if (bluetoothConnected) {
        enviarEstadoAgua();
      }
    }
  }
}

void girarMotorAgua(int direcao, int passos) {
  digitalWrite(motorA_direcao, direcao);
  for (int i = 0; i < passos; i++) {
    digitalWrite(motorA_passo, HIGH);
    delay(2);
    digitalWrite(motorA_passo, LOW);
    delay(2);
  }
}

void girarMotorRacao(int direcao, int passos) {
  digitalWrite(motorR_direcao, direcao);
  for (int i = 0; i < passos; i++) {
    digitalWrite(motorR_passo, HIGH);
    delay(2);
    digitalWrite(motorR_passo, LOW);
    delay(2);
  }
}

void controlarRacaoModo1() {
  bool botaoAtual = (digitalRead(botao_racao) == HIGH);
  
  if (botaoAtual && !botaoPressionado && (millis() - ultimoTempoBotaoRacao > debounceDelay)) {
    botaoPressionado = true;
    ultimoTempoBotaoRacao = millis();
    
    if (!motorAberto) {
      enchendoRacao = true;
      lcd.clear();
      lcd.print("Enchendo racao...");
      forcarExibicaoRacao = true;
      exibindoMensagem = false; // Garante que outras mensagens não interfiram
      girarMotorRacao(0, passos180Graus);
      motorAberto = true;
    }
  }
  else if (!botaoAtual && botaoPressionado) {
    botaoPressionado = false;
    
    if (motorAberto) {
      enchendoRacao = true;
      lcd.clear();
      lcd.print("Enchendo racao...");
      forcarExibicaoRacao = true;
      exibindoMensagem = false; // Garante que outras mensagens não interfiram
      girarMotorRacao(1, passos180Graus);
      motorAberto = false;
      enchendoRacao = false;
      // CORREÇÃO: Parar exibição forçada IMEDIATAMENTE - SEM DELAY
      forcarExibicaoRacao = false;
      exibindoMensagem = false;
      lcd.clear();
    }
  }
  
  botaoPressionado = botaoAtual;
}

// CORREÇÃO: Só verifica horários se a agenda foi configurada
void controlarRacaoModo2(DateTime now) {
  // CORREÇÃO: Se não há horários configurados, não faz nada
  if (vezes == 0 || tempoLiberacaoRacao == 0) {
    return;
  }
  
  // Verifica se está liberando ração
  if (enchendoRacao) {
    unsigned long tempoDecorrido = millis() - tempoInicioRacao;
    unsigned long tempoRestante = (tempoLiberacaoRacao * 1000) - tempoDecorrido;
    
    // ALTERAÇÃO CRÍTICA: Exibição estável sem piscar durante contagem regressiva
    if (tempoRestante > 0) {
      if (!forcarExibicaoRacao) {
        lcd.clear();
        lcd.print("Enchendo racao..");
        forcarExibicaoRacao = true;
        exibindoMensagem = false;
      }
      lcd.setCursor(0, 1);
      lcd.print("Tempo: ");
      lcd.print((tempoRestante / 1000) + 1); // +1 para mostrar tempo inteiro
      lcd.print("s     ");
    }
    
    if (tempoDecorrido >= tempoLiberacaoRacao * 1000) {
      // Tempo acabou, fecha o compartimento
      lcd.clear();
      lcd.print("Fechando racao...");
      girarMotorRacao(1, passos180Graus);
      enchendoRacao = false;
      motorAberto = false;
      forcarExibicaoRacao = false;
      exibindoMensagem = false;
      lcd.clear();
      // CORREÇÃO: Envia R1 (cheio/parado) ao finalizar
      if (modo == 2 && bluetoothConnected) {
        enviarEstadoRacao();
      }
    }
    return;
  }
  
  // Verifica os horários programados
  for (int i = 0; i < vezes; i++) {
    if (now.hour() == hora_racao[i] && 
        now.minute() == minuto_racao[i] &&
        now.second() == segundo_racao[i] && // MELHORIA: Verifica também os segundos
        !horarioAcionado[i]) {
      
      horarioAcionado[i] = true;
      enchendoRacao = true;
      tempoInicioRacao = millis();
      
      lcd.clear();
      lcd.print("Horario ");
      lcd.print(i + 1);
      lcd.print(": Racao...");
      forcarExibicaoRacao = true;
      exibindoMensagem = false; // Garante que outras mensagens não interfiram
      
      // Abre o compartimento
      girarMotorRacao(0, passos180Graus);
      motorAberto = true;
      
      // CORREÇÃO: Envia R0 (enchendo) imediatamente ao iniciar
      if (modo == 2 && bluetoothConnected) {
        enviarEstadoRacao();
      }
      
      // Sai do loop após encontrar um horário válido
      break;
    }
  }
}

void configurarTimer() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Num de horarios:");
  int opcao = 2;
  bool selecionado = false;
  
  while (!selecionado) {
    int botao = lerTeclado();
    if (botao == 4) opcao = 2;
    else if (botao == 1) opcao = 3;
    else if (botao == 5) {
      vezes = opcao;
      selecionado = true;
    }
    lcd.setCursor(0, 1);
    if (opcao == 2) lcd.print(">2 vezes   ");
    else lcd.print(">3 vezes   ");
    delay(100);
  }
  
  // ALTERAÇÃO: Exibir mensagem de confirmação do número de horários selecionados
  lcd.clear();
  lcd.print(vezes);
  lcd.print(" Horarios Sel.");
  delay(1500);
  lcd.clear();
  
  for (int i = 0; i < vezes; i++) {
    configurarHorario(i);
  }
  
  configurarTempoLiberacao();
  
  lcd.clear();
  lcd.print("Agenda ajustada.");
  delay(2000);
  lcd.clear();
}

void configurarTempoLiberacao() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Liberar por:");
  int tempo = 5; // CORREÇÃO: Valor inicial 5s em vez de usar valor anterior
  bool selecionado = false;
  
  while (!selecionado) {
    int botao = lerTeclado();
    if (botao == 2) {
      tempo = (tempo + 1 > 60) ? 60 : (tempo + 1);
    } else if (botao == 3) {
      tempo = (tempo - 1 < 1) ? 1 : (tempo - 1);
    } else if (botao == 5) {
      tempoLiberacaoRacao = tempo;
      selecionado = true;
    }
    
    lcd.setCursor(0, 1);
    lcd.print(">");
    if (tempo < 10) lcd.print("0");
    lcd.print(tempo);
    lcd.print("s        ");
    delay(150);
  }
}

// MELHORIA: Configuração de horários com segundos
void configurarHorario(int index) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Horario "); 
  lcd.print(index + 1); 
  lcd.print(":");
  
  int hora = 12; // CORREÇÃO: Valor inicial 12h em vez de usar valor anterior
  int minuto = 0; // CORREÇÃO: Valor inicial 0min em vez de usar valor anterior
  int segundo = 0; // MELHORIA: Valor inicial 0 segundos
  int campo = 0;
  
  while (true) {
    int botao = lerTeclado();
    if (botao == 2) {
      if (campo == 0) hora = (hora + 1) % 24;
      else if (campo == 1) minuto = (minuto + 1) % 60;
      else if (campo == 2) segundo = (segundo + 1) % 60; // MELHORIA: Ajuste de segundos
    } else if (botao == 3) {
      if (campo == 0) hora = (hora - 1 + 24) % 24;
      else if (campo == 1) minuto = (minuto - 1 + 60) % 60;
      else if (campo == 2) segundo = (segundo - 1 + 60) % 60; // MELHORIA: Ajuste de segundos
    } else if (botao == 4) campo = (campo - 1 + 3) % 3; // MELHORIA: 3 campos agora
    else if (botao == 1) campo = (campo + 1) % 3; // MELHORIA: 3 campos agora
    else if (botao == 5) break;
    
    lcd.setCursor(0, 1);
    if (campo == 0) lcd.print(">");
    else lcd.print(" ");
    if (hora < 10) lcd.print("0");
    lcd.print(hora);
    lcd.print("h");
    
    if (campo == 1) lcd.print(">");
    else lcd.print(" ");
    if (minuto < 10) lcd.print("0");
    lcd.print(minuto);
    lcd.print("min");
    
    if (campo == 2) lcd.print(">"); // MELHORIA: Display de segundos
    else lcd.print(" ");
    if (segundo < 10) lcd.print("0");
    lcd.print(segundo);
    lcd.print("s");
    
    delay(150);
  }
  
  hora_racao[index] = hora;
  minuto_racao[index] = minuto;
  segundo_racao[index] = segundo; // MELHORIA: Armazena segundos
  
  // ALTERAÇÃO: Adicionada mensagem de confirmação com tempo
  lcd.clear();
  lcd.print("Hor. ");
  lcd.print(index + 1);
  lcd.print(" Ajustado!");
  lcd.setCursor(0, 1);
  // Formata o horário no formato xx:xx:xx
  if (hora < 10) lcd.print("0");
  lcd.print(hora);
  lcd.print(":");
  if (minuto < 10) lcd.print("0");
  lcd.print(minuto);
  lcd.print(":");
  if (segundo < 10) lcd.print("0");
  lcd.print(segundo);
  
  delay(1500); // Pequeno delay para prevenir confirmação acidental
  lcd.clear();
}

void verificarTecladoModo2() {
  int botao = lerTeclado();
  if ((botao == 1 || botao == 4) && millis() - ultimoTempoNavegacao > navegacaoDelay) {
    ultimoTempoNavegacao = millis();
    for (int i = 0; i < vezes; i++) {
      exibirHorarioAgenda(i);
      delay(2000);
    }
    exibirTempoLiberacao();
    delay(2000);
    
    DateTime now = rtcPresent ? rtc.now() : DateTime(F(__DATE__), F(__TIME__));
    exibirModoAtivo(2, now);
  }
  if (botao == 5) {
    modo = 0;
    modoConfigurado = false;
    for (int i = 0; i < 3; i++) {
      horarioAcionado[i] = false;
    }
    lcd.clear();
  }
}

// MELHORIA: Exibe horários com segundos
void exibirHorarioAgenda(int index) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Agenda "); 
  lcd.print(index + 1); 
  lcd.print(":");
  lcd.setCursor(0, 1);
  if (hora_racao[index] < 10) lcd.print("0");
  lcd.print(hora_racao[index]);
  lcd.print(":");
  if (minuto_racao[index] < 10) lcd.print("0");
  lcd.print(minuto_racao[index]);
  lcd.print(":");
  if (segundo_racao[index] < 10) lcd.print("0"); // MELHORIA: Mostra segundos
  lcd.print(segundo_racao[index]);
  lcd.print("   ");
}

void exibirTempoLiberacao() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Tempo Ajustado:");
  lcd.setCursor(0, 1);
  if (tempoLiberacaoRacao < 10) lcd.print("0");
  lcd.print(tempoLiberacaoRacao);
  lcd.print("s");
  lcd.print("        ");
}

void verificarTecladoModo1() {
  int botao = lerTeclado();
  if (botao == 5) {
    modo = 0;
    lcd.clear();
  }
}

// CORREÇÃO: Função ajustarHoraAtual com segundos
void ajustarHoraAtual() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Hora Atual: ");
  
  DateTime now = rtc.now();
  int hora = now.hour();
  int minuto = now.minute();
  int segundo = now.second(); // CORREÇÃO: Adicionado segundos
  int campo = 0;
  
  while (true) {
    int botao = lerTeclado();
    if (botao == 2) {
      if (campo == 0) hora = (hora + 1) % 24;
      else if (campo == 1) minuto = (minuto + 1) % 60;
      else if (campo == 2) segundo = (segundo + 1) % 60; // CORREÇÃO: Adicionado ajuste de segundos
    } else if (botao == 3) {
      if (campo == 0) hora = (hora - 1 + 24) % 24;
      else if (campo == 1) minuto = (minuto - 1 + 60) % 60;
      else if (campo == 2) segundo = (segundo - 1 + 60) % 60; // CORREÇÃO: Adicionado ajuste de segundos
    } else if (botao == 4) campo = (campo - 1 + 3) % 3; // CORREÇÃO: 3 campos agora
    else if (botao == 1) campo = (campo + 1) % 3; // CORREÇÃO: 3 campos agora
    else if (botao == 5) break;
    
    lcd.setCursor(0, 1);
    if (campo == 0) lcd.print(">");
    else lcd.print(" ");
    if (hora < 10) lcd.print("0");
    lcd.print(hora);
    lcd.print("h");
    
    if (campo == 1) lcd.print(">");
    else lcd.print(" ");
    if (minuto < 10) lcd.print("0");
    lcd.print(minuto);
    lcd.print("min");
    
    if (campo == 2) lcd.print(">"); // CORREÇÃO: Adicionado display de segundos
    else lcd.print(" ");
    if (segundo < 10) lcd.print("0");
    lcd.print(segundo);
    lcd.print("s");
    
    delay(150);
  }
  
  rtc.adjust(DateTime(now.year(), now.month(), now.day(), hora, minuto, segundo)); // CORREÇÃO: Incluído segundos
  lcd.clear();
  lcd.print("Hora ajustada!");
  delay(2000);
  lcd.clear();
}