# ORBITAL v2.1 UI copy

## Validation messages

| Code | Severity | Cause | Fix |
| --- | --- | --- | --- |
| E_UNKNOWN_PART | BLOCK | The design refers to an unknown part or contains an invalid number. | Reload a valid design or replace the invalid entry. |
| E_MISSING_REQUIRED_PART | BLOCK | A required piece of the vehicle is missing. | Add the part named in the warning. |
| E_SOCKET_LIMIT | BLOCK | There are too many parts of this type for the available mounts. | Remove an extra part of this type. |
| E_PROP_OVER_CAPACITY | BLOCK | You are trying to load more propellant than the tanks can hold. | Load less propellant or increase tank capacity. |
| E_BUS_OVERLOAD | BLOCK | The fueled spacecraft is too heavy for its central frame. | Use a larger bus or remove mass. |
| E_BUS_INSTRUMENT_SLOTS | BLOCK | The spacecraft has more instruments than the bus can mount. | Remove an instrument or use a larger bus. |
| E_COMPUTER_INSTRUMENT_LIMIT | BLOCK | The computer cannot run this many instruments. | Remove an instrument or choose a computer that can run more. |
| E_ENGINE_COUNT | BLOCK | The number of booster engines is outside this engine's allowed range. | Set the engine count within the range shown for that engine. |
| E_TWR_LIFTOFF | BLOCK | The engines provide too little thrust for the rocket's liftoff mass. | Increase booster thrust or reduce liftoff mass. |
| E_TWR_STAGE2 | BLOCK | The upper stage provides too little thrust for the mass it carries. | Choose an upper-stage engine with more thrust or reduce the mass it carries. |
| E_LV_DELTA_V | BLOCK | The rocket cannot change its speed enough to reach parking orbit and send this payload toward the Moon. | Choose larger stages or more efficient engines, or lighten the spacecraft. |
| E_FAIRING_MASS | BLOCK | The spacecraft is heavier than this fairing's payload limit. | Choose a fairing rated for more mass or lighten the spacecraft. |
| E_FAIRING_SIZE | BLOCK | This fairing does not fit the selected spacecraft bus. | Choose a fairing that fits your bus. |
| E_BUDGET | BLOCK | The mission costs more than the selected budget allows. | Replace expensive parts or ground services with cheaper options. |
| R_SC_DELTA_V_INSERTION | RISK | The probe cannot change its speed enough to enter lunar orbit, so it will fly past the Moon. | Load more propellant within tank capacity, use a more efficient engine, or reduce mass. |
| R_SC_DELTA_V_LIFETIME | RISK | The probe can enter orbit, but will run out of propellant for orbit corrections before the planned mission ends. | Load more propellant within tank capacity or choose an orbit that needs fewer corrections. |
| R_POWER_GENERATION | RISK | The power sources cannot run the equipment and recharge the batteries, so the mission will fail when it passes through shadow. | Add solar array area (or an RTG where sunlight is weak), or reduce electrical loads by removing instruments. |
| R_BATTERY_ECLIPSE | RISK | The batteries cannot keep the spacecraft powered through its first eclipse. | Add usable battery capacity or reduce electrical loads. |
| R_NEEDS_FINE_POINTING | RISK | An instrument or antenna needs precise aiming and will be unusable without it. | Add a Reaction Wheel Set. |
| R_STORAGE | RISK | Storage fills before the next ground contact, so some data is lost. | Choose a computer with more storage or a ground network with shorter gaps. |
| R_NO_INSTRUMENT | RISK | The spacecraft carries no scientific instruments, so it will collect no science data. | Add an instrument that supports your objective. |
| R_PRIMARY_INSTRUMENT_MISSING | RISK | The main objective has no working instrument, so it cannot be completed. | Install a matching instrument or add the pointing control it requires. |
| W_DATA_BOTTLENECK | WARN | Storage or downlink limits keep some collected data from reaching Earth. | Check which limit is binding, then increase storage, downlink rate, or ground contact as needed. |

## Part tooltips

