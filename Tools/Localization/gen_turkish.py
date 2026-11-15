#!/usr/bin/env python3
"""Writes Source/ORBITAL_SpaceApps/Game/OrbitalTextTurkish.inl from the table below.

Run from the repository root:  python3 Tools/Localization/gen_turkish.py
Keys are the English lines exactly as the game builds them. Where a line carries numbers, write
any example numbers (use different ones when there are several); see Game/OrbitalText.h.
Non-ASCII letters are written as \\u escapes so the source file stays plain ASCII."""
import json, re, pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
T = {}

def add(en, tr):
    assert en not in T or T[en] == tr, en
    T[en] = tr

# ---- sockets, categories, builder
SOCKETS = {
    "Stage 1 tank": "1. kademe tankı", "Stage 1 engine": "1. kademe motoru",
    "Stage 2 tank": "2. kademe tankı", "Stage 2 engine": "2. kademe motoru",
    "Fairing": "Kaporta", "Spacecraft bus": "Uzay aracı gövdesi", "Propellant tank": "Yakıt tankı",
    "Spacecraft engine": "Uzay aracı motoru", "Attitude control": "Yönelim kontrolü",
    "Power source": "Güç kaynağı", "Battery": "Batarya", "Antenna": "Anten", "Computer": "Bilgisayar",
    "Instrument deck": "Alet yuvası",
}
for en, tr in SOCKETS.items():
    add(en, tr)
for en in ("Propellant tank", "Attitude control", "Power source", "Battery", "Instrument deck"):
    add(en + " 7", SOCKETS[en] + " 7")

