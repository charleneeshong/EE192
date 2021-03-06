#include "mbed.h"
//TELEMETRY
#include "telemetry.h"
#include "MODSERIAL.h"

//TELEMETRY
MODSERIAL telemetry_serial(PTC15, PTC14);

telemetry::MbedHal telemetry_hal(telemetry_serial);
telemetry::Telemetry telemetry_obj(telemetry_hal);

//telemetry::Numeric<float> tele_servo_pwm(telemetry_obj, "servo", "Servo PWM", "PWM (us)", 0);
telemetry::NumericArray<uint16_t, 128> tele_linescan_cam1(telemetry_obj, "Camera 1", "Linescan", "ADC", 0);
telemetry::NumericArray<uint16_t, 128> tele_linescan_cam2(telemetry_obj, "Camera 2", "Linescan", "ADC", 0);
telemetry::Numeric<float> tele_linewidth_cam1(telemetry_obj, "Linescan", "Linewidth","pixels",0);
telemetry::Numeric<float> tele_linewidth_cam2(telemetry_obj, "Linescan", "number of lines", "count", 0);


//telemetry::NumericArray<uint16_t, 128> tele_linescan_derivative_cam1(telemetry_obj, "linescan", "derivative", "camera", 0);
//telemetry::NumericArray<uint16_t, 128> tele_linescan_derivative_cam2(telemetry_obj, "linescan", "derivative", "camera", 0);
telemetry::Numeric<uint32_t> tele_time_ms(telemetry_obj, "time", "Time", "ms", 0);
//telemetry::Numeric<float> tele_cam1_max(telemetry_obj, "camera 1","max value", "pixel value", 0);
//telemetry::Numeric<float> tele_cam2_max(telemetry_obj, "camera 2","max value", "pixel value", 0);
//telemetry::Numeric<float> tele_velocity_velMA(telemetry_obj, "VELOCITY", "m/s", "count", 0);
//telemetry::Numeric<float> tele_cam1_max_i(telemetry_obj, "camera 1", "max pixel index", "index", 0);
//elemetry::NumericArray<uint16_t, 128> tele_diff_cam1(telemetry_obj, "CAMERA", "Linescan", "frame difference",0);
//telemetry::NumericArray<uint16_t, 128> tele_diff_cam2(telemetry_obj, "CAMERA", "Linescan", "frame difference",0);




// Line Camera Pins
//CAM1
AnalogIn A0_CAM1(PTC10);
DigitalOut SI_CAM1(PTC16);
DigitalOut CLK_CAM1(PTC17);

//CAM2
AnalogIn A0_CAM2(PTC11);
DigitalOut SI_CAM2(PTC2);
DigitalOut CLK_CAM2(PTC3);

// Interrupts Pins
//Hall Sensors
InterruptIn hallA(PTC12);
InterruptIn hallB(PTA2);

// Tickers
Ticker timestep;

// Digital pin to read from Hall Sensor
PwmOut motor(PTD1);
DigitalOut brakeEN(PTB9);

// Servo Pin
PwmOut servo(PTA1);

//Push Buttons
DigitalIn SELECT(PTC4);
DigitalIn UP(PTD0);
DigitalIn DOWN(PTD3);

volatile int revs = 0;
volatile int revsA = 0;
volatile int revsB = 0;

//Initialize needed variables
volatile float refVel = 2.05; // Corresponds to m/s
const float dt = 0.010; //PI interrupt in SECONDS 50 Hz
const float Kp = 0.18; //Proportional Gain
const float Ki = 0.01; //Integral Gain
const float maxPWM = 0.20;
const float minPWM = 0.0;

//transform to m/s with gear ratio (9:1 gear ratio) (r = 32.5 mm)
const float r = 0.0325; // meters
const float gear = 9.0; // 9:1 gear down ratio
const float velTransform = (2.0*3.141592*r) / (gear*4*dt);

//Volatile variables to be used in interrupts
volatile float vel = 0.0;
volatile float velMA = 0.0;
volatile float velSum = 0.0;
volatile int resolution = 10;
volatile int count = 1;
volatile int ramp = 1;

//Volatile CONTROL-related variables to be used in interrupts
volatile float dutyInput = 0.0;
volatile float intgrlTerm = 0.0;
volatile float err = 0.0;

float l_end = 1110.0;
float middle = 1520.0;
float r_end = 1970.0;

// Servo steps
float divi = 128.0;
float steps = r_end - l_end;
//float step = steps/divi;
float step = steps/114;

// Servo position
volatile float pw = 0.0;
volatile float old_pw = 0.0;
volatile float max_cam1 = 0.0;
volatile float max_cam2 = 0.0;
volatile int max_i_cam1 = 0;
volatile int max_i_cam2;
volatile float pos = 0.0;
volatile int old_max_i = 0;
volatile float old_max_cam1;
volatile float old_max_cam2;

