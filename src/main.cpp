// cyd-flight-radar
// Phase 6: closest-aircraft route lookup (origin -> destination) via adsbdb

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <math.h>
#include "secrets.h"

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

static const char *TOKEN_URL =
    "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token";

static const float RADAR_RANGE_MI = 25.0f;
static const int   RANGE_RINGS    = 3;
static const float BOX_LAT_HALF = 0.7f;
static const float BOX_LON_HALF = 0.9f;

#define PH_DIM    0x03E0
#define PH_MED    0x05E0
#define PH_BRIGHT 0x07E0
#define PH_WHITE  0xAFF5

int CX, CY, RADIUS, SPR_SIZE;
int scx, scy;

String g_access_token = "";
unsigned long g_token_expiry_ms = 0;

struct Aircraft {
  char  callsign[10];
  float lat, lon, alt_m, track_deg, velocity;
  bool  on_ground;
  float dist_mi, bearing_deg;
  float cur_lat, cur_lon;
};
static const int MAX_AIRCRAFT = 40;
Aircraft g_aircraft[MAX_AIRCRAFT];
int g_aircraft_count = 0;

// --- route cache for the closest aircraft ---
char g_route_callsign[10] = "";   // which callsign the cached route is for
char g_route_from[20] = "";       // origin municipality or code
char g_route_to[20]   = "";       // destination municipality or code
bool g_route_known = false;

float deg2rad(float d){ return d*0.0174532925f; }

float haversineMi(float la1,float lo1,float la2,float lo2){
  const float R=3958.8f;
  float dla=deg2rad(la2-la1), dlo=deg2rad(lo2-lo1);
  float a=sinf(dla/2)*sinf(dla/2)+cosf(deg2rad(la1))*cosf(deg2rad(la2))*sinf(dlo/2)*sinf(dlo/2);
  return R*2*atan2f(sqrtf(a),sqrtf(1-a));
}
float bearingDeg(float la1,float lo1,float la2,float lo2){
  float y=sinf(deg2rad(lo2-lo1))*cosf(deg2rad(la2));
  float x=cosf(deg2rad(la1))*sinf(deg2rad(la2))-sinf(deg2rad(la1))*cosf(deg2rad(la2))*cosf(deg2rad(lo2-lo1));
  float b=atan2f(y,x)/0.0174532925f;
  if(b<0)b+=360; return b;
}

bool wifiConnect(){
  WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  int t=0; while(WiFi.status()!=WL_CONNECTED && t<30){delay(500);Serial.print(".");t++;}
  Serial.println();
  return WiFi.status()==WL_CONNECTED;
}

bool getOpenSkyToken(){
  if(g_access_token.length()>0 && millis()+30000<g_token_expiry_ms) return true;
  HTTPClient http; http.begin(TOKEN_URL);
  http.addHeader("Content-Type","application/x-www-form-urlencoded");
  String body="grant_type=client_credentials";
  body+="&client_id="; body+=OPENSKY_CLIENT_ID;
  body+="&client_secret="; body+=OPENSKY_CLIENT_SECRET;
  int code=http.POST(body);
  if(code!=200){http.end();return false;}
  String resp=http.getString(); http.end();
  JsonDocument doc;
  if(deserializeJson(doc,resp)) return false;
  const char* tk=doc["access_token"]; int ex=doc["expires_in"]|0;
  if(!tk||ex<=0) return false;
  g_access_token=tk; g_token_expiry_ms=millis()+(unsigned long)(ex-60)*1000UL;
  return true;
}

