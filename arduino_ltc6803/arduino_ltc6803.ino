#include <SPI.h>

/*
 * LTC6803 driver for Arduino
 * 
 * This file contains helpers to communicate with SPI-enabled LTC6803 battery monitor chip family.
 * 
 * Author:  JCZD
 * Date:    2020-11
 * License: GPLv2
 * 
 * WARNING: change in CDC paramters require code tunning! current settings may not be optimal, see defaultConfig
 * 
 * TODOs, in order of relative importance (more urgent first)
 * - do what's needed to remove the above cdc-related warning
 * - watchdog
 * - add helper functions for GPIOs
 * - improve parsing of CFGR config register bytes (make it human-friedly)
 * - investigate why self-test won't return the expected values
 * - broadcast commands
 */

// datasheet page 22
byte WRCFG[2] {0x01,0xc7};  // Write Configuration Register
byte RDCFG[2] {0x02,0xce};  // Read Configuration Register
byte RDCV[2]  {0x04,0xdc};  // Read all cell voltages
byte RDCVA[2] {0x06,0xd2};  // Read cell voltages 1-4
byte RDCVB[2] {0x08,0xf8};  // Read cell voltages 5-8
byte RDCVC[2] {0x0a,0xf6};  // Read cell voltages 9-12
byte RDFLG[2] {0x0c,0xe4};  // flag register
byte RDTMP[2] {0x0e,0xea};  // temperature register
byte STCVAD[2] {0x10,0xb0}; // start cell voltage ADC conversion and status (all)
byte STOWAD[2] {0x20,0x20}; // start open-wire ADC conversions and poll status
byte STTMPAD[2] {0x30,0x50};// start temperature ADC conversions and poll status
byte PLADC[2] {0x40,0x07};  // poll adc converter status
byte PLINT[2] {0x50,0x77};  // poll interrupt status
byte DAGN[2] {0x52,0x79};   // start diagose and poll status
byte RDDGNR[2] {0x54,0x6b}; // read diagnose register
byte STCVDC[2] {0x60,0xe7}; // start cell voltage ADC conversions and poll status, with discharge permitted
byte STOWDC[2] {0x70,0x97}; // start open-wire ADC conversions and poll status, with discharge permitted

unsigned long LOOP_OVERFLOW = 0;

#define VLSB .0015 // 1.5 mV

#define LTC1 0x80 // Board Address for 1st LTC6803G-2
#define LTC2 0x81 // Board Address for 2nd LTC6803G-2
#define LTC3 0x82 // Board Address for 3rd LTC6803G-2

byte byteHolder;
// configuration for 1st module of LTC6803-4 (negate LTC6803-4)
//byte CFFMVMC[6] = {0x09, 0x00, 0x00, 0x00, 0x00, 0x00};
// configuration for 1st module to assert LTC6803-4
//byte asLTC6I803[6] = {0x29, 0x00, 0x00, 0xFC, 0xA4, 0xBA};

byte defaultConfig[6] = {
  0x09, // GPIOs driving, comparator off, 10-cell mode
  0x00, // DCC disabled for cells 1-8
  0b11110000, // Interrupts turned off for cells 1-4, DCC disabled for cells 9-12
  0b00001111, // Interrupts turned off for cells 5-12, mask cells if less than 12 in MCxI
  0x04, // undervoltage comparison voltage [V] = VUV-31*16*VLSB (3.256 V)
  0x05  // overvoltage comparison voltage [V]= VOV-32*16*VLSB (4.232 V)
 };

byte WRFG[1] = {0x01};
byte addr0[1] = {LTC1};
byte addr1[1] = {LTC2};
byte addr2[1] = {LTC3};