for en, tr in {
    "Structure": "Yapı", "Propulsion": "İtki", "Power": "Güç", "Control": "Kontrol", "Comms": "İletişim",
    "Instruments": "Aletler", "Stage 1": "1. Kademe", "Stage 2": "2. Kademe",
    "MUST FIX": "DÜZELTİLMELİ", "RISK": "RİSK", "NOTE": "NOT",
    "Ice": "Buz", "Terrain": "Arazi", "Thermal": "Isı", "Imaging": "Görüntüleme",
    "1 station": "1 istasyon", "3 stations": "3 istasyon", "Easy": "Kolay", "Hard": "Zor",
    "Builder": "Tasarım Atölyesi", "Mission": "Görev", "Budget: 1 M credits": "Bütçe: 1 M kredi",
    "lunar polar survey": "Ay kutup araştırması", "Menu": "Menü",
    "1  SPACECRAFT": "1  UZAY ARACI", "2  ROCKET": "2  ROKET", "3  MISSION PLAN": "3  GÖREV PLANI",
    "SPACECRAFT": "UZAY ARACI", "ROCKET WITH SPACECRAFT INSIDE": "ROKET (UZAY ARACI İÇİNDE)",
    "needs its parent part": "önce ana parçayı takın", "+ add": "+ ekle",
    "Click a row above to choose a part for it.": "Parça seçmek için yukarıdan bir satıra tıklayın.",
    "PARTS FOR": "PARÇALAR", "... more parts": "... başka parçalar var",
    "7 M credits": "7 M kredi", "FITTED": "TAKILI", "Remove this part": "Bu parçayı çıkar",
    "Orbit": "Yörünge", "Years": "Yıl", "Ground": "Yer istasyonu", "Primary goal": "Ana hedef",
    "Difficulty": "Zorluk", "Propellant  111 / 222 kg": "Yakıt  111 / 222 kg",
    "Stage 1 engines  x3": "1. kademe motorları  x3",
    "DASHBOARD": "GÖSTERGE PANELİ", "fit the required parts first": "önce zorunlu parçaları takın",
    "Predicted score 1.5": "Tahmini puan 1.5", "Science 2.5": "Bilim 2.5", "Cost 3 / 4 M": "Maliyet 3 / 4 M",
    "DESIGN REVIEW": "TASARIM İNCELEMESİ",
    "No open items. Ready to launch.": "Açık madde yok. Fırlatmaya hazır.",
    "... more items": "... başka maddeler var", "How to fix": "Çözüm", "Still missing": "Eksik parçalar",
    "Small & cheap": "Küçük ve ucuz", "Full survey": "Tam araştırma", "Weak battery": "Zayıf batarya",
    "Small arrays": "Küçük paneller",
    "READY-MADE DESIGNS  (click one to load it)": "HAZIR TASARIMLAR  (yüklemek için birine tıklayın)",
    "LAUNCH  (first fix the MUST FIX items)": "FIRLAT  (önce DÜZELTİLMELİ maddeleri giderin)",
    "LAUNCH": "FIRLAT",
    "DESIGN REVIEW: this design can launch, but it carries risks.":
        "TASARIM İNCELEMESİ: bu tasarım fırlatılabilir, ancak riskler taşıyor.",
    "Back to assembly": "Tasarıma dön", "Launch anyway": "Yine de fırlat",
    # refusals
    "This part is not in the catalog.": "Bu parça katalogda yok.",
    "This part does not fit that socket.": "Bu parça o yuvaya uymuyor.",
    "This part is for crewed missions only.": "Bu parça yalnızca insanlı görevler içindir.",
    "Attach the part this socket sits on first.": "Önce bu yuvanın bağlı olduğu parçayı takın.",
    "No free socket of this type. Remove or replace one.": "Bu türde boş yuva yok. Birini çıkarın veya değiştirin.",
    "No free instrument deck on this bus.": "Bu gövdede boş alet yuvası yok.",
    "This engine cannot be clustered that way.": "Bu motor o sayıda kullanılamaz.",
    "That mission plan value is not allowed.": "Bu görev planı değeri kullanılamaz.",
    "That socket does not exist.": "Böyle bir yuva yok.",
    "Launch refused by the design review.": "Fırlatma tasarım incelemesinde reddedildi.",
    "Launch refused: fix the red items first.": "Fırlatma reddedildi: önce kırmızı maddeleri giderin.",
    "The design can only be changed on the assembly screen.": "Tasarım yalnızca tasarım ekranında değiştirilebilir.",
    # menu
    "Design a mission to the Moon's poles. Build it, launch it, and learn why it worked.":
        "Ay'ın kutuplarına bir görev tasarlayın. Aracı kurun, fırlatın ve neden başarılı olduğunu öğrenin.",
    "Missions": "Görevler", "Settings": "Ayarlar", "Credits": "Emeği Geçenler", "Quit": "Çıkış",
    "Team Three-Body Problem.": "Three-Body Problem. Takımı",
    "CHOOSE A MISSION": "GÖREV SEÇİN",
    "Same Moon, same parts. Each mission has a different goal and budget.":
        "Aynı Ay, aynı parçalar. Her görevin hedefi ve bütçesi farklıdır.",
    "Start": "Başla", "Back": "Geri",
    "Ice Prospector": "Buz Arayıcı", "Landing Site Scout": "İniş Alanı Kâşifi",
    "Full Polar Survey": "Tam Kutup Araştırması",
    "Search the lunar poles for water ice on a tight budget. Carry only what the search needs.":
        "Ay kutuplarında dar bir bütçeyle su buzu arayın. Yalnızca aramanın gerektirdiğini taşıyın.",
    "Image candidate landing sites near the pole. Cameras make a lot of data, so plan how to send it home.":
        "Kutup yakınındaki aday iniş alanlarını görüntüleyin. Kameralar çok veri üretir; bu veriyi Dünya'ya nasıl göndereceğinizi planlayın.",
    "Fly all five instruments with a larger budget: ice, terrain, temperature, landing sites and shadowed craters.":
        "Daha büyük bir bütçeyle beş aletin hepsini uçurun: buz, arazi, sıcaklık, iniş alanları ve gölgeli kraterler.",
    "NASA DATA AND RESOURCES": "NASA VERİLERİ VE KAYNAKLARI",
    "NSSDC planetary fact sheets: Earth and Moon constants for the engineering model.":
        "NSSDC gezegen bilgi sayfaları: mühendislik modeli için Dünya ve Ay sabitleri.",
    "Blue Marble Next Generation: Earth texture. NASA Earth Observatory / NASA Visible Earth.":
        "Blue Marble Next Generation: Dünya dokusu. NASA Earth Observatory / NASA Visible Earth.",
    "CGI Moon Kit: Moon texture from LRO/LROC data. NASA's Scientific Visualization Studio.":
        "CGI Moon Kit: LRO/LROC verilerinden Ay dokusu. NASA Scientific Visualization Studio.",
    "Deep Star Maps 2020: star background. NASA/Goddard Space Flight Center Scientific Visualization Studio. Gaia DR2: ESA/Gaia/DPAC.":
        "Deep Star Maps 2020: yıldız arka planı. NASA/Goddard Space Flight Center Scientific Visualization Studio. Gaia DR2: ESA/Gaia/DPAC.",
    "Credit: NASA": "Kaynak: NASA",
    "TEAM THREE-BODY PROBLEM.": "THREE-BODY PROBLEM. TAKIMI",
    "Muhammet: Unreal project setup, scene, lighting, flight view and submission materials.":
        "Muhammet: Unreal proje kurulumu, sahne, ışıklandırma, uçuş görünümü ve teslim materyalleri.",
    "Issa: team leader, game design, vehicle assembly, game screens and mission timeline.":
        "Issa: takım lideri, oyun tasarımı, araç montajı, oyun ekranları ve görev zaman çizelgesi.",
    "Ram: engineering integration, JSON loader, dashboard, session flow and documentation.":
        "Ram: mühendislik entegrasyonu, JSON yükleyici, gösterge paneli, oturum akışı ve dokümantasyon.",
    "NOTES": "NOTLAR",
    "Launch sounds: NASA Historical Sounds (Atlas V: Launch, SLS Test Fire). Other sounds were generated for this project.":
        "Fırlatma sesleri: NASA Historical Sounds (Atlas V: Launch, SLS Test Fire). Diğer sesler bu proje için üretildi.",
    "The team used Claude, ChatGPT/Codex and Gemini to help write code, tests, documentation and UI text, and directed and reviewed that work.":
        "Takım; kod, test, dokümantasyon ve arayüz metinlerinin yazımında Claude, ChatGPT/Codex ve Gemini'den yardım aldı, bu çalışmayı yönlendirdi ve gözden geçirdi.",
    "ORBITAL uses a simplified educational model. Its estimates are unsuitable for real flight planning.":
        "ORBITAL basitleştirilmiş bir eğitim modeli kullanır. Tahminleri gerçek uçuş planlaması için uygun değildir.",
    "CREDITS": "EMEĞİ GEÇENLER", "SETTINGS": "AYARLAR", "Sound volume:  80%": "Ses düzeyi:  %80",
    "Graphics quality": "Grafik kalitesi", "Language": "Dil",
    "Low": "Düşük", "Medium": "Orta", "High": "Yüksek", "Epic": "En yüksek",
    "Lower the quality if the flight view stutters on your computer.":
        "Uçuş görünümü bilgisayarınızda takılıyorsa kaliteyi düşürün.",
    # dashboard
    "Mass": "Kütle", "Budget": "Bütçe", "Data": "Veri", "Communications": "İletişim", "Time": "Süre",
    "Bus wet mass": "Gövde dolu kütlesi", "Daily downlink capacity": "Günlük veri indirme kapasitesi",
    "Effective antenna rate": "Etkin anten hızı", "Fairing payload mass": "Kaporta yük kütlesi",
    "Launcher delta-v": "Fırlatıcı delta-v", "Onboard storage": "Araç içi depolama",
    "Power generation": "Güç üretimi", "Spacecraft delta-v": "Uzay aracı delta-v",
    "Supported mission lifetime": "Desteklenen görev ömrü", "Total mission cost": "Toplam görev maliyeti",
    "Usable eclipse battery": "Tutulmada kullanılabilir batarya",
    "1 / 2 M credits": "1 / 2 M kredi", "1 / 2 years": "1 / 2 yıl", "1 / 2 GB/day": "1 / 2 GB/gün",
    # review titles (built from the result codes)
    "Unknown part": "Bilinmeyen parça", "Missing required part": "Zorunlu parça eksik",
    "Socket limit": "Yuva sınırı aşıldı", "Prop over capacity": "Yakıt kapasiteyi aşıyor",
    "Bus overload": "Gövde aşırı yüklü", "Bus instrument slots": "Gövdede alet yuvası yetersiz",
    "Computer instrument limit": "Bilgisayarın alet sınırı aşıldı", "Engine count": "Motor sayısı hatalı",
    "Thrust-to-weight liftoff": "Kalkışta itki/ağırlık oranı yetersiz",
    "Thrust-to-weight stage2": "Üst kademede itki/ağırlık oranı yetersiz",
    "Fairing mass": "Kaporta kütle sınırı aşıldı", "Fairing size": "Kaporta boyutu uymuyor",
    "Spacecraft delta-v insertion": "Yörüngeye giriş için delta-v yetersiz",
    "Spacecraft delta-v lifetime": "Görev ömrü için delta-v yetersiz",
    "Battery eclipse": "Batarya tutulmayı karşılamıyor",
    "Needs fine pointing": "Hassas yönelim gerekli", "Storage": "Depolama yetersiz",
    "No instrument": "Bilimsel alet yok", "Primary instrument missing": "Ana hedefin aleti eksik",
    "Data bottleneck": "Veri darboğazı",
    # power and telemetry panels
    "POWER IN THE MOON'S SHADOW": "AY'IN GÖLGESİNDE GÜÇ",
    "Battery / needed in shadow": "Batarya / gölgede gereken",
    "Power made / needed": "Üretilen güç / gereken",
    "Power use, time in shadow": "Güç tüketimi, gölgede geçen süre",
    "1 W, 2 of 3 min": "1 W, 2 / 3 dk",
    "Solar panels produce no power in the Moon's shadow; an RTG keeps working. The battery must cover the rest.":
        "Güneş panelleri Ay'ın gölgesinde güç üretmez; RTG çalışmaya devam eder. Geri kalanını batarya karşılamalıdır.",
    "Sunlight here is 1/1560 of Earth's, so solar arrays give almost nothing. An RTG works anywhere.":
        "Buradaki güneş ışığı Dünya'dakinin 1/1560'ı kadardır; güneş panelleri neredeyse hiç güç üretmez. RTG her yerde çalışır.",
    "Mission phase": "Görev aşaması", "Rocket fuel left": "Kalan roket yakıtı",
    "Spacecraft fuel left": "Kalan uzay aracı yakıtı", "Battery energy left": "Kalan batarya enerjisi",
    "Data sent to Earth": "Dünya'ya gönderilen veri",
    # orbit map
    "ORBIT MAP  (not to scale)": "YÖRÜNGE HARİTASI  (ölçekli değil)", "Earth": "Dünya", "Moon": "Ay",
    "Climbing to orbit": "Yörüngeye tırmanış", "Parking orbit": "Park yörüngesi",
    "On the way to the Moon": "Ay yolunda", "Lunar orbit": "Ay yörüngesi",
    "Flew past the Moon": "Ay'ın yanından geçti", "Contact lost": "Bağlantı koptu",
    # attitude indicator
    "ATTITUDE": "YÖNELİM", "Nose above horizon: 67 deg": "Burnun ufka göre açısı: 67 derece",
    "Altitude": "İrtifa", "Vertical climb": "Dikey tırmanış", "Gravity turn": "Yerçekimi dönüşü",
    "Nearly level, gaining speed": "Neredeyse yatay, hız kazanıyor",
    # Pluto power test
    "Pluto Probe: Power Test": "Plüton Sondası: Güç Testi",
    "Pluto gets 1/1560 of Earth's sunlight, so solar arrays are useless there. Prove the probe's power system in lunar orbit with sunlight dimmed to Pluto's level. You will need an RTG.":
        "Plüton, Dünya'ya ulaşan güneş ışığının 1/1560'ını alır; orada güneş panelleri işe yaramaz. Sondanın güç sistemini, güneş ışığı Plüton düzeyine kısılmış halde Ay yörüngesinde kanıtlayın. Bir RTG gerekecek.",
    "Sunlight: 1/1560 of Earth's": "Güneş ışığı: Dünya'dakinin 1/1560'ı",
    "RTG probe  (works here)": "RTG'li sonda  (burada çalışır)",
    "Solar probe  (fails here)": "Güneş panelli sonda  (burada çalışmaz)",
    "ORBIT MAP  (power test at the Moon)": "YÖRÜNGE HARİTASI  (Ay'da güç testi)",
    "The real trip to Pluto takes about 9.5 years.": "Plüton'a gerçek yolculuk yaklaşık 9.5 yıl sürer.",
    # flight view and report
    "Right mouse: orbit": "Sağ fare: çevir", "Wheel: zoom": "Tekerlek: yakınlaştır", "Skip  >>": "Geç  >>",
    "Phase reached": "Ulaşılan aşama", "Score: 1.5": "Puan: 1.5", "Science: 2.5": "Bilim: 2.5",
    "Cause": "Neden", "Fix": "Çözüm", "Redesign": "Yeniden tasarla",
    "T+ 1.5 days": "T+ 1.5 gün", "T+ 1.5 years": "T+ 1.5 yıl",
    "Mission success. Score 1.5.": "Görev başarılı. Puan 1.5.",
    "Mission not successful. Score 1.5.": "Görev başarısız. Puan 1.5.",
    "Launch refused at design review.": "Fırlatma tasarım incelemesinde reddedildi.",
    "Liftoff! The rocket has left the pad with a thrust-to-weight ratio of 1.5.":
        "Kalkış! Roket, 1.5 itki/ağırlık oranıyla rampadan ayrıldı.",
    "The first stage has run out of fuel and safely separated from the rocket.":
        "Birinci kademenin yakıtı bitti ve roketten güvenle ayrıldı.",
    "The protective fairing has opened, exposing the spacecraft to space.":
        "Koruyucu kaporta açıldı; uzay aracı artık uzay boşluğunda.",
    "The vehicle has successfully reached a temporary parking orbit around Earth.":
        "Araç, Dünya çevresindeki geçici park yörüngesine başarıyla ulaştı.",
    "The final engine burn is complete, and the spacecraft is now coasting toward the Moon.":
        "Son motor ateşlemesi tamamlandı; uzay aracı şimdi Ay'a doğru süzülüyor.",
    "The spacecraft used a small amount of fuel to perfectly adjust its aim toward the Moon.":
        "Uzay aracı, Ay'a yönelimini hassas biçimde düzeltmek için az miktarda yakıt kullandı.",
    "The spacecraft is firing its engine for 1.5 minutes to slow down and enter lunar orbit.":
        "Uzay aracı yavaşlayıp Ay yörüngesine girmek için motorunu 1.5 dakika ateşliyor.",
    "Success! The spacecraft has captured into a stable orbit around the Moon (LPO_100).":
        "Başarılı! Uzay aracı Ay çevresinde kararlı bir yörüngeye girdi (LPO_100).",
    "Success! The spacecraft has captured into a stable orbit around the Moon (ELL_30x216).":
        "Başarılı! Uzay aracı Ay çevresinde kararlı bir yörüngeye girdi (ELL_30x216).",
    "The spacecraft could not slow down enough and has flown past the Moon.":
        "Uzay aracı yeterince yavaşlayamadı ve Ay'ın yanından geçip gitti.",
    "The spacecraft is passing through the Moon's shadow and is relying entirely on battery power.":
        "Uzay aracı Ay'ın gölgesinden geçiyor ve yalnızca batarya gücüyle çalışıyor.",
    "The spacecraft lost power in the Moon's shadow and contact was lost.":
        "Uzay aracı Ay'ın gölgesinde güçsüz kaldı ve bağlantı koptu.",
    "Another year complete! The spacecraft has sent 1.5 GB of science data back to Earth.":
        "Bir yıl daha tamamlandı! Uzay aracı Dünya'ya 1.5 GB bilimsel veri gönderdi.",
    "After 1.5 years, the spacecraft has used all its fuel and can no longer maintain its orbit.":
        "1.5 yılın ardından uzay aracının yakıtı tükendi; artık yörüngesini koruyamıyor.",
    "The spacecraft reached the end of its planned 1.5-year mission and returned 2.5 GB of data.":
        "Uzay aracı planlanan 1.5 yıllık görevinin sonuna ulaştı ve 2.5 GB veri gönderdi.",
    "The mission has ended; review the final engineering report for your results.":
        "Görev sona erdi; sonuçlarınız için mühendislik raporunu inceleyin.",
}.items():
    add(en, tr)

