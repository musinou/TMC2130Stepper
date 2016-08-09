#define TMC2130DEBUG

#if defined(ARDUINO) && ARDUINO >= 100
	#include <Arduino.h>
#endif
#include <SPI.h>
#include "TMC2130Stepper.h"

#define REG_GCONF			  0x00
#define REG_GSTAT			  0x01
#define REG_IOIN			  0x04
#define REG_IHOLD_IRUN		  0x10
#define REG_TPOWERDOWN		  0x11
#define REG_TSTEP 			  0x12
#define REG_TPWMTHRS		  0x13
#define REG_TCOOLTHRS		  0x14
#define REG_THIGH			  0x15
#define REG_XDIRECT			  0x2D
#define REG_VDCMIN			  0x33
#define REG_MSCNT			  0x6A
#define REG_MSCURACT		  0x6B
#define REG_CHOPCONF		  0x6C
#define REG_COOLCONF		  0X6D
#define REG_DCCTRL			  0x6E
#define REG_DRVSTATUS		  0x6F
#define REG_PWMCONF			  0X70
#define REG_PWMSCALE		  0X71
#define REG_ENCMCTRL		  0x72
#define REG_LOSTSTEPS		  0x73

TMC2130Stepper::TMC2130Stepper(uint8_t pinEN, uint8_t pinDIR, uint8_t pinStep, uint8_t pinCS) {
	_started = false;

	this->_pinEN = pinEN;
	this->_pinDIR = pinDIR;
	this->_pinSTEP = pinStep;
	this->_pinCS = pinCS;
}

void TMC2130Stepper::begin() {
#ifdef TMC2130DEBUG
	Serial.println("TMC2130 Stepper driver library");
	Serial.print("Enable pin: ");
	Serial.println(_pinEN);
	Serial.print("Direction pin: ");
	Serial.println(_pinDIR);
	Serial.print("Step pin: ");
	Serial.println(_pinSTEP);
	Serial.print("Chip select pin: ");
	Serial.println(_pinCS);
#endif
	//set pins
	pinMode(_pinEN, OUTPUT);
	pinMode(_pinDIR, OUTPUT);
	pinMode(_pinSTEP, OUTPUT);
	pinMode(_pinCS, OUTPUT);
	digitalWrite(_pinEN, HIGH); //deactivate driver (LOW active)
	digitalWrite(_pinDIR, LOW); //LOW or HIGH
	digitalWrite(_pinSTEP, LOW);
	digitalWrite(_pinCS, HIGH);

	pinMode(12, OUTPUT);
	pinMode(11, INPUT);
	pinMode(13, OUTPUT);
	digitalWrite(12, LOW);
	digitalWrite(11, HIGH);
	digitalWrite(13, LOW);

	SPI.begin();
	_started = true;
}