// Line cam vars
int PIXELS = 128;
unsigned short data_cam1[128];
unsigned short data_cam2[128];
unsigned short prev_data_cam1[128];
unsigned short prev_data_cam2[128];
unsigned short derivative_cam1[128];
unsigned short derivative_cam2[128];
unsigned short diff_cam1[128];
unsigned short diff_cam2[128];


const float thresh = 0.8;


//CALIBRATION VARIABLES
volatile int linewidth_cam1 = 0;
volatile int linewidth_cam2 = 0;
volatile int lines_cam1 = 0;
volatile int lines_cam2 = 0;
volatile bool rising_edge_cam1 = false;
volatile bool rising_edge_cam2 = false;


//Constant STEERING CONTROL-related variables
const float Kp_steer = 0.68; //in degrees of steering per pixel error
const float Kd_steer = 0.35; //in degrees/s of steering change per pixel error
const float refSteer = 64.0;

//works pretty well with .65 and .25
//ditto with .68 and .40


//Volatile STEERING CONTROL-related variables
volatile float prev_steer_err = 0.0;
volatile float err_steer = 0.0;
volatile float d_err_steer = 0.0;
volatile float steer_angle = 0.0;
volatile float prev_steer_angle = 0.0;

const float l_end_angle = -20.0;
const float r_end_angle = 20.0;

volatile float exp_time = 0.001; //1ms



void calculateLinewidth() {
	linewidth_cam1 = 0;
	linewidth_cam2 = 0;
	rising_edge_cam1 = false;
	rising_edge_cam2 = false;
	for (int i = 0; i < PIXELS; i++) {
		//tele_linescan_derivative_cam1[i] = derivative_cam1[i];
		//tele_linescan_derivative_cam2[i] = derivative_cam2[i];
		if (i > 0) {

		}
		if (data_cam1[i] > thresh * max_cam1) {
			linewidth_cam1 += 1;
			rising_edge_cam1 = true;
		} else if (rising_edge_cam1) {
			rising_edge_cam1 = false;
		}
		if (data_cam2[i] > thresh * max_cam2) {
			linewidth_cam2 += 1;
			rising_edge_cam2 = true;
		} else if (rising_edge_cam2) {
			rising_edge_cam2 = false ;
		}
	}

	tele_linewidth_cam1 = linewidth_cam1;
	tele_linewidth_cam2 = linewidth_cam2;
}




//VELOCITY!
//ISR for HALL sensor reading
void rpmCounterA() {
    revsA++;
}

void rpmCounterB() {
    revsB++;
}

float angle2pw_us(float angle) {
    float servo_pulse_width = angle * (r_end - l_end) / (r_end_angle - l_end_angle) + middle;
    if (servo_pulse_width > r_end) {
            servo_pulse_width = r_end;
    }
    else if (servo_pulse_width < l_end) {
        servo_pulse_width = l_end;
    }
    return servo_pulse_width;
}

void steeringPD() {
	max_cam1 = 0;
	max_cam2 = 0;



	//Fake ANALOG READ
	SI_CAM1 = 1;
	SI_CAM2 = 1;
	CLK_CAM1 = 1;
	CLK_CAM2 = 1;
	SI_CAM1 = 0;
	SI_CAM2 = 0;
	CLK_CAM1 = 0;
	CLK_CAM2 = 0;
	for(int i = 0; i < PIXELS; i++) {
		CLK_CAM1 = 1;
		CLK_CAM2 = 1;
		CLK_CAM1 = 0;
		CLK_CAM2 = 0;
	}

	wait(exp_time);
	SI_CAM1 = 1;
	SI_CAM2 = 1;
	CLK_CAM1 = 1;
	CLK_CAM2 = 1;
	SI_CAM1 = 0;
	SI_CAM2 = 0;
	data_cam1[0] = A0_CAM1.read_u16();
	CLK_CAM1 = 0;
	data_cam2[0] = A0_CAM2.read_u16();
	CLK_CAM2 = 0;

	for(int i = 1; i < PIXELS; i++) {


		CLK_CAM1 = 1;
		CLK_CAM2 = 1;
		data_cam1[i] = A0_CAM1.read_u16();
		data_cam2[i] = A0_CAM2.read_u16();

		//TELEMETRY
		tele_linescan_cam1[i] = data_cam1[i];
		tele_linescan_cam2[i] = data_cam2[i];
		if (data_cam1[i] > max_cam1) { //calc max
			max_cam1 = data_cam1[i];
			max_i_cam1 = i;
		}
		if (data_cam2[i] > max_cam2) {
			max_cam2 = data_cam2[i];
			max_i_cam2 = i;
		}

		CLK_CAM1 = 0;
		CLK_CAM2 = 0;

//		diff_cam1[i] = data_cam1[i] - prev_data_cam1[i];
//		diff_cam2[i] = data_cam2[i] - prev_data_cam2[i];
//		tele_diff_cam1[i] = diff_cam1[i];
//		tele_diff_cam2[i] = diff_cam2[i];
//		prev_data_cam1[i] = data_cam1[i];
//		prev_data_cam2[i] = data_cam2[i];
	}

//	tele_cam1_max = max_cam1;
//	tele_cam2_max = max_cam2;
	calculateLinewidth();
	if (linewidth_cam1 < 15 && linewidth_cam1 > 6) {
		pos = r_end - (step * max_i_cam1);
		pw = pos * (0.000001);  //average of the pw
	} else if (linewidth_cam2 < 12 && linewidth_cam2 > 5) {
		pos = r_end - (step * max_i_cam2);
		pw = pos * (0.000001);  //average of the pw
	}
	pw = pw;



//	err_steer = refSteer - max_i_cam1;
//	d_err_steer = (err_steer - prev_steer_err) / dt;
//	steer_angle = Kp_steer * err_steer + Kd_steer * d_err_steer;
//	pw = angle2pw_us(steer_angle);



	//ACTUATION!

	////Send INPUT value to servo
	if (max_cam1!= 0) {
		servo.pulsewidth(pw);
		//TELEMETRY
		//tele_servo_pwm = pw*1000000;
	}
}