void setup() {
 // put your setup code here, to run once:
 
 pinMode(10, OUTPUT);
 pinMode(11, OUTPUT);
 pinMode(12, INPUT);
 pinMode(13, OUTPUT);
 pinMode(13, OUTPUT); // battery-side digital isolator enable switch
 digitalWrite(10, HIGH);
 Serial.begin(9600);
 Serial.println("##################################################################");
 Serial.println("#                                                                #");
 Serial.println("# LTC6803 Interrogator                                           #");
 Serial.println("#                                                                #");
 Serial.println("##################################################################");
 
 SPI.begin();
 SPI.setClockDivider(SPI_CLOCK_DIV64); // as low as possible, 128 probabbly the lowest value for Nano
 SPI.setDataMode(SPI_MODE3);
 SPI.setBitOrder(MSBFIRST);

 // enable battery-side digital isolator
 digitalWrite(13, LOW);

  writeConfig(addr0);
      enableAll(addr0);
  
  writeConfig(addr1);
      enableAll(addr1);
  
  writeConfig(addr2);
      enableAll(addr2);
  
}

int count = 0;

void loop() {
  if (readConfig(addr0,false) == 0) {
    if ( count == 0 ) {
      readConfig(addr0, true);
      goToSleep(addr0);
      delay(1800);
      Serial.println("Rewriting config to enable CDC (comparator duty cycle) in 1 [s].");
      delay(1000);
      LOOP_OVERFLOW += 1;
      writeConfig(addr0);
      doSelfTest(addr0);
      Serial.println("Test always fails. Enabling polling method in 1 [s].");
      delay(1000);
      enableAll(addr0);
    } else {
      getAll(addr0);
    }
  }
  
  if (readConfig(addr1,false) == 0) {
    if ( count == 0 ) {
      readConfig(addr1, true);
      doSelfTest(addr1);
    } else {
      getAll(addr1);
    }
  }
  
  if (readConfig(addr2,false) == 0) {
    if ( count == 0 ) {
      readConfig(addr2, true);
      doSelfTest(addr2);
    } else {
      getAll(addr2);
    }
  }
  
  delay(2000);
  if ( count == 0 ) {
    count = 10;
  } else {
    count--;
  }
    
    Serial.println("##############################"+String(count)+"####################################");
  //}
//}

//  spiStart();
//  SPI.transfer(getPEC(WRFG,1));
//  spiEnd();
//Serial.println("Loop done..");
 delay(2000);
}

void getAll(byte * ltc_addr) {
  readVoltages(ltc_addr);
  readTemperatures(ltc_addr);
}






// Calculate PEC, n is  size of DIN
byte getPEC(byte *din, int n) {
byte pec, in0, in1, in2;
pec = 0x41;
for(int j=0; j<n; j++) {
  for(int i=0; i<8; i++) {
    in0 = ((din[j] >> (7 - i)) & 0x01) ^ ((pec >> 7) & 0x01);
    in1 = in0 ^ ((pec >> 0) & 0x01);
    in2 = in0 ^ ((pec >> 1) & 0x01);
    pec = in0 | (in1 << 1) | (in2 << 2) | ((pec << 1) & ~0x07);
  }
}
return pec;
}

void spiStart(byte *ltc_addr) {
 delay(10);
 SPI.begin();
 if(digitalRead(10) == LOW) {
   digitalWrite(10,HIGH);
   delayMicroseconds(5); //Ensure a valid pulse
 }
 digitalWrite(10, LOW);
 //delayMicroseconds(20);
  SPI.transfer(ltc_addr[0]);
  SPI.transfer(getPEC(ltc_addr,1));
}

void spiEnd(uint8_t flag) { //Wait poll Status
 SPI.end();
 if(flag > 0) {
   delayMicroseconds(10);
   digitalWrite(10, HIGH);
 }
}

// Send multiple data to LTC
void sendBytes(byte * data, int n){
 for (int i = 0; i<n; i++){
   SPI.transfer(data[i]);
   }
 }

void spiEnd(void) {
 spiEnd(1);
}