uint32_t TMC2130Stepper::send2130(uint8_t addressByte, uint32_t *config, uint32_t value, uint32_t mask) {
	uint8_t s;

	SPI.beginTransaction(SPISettings(16000000/8, MSBFIRST, SPI_MODE3));
	digitalWrite(_pinCS, LOW);

	s = SPI.transfer(addressByte & 0xFF);
#ifdef TMC2130DEBUG
	Serial.print("Address byte: ");
	Serial.println(addressByte, HEX);
	Serial.print("Config: ");
	Serial.println(*config, HEX);
	Serial.print("Value: ");
	Serial.println(value, HEX);
	Serial.print("Mask: ");
	Serial.println(mask, HEX);
	Serial.print("s: ");
	Serial.println(s, HEX);
#endif

	if (addressByte << 7) { // Check if WRITE command
		*config &= ~mask; // Clear bits being set
		*config |= (value & mask); // Set new values
		SPI.transfer((*config >> 24) & 0xFF);
		SPI.transfer((*config >> 16) & 0xFF);
		SPI.transfer((*config >>  8) & 0xFF);
		SPI.transfer(*config & 0xFF);
#ifdef TMC2130DEBUG
		Serial.println("WRITE cmd");
		Serial.print("New config: ");
		Serial.println(*config, HEX);
#endif
	} else { // READ command
		*config  = SPI.transfer((*config >> 24) & 0xFF);
		*config <<= 8;
		*config |= SPI.transfer((*config >> 16) & 0xFF);
		*config <<= 8;
		*config |= SPI.transfer((*config >>  8) & 0xFF);
		*config <<= 8;
		*config |= SPI.transfer( *config 		& 0xFF);
#ifdef TMC2130DEBUG
		Serial.println("WRITE cmd");
		Serial.print("New config: ");
		Serial.println(*config, HEX);
#endif
	}

	digitalWrite(_pinCS, HIGH);
	SPI.endTransaction();

	return s;
}
//#ifdef TMC2130DEBUG
void TMC2130Stepper::checkStatus() {
	uint32_t data = 0x0;
	uint8_t s;

    //show REG_GSTAT
    s = send2130(READ|REG_GSTAT, &data, 0x0, 0x0);
    Serial.print("GSTAT:     0x0");
    Serial.print(data, HEX);
    Serial.print("\t - ");
    Serial.print("Status: 0x");
    Serial.print(s, HEX);
    if(s & 0x01) Serial.print(" reset");
    if(s & 0x02) Serial.print(" error");
    if(s & 0x04) Serial.print(" sg2");
    if(s & 0x08) Serial.print(" standstill");
    Serial.println(" ");

    //show REG_DRVSTATUS
    s = send2130(READ|REG_DRVSTATUS, &data, 0x0, 0x0);
    Serial.print("DRVSTATUS: 0x");
    Serial.print(data, HEX);
    Serial.print("\t - ");
    Serial.print("Status: 0x");
    Serial.print(s, HEX);
    if(s & 0x01) Serial.print(" reset");
    if(s & 0x02) Serial.print(" error");
    if(s & 0x04) Serial.print(" sg2");
    if(s & 0x08) Serial.print(" standstill");
    Serial.println(" ");
}
//#endif

void TMC2130Stepper::setSSSCurrent() {
	setCurrent(1000, 1);
}

/*	
	Requested current = mah = I_rms/1000
	Equation for current:
	I_rms = (CS+1)/32 * V_fs/(R_sense+0.02ohm) * 1/sqrt(2)
	Solve for CS ->
	CS = 32*sqrt(2)*I_rms*(R_sense+0.02)/V_fs - 1
	
	Example:
	vsense = 0b0 -> V_fs = 0.325V
	mah = 1640mAh = I_rms/1000 = 1.64A
	R_sense = 0.10 Ohm
	->
	CS = 32*sqrt(2)*1.64*(0.10+0.02)/0.325 - 1 = 27.4
	CS = 27
*/	
void TMC2130Stepper::setCurrent(uint16_t mah, float Rsense) {
	float V_fs;
	if (val_vsense)
		V_fs = 0.325;
	else
		V_fs = 0.180;
	uint8_t CS = 32.0*1.41421*mah/1000.0*(Rsense+0.02)/V_fs - 1;
	set_IRUN(CS);
}

///////////////////////////////////////////////////////////////////////////////////////
// REG_GCONF

uint32_t TMC2130Stepper::get_GCONF() {
#ifdef TMC2130DEBUG
	Serial.println("Get GCONF: ");
#endif
	uint32_t data = 0x0;
	send2130(READ|REG_GCONF, &data, 0x0, 0x0);
	return data;
}

void TMC2130Stepper::set_GCONF(uint32_t value) {
	send2130(WRITE|REG_GCONF, &cur_GCONF, value, 0xFFFFFFFF);
}

uint8_t TMC2130Stepper::get_I_SCALE_ANALOG() {return val_i_scale_analog;}

void TMC2130Stepper::set_I_SCALE_ANALOG(uint8_t value) {
	if (value > 1) value = 1;
	val_i_scale_analog = value;
	send2130(WRITE|REG_GCONF, &cur_GCONF, value, 0b1);
}

uint8_t TMC2130Stepper::get_INTERNAL_R_SENSE() {return val_internal_r_sense;}

void TMC2130Stepper::set_INTERNAL_R_SENSE(uint8_t value) {
	if (value > 1) value = 1;
	val_internal_r_sense = value;
	send2130(WRITE|REG_GCONF, &cur_GCONF, (uint32_t)value << 1, (uint32_t)0b1 << 1);
}

uint8_t TMC2130Stepper::get_EN_PWM_MODE() {return val_en_pwm_mode;}