void velocity_OPENLOOP()
{
	motor.write(0.10f);
}

void velocityPI() {
	//Calculate velocity (revolutions/fixed time unit)
	revs = revsA + revsB;
	vel = (float) (revs) * velTransform;

	//Code begins when switch is turned on
	if (vel > 0.0){
		//Calculate moving average
		if (count < resolution) {
			velSum = velSum + vel;
			velMA = vel;
			//printf("%i\r\n",count);
			count++;
			//printf("%f\r\n",velMA);
		} else if (count == resolution) {
			velMA = (velSum + vel)/resolution;
			//printf("%i\r\n",count);
			count++;
			//printf("%f\r\n",velMA);
		} else {
			velMA = velMA + (vel/resolution) - (velMA/resolution);
			//printf("%f\r\n",velMA);
		}

		//tele_velocity_velMA = velMA;

		//Calculate ERROR
		err = refVel - velMA;

		//Accumulate error for integral term
		intgrlTerm = intgrlTerm + (Ki*err);

		//Anti-Windup check!
		if (intgrlTerm > maxPWM){
			intgrlTerm = maxPWM;
		} else if (intgrlTerm < minPWM){
			intgrlTerm = minPWM;
		}

		//Calculate INPUT value
		//dutyInput = (Kp*err) + intgrlTerm;
		dutyInput = (Kp*err);

		//Clamp REFERENCE value if needed
		if (dutyInput > maxPWM) {
			dutyInput = maxPWM;
		} else if (dutyInput < minPWM) {
			dutyInput = minPWM;
		}

		if (ramp<100){
			dutyInput = 0.10f;
			ramp++;
		}

		//ACTUATION!
		//Send INPUT value to motor
		motor.write(dutyInput);
		revsA = 0;
		revsB = 0;
	}
}

void control() {
//	tele_hall_revs = revsB/1.0;
//	revsB = 0;
//	tele_servo_pwm = pw*1000000;
//
//	if(UP) {
//		tele_button_push = 1.0;
//		refVel = refVel + 0.01;
//	} else if(DOWN) {
//		tele_button_push = 1.0;
//		refVel = refVel - 0.01;
//	} else {
//		tele_button_push = 0.0;
//		refVel = refVel;
//	}

	steeringPD();
//	if (pw > middle + 500) {
//		refVel = 1.7 - (pw * 1000000 - middle) / 1000;
//	} else if (pw < middle - 500) {
//		refVel = 1.7 - (middle - pw * 1000000) / 1000;
//	} else {
//		refVel = 1.7;
//	}

	velocityPI();

}

//TELEMETRY
void print_serial() {
	tele_time_ms = telemetry_hal.get_time_ms();
    telemetry_obj.do_io();
}

//Function to initialize system
void setup() {

    //set pwm periods
    motor.period_us(50); // 20 kHz frequency
    servo.period_ms(10); //Set servo PWM period = 3ms

    //set regular interrupts
    hallA.rise(&rpmCounterA);
    hallA.fall(&rpmCounterA);

    hallB.rise(&rpmCounterB);
    hallB.fall(&rpmCounterB);

    //set Ticker interrupts
    timestep.attach(&control,dt);

    brakeEN.write(1);

    //set warm-up speed and command motor
    dutyInput = 0.10f;
    motor.write(dutyInput);
}

//Main CODE
int main() {
    //run setup function
    //TELEMETRY
    telemetry_serial.baud(38400);
    //tele_servo_pwm.set_limits(0.0, 2000.0); // lower bound, upper bound
    telemetry_obj.transmit_header();
	setup();


    //run while loop
	int counter = 0;
    while (1) {
    	counter++;
    	if (counter % 2 == 1) {
    		print_serial();
    	}
    }
}