/*
 * read a number of bytes on the selected register and verify checksum
 * 
 * RDCV and reg seem to be equivalent beyond the first sedMultiplebyteSPI() call.
 */
bool readBytes(byte *ltc_addr, byte *reg, byte *res, int num_bytes, bool verbose = false) {
  byte pec;
  //Serial.println("Reading "+String(num_bytes,DEC)+"bytes + PEC");
  
  spiStart(ltc_addr);
  sendBytes(reg, 2);
   for(int i = 0; i < num_bytes; i++){
    //res[i] = SPI.transfer(reg[0]);
    res[i] = SPI.transfer(RDCV[0]);
  }
  //pec = SPI.transfer(reg[0]);
  pec = SPI.transfer(RDCV[0]);
  spiEnd();
  
  if (getPEC(res,num_bytes) != pec) { 
    if ( verbose) { Serial.println("PEC Mismatch: read 0x"+String(pec,HEX)+" but computed 0x"+String(getPEC(res,num_bytes),HEX)); }
    return false;
   } else {
    //Serial.println("PEC MATCH!!!");
    return true;
  }
}

void enableAll(byte *ltc_addr) {
 // enable voltage polling/conversion
 spiStart(ltc_addr);
 sendBytes(STCVAD, 2);  // chose one of STVCAD,STOWAD,STCVDC,STOWDC
 // what about "Poll Data" in Table 6. "Address Poll Command" on page 21?
 //byte result;
 //result = SPI.transfer(RDCV[0]);
 //Serial.println("Reading voltages on 0x"+String(ltc_addr[0],HEX)+" result: "+String(result,HEX));
 spiEnd();
 
 spiStart(ltc_addr);
 sendBytes(STTMPAD, 2);
 spiEnd();
 
 delay(15); // see table on page 4 for fine-tuning
  
}

void goToSleep(byte *ltc_addr) {
  Serial.println("Telling 0x"+String(ltc_addr[0],HEX)+" to go to sleep... don't let the bed bugs byte!");
  Serial.println("zzzzzzzzzzzzzzzzzzzzzzzzzzzz"+String(LOOP_OVERFLOW)+"zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz");
  spiStart(ltc_addr);
  sendBytes(WRCFG, 2);
  byte sleepConfig[6] = {
    0x00, // GPIOs pull-down off, comparator in standby mode, 10-cell mode
    defaultConfig[1],
    defaultConfig[2],
    defaultConfig[3],
    defaultConfig[4],
    defaultConfig[5]
  };
  sendBytes(sleepConfig, 6);
  SPI.transfer(getPEC(sleepConfig,6));
  spiEnd();

}