void TMC2130Stepper::set_EN_PWM_MODE(uint8_t value) {
	if (value > 1) value = 1;
	val_en_pwm_mode = value;
	send2130(WRITE|REG_GCONF, &cur_GCONF, (uint32_t)value << 2, (uint32_t)0b1 << 2);
}

uint8_t TMC2130Stepper::get_ENC_COMMUTATION() {return val_enc_commutation;}

void TMC2130Stepper::set_ENC_COMMUTATION(uint8_t value) {
	if (value > 1) value = 1;
	val_enc_commutation = value;
	send2130(WRITE|REG_GCONF, &cur_GCONF, (uint32_t)value << 3, (uint32_t)0b1 << 3);
}

uint8_t TMC2130Stepper::get_SHAFT() {return val_shaft;}

void TMC2130Stepper::set_SHAFT(uint8_t value) {
#ifdef TMC2130DEBUG
	Serial.print("Set shaft to ");
	Serial.println(value);
#endif
	if (value > 1) value = 1;
	val_shaft = value;
	send2130(WRITE|REG_GCONF, &cur_GCONF, (uint32_t)value << 4, (uint32_t)0b1 << 4);
}

uint8_t TMC2130Stepper::get_DIAG0_ERROR() {return val_diag0_error;}

void TMC2130Stepper::set_DIAG0_ERROR(uint8_t value) {
	if (value > 1) value = 1;
	val_diag0_error = value;
	send2130(WRITE|REG_GCONF, &cur_GCONF, (uint32_t)value << 5, (uint32_t)0b1 << 5);
}

uint8_t TMC2130Stepper::get_DIAG0_OTPW() {return val_diag0_otpw;}

void TMC2130Stepper::set_DIAG0_OTPW(uint8_t value) {
	if (value > 1) value = 1;
	val_diag0_otpw = value;
	send2130(WRITE|REG_GCONF, &cur_GCONF, (uint32_t)value << 6, (uint32_t)0b1 << 6);
}

uint8_t TMC2130Stepper::get_DIAG0_STALL() {return val_diag0_stall;}

void TMC2130Stepper::set_DIAG0_STALL(uint8_t value) {
	if (value > 1) value = 1;
	val_diag0_stall = value;
	send2130(WRITE|REG_GCONF, &cur_GCONF, (uint32_t)value << 7, (uint32_t)0b1 << 7);
}

uint8_t TMC2130Stepper::get_DIAG1_STALL() {return val_diag1_stall;}

void TMC2130Stepper::set_DIAG1_STALL(uint8_t value) {
	if (value > 1) value = 1;
	val_diag1_stall = value;
	send2130(WRITE|REG_GCONF, &cur_GCONF, (uint32_t)value << 8, (uint32_t)0b1 << 8);
}

uint8_t TMC2130Stepper::get_DIAG1_INDEX() {return val_diag1_index;}

void TMC2130Stepper::set_DIAG1_INDEX(uint8_t value) {
	if (value > 1) value = 1;
	val_diag1_index = value;
	send2130(WRITE|REG_GCONF, &cur_GCONF, (uint32_t)value << 9, (uint32_t)0b1 << 9);
}

uint8_t TMC2130Stepper::get_DIAG1_ONSTATE() {return val_diag1_onstate;}

void TMC2130Stepper::set_DIAG1_ONSTATE(uint8_t value) {
	if (value > 1) value = 1;
	val_diag1_onstate = value;
	send2130(WRITE|REG_GCONF, &cur_GCONF, (uint32_t)value << 10, (uint32_t)0b1 << 10);
}

uint8_t TMC2130Stepper::get_DIAG1_STEPS_SKIPPED() {return val_diag1_steps_skipped;}

void TMC2130Stepper::set_DIAG1_STEPS_SKIPPED(uint8_t value) {
	if (value > 1) value = 1;
	val_diag1_steps_skipped = value;
	send2130(WRITE|REG_GCONF, &cur_GCONF, (uint32_t)value << 11, (uint32_t)0b1 << 11);
}

uint8_t TMC2130Stepper::get_DIAG0_INT_PUSHPULL() {return val_diag0_int_pushpull;}

void TMC2130Stepper::set_DIAG0_INT_PUSHPULL(uint8_t value) {
	if (value > 1) value = 1;
	val_diag0_int_pushpull = value;
	send2130(WRITE|REG_GCONF, &cur_GCONF, (uint32_t)value << 12, (uint32_t)0b1 << 12);
}

