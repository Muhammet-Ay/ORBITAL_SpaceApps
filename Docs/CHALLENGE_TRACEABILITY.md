# ORBITAL — Challenge Traceability

Bu dosya, projede kullanılan NASA verilerinin ve görsellerinin kaynağını izler.

## NASA görsel varlıkları (L_Mission sahnesi)

Credit: NASA

| Varlık (Unreal) | Kaynak | İndirilen dosya | İşlem |
|---|---|---|---|
| `/Game/Textures/NASA/T_Earth_BlueMarble_2K` | Blue Marble Next Generation w/ Topography and Bathymetry, Aralık 2004. NASA Visible Earth: https://visibleearth.nasa.gov/images/73909/december-blue-marble-next-generation-w-topography-and-bathymetry | https://assets.science.nasa.gov/content/dam/science/esd/eo/images/bmng/bmng-topography-bathymetry/december/world.topo.bathy.200412.3x5400x2700.jpg | 5400x2700 JPEG, 2048x1024 PNG'ye küçültüldü |
| `/Game/Textures/NASA/T_Moon_LROC_4K` | CGI Moon Kit, NASA Scientific Visualization Studio (SVS 4720): https://svs.gsfc.nasa.gov/4720 | https://svs.gsfc.nasa.gov/vis/a000000/a004700/a004720/lroc_color_poles_4k.tif | 4096x2048 TIFF, aynı çözünürlükte PNG'ye çevrildi |
| `/Game/Textures/NASA/T_StarMap_2020_4K` | Deep Star Maps 2020, NASA Scientific Visualization Studio (SVS 4851): https://svs.gsfc.nasa.gov/4851 | https://svs.gsfc.nasa.gov/vis/a000000/a004800/a004851/starmap_2020_4k.exr | 4096x2048 EXR, değiştirilmeden içe aktarıldı (HDR sıkıştırma) |

Ayrıntılı atıf:

- Dünya: Credit: NASA Earth Observatory / NASA Visible Earth (Blue Marble Next Generation, Reto Stöckli).
- Ay: Credit: NASA's Scientific Visualization Studio (CGI Moon Kit, LRO/LROC verisi).
- Yıldızlar: Credit: NASA/Goddard Space Flight Center Scientific Visualization Studio. Gaia DR2: ESA/Gaia/DPAC.

İndirme tarihi: 2026-11-14. Dokularda içerik değişikliği yapılmadı, yalnızca boyut ve dosya biçimi değişti.

## Sahne gösterim ölçeği (L_Mission)

Bunlar gösterim amaçlıdır, mühendislik hesabında kullanılmaz. Mühendislik değerleri v2.1 belgelerinden gelir.

| Öğe | Değer |
|---|---|
| Ölçek | 1:100 (1 Unreal birimi = 1 cm) |
| Dünya yarıçapı | 63,71 km (gerçek 6371 km) |
| Ay yarıçapı | 17,374 km (gerçek 1737,4 km), Dünya'nın 0,273 katı |
| Dünya-Ay mesafesi | Yaklaşık 410 km, gösterim için sıkıştırıldı (1:100 ölçekte gerçek değer 3844 km olurdu) |
| Sky Atmosphere | Yalnızca Dünya, yükseklik 1 km, yoğunluk katsayıları 60 ile çarpıldı |

## Sesler

Fırlatma sesleri NASA kaydıdır. Credit: NASA. Kaynak sayfa: NASA Historical Sounds, https://www.nasa.gov/historical-sounds/ . Kullanım koşulları: NASA Media Usage Guidelines, https://www.nasa.gov/multimedia/guidelines/index.html . Ayrılma ve tıklama sesleri NASA kaydı değildir; proje için kod ile üretildi (gürültü ve sinüs dalgası sentezi) ve ekip tarafından CC0 olarak kullanılır.

NASA ses varlıkları içe aktarılmamışsa oyun sentez motor sesine geri döner (`OrbitalFlight.cpp`, `SetBurning`).

| Varlık (Unreal) | Kaynak | Lisans |
|---|---|---|
| `/Game/Audio/NASA/S_NASA_Ignition` | NASA Historical Sounds, "Atlas V: Launch": https://www.nasa.gov/wp-content/uploads/2015/01/590329main_ringtone_SDO_launchNats.mp3 . Sabit bölümü kesildi, sesi yükseltildi, WAV'a çevrildi, döngü olarak işaretlendi. Birinci kademe ateşlemesinde bir kez çalar. | Credit: NASA, NASA Media Usage Guidelines |
| `/Game/Audio/NASA/S_NASA_EngineLoop` | NASA Historical Sounds, "SLS Test Fire": https://www.nasa.gov/wp-content/uploads/2015/01/663784main_SLS_Audio_D.mp3 . Konuşma bölümü kesildi, ilk 10 saniyesi WAV olarak alındı. Motor yanarken çalar. | Credit: NASA, NASA Media Usage Guidelines |
| `/Game/Audio/S_EngineLoop` | Ekip üretimi, sentez (alçak geçirgen gürültü ve 55/82 Hz ton), 3,6 sn döngü. NASA motor sesi yoksa yedek olarak kullanılır. | CC0 |
| `/Game/Audio/S_Separation` | Ekip üretimi, sentez (alçalan ton ve gürültü patlaması), 0,7 sn | CC0 |
| `/Game/Audio/S_Click` | Ekip üretimi, sentez (1200 Hz kısa ton), 0,07 sn | CC0 |
