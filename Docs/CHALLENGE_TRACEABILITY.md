# ORBITAL — Challenge Traceability

This file records where the NASA data, images and sounds used in the project come from, and what
was done to them.

## NASA images (L_Mission scene)

Credit: NASA

| Asset (Unreal) | Source | Downloaded file | Processing |
|---|---|---|---|
| `/Game/Textures/NASA/T_Earth_BlueMarble_2K` | Blue Marble Next Generation w/ Topography and Bathymetry, December 2004. NASA Visible Earth: https://visibleearth.nasa.gov/images/73909/december-blue-marble-next-generation-w-topography-and-bathymetry | https://assets.science.nasa.gov/content/dam/science/esd/eo/images/bmng/bmng-topography-bathymetry/december/world.topo.bathy.200412.3x5400x2700.jpg | 5400x2700 JPEG, reduced to a 2048x1024 PNG |
| `/Game/Textures/NASA/T_Moon_LROC_4K` | CGI Moon Kit, NASA Scientific Visualization Studio (SVS 4720): https://svs.gsfc.nasa.gov/4720 | https://svs.gsfc.nasa.gov/vis/a000000/a004700/a004720/lroc_color_poles_4k.tif | 4096x2048 TIFF, converted to PNG at the same resolution |
| `/Game/Textures/NASA/T_StarMap_2020_4K` | Deep Star Maps 2020, NASA Scientific Visualization Studio (SVS 4851): https://svs.gsfc.nasa.gov/4851 | https://svs.gsfc.nasa.gov/vis/a000000/a004800/a004851/starmap_2020_4k.exr | 4096x2048 EXR, imported unchanged (HDR compression) |
| `/Game/Materials/FX/T_Clouds` (M_Clouds) | Blue Marble: Clouds, NASA Earth Observatory / NASA Visible Earth: https://visibleearth.nasa.gov/images/57747/blue-marble-clouds | https://assets.science.nasa.gov/content/dam/science/esd/eo/content-feature/bluemarble/images/cloud_combined_2048.jpg | 2048x1024 JPEG, imported unchanged (greyscale) |

Detailed credits:

- Earth: Credit: NASA Earth Observatory / NASA Visible Earth (Blue Marble Next Generation, Reto Stöckli).
- Moon: Credit: NASA's Scientific Visualization Studio (CGI Moon Kit, LRO/LROC data).
- Clouds: Credit: NASA Goddard Space Flight Center, Reto Stöckli (Blue Marble clouds, MODIS data).
- Stars: Credit: NASA/Goddard Space Flight Center Scientific Visualization Studio. Gaia DR2: ESA/Gaia/DPAC.

Download date: 2026-11-14. The content of the textures was not changed; only size and file format.

## NASA data in the engineering model

Earth and Moon constants (radius, gravitational parameter, distance) come from the NSSDC planetary
fact sheets: https://nssdc.gsfc.nasa.gov/planetary/factsheet/ . They are used in the delta-v,
eclipse and orbit calculations of `OrbitalEngineering_v2.1.h`. The model's simplifications are listed
in `Docs/ENGINEERING_ASSUMPTIONS.md`.

## Scene display scale (L_Mission)

These values are for display only and are not used in the engineering calculations. The
engineering values come from the v2.1 documents.

| Item | Value |
|---|---|
| Scale | 1:100 (1 Unreal unit = 1 cm) |
| Earth radius | 63.71 km (real 6371 km) |
| Moon radius | 17.374 km (real 1737.4 km), 0.273 of Earth |
| Earth-Moon distance | About 410 km, compressed for display (the true value at 1:100 would be 3844 km) |
| Sky Atmosphere | Earth only, height 1 km, density coefficients multiplied by 60 |
| Ascent altitudes and cloud layer | In true proportion to the scene Earth (cloud layer at 10 km real altitude) |

## Sounds

The launch sounds are NASA recordings. Credit: NASA. Source page: NASA Historical Sounds,
https://www.nasa.gov/historical-sounds/ . Terms of use: NASA Media Usage Guidelines,
https://www.nasa.gov/multimedia/guidelines/index.html . The separation and click sounds are not
NASA recordings; they were produced for the project with code (noise and sine-wave synthesis, no
AI audio generation) and the team releases them as CC0.

If the NASA sound assets have not been imported, the game falls back to the synthesised engine
sound (`OrbitalFlight.cpp`, `SetBurning`).

| Asset (Unreal) | Source | Licence |
|---|---|---|
| `/Game/Audio/NASA/S_NASA_Ignition` | NASA Historical Sounds, "SLS Test Fire": https://www.nasa.gov/wp-content/uploads/2015/01/663784main_SLS_Audio_D.mp3 . The spoken part was cut out; the first 10 seconds of the remainder were kept, with a fade-out, as WAV. Plays once when the first stage ignites. | Credit: NASA, NASA Media Usage Guidelines |
| `/Game/Audio/NASA/S_NASA_EngineLoop` | NASA Historical Sounds, "Atlas V: Launch": https://www.nasa.gov/wp-content/uploads/2015/01/590329main_ringtone_SDO_launchNats.mp3 . A steady 14-second section was cut, made louder, converted to WAV and its ends blended so it loops. Plays while an engine burns. | Credit: NASA, NASA Media Usage Guidelines |
| `/Game/Audio/S_EngineLoop` | Made by the team, synthesis (low-pass noise and 55/82 Hz tones), 3.6 s loop. Fallback when the NASA engine sound is missing. | CC0 |
| `/Game/Audio/S_Separation` | Made by the team, synthesis (falling tone and noise burst), 0.7 s | CC0 |
| `/Game/Audio/S_Click` | Made by the team, synthesis (short 1200 Hz tone), 0.07 s | CC0 |