uint8_t TMC2130Stepper::get_DIAG1_PUSHPULL() {return val_diag1_pushpull;}

void TMC2130Stepper::set_DIAG1_PUSHPULL(uint8_t value) {
	if (value > 1) value = 1;
	val_diag1_pushpull = value;
	send2130(WRITE|REG_GCONF, &cur_GCONF, (uint32_t)value << 13, (uint32_t)0b1 << 13);
}

uint8_t TMC2130Stepper::get_SMALL_HYSTERISIS() {return val_small_hysterisis;}

void TMC2130Stepper::set_SMALL_HYSTERISIS(uint8_t value) {
	if (value > 1) value = 1;
	val_small_hysterisis = value;
	send2130(WRITE|REG_GCONF, &cur_GCONF, (uint32_t)value << 14, (uint32_t)0b1 << 14);
}

uint8_t TMC2130Stepper::get_STOP_ENABLE() {return val_stop_enable;}

void TMC2130Stepper::set_STOP_ENABLE(uint8_t value) {
	if (value > 1) value = 1;
	val_stop_enable = value;
	send2130(WRITE|REG_GCONF, &cur_GCONF, (uint32_t)value << 15, (uint32_t)0b1 << 15);
}

uint8_t TMC2130Stepper::get_DIRECT_MODE() {return val_direct_mode;}

void TMC2130Stepper::set_DIRECT_MODE(uint8_t value) {
	if (value > 1) value = 1;
	val_direct_mode = value;
	send2130(WRITE|REG_GCONF, &cur_GCONF, (uint32_t)(uint32_t)value << 16, (uint32_t)0b1 << 16);
}

/*
bit 18 not implemented:
test_mode 0: 
Normal operation 1:
Enable analog test output on pin DCO. IHOLD[1..0] selects the function of DCO: 
0…2: T120, DAC, VDDH Attention:
Not for user, set to 0 for normal operation! 
*/

///////////////////////////////////////////////////////////////////////////////////////
// REG_IHOLD_IRUN

uint8_t TMC2130Stepper::get_IHOLD() {return val_ihold;}

void TMC2130Stepper::set_IHOLD(uint8_t value) {
	if (value > 31) value = 31;
	val_ihold = value;
	send2130(WRITE|REG_IHOLD_IRUN, &cur_IHOLD_IRUN, value, 0x1F);
}

uint8_t TMC2130Stepper::get_IRUN() {return val_irun;}

void TMC2130Stepper::set_IRUN(uint8_t value) {
	if (value > 31) value = 31;
	val_irun = value;
	send2130(WRITE|REG_IHOLD_IRUN, &cur_IHOLD_IRUN, (uint32_t)value << 8, 0x1F00);
}

uint8_t TMC2130Stepper::get_IHOLD_DELAY() {return val_ihold_delay;}

void TMC2130Stepper::set_IHOLD_DELAY(uint8_t value) {
	if (value > 15) value = 15;
	val_ihold_delay = value;
	send2130(WRITE|REG_IHOLD_IRUN, &cur_IHOLD_IRUN, (uint32_t)(uint32_t)value << 16, 0xF000);
}

///////////////////////////////////////////////////////////////////////////////////////
// REG_TPOWERDOWN

uint8_t TMC2130Stepper::get_TPOWERDOWN() {return val_tpowerdown;}

void TMC2130Stepper::set_TPOWERDOWN(uint8_t value) {
	val_tpowerdown = value;
	send2130(WRITE|REG_TPOWERDOWN, &cur_TPOWERDOWN, value, 0xFF);
}

///////////////////////////////////////////////////////////////////////////////////////
// REG_TSTEP

uint32_t TMC2130Stepper::get_TSTEP() {
	uint32_t data = 0x0;
	send2130(READ|REG_TSTEP, &data, 0x0, 0x0);
	return data;
}

///////////////////////////////////////////////////////////////////////////////////////
// REG_TPWMTHRS

uint32_t TMC2130Stepper::get_TPWMTHRS() {return val_tpwmthrs;}

