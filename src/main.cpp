#include "LCDIC2.h"

#include <bluefairy.h>
using namespace bluefairy;
using namespace ciag::bluefairy;
#include <SocketIOclient.h>

#include <Arduino.h>

#include <utility>
#include <ESP32Servo.h>
#include "ESP32Tone.h"
#include <map>

#include <vector>
#include "FS.h"
#include "SD_MMC.h"
//-----------------------------------------------------------
//-------------------Configuration section-------------------
//-----------------------------------------------------------

const char *networkSSID = "network";
const char *networkPassword = "password";
String baseURL = "backend.ezra.lol"; // DO NOT ADD https / http for IPS!
uint port = 1200;
String username = "MyDoom99";
String password = "MyDoom99";
bool debug = true;
enum PressType
{
    SHORT = 1000,
    LONG = 5000
};

// Pins:
int speaker = 2; // DO NOT USE UINT, as the lib for PWM that controlls the speaker and the servo get f**ed up as they have their own tone method.
uint action = 12;
uint servoPort = 13;
uint sda = 14;
uint scl = 15;
uint hangSensor = 16;
//-----------------------------------------------------------
//----------------------Internal Library---------------------
//-----------------------------------------------------------

Scheduler scheduler;
Servo servo;
void subLoop(bool = true);

class LCD
{
private:
    uint lines;
    uint chars;
    uint address;
    boolean scrollDirection = false;
    uint scrollDelay = 2000;
    LCDIC2 *display;

public:
    /**
     * A LCD Wrapper for use with I2CLCD Library.
     * @param lines Amount of lines the display has
     * @param chars Chars per row
     */
    LCD(uint lines, uint chars, uint address)
    {
        this->lines = lines;
        this->chars = chars;
        this->address = address;
        display = new LCDIC2(address, chars, lines);
        display->begin();
        display->setDisplay(true);

        clearLcd();
    }
    void off()
    {
        display->setBacklight(false);
    }

    void on()
    {
        display->setBacklight(true);
    }

    LCD *setScrollDirection(bool direction)
    {
        scrollDirection = direction;

        return this;
    }

    LCD *writeAt(String str, uint x, uint y)
    {
        if (debug)
        {
            Serial.println("[LOG] " + str);
        }

        display->setCursor(x, y);
        display->print(std::move(str));
        return this;
    }

    LCD *writeCenter(const String &toWrite, uint y = 0)
    {
        if (toWrite.length() - chars <= 0)
        {
            writeAt(toWrite, 0, y);
        }
        else
        {
            writeAt(toWrite, int((chars - toWrite.length()) / 2), y);
        }
        return this;
    }

    LCD *writeCenter(const String &a, const String &b)
    {
        writeCenter(a, 0);
        writeCenter(b, 1);
        return this;
    }

    bluefairy::TaskNode *scrollTask{};

    bool autoScrollActive = false;

    LCD *autoScroll(bool enabled)
    {
        if ((enabled && autoScrollActive) || (!enabled && !autoScrollActive))
        {
            return this;
        }
        if (autoScrollActive)
        {
            scheduler.removeTask(scrollTask);
            autoScrollActive = false;
        }
        else
        {
            autoScrollActive = true;
            if (scrollDirection)
            {
                scrollTask = scheduler.every(scrollDelay, [this]()
                                             { display->moveRight(); });
            }
            else
            {
                scrollTask = scheduler.every(scrollDelay, [this]()
                                             { display->moveLeft(); });
            }
        }
        return this;
    }

    LCD *clearLcd()
    {
        display->clear();
        display->home();
        display->setCursor(false);
        display->setShift(false);
        display->setCursor(0, 0);

        return this;
    }
    LCD *delayedClear(int amountMS = 500)
    {
        delay(amountMS);
        return clearLcd();
    }

    int up = false;

    void showUpdate(uint y = 0)
    {
        writeAt(String(!up), 0, y);
        up = !up;
    }

    ~LCD()
    {
        delete display;
    }
};

class Page
{
private:
    LCD *display;

    String lineA;
    String lineB;

public:
    Page(LCD *display, const String &lineA, const String &lineB)
    {
        this->display = display;
        this->lineA = lineA;
        this->lineB = lineB;
    }

    void render(const std::map<String, String> &replacers)
    {
        display->clearLcd();
        String a = lineA;
        for (const auto &replace : replacers)
        {
            a.replace("{" + replace.first + "}", replace.second);
        }
        String b = lineB;
        for (const auto &replace : replacers)
        {
            b.replace("{" + replace.first + "}", replace.second);
        }
        display->writeCenter(a, b);
    }

    void render()
    {
        display->clearLcd();
        display->writeCenter(lineA, lineB);
    }
};

enum PageId
{
    CODE,
    USER,
    ROUTINE
};

