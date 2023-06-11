<h1>ProClimb-Client</h1>

<h2>Introduction</h2>
This project is split up into to sections:
<h3>[Backend](https://github.com/shmezi/ProClimbBackend.git)</h3>
The backend is written in kotlin using [Ktor](https://github.com/ktorio/ktor)
and [AlexLIb](https://github.com/shmezi/alexlib).
The backend handles the data, authentication, webhooks and the frontend website.

<h3>[Client](https://github.com/shmezi/ProClimb-Client)</h3>
The Client is written in CPP and communicates with the backend using webhook commands.
The client is designed to run on an esp32 and send data to the backend.
<h2>Setup</h2>

1. [Build backend](https://github.com/shmezi/ProClimbBackend.git)
2. Run backend with port 1200 open for connections
3. Run a [MongoDb](https://www.mongodb.com/) instance that the backend can connect to
4. Edit the config file to point of the backend and mongodb instance
5. Edit [esp32 code](https://github.com/shmezi/ProClimb-Client/blob/master/src/main.cpp#L21-L40) to point to the backend
   and attach to network (And other configurations).
6. Build / Upload code to the esp32 using platformio.
7. Start up the esp32 and it should connect to the backend and start sending data.
8. Boom you're set up.

<h2>Building board</h2>
Components:

- ESP32 / ESP32CAM
- Small speaker with signal wire
- 7v power supply (DO NOT SKIMP)
- 2x Buttons
- Jumper cables
- LCD screen with I2C interface

TODO:
- [ ] Add schematic
- [ ] Add board layout
- [ ] Add 3d model
- [ ] Add pictures
- [ ] Clean backend code
- [ ] Backend create section
- [ ] Add more to readme
- [ ] SD Card config
- [ ] Better connection between pages
- [ ] Add more to readme