void TMC2130Stepper::set_TPWMTHRS(uint32_t value) {
	if (value > 1048575) value = 1048575;
	val_tpwmthrs = value;
	send2130(WRITE|REG_TPWMTHRS, &cur_TPWMTHRS, value, 0xFFFFF);
}

///////////////////////////////////////////////////////////////////////////////////////
// REG_TCOOLTHRS

uint32_t TMC2130Stepper::get_TCOOLTHRS() {return val_tcoolthrs;}

void TMC2130Stepper::set_TCOOLTHRS(uint32_t value) {
	if (value > 1048575) value = 1048575;
	val_tcoolthrs = value;
	send2130(WRITE|REG_TCOOLTHRS, &cur_TCOOLTHRS, value, 0xFFFFF);
}

///////////////////////////////////////////////////////////////////////////////////////
// REG_THIGH

uint32_t TMC2130Stepper::get_THIGH() {return val_thigh;}

void TMC2130Stepper::set_THIGH(uint32_t value) {
	if (value > 1048575) value = 1048575;
	val_thigh = value;
	send2130(WRITE|REG_THIGH, &cur_THIGH, value, 0xFFFFF);
}

///////////////////////////////////////////////////////////////////////////////////////
// REG_XDRIRECT

int16_t TMC2130Stepper::get_XDIRECT_A() {return val_xdirect_a;}

void TMC2130Stepper::set_XDIRECT_A(int16_t value) {
	if (value < -255) value = -255;
	else if (value > 255) value = 255;
	val_xdirect_a = value;
	send2130(WRITE|REG_XDIRECT, &cur_XDIRECT, value, 0x1FF);
}

int16_t TMC2130Stepper::get_XDIRECT_B() {return val_xdirect_b;}

void TMC2130Stepper::set_XDIRECT_B(int16_t value) {
	if (value < -255) value = -255;
	else if (value > 255) value = 255;
	val_xdirect_b = value;
	send2130(WRITE|REG_XDIRECT, &cur_XDIRECT, (uint32_t)value << 16, 0x1FF0000);
}

///////////////////////////////////////////////////////////////////////////////////////
// REG_VDCMIN

uint32_t TMC2130Stepper::get_VDCMIN() {return val_vdcmin;}

void TMC2130Stepper::set_VDCMIN(uint32_t value) {
	if (value > 16777215) value = 16777215;
	val_vdcmin = value;
	send2130(WRITE|REG_VDCMIN, &cur_VDCMIN, value, 0xFFFFFF);
}

///////////////////////////////////////////////////////////////////////////////////////
// REG_CHOPCONF

uint32_t TMC2130Stepper::get_CHOPCONF() {
	uint32_t data = 0x0;
	send2130(READ|REG_CHOPCONF, &data, 0x0, 0x0);
	return data;
}

void TMC2130Stepper::set_CHOPCONF(uint32_t value) {
	send2130(WRITE|REG_CHOPCONF, &cur_CHOPCONF, value, 0xFFFFFFFF);
}

uint8_t TMC2130Stepper::get_TOFF() {return val_toff;}

void TMC2130Stepper::set_TOFF(uint8_t value) {
	if (value > 15) value = 15;
	val_toff = value;
	send2130(WRITE|REG_CHOPCONF, &cur_CHOPCONF, value, 0xF);
}

uint8_t TMC2130Stepper::get_HSTRT() {return val_hstrt;}

void TMC2130Stepper::set_HSTRT(uint8_t value) {
	if (val_chm	== 0) {
		if (value < 1) value = 1;
		else if (value > 8) value = 8;
		val_hstrt = value;
		send2130(WRITE|REG_CHOPCONF, &cur_CHOPCONF, (value-1) << 4, 0x70);
	}
#ifdef TMC2130DEBUG
	else {Serial.println("chm bit is set to 1 -> TFD is in use. No change made.");}
#endif	
}

uint8_t TMC2130Stepper::get_TFD() {return val_tfd;}

void TMC2130Stepper::set_TFD(uint8_t value) {
	if (val_chm == 1) {
		if (value > 15) value = 15;
		val_tfd = value;
		value = ((uint32_t)value << 4) | (value & 0b111); // Create space between MSB and the bits 0..2
		send2130(WRITE|REG_CHOPCONF, &cur_CHOPCONF, (uint32_t)value << 4, 0x870);
	}
#ifdef TMC2130DEBUG
	else {Serial.println("chm bit is set to 0 -> HSTRT is in use. No change made.");}
#endif	
}