auto replacers = std::map<String, String>();

class Menu
{
private:
    std::map<PageId, Page *> pages = std::map<PageId, Page *>();
    LCD *display;

public:
    explicit Menu(LCD *display)
    {
        this->display = display;
    }

    Menu *createPage(PageId id, const String &lineA, const String &lineB)
    {
        pages[id] = new Page(display, lineA, lineB);
        return this;
    }

    Menu *addPage(const PageId &id, Page *page)
    {
        pages[id] = page;
        return this;
    }

    Menu *setPage(const PageId pageId)
    {
        pages[pageId]->render(replacers);
        return this;
    }
};

LCD *screen;
Menu *menu;
auto pressActions = std::map<uint, std::function<void()>>();
auto releaseActions = std::map<uint, std::function<void(PressType)>>();

auto listeningTo = std::vector<uint>();
auto startTime = std::map<uint, ulong>();

void buttonLoop()
{
    for (uint pin : listeningTo)
    {

        if (!digitalRead(pin))
        {
            // On-Press
            if (startTime.count(pin) == 0)
            {
                startTime[pin] = millis();
                if (pressActions.count(pin) > 0)
                    pressActions[pin]();
            }
        }
        else
        {
            // On-Release
            if (startTime.count(pin) >= 1)
            {
                ulong time = millis() - startTime[pin];
                if (time < LONG)
                {
                    releaseActions[pin](SHORT);
                }
                else
                {
                    releaseActions[pin](LONG);
                }
                startTime.erase(pin);
            }
        }
    }
}

void delayWithLoop(uint delayBy = 1000)
{
    uint waitFor = millis() + delayBy;
    while (millis() < waitFor)
    {
        subLoop();
    }
}

void listenTo(uint pin, const std::function<void(PressType)> &onRelease)
{
    pinMode(pin, INPUT_PULLUP);
    releaseActions[pin] = onRelease;
    listeningTo.push_back(pin);
}

void listenTo(uint pin,
              const std::function<void()> &onPress,
              const std::function<void(PressType)> &onRelease)
{
    pinMode(pin, INPUT_PULLUP);
    pressActions[pin] = onPress;
    releaseActions[pin] = onRelease;
    listeningTo.push_back(pin);
}

std::vector<String> splitString(const String &toSplit, char delimiter)
{
    String current = "";
    std::vector<String> splits = std::vector<String>();
    for (char x : toSplit)
    {
        if (x != delimiter)
        {
            current += x;
        }
        else
        {
            splits.push_back(current);
            current = "";
        }
    }
    if (current != "")
        splits.push_back(current);
    return splits;
}

/**
 * command system
 */
auto commands = std::map<String, std::function<void(std::vector<String> args)>>();

void registerCommand(const String &command, const std::function<void(std::vector<String> args)> &fun)
{
    commands[command] = fun;
}

void runCommand(const std::vector<String> &args)
{
    const auto &command = args[0];

    if (commands.count(command) > 0)
    {
        commands[command](args);
    }
}

String converter(const uint8_t *str)
{
    return {(char *)str};
}

/**
 * Web sockets:
 */
WebSocketsClient ws;
bool connected = false;

void onEvent(WStype_t type, const uint8_t *payload, size_t length)
{
    if (type == WStype_TEXT)
    {
        runCommand(splitString(converter(payload), ' '));
    }

    if (type == WStype_CONNECTED && !connected)
    {
        ws.sendTXT("disconnect");
        connected = true;
        screen->showUpdate();
    }
}

void connectWS()
{

    ws.begin(baseURL, port, "/api/connection");
    ws.setAuthorization(username.c_str(), password.c_str());
    ws.onEvent(onEvent);
    ws.setReconnectInterval(5000);
}

String *code;
String routineId;
String controllerId;

int hangTime;
int pauseTime;
int roundCount;
int restTime;
int numberOfSets;

void disconnect()
{
    screen->clearLcd()->writeCenter("Disconnected", 0)->delayedClear(1500);
    ws.sendTXT("disconnect");
}

void setUpdate(String message)
{
    replacers["status"] = std::move(message);
    menu->setPage(ROUTINE);
}

bool inRoutine = false;
enum RoutineResult
{
    SUCCESS,
    FAILURE,
    STOP
};
uint count = 0;

