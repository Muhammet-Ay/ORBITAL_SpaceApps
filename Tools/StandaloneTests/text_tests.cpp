// Plain C++ tests for Game/OrbitalText.h (the English -> Turkish line translator).
#include "Game/OrbitalText.h"
#include <cstdio>

static int checks = 0, failures = 0;
static void Same(const std::wstring& in, const std::wstring& want)
{
    ++checks;
    const std::wstring got = Orbital::TurkishText().Translate(in);
    if (got != want)
    {
        ++failures;
        std::printf("FAIL: %ls\n  got  %ls\n  want %ls\n", in.c_str(), got.c_str(), want.c_str());
    }
}

int main()
{
    Same(L"Remove this part", L"Bu parçayı çıkar");
    Same(L"Budget: 360 M credits", L"Bütçe: 360 M kredi");
    Same(L"Mission: Landing Site Scout   Budget: 360 M credits",
        L"Görev: İniş Alanı Kâşifi   Bütçe: 360 M kredi");
    Same(L"Propellant tank 2", L"Yakıt tankı 2");
    Same(L"Stage 2 engine", L"2. kademe motoru");
    Same(L"PARTS FOR: PROPELLANT TANK 1", L"PARÇALAR: YAKIT TANKI 1");
    Same(L"PARTS FOR: STAGE 1 TANK", L"PARÇALAR: 1. KADEME TANKI");
    Same(L"70 kg    7 M credits    FITTED", L"70 kg    7 M kredi    TAKILI");
    Same(L"Propellant  35 / 400 kg", L"Yakıt  35 / 400 kg");
    Same(L"Stage 1 engines  x2", L"1. kademe motorları  x2");
    Same(L"Sound volume:  65%", L"Ses düzeyi:  %65");
    Same(L"Fairing payload mass  376 / 600 kg  (+37%)", L"Kaporta yük kütlesi  376 / 600 kg  (+37%)");
    Same(L"Total mission cost  181 / 360 M credits  (+50%)", L"Toplam görev maliyeti  181 / 360 M kredi  (+50%)");
    Same(L"Supported mission lifetime  0 / 1 years  (-100%)", L"Desteklenen görev ömrü  0 / 1 yıl  (-100%)");
    Same(L"Predicted score 80.8    Science 100.0    Cost 181 / 360 M",
        L"Tahmini puan 80.8    Bilim 100.0    Maliyet 181 / 360 M");
    Same(L"MUST FIX:  Missing required part", L"DÜZELTİLMELİ:  Zorunlu parça eksik");
    Same(L"Still missing: Spacecraft bus, Antenna.", L"Eksik parçalar: Uzay aracı gövdesi, Anten.");
    Same(L"Graphics quality:", L"Grafik kalitesi:");
    Same(L"ORBITAL  |  Builder", L"ORBITAL  |  Tasarım Atölyesi");
    Same(L"Add a Reaction Wheel Set.", L"Bir Tepki Tekeri Seti ekleyin.");
    Same(L"Cause: Add a Reaction Wheel Set.", L"Neden: Bir Tepki Tekeri Seti ekleyin.");
    Same(L"Score: 80.8     Science: 100.0", L"Puan: 80.8     Bilim: 100.0");
    Same(L"Mission success. Score 80.8.", L"Görev başarılı. Puan 80.8.");
    Same(L"Liftoff! The rocket has left the pad with a thrust-to-weight ratio of 1.43.",
        L"Kalkış! Roket, 1.43 itki/ağırlık oranıyla rampadan ayrıldı.");
    Same(L"Success! The spacecraft has captured into a stable orbit around the Moon (LPO_50).",
        L"Başarılı! Uzay aracı Ay çevresinde kararlı bir yörüngeye girdi (LPO_50).");
    Same(L"The spacecraft reached the end of its planned 2-year mission and returned 310.5 GB of data.",
        L"Uzay aracı planlanan 2 yıllık görevinin sonuna ulaştı ve 310.5 GB veri gönderdi.");
    Same(L"Right mouse: orbit   Wheel: zoom", L"Sağ fare: çevir   Tekerlek: yakınlaştır");
    // No entry: unchanged. Already Turkish: unchanged.
    Same(L"T+ 02:37", L"T+ 02:37");
    Same(L"ASCENT | FAIRING_RELEASE", L"ASCENT | FAIRING_RELEASE");
    Same(L"English", L"English");
    Same(L"Bu parçayı çıkar", L"Bu parçayı çıkar");
    Same(L"    FITTED", L"    TAKILI");
    Same(L"", L"");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