int8_t TMC2130Stepper::get_HEND() {return val_hend;}

void TMC2130Stepper::set_HEND(int8_t value) {
	if (val_chm == 0) {
		if (value < -3) value = -3;
		if (value > 12) value = 12;
		val_hend = value;
		send2130(WRITE|REG_CHOPCONF, &cur_CHOPCONF, (value+3) << 7, 0x780);
	}
#ifdef TMC2130DEBUG
	else {Serial.println("chm bit is set to 1 -> OFFSET is in use. No change made.");}
#endif	
}

int8_t TMC2130Stepper::get_OFFSET() {return val_offset;}

void TMC2130Stepper::set_OFFSET(int8_t value) {
	if (val_chm == 1) {
		if (value < -3) value = -3;
		if (value > 12) value = 12;
		val_hend = value;
		send2130(WRITE|REG_CHOPCONF, &cur_CHOPCONF, (value+3) << 7, 0x780);
	}
#ifdef TMC2130DEBUG
	else {Serial.println("chm bit is set to 1 -> OFFSET is in use. No change made.");}
#endif	
}

uint8_t TMC2130Stepper::get_DISFDCC() {return val_disfdcc;}

void TMC2130Stepper::set_DISFDCC(uint8_t value) {
	if (val_chm == 1) {
		if (value > 1) value = 1;
		val_disfdcc = value;
		send2130(WRITE|REG_CHOPCONF, &cur_CHOPCONF, (uint32_t)value << 12, (uint32_t)0b1 << 12);
	}
#ifdef TMC2130DEBUG
	else {Serial.println("chm bit is set to 0 -> No change made.");}
#endif
}

uint8_t TMC2130Stepper::get_RNDTF() {return val_rndtf;}

void TMC2130Stepper::set_rndtf(uint8_t value) {
	if (value > 1) value = 1;
	val_rndtf = value;
	send2130(WRITE|REG_CHOPCONF, &cur_CHOPCONF, (uint32_t)value << 13, (uint32_t)0b1 << 13);
}

uint8_t TMC2130Stepper::get_CHM() {return val_chm;}

void TMC2130Stepper::set_CHM(uint8_t value) {
	if (value > 1) value = 1;
	val_chm = value;
	send2130(WRITE|REG_CHOPCONF, &cur_CHOPCONF, (uint32_t)value << 14, (uint32_t)0b1 << 14);
}

uint8_t TMC2130Stepper::get_TBL() {return val_tbl;}

void TMC2130Stepper::set_TBL(uint8_t value) {
	uint8_t valid[] = {54, 36, 24, 16};

	if (value < valid[3]) value = valid[3]; // Make sure we find a match for low values
	for (int i = 0; i<4; i++) {
		if (value >= valid[i]) {
			value = valid[i];
			break;
		}
	}

	val_tbl = value;
	send2130(WRITE|REG_CHOPCONF, &cur_CHOPCONF, (uint32_t)value << 15, 0x18000);
}

uint8_t TMC2130Stepper::get_VSENSE() {return val_vsense;}

void TMC2130Stepper::set_VSENSE(uint8_t value) {
	if (value > 1) value = 1;
	val_vsense = value;
	send2130(WRITE|REG_CHOPCONF, &cur_CHOPCONF, (uint32_t)(uint32_t)value << 17, (uint32_t)0b1 << 17);
}

uint8_t TMC2130Stepper::get_VHIGHFS() {return val_vhighfs;}

void TMC2130Stepper::set_VHIGHFS(uint8_t value) {
	if (value > 1) value = 1;
	val_vhighfs = value;
	send2130(WRITE|REG_CHOPCONF, &cur_CHOPCONF, (uint32_t)(uint32_t)value << 18, (uint32_t)0b1 << 18);
}

uint8_t TMC2130Stepper::get_VHIGHCHM() {return val_vhighchm;}

void TMC2130Stepper::set_VHIGHCHM(uint8_t value) {
	if (value > 1) value = 1;
	val_vhighchm = value;
	send2130(WRITE|REG_CHOPCONF, &cur_CHOPCONF, (uint32_t)(uint32_t)value << 19, (uint32_t)0b1 << 19);
}