RoutineResult startRoutine()
{
    inRoutine = true;
    setUpdate("Hang to start!");
    while (digitalRead(hangSensor))
    {
        if (!digitalRead(action))
        {
            return STOP;
        }
        subLoop(false);
    }

    bool toggle = true;
    for (int i = 0; i < numberOfSets; ++i)
    {
        for (int j = 0; j < roundCount; ++j)
        {
            for (int k = 0; k < hangTime; ++k)
            {

                while (digitalRead(hangSensor))
                {
                    if (!digitalRead(action))
                    {
                        noTone(speaker);
                        return FAILURE;
                    }
                    if (toggle)
                    {
                        tone(speaker, 1000);
                        setUpdate("Return hanging!");
                        toggle = false;
                    }
                    subLoop(false);
                }
                noTone(speaker);
                setUpdate("Hang for: " + String(hangTime - k));
                toggle = true;
                delayWithLoop();
            }
            tone(speaker, 500, 250);
            if (j + 1 < roundCount)
            {
                for (int k = 0; k < pauseTime; ++k)
                {
                    setUpdate("Resting: " + String(pauseTime - k));
                    delayWithLoop();
                }
                tone(speaker, 1000, 250);
            }
        }

        for (int k = 0; k < restTime; ++k)
        {
            setUpdate("Resting: " + String(restTime - k));
            delayWithLoop();
        }
        tone(speaker, 2000, 250);
    }
    tone(speaker, 2500, 250);
    delay(50);
    tone(speaker, 3000, 250);
    delay(50);
    tone(speaker, 4000, 250);

    menu->setPage(ROUTINE);
    servo.write(180);
    delay(500);
    servo.write(0);
    delay(500);
    servo.write(180);
    delay(500);
    servo.write(0);
    inRoutine = false;
    return SUCCESS;
}

//-----------------------------------------------------------
//-------------------------Actual code-----------------------
//-----------------------------------------------------------
void setup()
{
    pinMode(hangSensor, INPUT_PULLUP);
    pinMode(speaker, OUTPUT);
    servo.attach(servoPort, 1000, 2000);
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);
    /**
     * Serial setup
     */
    Serial.begin(115200);
    Serial.write("Started!");
    /**
     * Screen startup
     */
    Wire.begin(sda, scl);
    screen = new LCD(2, 16, 0x27);

    menu = new Menu(screen);
    screen->writeCenter("Starting up!", 0)->delayedClear();
    screen->writeCenter("Screen loaded!", 0)->delayedClear();
    /**
     * Wifi startup
     */
    WiFi.begin(networkSSID, networkPassword);
    screen->writeCenter("Connecting to network", 0);
    screen->writeCenter("SSID: " + String(networkSSID), 0);

    screen->writeCenter("Disconnected!", 1);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);

    }
    screen->clearLcd()->writeCenter("Connected!", 1);
    delay(500);
    screen->clearLcd()->writeCenter("Connecting to socket!");
    connectWS();
    screen->clearLcd();
    screen->writeCenter("Waiting for code!");

    menu->createPage(CODE, "ProClimb", "{code}");
    menu->createPage(USER, "User: {controllerId}", "Routine: {routineId}");
    menu->createPage(ROUTINE, "Routine: {routineId}", "{status}");

    /**
     *
     */
    registerCommand("CODE", [](const std::vector<String> &args)
                    {
                        replacers["code"] = args[1];
                        controllerId = "";
                        hangTime = 0;
                        pauseTime = 0;
                        roundCount = 0;
                        restTime = 0;
                        numberOfSets = 0;
                        menu->setPage(CODE); });

    registerCommand("CONNECTION", [](const std::vector<String> &args)
                    {
        replacers["controllerId"] = args[1];
        replacers["routineId"] = "Select!";
        menu->setPage(USER); });

    registerCommand("ROUTINE", [](const std::vector<String> &args)
                    {
        controllerId = args[1];
        routineId = args[2];
        hangTime = std::stol(args[3].c_str());
        pauseTime = std::stol(args[4].c_str());
        roundCount = std::stol(args[5].c_str());
        restTime = std::stol(args[6].c_str());
        numberOfSets = std::stol(args[7].c_str());

        replacers["routineId"] = routineId;
        replacers["controllerId"] = controllerId;
        replacers["hangTime"] = hangTime;
        replacers["pauseTime"] = pauseTime;
        replacers["roundCount"] = roundCount;
        replacers["restTime"] = restTime;
        replacers["numberOfSets"] = numberOfSets;

        menu->setPage(USER); });

    listenTo(action, [](PressType type)
             {
                 if (type == LONG) {
                     disconnect();
                 } else {
                     if (routineId != nullptr && !inRoutine) {
                         switch (startRoutine()) {
                             case SUCCESS:
                                ws.sendTXT("success");
                                 break;
                             case FAILURE:
                                 ws.sendTXT("failure");
                                 break;
                             case STOP:
                                 break;
                         }
                         menu->setPage(USER);
                         inRoutine = false;
                         delay(1000);
                     }
                 } }

    );
}

void subLoop(bool button)
{
    buttonLoop();
}

void loop()
{
    ws.loop();
    buttonLoop();
}
