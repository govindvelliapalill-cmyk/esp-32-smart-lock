
#include <WiFi.h>
#include <WebServer.h>
#include <AccelStepper.h>
#include <esp_system.h>

// ==================================================
// 1. WI-FI SETTINGS
// ==================================================

const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// ==================================================
// 2. LOGIN SETTINGS
// ==================================================

const char* WEB_USERNAME = "admin";
const char* WEB_PASSWORD = "CHANGE_TO_A_STRONG_PASSWORD";

const unsigned long SESSION_TIMEOUT = 30UL * 60UL * 1000UL;

String activeSession = "";
unsigned long lastActivity = 0;

// ==================================================
// 3. MOTOR SETTINGS
// ==================================================

#define IN1 13
#define IN2 12
#define IN3 14
#define IN4 27

// ULN2003 / 28BYJ-48 half-step configuration
AccelStepper stepper(
  AccelStepper::HALF4WIRE,
  IN1, IN3, IN2, IN4
);

// Both directions use exactly 4096 commanded steps.
const long CLOCKWISE_STEPS = 4096;
const long ANTICLOCKWISE_STEPS = 4096;

const float MOTOR_MAX_SPEED = 500.0;
const float MOTOR_ACCELERATION = 250.0;

bool motorBusy = false;
String motorStatus = "Ready";
String lastDirection = "None";

// ==================================================
// 4. WEB SERVER
// ==================================================

WebServer server(80);
const char* cookieHeaders[] = {"Cookie"};

// ==================================================
// 5. LOGIN PAGE
// ==================================================

const char LOGIN_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Smart Door Lock Login</title>
<style>
*{box-sizing:border-box}
body{
  margin:0;min-height:100vh;display:flex;
  justify-content:center;align-items:center;
  background:#101827;color:white;
  font-family:Arial,sans-serif;padding:20px
}
.card{
  width:100%;max-width:360px;background:#1e293b;
  padding:28px;border-radius:16px
}
h2,p{text-align:center}
p{color:#cbd5e1}
input{
  width:100%;padding:13px;margin:8px 0;
  border:1px solid #475569;border-radius:8px;
  background:#0f172a;color:white;font-size:16px
}
button{
  width:100%;padding:13px;margin-top:12px;
  border:0;border-radius:8px;background:#059669;
  color:white;font-size:16px;font-weight:bold
}
#message{font-size:14px;color:#fca5a5}
</style>
</head>
<body>
<div class="card">
  <h2>🔐 Smart Door Lock</h2>
  <p>Sign in to control the motor</p>
  <form method="POST" action="/login">
    <input name="username" placeholder="Username"
      autocomplete="username" required>
    <input name="password" type="password"
      placeholder="Password" autocomplete="current-password" required>
    <button type="submit">Login</button>
  </form>
  <p id="message">Authorized users only</p>
</div>
<script>
if(new URLSearchParams(location.search).get("error")==="1"){
  document.getElementById("message").textContent=
    "Invalid username or password.";
}
</script>
</body>
</html>
)rawliteral";

// ==================================================
// 6. MOTOR CONTROL PAGE
// ==================================================

