#include <Arduino.h>

//definición de pines
const int PinENA = 8;
const int PinIN1 = 7;
const int PinIN2 = 6;

void setup() {
  // inicializar la comunicación serial a 9600 bits por segundo:
  Serial.begin(9600);
  // configuramos los pines como salida
  pinMode(PinENA, OUTPUT);
  pinMode(PinIN1, OUTPUT);
  pinMode(PinIN2, OUTPUT);
  //Inicializamos los pines
  analogWrite(PinENA,0);
  digitalWrite (PinIN1, LOW);
  digitalWrite (PinIN2, LOW);
  
}

void loop() {
  
  MotorHorario(200); //Motor horario con velocidad de 200(PWM 0-250)
  Serial.println("Giro del Motor en sentido horario");
  delay(5000);
  
  MotorAntihorario(200);  //Motor horario con velocidad de 200(PWM 0-250)
  Serial.println("Giro del Motor en sentido antihorario");
  delay(5000);
  
  MotorStop(); //Motor Apagado
  Serial.println("Motor Detenido");
  delay(3000);
  
}

//función para girar el motor en sentido horario
void MotorHorario(int velocidad) //velocidad 0-250
{
  digitalWrite (PinIN1, HIGH);
  digitalWrite (PinIN2, LOW);
  analogWrite(PinENA,velocidad);
}
//función para girar el motor en sentido antihorario
void MotorAntihorario(int velocidad) //velocidad 0-250
{
  digitalWrite (PinIN1, LOW);
  digitalWrite (PinIN2, HIGH);
  analogWrite(PinENA,velocidad);
}

//función para apagar el motor
void MotorStop()
{
  digitalWrite (PinIN1, LOW);
  digitalWrite (PinIN2, LOW);
  analogWrite(PinENA,0);
}