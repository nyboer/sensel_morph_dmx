#include <math.h> 

//https://github.com/sensel/sensel-api-arduino
#include "sensel.h"
//https://github.com/PaulStoffregen/DmxSimple
#include <DmxSimple.h> 

//array of ints to record gestures
int gestures[512] = {};
//array of rgb colors to interpolate
int rgbs[16][3] = {
 
  {255,  0  , 0  }, //red
  {253,  105, 0  }, //red org
  {253,  141, 0  }, //org
  {255,  255, 0  }, //yel

  {171,  250, 0  }, //yel grn 
  {119,  255, 0  }, //grn yel
  {0  ,  255, 0  }, //grn
  {0  ,  250, 140}, //grn blu

  {0  ,  190, 215}, //blu grn
  {0  ,  255, 255  }, //cyan
  {0  ,  110 ,250  }, //blue cyan
  {0  ,  0  , 255  }, //blue

  {90 ,  0  , 250  }, //blu purp
  {180,  0  , 250  }, //purp
  {255,  0  , 255  }, //mag
  {255,  0  , 140  }, //pink
 
};


//Frame struct for containing the contact array and number of contacts
SenselFrame frame;
//Whether the Sensel device has been initialized
bool sensel_ready = false;

const long interval = 33; // interval at which to record and play scenes (milliseconds)
unsigned long pvs_ms = 0; 

//for interpolation routines
const float Pi = 3.14159265359;
const float Segcount = 16;
//centers of color wheels on left and right
//array of coords for centers of color wheels left and right:
float centers[2][2] = { {60,60},{180,60} };
float interpolation = 0.;
int seg_id = 0;
int newcolor[3] = {0,0,0};

//takes coordinates from morph, 
//provides a radial segment ID (angle in range of 0-15)
//and an interpolation factor to get color between segments
void radianSeg(float x, float y, int side){
  
  //use center of color wheel to normalize coords
  float x_ = x - centers[side][0];
  float y_ = (y - centers[side][1]) * (-1); 
 
  float testneg = (float) x_ < 0;
  float adj =  (2*Pi * testneg);
  float rads =  atan2(x_ , y_) + adj; //atan returns range of -PI to PI, need 0 to 2*PI
  float divs = 2*Pi/Segcount; //0.39269875
  float fseg = rads/divs; //seg id with decimal 

  //the two things we are after from this
  int segment_id = (int) fseg;
  float interp = fseg - segment_id;
  
  interpolation = interp;
  seg_id = segment_id; 
  
}

float distance(float x1, float y1, float x2, float y2){
  float dist =  sqrt( pow(x2-x1,2) + pow(y2-y1,2));
  return dist;
}

//scale distance 0-57.5 to 0-127 for brightness
int brightness(float d){
  float fbright =  d * (127/57.5);
  if(fbright>127){
    fbright = 127;
  }
  int bright = (int) fbright;
  return bright;
}
//scale force 500-1100 to 128-250 for brightness
int strobe(float z){
    float fstrobe =  128 + ( (z-500.) * (122./600.) );
  if(fstrobe>250){
    fstrobe = 250;
  }
  int strobe_time = (int) fstrobe;
  return strobe_time;
}


//linear interp c = a + (b-a)*t
//should probably protect against size of 0!
void lerpRGB(float t,int a[], int b[], uint8_t size, int side)
{
  String cmma = ",";
  
  for(int i = 0; i < size; i++){
      newcolor[i] = a[i] + (b[i]-a[i]) * t;
      SenselDebugSerial.print(newcolor[i]);
      if(i<size-1){
        SenselDebugSerial.print(cmma);
      }
    }
    SenselDebugSerial.println(" ");
    SenselDebugSerial.println("____");
}

//ref: https://www.amazon.com/Litake-Lights-DMX-512-Lighting-Projector/dp/B01KZPDVQO/ for PAR light
void lamp_color(int red, int green, int blue,int side) {
    int chs[2][3]={ {2,3,4},{6,7,8} };
    int ch[3] = {};
    for(int i = 0; i < 3; i++){
      ch[i] = chs[side][i];  
    }
    //send RGB values to DMX lights on appropriate channels.
    DmxSimple.write(ch[0], red);
    DmxSimple.write(ch[1], green);
    DmxSimple.write(ch[2], blue);
}

//0-127 flash slow to fast. 128-255 brightness
void lamp_mode(int b, int side) {
    int ch[2] = {1,5};
    DmxSimple.write(ch[side], b);
}

void getlit(float x, float y, float z, int side) {
  String msg[2] = { "left","right"};
  SenselDebugSerial.println(" ");
  SenselDebugSerial.print("side: ");
  SenselDebugSerial.print(msg[side]);
  SenselDebugSerial.println(" ");
  //get the segment id and interpolation value:
  radianSeg(x,y,side);
  //calculate radius:
  float dist = distance(x, y, centers[side][0], centers[side][1]);
  //interpolate color between nearest segments:
  int seg_id_next = (seg_id+1) % 16;
  lerpRGB(interpolation,rgbs[seg_id],rgbs[seg_id_next],3,side);
  //send color info to DMX lights:
  lamp_color(newcolor[0],newcolor[1],newcolor[2],side);
  //calculate and send brightness value. press hard for strobe time
  if(z<500){
    int bright = brightness(dist);
    lamp_mode(bright,side);
  }else{
    int blinktime = strobe(z);
    SenselDebugSerial.print("blinktime: ");
    SenselDebugSerial.print(blinktime);
    SenselDebugSerial.println(" ");
    lamp_mode(blinktime,side);
  }
}

//apply contacts to DMX
void senselParseFrame(SenselFrame *frame){
     for(int i = 0; i < frame->n_contacts; i++){
      float dist = 0;
      int id = frame->contacts[i].id;
      float x = frame->contacts[i].x_pos;
      float y = frame->contacts[i].y_pos;
      float force = frame->contacts[i].total_force;
      int side = 0;
      int bright = 0;
      int seg_id_next = 0;
      //left side.
      if(x<120 && y<120){
//    SenselDebugSerial.println(" ");
//    SenselDebugSerial.print("force: ");
//    SenselDebugSerial.print(force);
//    SenselDebugSerial.println(" ");
        side = 0;
        getlit(x,y,force,side);
      }
      //right side
      if(x>120 && y<120){
        side = 1;        
        getlit(x,y,force,side);
      }
      if(x>120 && y>120){
        //bottom right
      }
      if(x<120 && y>120){
        //bottom left
      }
    }
  
}

void setup() {
  //SETUP DMX
  /* The most common pin for DMX output is pin 3, which DmxSimple
  ** uses by default. If you need to change that, do it here. */
  DmxSimple.usePin(3);

  /* DMX devices typically need to receive a complete set of channels
  ** even if you only need to adjust the first channel.*/
  DmxSimple.maxChannel(4);

  //SETUP SENSEL MORPH
   //Open serial for SenselSerial declared in sensel.h
  senselOpen();
  //Set frame content to scan. No pressure or label support.
  senselSetFrameContent(SENSEL_REG_CONTACTS_FLAG);
  //Start scanning the Sensel device
  senselStartScanning();
  //Mark the Sensel device as ready
  sensel_ready = true;
 
}

void loop() {
  //When ready, start reading frames
  if(sensel_ready)
  {
    //Read the frame of contacts from the Sensel device
    senselGetFrame(&frame);

    //Print the frame of contact data to SenselDebugSerial if defined
    senselParseFrame(&frame);
  }
}
