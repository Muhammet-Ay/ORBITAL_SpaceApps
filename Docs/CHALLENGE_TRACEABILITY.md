# ORBITAL — Challenge Traceability

Bu dosya, projede kullanılan NASA verilerinin ve görsellerinin kaynağını izler.

## NASA görsel varlıkları (L_Mission sahnesi)

Credit: NASA

| Varlık (Unreal) | Kaynak | İndirilen dosya | İşlem |
|---|---|---|---|
| `/Game/Textures/NASA/T_Earth_BlueMarble_2K` | Blue Marble Next Generation w/ Topography and Bathymetry, Aralık 2004. NASA Visible Earth: https://visibleearth.nasa.gov/images/73909/december-blue-marble-next-generation-w-topography-and-bathymetry | https://assets.science.nasa.gov/content/dam/science/esd/eo/images/bmng/bmng-topography-bathymetry/december/world.topo.bathy.200412.3x5400x2700.jpg | 5400x2700 JPEG, 2048x1024 PNG'ye küçültüldü |
| `/Game/Textures/NASA/T_Moon_LROC_2K` | CGI Moon Kit, NASA Scientific Visualization Studio (SVS 4720): https://svs.gsfc.nasa.gov/4720 | https://svs.gsfc.nasa.gov/vis/a000000/a004700/a004720/lroc_color_poles_2k.tif | 2048x1024 TIFF, aynı çözünürlükte PNG'ye çevrildi |

Ayrıntılı atıf:

- Dünya: Credit: NASA Earth Observatory / NASA Visible Earth (Blue Marble Next Generation, Reto Stöckli).
- Ay: Credit: NASA's Scientific Visualization Studio (CGI Moon Kit, LRO/LROC verisi).

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