uint8_t TMC2130Stepper::get_SYNC() {return val_sync;}

void TMC2130Stepper::set_SYNC(uint8_t value) {
	if (value > 15) value = 15;
	val_sync = value;
	send2130(WRITE|REG_CHOPCONF, &cur_CHOPCONF, (uint32_t)(uint32_t)value << 20, 0xF00000);
}

uint8_t TMC2130Stepper::get_MRES() {return val_mres;}

void TMC2130Stepper::set_MRES(uint8_t value) {
	int valid[] 	= {		256, 	128, 	 64, 	 32, 	 16, 	  8, 	  4, 	  2, 	  0 };
	uint32_t _hex[] = {  0b0000, 0b0001, 0b0010, 0b0011, 0b0100, 0b0101, 0b0110, 0b0111, 0b1000 };

	for (int i = 0; i<9; i++) {
		if (value >= valid[i]) {
			val_mres = valid[i];
			send2130(WRITE|REG_CHOPCONF, &cur_CHOPCONF, _hex[i] << 24, 0xF000000);
			break;
		}
	}
}

uint8_t TMC2130Stepper::get_INTPOL() {return val_intpol;}

void TMC2130Stepper::set_INTPOL(uint8_t value) {
	if (value > 1) value = 1;
	val_intpol = value;
	send2130(WRITE|REG_CHOPCONF, &cur_CHOPCONF, (uint32_t)(uint32_t)value << 28, (uint32_t)0b1 << 28);
}

uint8_t TMC2130Stepper::get_DEDGE() {return val_dedge;}

void TMC2130Stepper::set_DEDGE(uint8_t value) {
	if (value > 1) value = 1;
	val_dedge = value;
	send2130(WRITE|REG_CHOPCONF, &cur_CHOPCONF, (uint32_t)(uint32_t)value << 29, (uint32_t)0b1 << 29);
}

uint8_t TMC2130Stepper::get_DISS2G() {return val_diss2g;}

void TMC2130Stepper::set_DISS2G(uint8_t value) {
	if (value > 1) value = 1;
	val_diss2g = value;
	send2130(WRITE|REG_CHOPCONF, &cur_CHOPCONF, (uint32_t)(uint32_t)value << 30, (uint32_t)0b1 << 30);
}

///////////////////////////////////////////////////////////////////////////////////////
// REG_COOLCONF

void TMC2130Stepper::set_COOLCONF(uint32_t value) {
	send2130(WRITE|REG_COOLCONF, &cur_COOLCONF, value, 0xFFFFFFFF);
}

uint8_t TMC2130Stepper::get_SEMIN() {return val_semin;}

void TMC2130Stepper::set_SEMIN(uint8_t value) {
	if (value > 15) value = 15;
	val_semin = value;
	send2130(WRITE|REG_COOLCONF, &cur_COOLCONF, value, 0xF);
}

uint8_t TMC2130Stepper::get_SEUP() {return val_seup;}

void TMC2130Stepper::set_SEUP(uint8_t value) {
	uint8_t valid[] = {8, 4, 2, 1};

	if (value < valid[3]) value = valid[3]; // Make sure we find a match for low values
	for (int i = 0; i<4; i++) {
		if (value >= valid[i]) {
			value = valid[i];
			break;
		}
	}

	val_seup = value;
	send2130(WRITE|REG_COOLCONF, &cur_COOLCONF, (uint32_t)value << 5, 0x60);
}

uint8_t TMC2130Stepper::get_SEMAX() {return val_semax;}

void TMC2130Stepper::set_SEMAX(uint8_t value) {
	if (value > 15) value = 15;
	val_semin = value;
	send2130(WRITE|REG_COOLCONF, &cur_COOLCONF, (uint32_t)(uint32_t)value << 8, 0xF00);
}

uint8_t TMC2130Stepper::get_SEDN() {return val_sedn;}

void TMC2130Stepper::set_SEDN(uint8_t value) {
	uint8_t valid[] = {32, 8, 2, 1};

	if (value < valid[3]) value = valid[3]; // Make sure we find a match for low values
	for (int i = 0; i<4; i++) {
		if (value >= valid[i]) {
			value = valid[i];
			break;
		}
	}

	val_sedn = value;
	send2130(WRITE|REG_COOLCONF, &cur_COOLCONF, (uint32_t)(uint32_t)value << 13, 0x6000);
}