// run ADC converter self test. see page 16
int doSelfTest(byte *ltc_addr) {
  int err_count = 0;
  
  Serial.println("Running diagnose on 0x"+String(ltc_addr[0],HEX));
  
  byte res[2];
  // read diagnose register, 2 bytes
  readBytes(ltc_addr,RDDGNR,res,2);
  //Serial.println("DGNR0: 0x"+String(res[0],HEX));
  //Serial.println("DGNR1: 0x"+String(res[1],HEX));
  
  double ref_voltage = (((res[1]&0x0f)*0xff + res[0])-512)*VLSB;
  if (ref_voltage >= 2.1 && ref_voltage <= 2.9 ) {
    Serial.println("Reference voltage nominal: "+String(ref_voltage)+" [V]");
  } else {
    Serial.println("ERROR: Reference voltage out of range: "+String(ref_voltage)+" [V]");
    err_count++;
  }
  
  if ((res[1]&0x10)>>5 == 1) {
    Serial.println("ERROR: Mux failure");
    err_count++;
  }
  
  Serial.println("Revision code: "+String((res[1]&0xc0)>>6,HEX));
  
  byte temperatures[5];
  if (readBytes(ltc_addr,RDTMP,res,5)) {
    Serial.println("Temperature Self Test 1: "+String((float)res[3]/10.)+" [°C] (0x"+String(res[3],HEX)+")");
    Serial.println("Temperature Self Test 2: "+String((float)res[4]/10.)+" [°C] (0x"+String(res[3],HEX)+")");
  } else {
    Serial.println("ERROR: Temperature read failure");
  }

  
  // read flag register, 3 bytes
  readBytes(ltc_addr,RDFLG,res,3);
  Serial.println("Flag register: [FLGR2 "+String(res[2],HEX)+" "+String(res[1],HEX)+" "+String(res[0],HEX)+" FLGR0]");
  
  // TODO PLINT poll Interrupt status

  
  spiStart(ltc_addr);
  // start diagnose and poll status
  sendBytes(DAGN, 2);
  spiEnd();
  delay(20);
  // TODO PLADC poll ADC converter status
  
  // so far this has always failed ; see page 16
  Serial.println("All bytes below should read 0x555 or 0xAAA");
  
  byte voltages[18];
  if (readBytes(ltc_addr,RDCV,voltages,18)) {
    //for (int i = 0 ; i < 18 ; i++ ) {
    //  Serial.println("CVR0"+String(i,DEC)+": "+String(voltages[i],HEX));
    //}
    Serial.println("Voltage register: [CVR17 "+
        String(voltages[17],HEX)+String(voltages[16],HEX)+String(voltages[15],HEX)+" "+
        String(voltages[14],HEX)+String(voltages[13],HEX)+String(voltages[12],HEX)+" "+
        String(voltages[11],HEX)+String(voltages[10],HEX)+String(voltages[9], HEX)+" "+
        String(voltages[8], HEX)+String(voltages[7], HEX)+String(voltages[6], HEX)+" "+
        String(voltages[5], HEX)+String(voltages[4], HEX)+String(voltages[3], HEX)+" "+
        String(voltages[2], HEX)+String(voltages[1], HEX)+String(voltages[0], HEX)+
        " CVR00]"
      );
  } else { err_count += 1; }

  if (readBytes(ltc_addr,RDTMP,temperatures,5)) {
    //for (int i = 0 ; i < 5 ; i++ ) {
    //  Serial.println("TMPR"+String(i,DEC)+": "+String(temperatures[i],HEX));
    //}
    Serial.println("Temperature registers: [TMPR4 "+
        String(temperatures[4], HEX)+" "+
        String(temperatures[3], HEX)+" "+
        String(temperatures[2], HEX)+" "+
        String(temperatures[1], HEX)+" "+
        String(temperatures[0], HEX)+
        " TMPR0]"
      );
  } else { err_count += 1; }

  if ( err_count == 0 ) {
    Serial.println("Diagnotics finished with no obvious errors.");
  } else {
    Serial.println("Diagnotics finished with "+String(err_count,DEC)+" errors.");
  }
  return err_count;
}

/* Write Configuration Registers for LTC6803-4 using Broadcast Write
 *
 * GPIO1 is CFRG0&0x20
 * GPIO2 is CFRG0&0x40
 * 
 * for more options see Table 10. page 23
*/
void writeConfig(byte *ltc_addr){
 Serial.println("Writing configuration register on 0x"+String(ltc_addr[0],HEX));spiStart(ltc_addr);
 sendBytes(WRCFG, 2);
 sendBytes(defaultConfig, 6);
 SPI.transfer(getPEC(defaultConfig,6));
 spiEnd();
}


//// Read Cell Configuration Registers for LTC6803-4 using Addressable Read
int readConfig(byte *ltc_addr, bool verbose){
  if (verbose){ Serial.println("Reading configuration register on 0x"+String(ltc_addr[0],HEX));}
  byte res[6];
  if (readBytes(ltc_addr,RDCFG,res,6) ) {
    for (int i = 0 ; i < 6 ; i++ ) {
      if (verbose){ Serial.println("CFGR"+String(i,DEC)+": 0x"+String(res[i],HEX)); }
    }
    return 0;
  } else {
    return 1;
  }
 
 }