# ---- part names (PART_CATALOG.json)
NAMES = {
    "bus_small": "Sonda Gövdesi S", "bus_medium": "Sonda Gövdesi M", "command_module": "Komuta Modülü",
    "crew_capsule": "Mürettebat Kapsülü", "life_support": "Yaşam Destek Modülü",
    "sc_tank_s": "Yakıt Tankı S", "sc_tank_m": "Yakıt Tankı M", "sc_tank_l": "Yakıt Tankı L",
    "sc_engine_mono": "Tek Yakıtlı Motor", "sc_engine_biprop": "Çift Yakıtlı Motor",
    "rcs": "RCS İticileri", "reaction_wheels": "Tepki Tekeri Seti",
    "rtg": "Radyoizotop Jeneratörü (RTG)", "solar_s": "Güneş Paneli S", "solar_l": "Güneş Paneli L", "battery_s": "Batarya S", "battery_l": "Batarya L",
    "antenna_lga": "Düşük Kazançlı Anten", "antenna_mga": "Orta Kazançlı Anten",
    "antenna_hga": "Yüksek Kazançlı Anten", "obc_basic": "Araç Bilgisayarı (Temel)",
    "obc_advanced": "Araç Bilgisayarı (Gelişmiş)", "inst_neutron": "Nötron Spektrometresi",
    "inst_altimeter": "Lazer Altimetre", "inst_radiometer": "Termal Radyometre",
    "inst_camera": "Yüksek Çözünürlüklü Kamera", "inst_radar": "Görüntüleme Radarı",
    "s1_tank_200": "Birinci Kademe Tankı 200", "s1_tank_285": "Birinci Kademe Tankı 285",
    "s1_engine_kl190": "KL-190 Kalkış Motoru", "s1_engine_kl380": "KL-380 Kalkış Motoru",
    "s2_tank_7": "İkinci Kademe Tankı 7", "s2_tank_21": "İkinci Kademe Tankı 21",
    "s2_engine_kl35": "KL-35 Üst Kademe Motoru", "s2_engine_hl10": "HL-10 Üst Kademe Motoru",
    "fairing_s": "Yük Kaportası S", "fairing_l": "Yük Kaportası L", "sep_system": "Ayırma Sistemi",
    "les": "Fırlatma Kaçış Sistemi",
}
TIPS = {
    "bus_small": "Hafif ve düşük maliyetli insansız sondalar için kompakt bir gövde.",
    "bus_medium": "Beş alete kadar yer sunan daha büyük bir insansız uzay aracı gövdesi.",
    "command_module": "Gelecekteki insanlı görevler için ayrılmış bir kontrol modülü.",
    "crew_capsule": "Gelecekteki insanlı görevler için ayrılmış küçük bir kapsül.",
    "life_support": "Gelecekteki insanlı görevler için hava, su ve sıcaklık kontrolü sağlar.",
    "sc_tank_s": "150 kg'a kadar yakıt alan küçük bir uzay aracı tankı.",
    "sc_tank_m": "400 kg'a kadar yakıt alan orta boy bir uzay aracı tankı.",
    "sc_tank_l": "900 kg'a kadar yakıt alan büyük bir uzay aracı tankı.",
    "sc_engine_mono": "Yakıtı çift yakıtlı motora göre daha verimsiz kullanan, daha basit bir uzay aracı motoru.",
    "sc_engine_biprop": "Tek yakıtlı motordan daha pahalı ama daha verimli bir uzay aracı motoru.",
    "rcs": "Uzay aracını döndüren küçük iticiler; her uzay aracında zorunludur.",
    "reaction_wheels": "Bazı aletlerin ve yüksek kazançlı antenin ihtiyaç duyduğu hassas yönelimi sağlar.",
    "solar_s": "Güneş ışığında elektrik sağlayan küçük bir güneş paneli.",
    "solar_l": "Güneş ışığında daha fazla elektrik sağlayan daha büyük bir güneş paneli.",
    "battery_s": "Gölgede uzay aracı sistemlerini çalışır tutan küçük bir batarya.",
    "battery_l": "Gölgede geçen süre için daha fazla enerji depolayan daha büyük bir batarya.",
    "rtg": "Bozunan plütonyumun ısısından, güneşte de gölgede de yaklaşık 200 W üretir. Ağır ve çok pahalıdır.",
    "antenna_lga": "Veriyi yavaş gönderir, uzay aracı hangi yöne bakarsa baksın çalışır.",
    "antenna_mga": "Kaba yönelimle orta hızda veri gönderir.",
    "antenna_hga": "Veriyi hızlı gönderir ama hassas yönelim için Tepki Tekeri Seti gerektirir.",
    "obc_basic": "İki alete kadar çalıştırır ve 16 GB veri depolar.",
    "obc_advanced": "Beş alete kadar çalıştırır ve 256 GB veri depolar.",
    "inst_neutron": "Olası su buzuna işaret eden, Ay toprağındaki hidrojeni algılar.",
    "inst_altimeter": "Gölgeli kraterlerin içi dahil, arazi yüksekliğini lazer darbeleriyle ölçer.",
    "inst_radiometer": "Soğuk tuzakları bulmak için yüzey sıcaklıklarını ölçer.",
    "inst_camera": "Olası iniş alanlarının ayrıntılı görüntülerini çeker ve çok veri üretir.",
    "inst_radar": "Sürekli gölgede kalan kraterleri görüntüler ve çok elektrik harcar.",
    "s1_tank_200": "200 ton yakıt alan bir birinci kademe tankı.",
    "s1_tank_285": "285 ton yakıt alan daha büyük bir birinci kademe tankı.",
    "s1_engine_kl190": "Bir ila üç adet takılabilen, düşük maliyetli bir kalkış motoru.",
    "s1_engine_kl380": "Tek veya çift takılabilen, yüksek itkili bir kalkış motoru.",
    "s2_tank_7": "7 ton yakıt alan bir üst kademe tankı.",
    "s2_tank_21": "20.8 ton yakıt alan daha büyük bir üst kademe tankı.",
    "s2_engine_kl35": "Orta düzeyde yakıt verimliliğine sahip, düşük maliyetli bir üst kademe motoru.",
    "s2_engine_hl10": "Daha pahalı ve düşük itkili ama verimli bir üst kademe motoru.",
    "fairing_s": "Fırlatma sırasında Sonda Gövdesi S'yi korur ve 600 kg'a kadar yük kabul eder.",
    "fairing_l": "Fırlatma sırasında daha büyük yükleri korur ve 4,000 kg'a kadar yük kabul eder.",
    "sep_system": "Biten roket kademesini bırakır veya uzay aracını fırlatıcıdan ayırır.",
    "les": "Gelecekteki insanlı görevlerde mürettebat kapsülünü arızalı roketten uzaklaştırır.",
}
CODES = {
    "E_UNKNOWN_PART": ("Tasarım bilinmeyen bir parça veya geçersiz bir sayı içeriyor.",
                       "Geçerli bir tasarım yükleyin veya hatalı girdiyi değiştirin."),
    "E_MISSING_REQUIRED_PART": ("Aracın zorunlu bir parçası eksik.", "Uyarıda adı geçen parçayı ekleyin."),
    "E_SOCKET_LIMIT": ("Bu türden, mevcut yuvaların alabileceğinden fazla parça var.", "Bu türden fazla olan parçayı çıkarın."),
    "E_PROP_OVER_CAPACITY": ("Tankların alabileceğinden fazla yakıt yüklemeye çalışıyorsunuz.",
                             "Daha az yakıt yükleyin veya tank kapasitesini artırın."),
    "E_BUS_OVERLOAD": ("Yakıt dolu uzay aracı, gövdesinin taşıyabileceğinden ağır.", "Daha büyük bir gövde kullanın veya kütleyi azaltın."),
    "E_BUS_INSTRUMENT_SLOTS": ("Uzay aracında gövdenin taşıyabileceğinden fazla alet var.", "Bir aleti çıkarın veya daha büyük bir gövde kullanın."),
    "E_COMPUTER_INSTRUMENT_LIMIT": ("Bilgisayar bu kadar aleti çalıştıramaz.", "Bir aleti çıkarın veya daha fazla alet çalıştırabilen bir bilgisayar seçin."),
    "E_ENGINE_COUNT": ("Kalkış motoru sayısı bu motor için izin verilen aralığın dışında.", "Motor sayısını o motor için gösterilen aralığa getirin."),
    "E_TWR_LIFTOFF": ("Motorların itkisi roketin kalkış kütlesi için yetersiz.", "Kalkış itkisini artırın veya kalkış kütlesini azaltın."),
    "E_TWR_STAGE2": ("Üst kademenin itkisi taşıdığı kütle için yetersiz.", "Daha yüksek itkili bir üst kademe motoru seçin veya taşıdığı kütleyi azaltın."),
    "E_LV_DELTA_V": ("Roket, park yörüngesine ulaşıp bu yükü Ay'a göndermeye yetecek kadar hız değiştiremiyor.",
                     "Daha büyük kademeler veya daha verimli motorlar seçin ya da uzay aracını hafifletin."),
    "E_FAIRING_MASS": ("Uzay aracı bu kaportanın yük sınırından ağır.", "Daha fazla kütle taşıyabilen bir kaporta seçin veya uzay aracını hafifletin."),
    "E_FAIRING_SIZE": ("Bu kaporta seçilen uzay aracı gövdesine uymuyor.", "Gövdenize uyan bir kaporta seçin."),
    "E_BUDGET": ("Görevin maliyeti seçilen bütçeyi aşıyor.", "Pahalı parçaları veya yer hizmetlerini daha ucuz seçeneklerle değiştirin."),
    "R_SC_DELTA_V_INSERTION": ("Sonda, Ay yörüngesine girmeye yetecek kadar hız değiştiremiyor; bu yüzden Ay'ın yanından geçip gidecek.",
                               "Tank kapasitesi içinde daha fazla yakıt yükleyin, daha verimli bir motor kullanın veya kütleyi azaltın."),
    "R_SC_DELTA_V_LIFETIME": ("Sonda yörüngeye girebiliyor, ancak planlanan görev bitmeden yörünge düzeltmeleri için yakıtı tükenecek.",
                              "Tank kapasitesi içinde daha fazla yakıt yükleyin veya daha az düzeltme gerektiren bir yörünge seçin."),
    "R_POWER_GENERATION": ("Güç kaynakları hem ekipmanı çalıştırıp hem bataryaları şarj edemiyor; görev gölgeden geçerken başarısız olacak.",
                           "Güneş paneli alanını artırın (güneş ışığının zayıf olduğu yerde RTG ekleyin) veya alet çıkararak elektrik yüklerini azaltın."),
    "R_BATTERY_ECLIPSE": ("Bataryalar uzay aracını ilk tutulma boyunca çalışır tutamıyor.", "Kullanılabilir batarya kapasitesini artırın veya elektrik yüklerini azaltın."),
    "R_NEEDS_FINE_POINTING": ("Bir alet veya anten hassas yönelim gerektiriyor; bu olmadan kullanılamaz.", "Bir Tepki Tekeri Seti ekleyin."),
    "R_STORAGE": ("Depolama bir sonraki yer bağlantısından önce doluyor; bu yüzden bir kısım veri kaybolur.",
                  "Daha fazla depolaması olan bir bilgisayar veya daha kısa aralıklı bir yer ağı seçin."),
    "R_NO_INSTRUMENT": ("Uzay aracında bilimsel alet yok; bu yüzden bilimsel veri toplayamayacak.", "Hedefinizi destekleyen bir alet ekleyin."),
    "R_PRIMARY_INSTRUMENT_MISSING": ("Ana hedefin çalışan bir aleti yok; bu yüzden hedef tamamlanamaz.",
                                     "Uygun bir alet takın veya o aletin gerektirdiği yönelim kontrolünü ekleyin."),
    "W_DATA_BOTTLENECK": ("Depolama veya veri indirme sınırları, toplanan verinin bir kısmının Dünya'ya ulaşmasını engelliyor.",
                          "Hangi sınırın belirleyici olduğuna bakın; ardından depolamayı, veri indirme hızını veya yer bağlantısını artırın."),
}