bool queryAircraft(){
  if(!getOpenSkyToken()) return false;
  float lamin=(float)HOME_LAT-BOX_LAT_HALF, lamax=(float)HOME_LAT+BOX_LAT_HALF;
  float lomin=(float)HOME_LON-BOX_LON_HALF, lomax=(float)HOME_LON+BOX_LON_HALF;
  char url[256];
  snprintf(url,sizeof(url),
    "https://opensky-network.org/api/states/all?lamin=%.4f&lomin=%.4f&lamax=%.4f&lomax=%.4f",
    lamin,lomin,lamax,lomax);
  HTTPClient http; http.begin(url);
  http.addHeader("Authorization","Bearer "+g_access_token);
  int code=http.GET();
  if(code!=200){Serial.printf("states fail %d\n",code);http.end();return false;}
  String resp=http.getString(); http.end();
  JsonDocument doc;
  if(deserializeJson(doc,resp)) return false;
  JsonArray states=doc["states"].as<JsonArray>();
  g_aircraft_count=0;
  for(JsonArray st:states){
    if(g_aircraft_count>=MAX_AIRCRAFT) break;
    if(st[5].isNull()||st[6].isNull()) continue;
    Aircraft&a=g_aircraft[g_aircraft_count];
    const char* cs=st[1];
    if(cs){strncpy(a.callsign,cs,sizeof(a.callsign)-1);a.callsign[sizeof(a.callsign)-1]='\0';
      for(int i=strlen(a.callsign)-1;i>=0&&a.callsign[i]==' ';i--)a.callsign[i]='\0';}
    else strcpy(a.callsign,"?");
    a.lon=st[5].as<float>(); a.lat=st[6].as<float>();
    a.alt_m=st[7].isNull()?0:st[7].as<float>();
    a.on_ground=st[8].as<bool>();
    a.velocity=st[9].isNull()?0:st[9].as<float>();
    a.track_deg=st[10].isNull()?0:st[10].as<float>();
    a.cur_lat=a.lat; a.cur_lon=a.lon;
    a.dist_mi=haversineMi((float)HOME_LAT,(float)HOME_LON,a.lat,a.lon);
    a.bearing_deg=bearingDeg((float)HOME_LAT,(float)HOME_LON,a.lat,a.lon);
    g_aircraft_count++;
  }
  Serial.printf("parsed %d\n",g_aircraft_count);
  return true;
}

// Look up route for a callsign via adsbdb. Fills g_route_* globals.
void lookupRoute(const char* callsign){
  // Skip if we already have this callsign cached
  if(strcmp(callsign, g_route_callsign)==0) return;
  strncpy(g_route_callsign, callsign, sizeof(g_route_callsign)-1);
  g_route_callsign[sizeof(g_route_callsign)-1]='\0';
  g_route_known=false;
  g_route_from[0]='\0'; g_route_to[0]='\0';

  if(strlen(callsign)<3 || callsign[0]=='?') return;

  char url[80];
  snprintf(url,sizeof(url),"https://api.adsbdb.com/v0/callsign/%s",callsign);
  HTTPClient http; http.begin(url);
  int code=http.GET();
  if(code!=200){ http.end(); Serial.printf("route lookup %s -> %d\n",callsign,code); return; }
  String resp=http.getString(); http.end();

  JsonDocument doc;
  if(deserializeJson(doc,resp)) return;
  JsonObject fr = doc["response"]["flightroute"];
  if(fr.isNull()) return;

  const char* fromMuni = fr["origin"]["municipality"];
  const char* fromCode = fr["origin"]["iata_code"];
  const char* toMuni   = fr["destination"]["municipality"];
  const char* toCode   = fr["destination"]["iata_code"];

  // prefer municipality, fall back to code
  if(fromMuni) strncpy(g_route_from, fromMuni, sizeof(g_route_from)-1);
  else if(fromCode) strncpy(g_route_from, fromCode, sizeof(g_route_from)-1);
  g_route_from[sizeof(g_route_from)-1]='\0';

  if(toMuni) strncpy(g_route_to, toMuni, sizeof(g_route_to)-1);
  else if(toCode) strncpy(g_route_to, toCode, sizeof(g_route_to)-1);
  g_route_to[sizeof(g_route_to)-1]='\0';

  if(strlen(g_route_from)>0 || strlen(g_route_to)>0){
    g_route_known=true;
    Serial.printf("route %s: %s -> %s\n", callsign, g_route_from, g_route_to);
  }
}