int readVoltages(byte * ltc_addr){
 Serial.println("Reading voltages on 0x"+String(ltc_addr[0],HEX));

 int err_count = 0;
 byte res[6];
 unsigned int total_vRead = 0;
 

  int j = 0;
  if (readBytes(ltc_addr,RDCVA,res,6)) {
    for (int i = 0 ; i < 2 ; i++ ) {
      double va = (((res[3*i+1]&0x0f)*0xff + res[3*i])-512)*VLSB;
      Serial.println("Cell "+String(i*2+4*j)+": "+String(va)+" [V]");
      total_vRead += (((res[3*i+1]&0x0f)*0xff + res[3*i])-512);
      double vb = (((res[3*i+2]>>4)*0xff + ((res[3*i+2]&0x0f)<<4 | (res[3*i+1]&0xf0)>>4))-512)*VLSB;
      Serial.println("Cell "+String(1+i*2+4*j)+": "+String(vb)+" [V]");
      total_vRead += (((res[3*i+2]>>4)*0xff + ((res[3*i+2]&0x0f)<<4 | (res[3*i+1]&0xf0)>>4))-512);
    }
  } else { err_count += 1; }

  j++;
  if (readBytes(ltc_addr,RDCVB,res,6)) {
    for (int i = 0 ; i < 2 ; i++ ) {
      double va = (((res[3*i+1]&0x0f)*0xff + res[3*i])-512)*VLSB;
      Serial.println("Cell "+String(i*2+4*j)+": "+String(va)+" [V]");
      total_vRead += (((res[3*i+1]&0x0f)*0xff + res[3*i])-512);
      double vb = (((res[3*i+2]>>4)*0xff + ((res[3*i+2]&0x0f)<<4 | (res[3*i+1]&0xf0)>>4))-512)*VLSB;
      Serial.println("Cell "+String(1+i*2+4*j)+": "+String(vb)+" [V]");
      total_vRead += (((res[3*i+2]>>4)*0xff + ((res[3*i+2]&0x0f)<<4 | (res[3*i+1]&0xf0)>>4))-512);
    }
  } else { err_count += 1; }

  /*j++;
  if (readBytes(ltc_addr,RDCVC,res,6)) {
    for (int i = 0 ; i < 2 ; i++ ) {
      double va = (((res[3*i+1]&0x0f)*0xff + res[3*i])-512)*VLSB;
      Serial.println("Cell "+String(i*2+4*j)+": "+String(va)+" [V]");
      total_vRead += (((res[3*i+1]&0x0f)*0xff + res[3*i])-512);
      double vb = (((res[3*i+2]>>4)*0xff + ((res[3*i+2]&0x0f)<<4 | (res[3*i+1]&0xf0)>>4))-512)*VLSB;
      Serial.println("Cell "+String(1+i*2+4*j)+": "+String(vb)+" [V]");
      total_vRead += (((res[3*i+2]>>4)*0xff + ((res[3*i+2]&0x0f)<<4 | (res[3*i+1]&0xf0)>>4))-512);
    }
  } else { err_count += 1; }*/
  Serial.println("Total: "+String(total_vRead*VLSB)+" [V]");
 
  return err_count;
 }


int readTemperatures(byte * ltc_addr){
 Serial.println("Reading temperatures on 0x"+String(ltc_addr[0],HEX));

  byte res[5];
  if (readBytes(ltc_addr,RDTMP,res,5)) {
    Serial.println("External 1:  "+String((float)res[0]/10.)+" [°C]");
    Serial.println("External 2:  "+String((float)res[1]/10.)+" [°C]");
    Serial.println("Internal:    "+String((float)res[2]/10.)+" [°C]");
    return 0;
  } else {
    return 1;
  }
}