const char CONTROL_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Smart Door Lock Control</title>
<style>
*{box-sizing:border-box}
body{
  margin:0;min-height:100vh;display:flex;
  justify-content:center;align-items:center;
  padding:20px;background:#101827;color:white;
  font-family:Arial,sans-serif
}
.card{
  width:100%;max-width:460px;background:#1e293b;
  padding:25px;border-radius:16px;text-align:center
}
.subtitle{color:#cbd5e1}
.buttons{display:flex;gap:12px;margin:25px 0}
button{
  flex:1;min-width:0;padding:16px 8px;
  border:0;border-radius:10px;color:white;
  font-size:15px;font-weight:bold;cursor:pointer
}
button:disabled{opacity:.45;cursor:not-allowed}
.clockwise{background:#059669}
.anticlockwise{background:#2563eb}
.logout{width:100%;background:#475569;margin-top:18px}
#status{
  padding:14px;border-radius:8px;
  background:#0f172a;color:#86efac;
  overflow-wrap:anywhere
}
.note{font-size:12px;color:#94a3b8;line-height:1.5}
</style>
</head>
<body>
<div class="card">
  <h2>🔐 Smart Door Lock</h2>
  <p class="subtitle">Login successful</p>
  <h3>Motor Control</h3>
  <p class="subtitle">Each press commands 4096 steps</p>

  <div class="buttons">
    <button class="clockwise" id="cw"
      onclick="rotateMotor('clockwise')">
      ↻ Clockwise<br><br>4096 steps
    </button>
    <button class="anticlockwise" id="ccw"
      onclick="rotateMotor('anticlockwise')">
      ↺ Anticlockwise<br><br>4096 steps
    </button>
  </div>

  <div id="status" role="status">Checking motor status...</div>
  <p class="note">Wait for movement to finish before sending another command.</p>
  <button class="logout" onclick="logout()">Logout</button>
</div>

<script>
const cw=document.getElementById("cw");
const ccw=document.getElementById("ccw");
const statusBox=document.getElementById("status");
let requestPending=false;

async function updateStatus(){
  try{
    const response=await fetch("/status",{cache:"no-store"});
    if(response.status===401){location.replace("/");return;}
    if(!response.ok)throw new Error("Status unavailable");

    const data=await response.json();
    statusBox.textContent="Motor status: "+data.status+
      " | Last direction: "+data.direction;

    cw.disabled=data.busy||requestPending;
    ccw.disabled=data.busy||requestPending;
  }catch(e){
    statusBox.textContent="Cannot reach ESP32.";
    cw.disabled=true;
    ccw.disabled=true;
  }
}

async function rotateMotor(direction){
  if(requestPending)return;

  requestPending=true;
  cw.disabled=true;
  ccw.disabled=true;
  statusBox.textContent="Sending command...";

  try{
    const response=await fetch("/rotate",{
      method:"POST",
      headers:{"Content-Type":"application/x-www-form-urlencoded"},
      body:"direction="+encodeURIComponent(direction)
    });

    if(response.status===401){location.replace("/");return;}

    const data=await response.json();
    if(!response.ok)throw new Error(data.message||"Command rejected");
    statusBox.textContent=data.message;
  }catch(e){
    statusBox.textContent=e.message||"Could not send command.";
  }finally{
    requestPending=false;
    updateStatus();
  }
}

async function logout(){
  try{
    await fetch("/logout",{method:"POST"});
  }finally{
    location.replace("/");
  }
}

updateStatus();
setInterval(updateStatus,1000);
</script>
</body>
</html>
)rawliteral";

// ==================================================
// 7. SESSION HELPERS
// ==================================================

String generateSessionToken(){
  String token="";
  for(int i=0;i<4;i++){
    char part[9];
    snprintf(part,sizeof(part),"%08lx",
             (unsigned long)esp_random());
    token+=part;
  }
  return token;
}

void clearSession(){
  activeSession="";
  lastActivity=0;
}

bool isAuthenticated(){
  if(activeSession.length()==0)return false;

  unsigned long now=millis();
  if((unsigned long)(now-lastActivity)>=SESSION_TIMEOUT){
    clearSession();
    return false;
  }

  String cookie=server.header("Cookie");
  String expected="ESPSESSION="+activeSession;
  int pos=cookie.indexOf(expected);
  bool valid=false;

  if(pos>=0){
    int end=pos+expected.length();
    valid=(end==(int)cookie.length()||
           cookie[end]==';'||cookie[end]==' ');
  }

  if(!valid)return false;

  lastActivity=now;
  return true;
}

bool requireAuthentication(){
  if(!isAuthenticated()){
    server.sendHeader("Cache-Control","no-store");
    server.sendHeader("Location","/");
    server.send(401,"text/plain","Login required");
    return false;
  }
  return true;
}

void sendJson(int code,const String& json){
  server.sendHeader("Cache-Control","no-store");
  server.send(code,"application/json",json);
}

// ==================================================
// 8. HOME ROUTE
// ==================================================

void handleHome(){
  server.sendHeader("Cache-Control","no-store");

  if(isAuthenticated()){
    server.send_P(200,"text/html",CONTROL_PAGE);
  }else{
    server.send_P(200,"text/html",LOGIN_PAGE);
  }
}

// ==================================================
// 9. LOGIN ROUTE
// ==================================================

void handleLogin(){
  String username=server.arg("username");
  String password=server.arg("password");

  if(username==WEB_USERNAME && password==WEB_PASSWORD){
    activeSession=generateSessionToken();
    lastActivity=millis();

    server.sendHeader(
      "Set-Cookie",
      "ESPSESSION="+activeSession+
      "; Path=/; HttpOnly; SameSite=Strict"
    );
    server.sendHeader("Cache-Control","no-store");
    server.sendHeader("Location","/");
    server.send(303,"text/plain","Login successful");
  }else{
    server.sendHeader("Cache-Control","no-store");
    server.sendHeader("Location","/?error=1");
    server.send(303,"text/plain","Invalid credentials");
  }
}

// ==================================================
// 10. ROTATION ROUTE
// ==================================================

void handleRotate(){
  if(!requireAuthentication())return;

  if(motorBusy||stepper.distanceToGo()!=0){
    sendJson(409,
      "{\"ok\":false,\"message\":\"Motor is already moving\"}");
    return;
  }

  String direction=server.arg("direction");
  long movement=0;

  if(direction=="clockwise"){
    movement=CLOCKWISE_STEPS;
    lastDirection="Clockwise";
  }else if(direction=="anticlockwise"){
    movement=-ANTICLOCKWISE_STEPS;
    lastDirection="Anticlockwise";
  }else{
    sendJson(400,
      "{\"ok\":false,\"message\":\"Invalid direction\"}");
    return;
  }

  stepper.move(movement);
  motorBusy=true;
  motorStatus="Moving";

  sendJson(200,
    "{\"ok\":true,\"message\":\"4096-step rotation started\"}");
}

// ==================================================
// 11. STATUS ROUTE
// ==================================================

void handleStatus(){
  if(!requireAuthentication())return;

  String json="{\"busy\":";
  json+=(motorBusy?"true":"false");
  json+=",\"status\":\""+motorStatus+"\"";
  json+=",\"direction\":\""+lastDirection+"\"}";

  sendJson(200,json);
}

// ==================================================
// 12. LOGOUT ROUTE
// ==================================================

void handleLogout(){
  if(!requireAuthentication())return;

  clearSession();

  server.sendHeader(
    "Set-Cookie",
    "ESPSESSION=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict"
  );
  server.sendHeader("Cache-Control","no-store");
  server.send(200,"text/plain","Logged out");
}

// ==================================================
// 13. SETUP
// ==================================================

void setup(){
  Serial.begin(115200);

  stepper.setMaxSpeed(MOTOR_MAX_SPEED);
  stepper.setAcceleration(MOTOR_ACCELERATION);
  stepper.setCurrentPosition(0);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID,WIFI_PASSWORD);

  Serial.print("Connecting to Wi-Fi");
  while(WiFi.status()!=WL_CONNECTED){
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("ESP32 IP address: ");
  Serial.println(WiFi.localIP());

  server.collectHeaders(cookieHeaders,1);

  server.on("/",HTTP_GET,handleHome);
  server.on("/login",HTTP_POST,handleLogin);
  server.on("/rotate",HTTP_POST,handleRotate);
  server.on("/status",HTTP_GET,handleStatus);
  server.on("/logout",HTTP_POST,handleLogout);

  server.begin();
  Serial.println("Web server started");
}

// ==================================================
// 14. MAIN LOOP
// ==================================================

void loop(){
  server.handleClient();

  if(motorBusy){
    stepper.run();

    if(stepper.distanceToGo()==0){
      motorBusy=false;
      motorStatus="Ready";
      Serial.println("4096-step rotation completed");
    }
  }
}