void advancePositions(float dt){
  for(int i=0;i<g_aircraft_count;i++){
    Aircraft&a=g_aircraft[i];
    if(a.on_ground||a.velocity<1) continue;
    float dist_mi=(a.velocity*dt)/1609.34f;
    float br=deg2rad(a.track_deg);
    float dlat=(dist_mi*cosf(br))/69.0f;
    float dlon=(dist_mi*sinf(br))/(69.0f*cosf(deg2rad(a.cur_lat)));
    a.cur_lat+=dlat; a.cur_lon+=dlon;
    a.dist_mi=haversineMi((float)HOME_LAT,(float)HOME_LON,a.cur_lat,a.cur_lon);
    a.bearing_deg=bearingDeg((float)HOME_LAT,(float)HOME_LON,a.cur_lat,a.cur_lon);
  }
}

void sprTriangle(int x,int y,float hdg,uint16_t color){
  float a=deg2rad(hdg);
  float tx=x+5*sinf(a), ty=y-5*cosf(a);
  float lx=x+4*sinf(a+2.5f), ly=y-4*cosf(a+2.5f);
  float rx=x+4*sinf(a-2.5f), ry=y-4*cosf(a-2.5f);
  spr.fillTriangle(tx,ty,lx,ly,rx,ry,color);
}

void renderRadar(float sweepDeg){
  spr.fillSprite(TFT_BLACK);
  for(int r=1;r<=RANGE_RINGS;r++) spr.drawCircle(scx,scy,RADIUS*r/RANGE_RINGS,PH_DIM);
  spr.drawLine(scx-RADIUS,scy,scx+RADIUS,scy,PH_DIM);
  spr.drawLine(scx,scy-RADIUS,scx,scy+RADIUS,PH_DIM);
  spr.setTextColor(PH_MED,TFT_BLACK); spr.setTextSize(1);
  spr.setCursor(scx-2,scy-RADIUS-9); spr.print("N");
  spr.setCursor(scx-2,scy+RADIUS+2); spr.print("S");
  spr.setCursor(scx+RADIUS+2,scy-3); spr.print("E");
  spr.setCursor(scx-RADIUS-8,scy-3); spr.print("W");
  for(int t=10;t>=1;t--){
    float a=deg2rad(sweepDeg-t);
    uint16_t c=(t<=3)?PH_MED:PH_DIM;
    int ex=scx+(int)(RADIUS*sinf(a)), ey=scy-(int)(RADIUS*cosf(a));
    spr.drawLine(scx,scy,ex,ey,c);
  }
  { float a=deg2rad(sweepDeg);
    int ex=scx+(int)(RADIUS*sinf(a)), ey=scy-(int)(RADIUS*cosf(a));
    spr.drawLine(scx,scy,ex,ey,PH_WHITE); }
  for(int i=0;i<g_aircraft_count;i++){
    Aircraft&a=g_aircraft[i];
    if(a.dist_mi>RADAR_RANGE_MI||a.on_ground) continue;
    float rpx=(a.dist_mi/RADAR_RANGE_MI)*RADIUS;
    float ang=deg2rad(a.bearing_deg);
    int px=scx+(int)(rpx*sinf(ang)), py=scy-(int)(rpx*cosf(ang));
    float behind=sweepDeg-a.bearing_deg; while(behind<0)behind+=360; while(behind>=360)behind-=360;
    uint16_t col=(behind<20)?PH_WHITE:(behind<100)?PH_BRIGHT:PH_MED;
    sprTriangle(px,py,a.track_deg,col);
    spr.setTextColor(col,TFT_BLACK); spr.setTextSize(1);
    spr.setCursor(px+6,py-3); spr.print(a.callsign);
  }
  spr.fillCircle(scx,scy,2,PH_WHITE);
  spr.setTextColor(PH_DIM,TFT_BLACK); spr.setCursor(2,2);
  spr.printf("%.0fmi",RADAR_RANGE_MI);
  spr.pushSprite(0,0);
}