uint8_t TMC2130Stepper::get_SEIMIN() {return val_seimin;}

void TMC2130Stepper::set_SEIMIN(uint8_t value) {
	if (value > 1) value = 1;
	val_seimin = value;
	send2130(WRITE|REG_CHOPCONF, &cur_CHOPCONF, (uint32_t)value << 15, 0x8000);
}

int8_t TMC2130Stepper::get_SGT() {return val_sgt;}

void TMC2130Stepper::set_SGT(int8_t value) {
	if (value < -64) value = -64;
	else if (value > 63) value = 63;
	val_sgt = value;
	send2130(WRITE|REG_CHOPCONF, &cur_CHOPCONF, ((uint32_t)value+64) << 16, 0x7F0000);
}

uint8_t TMC2130Stepper::get_SFILT() {return val_sfilt;}

void TMC2130Stepper::set_SFILT(uint8_t value) {
	if (value > 1) value = 1;
	val_sfilt = value;
	send2130(WRITE|REG_CHOPCONF, &cur_CHOPCONF, (uint32_t)value << 24, (uint32_t)0b1 << 24);
}

///////////////////////////////////////////////////////////////////////////////////////
// REG_PWMCONF

void TMC2130Stepper::set_PWMCONF(uint32_t value) {
	send2130(WRITE|REG_PWMCONF, &cur_PWMCONF, value, 0xFFFFFFFF);
}

uint8_t TMC2130Stepper::get_PWM_AMPL() {return val_pwm_ampl;}

void TMC2130Stepper::set_PWM_AMPL(uint8_t value) {
	val_pwm_ampl = value;
	send2130(WRITE|REG_PWMCONF, &cur_PWMCONF, value, 0xFF);
}

uint8_t TMC2130Stepper::get_PWM_GRAD() {return val_pwm_grad;}

void TMC2130Stepper::set_PWM_GRAD(uint8_t value) {
	val_pwm_grad = value;
	send2130(WRITE|REG_PWMCONF, &cur_PWMCONF, (uint32_t)value << 8, 0xFF00);
}

uint8_t TMC2130Stepper::get_PWM_FREQ() {return val_pwm_freq;}

void TMC2130Stepper::set_PWM_FREQ(uint8_t value) {
	uint16_t valid[] = {1024, 683, 512, 410};

	if (value < valid[3]) value = valid[3]; // Make sure we find a match for low values
	for (int i = 0; i<4; i++) {
		if (value >= valid[i]) {
			value = valid[i];
			break;
		}
	}

	val_pwm_freq = value;
	send2130(WRITE|REG_PWMCONF, &cur_PWMCONF, (uint32_t)value << 16, 0x30000);
}

uint8_t TMC2130Stepper::get_PWM_AUTOSCALE() {return val_pwm_autoscale;}

void TMC2130Stepper::set_PWM_AUTOSCALE(uint8_t value) {
	if (value > 1) value = 1;
	val_pwm_autoscale = value;
	send2130(WRITE|REG_PWMCONF, &cur_PWMCONF, (uint32_t)value << 18, (uint32_t)0b1 << 18);
}

uint8_t TMC2130Stepper::get_PWM_SYMMETRIC() {return val_pwm_symmetric;}

void TMC2130Stepper::set_PWM_SYMMETRIC(uint8_t value) {
	if (value > 1) value = 1;
	val_pwm_symmetric = value;
	send2130(WRITE|REG_PWMCONF, &cur_PWMCONF, (uint32_t)value << 19, (uint32_t)0b1 << 19);
}

uint8_t TMC2130Stepper::get_FREEWHEEL() {return val_freewheel;}

void TMC2130Stepper::set_FREEWHEEL(uint8_t value) {
	if (value < 1) value = 1;
	else if (value > 4) value = 4;
	val_freewheel = value;
	send2130(WRITE|REG_PWMCONF, &cur_PWMCONF, ((uint32_t)value-1) << 20, 0x300000);
}

///////////////////////////////////////////////////////////////////////////////////////
// REG_DRVSTATUS

uint32_t TMC2130Stepper::get_DRVSTATUS() {
	uint32_t data = 0x0;
	send2130(READ|REG_DRVSTATUS, &data, 0x0, 0x0);
	return data;
}