catalog = json.loads((ROOT / "Docs/CLAUDE_RESEARCH/PART_CATALOG.json").read_text(encoding="utf-8"))
for part in catalog["parts"]:
    add(part["name"], NAMES[part["id"]])
copy = (ROOT / "Docs/CLAUDE_RESEARCH/ORBITAL_UI_COPY_v2.1.md").read_text(encoding="utf-8")
for line in copy.splitlines():
    cells = [c.strip() for c in line.strip().strip("|").split("|")]
    if len(cells) == 4 and cells[0] in CODES:
        add(cells[2], CODES[cells[0]][0])
        add(cells[3], CODES[cells[0]][1])
    elif len(cells) == 2 and cells[0] in TIPS:
        add(cells[1], TIPS[cells[0]])

def lit(s):
    out = []
    for ch in s:
        if ch in '\\"':
            out.append("\\" + ch)
        elif ord(ch) < 128:
            out.append(ch)
        else:
            out.append("\\u%04X" % ord(ch))
    # A hex-looking letter after an escape would not extend \\uXXXX, so no splitting is needed.
    return 'L"' + "".join(out) + '"'

lines = ["// Generated by Tools/Localization/gen_turkish.py. Do not edit by hand.\n"]
for en, tr in T.items():
    lines.append("{%s, %s},\n" % (lit(en), lit(tr)))
(ROOT / "Source/ORBITAL_SpaceApps/Game/OrbitalTextTurkish.inl").write_text("".join(lines), encoding="ascii", newline="\n")
print(len(T), "entries")