int g_closest_idx = -1;

void drawSidebar(){
  int sx=SPR_SIZE+2;
  tft.fillRect(SPR_SIZE,0,320-SPR_SIZE,240,TFT_BLACK);
  int shown=0,closest=-1; float cd=9999;
  for(int i=0;i<g_aircraft_count;i++){
    Aircraft&a=g_aircraft[i];
    if(a.dist_mi>RADAR_RANGE_MI||a.on_ground) continue;
    shown++; if(a.dist_mi<cd){cd=a.dist_mi;closest=i;}
  }
  g_closest_idx=closest;
  tft.setTextSize(1); tft.setTextColor(PH_WHITE,TFT_BLACK);
  tft.setCursor(sx,18); tft.printf("Trk:%d",shown);
  if(closest>=0){
    Aircraft&a=g_aircraft[closest];
    lookupRoute(a.callsign);  // refresh route cache for closest
    tft.setTextColor(PH_BRIGHT,TFT_BLACK); tft.setCursor(sx,38); tft.print(a.callsign);
    tft.setTextColor(PH_MED,TFT_BLACK);
    tft.setCursor(sx,54); tft.printf("%.1fmi",a.dist_mi);
    tft.setCursor(sx,66); tft.printf("%.0fft",a.alt_m*3.281f);
    tft.setCursor(sx,78); tft.printf("%.0fkt",a.velocity*1.944f);
    tft.setCursor(sx,90); tft.printf("hdg%.0f",a.track_deg);
    // route
    tft.setTextColor(PH_WHITE,TFT_BLACK);
    tft.setCursor(sx,110); tft.print("From:");
    tft.setTextColor(PH_BRIGHT,TFT_BLACK);
    tft.setCursor(sx,122);
    tft.print(g_route_known && strlen(g_route_from) ? g_route_from : "unknown");
    tft.setTextColor(PH_WHITE,TFT_BLACK);
    tft.setCursor(sx,142); tft.print("To:");
    tft.setTextColor(PH_BRIGHT,TFT_BLACK);
    tft.setCursor(sx,154);
    tft.print(g_route_known && strlen(g_route_to) ? g_route_to : "unknown");
  }
}

void setup(){
  Serial.begin(115200); delay(1000);
  Serial.println("\n=== cyd-flight-radar ===");
  Serial.println("Phase 6: route lookup");
  pinMode(TFT_BL,OUTPUT); digitalWrite(TFT_BL,HIGH);
  tft.init(); tft.setRotation(3); tft.fillScreen(TFT_BLACK);
  SPR_SIZE=220; RADIUS=105; scx=SPR_SIZE/2; scy=SPR_SIZE/2;
  if(!spr.createSprite(SPR_SIZE,SPR_SIZE)) Serial.println("Sprite alloc FAILED");
  tft.setTextColor(PH_BRIGHT,TFT_BLACK); tft.setTextSize(2);
  tft.setCursor(10,100); tft.print("Connecting...");
  if(!wifiConnect()){
    tft.fillScreen(TFT_BLACK); tft.setTextColor(TFT_RED,TFT_BLACK);
    tft.setCursor(10,100); tft.print("WiFi FAILED"); return;
  }
  queryAircraft(); drawSidebar();
}

unsigned long lastQuery=0, lastFrame=0;
float sweep=0;

void loop(){
  if(millis()-lastQuery > 15000){
    if(WiFi.status()!=WL_CONNECTED) wifiConnect();
    if(queryAircraft()) drawSidebar();
    lastQuery=millis();
  }
  unsigned long now=millis();
  float dt=(now-lastFrame)/1000.0f;
  if(dt<=0||dt>1) dt=0.02f;
  lastFrame=now;
  advancePositions(dt);
  sweep+=3.0f; if(sweep>=360)sweep-=360;
  renderRadar(sweep);
  delay(20);
}
