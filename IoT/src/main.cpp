#include <Arduino.h>
//MOTOR SHIELD L298N
int IN1 = 4; //d2
int IN2 = 13; //d3
int PWM1 = 5; //d1

void setup() {
  // inicializar la comunicación serial a 9600 bits por segundo:
  Serial.begin(9600);
  // configuramos los pines como salida
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
}

void loop() {
  
  MotorHorario();
  Serial.println("Giro del Motor en sentido horario");
  delay(5000);
  
  MotorAntihorario();
  Serial.println("Giro del Motor en sentido antihorario");
  delay(5000);
  
  MotorStop();
  Serial.println("Motor Detenido");
  delay(3000);
  
}

//función para girar el motor en sentido horario
void MotorHorario()
{
  digitalWrite (IN1, HIGH);
  digitalWrite (IN2, LOW);
}
//función para girar el motor en sentido antihorario
void MotorAntihorario()
{
  digitalWrite (IN1, LOW);
  digitalWrite (IN2, HIGH);
}

//función para apagar el motor
void MotorStop()
{
  digitalWrite (IN1, LOW);
  digitalWrite (IN2, LOW);
}