| Part ID | Tooltip |
| --- | --- |
| bus_small | A compact frame for lightweight, low-cost uncrewed probes. |
| bus_medium | A larger uncrewed spacecraft frame with room for up to five instruments. |
| command_module | A control module reserved for future crewed missions. |
| crew_capsule | A small capsule reserved for future crewed missions. |
| life_support | Supplies air, water, and temperature control for future crewed missions. |
| sc_tank_s | A small spacecraft tank that holds up to 150 kg of propellant. |
| sc_tank_m | A medium spacecraft tank that holds up to 400 kg of propellant. |
| sc_tank_l | A large spacecraft tank that holds up to 900 kg of propellant. |
| sc_engine_mono | A simpler spacecraft engine that uses propellant less efficiently than the bipropellant engine. |
| sc_engine_biprop | A more efficient spacecraft engine that costs more than the monopropellant engine. |
| rcs | Small thrusters that turn the spacecraft and are required on every spacecraft. |
| reaction_wheels | Provide the precise aiming that some instruments and the high-gain antenna need. |
| solar_s | A small solar array that supplies electricity in sunlight. |
| solar_l | A larger solar array that supplies more electricity in sunlight. |
| battery_s | A small battery that keeps spacecraft systems powered in shadow. |
| battery_l | A larger battery that stores more energy for time spent in shadow. |
| rtg | Makes about 200 W from the heat of decaying plutonium, in sunlight or in shadow. Heavy and very expensive. |
| antenna_lga | Sends data slowly and works at any spacecraft orientation. |
| antenna_mga | Sends data at a moderate rate with coarse aiming. |
| antenna_hga | Sends data quickly but needs a Reaction Wheel Set for precise aiming. |
| obc_basic | Runs up to two instruments and stores 16 GB of data. |
| obc_advanced | Runs up to five instruments and stores 256 GB of data. |
| inst_neutron | Detects hydrogen in lunar soil as a clue to possible water ice. |
| inst_altimeter | Uses laser pulses to measure terrain height, including inside shadowed craters. |
| inst_radiometer | Measures surface temperatures to locate cold traps. |
| inst_camera | Takes detailed images of potential landing sites and generates a lot of data. |
| inst_radar | Images permanently shadowed craters and uses substantial electrical power. |
| s1_tank_200 | A first-stage tank that holds 200 metric tons of propellant. |
| s1_tank_285 | A larger first-stage tank that holds 285 metric tons of propellant. |
| s1_engine_kl190 | A lower-cost booster engine that can be installed in groups of one to three. |
| s1_engine_kl380 | A higher-thrust booster engine that can be installed singly or in pairs. |
| s2_tank_7 | An upper-stage tank that holds 7 metric tons of propellant. |
| s2_tank_21 | A larger upper-stage tank that holds 20.8 metric tons of propellant. |
| s2_engine_kl35 | A lower-cost upper-stage engine with moderate propellant efficiency. |
| s2_engine_hl10 | An efficient upper-stage engine with a higher cost and lower thrust. |
| fairing_s | Protects Probe Bus S during launch and accepts a payload of up to 600 kg. |
| fairing_l | Protects larger payloads during launch and accepts a payload of up to 4,000 kg. |
| sep_system | Releases a spent rocket stage or separates the spacecraft from the launcher. |
| les | Pulls the crew capsule away from a failing rocket in future crewed missions. |

## Dashboard tooltips

| Resource | Tooltip |
| --- | --- |
| Mass | See how much the spacecraft weighs with and without propellant and how much the full stack weighs at liftoff. |
| Power | Compare electricity generated and used, and check whether stored energy lasts through eclipse. |
| Budget | Compare total mission cost, including hardware and operations, with your limit in M credits. |
| Data | Compare the data your instruments collect and store with the amount you can send to Earth each day. |
| Communications | See how fast your antenna sends data and how many hours of ground contact you have each day. |
| Time | Compare the planned mission length with how long the probe can maintain its orbit. |
| Delta-v | See how much each vehicle can change its speed, in m/s, compared with what its maneuvers require. |
