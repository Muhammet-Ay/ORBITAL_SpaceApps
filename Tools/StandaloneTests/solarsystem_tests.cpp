// Tests for Flight/OrbitalSolarSystem.h (planet table, Kepler solver, map orbit shapes).
// Build: g++ -std=c++17 -O2 -I../../Source/ORBITAL_SpaceApps solarsystem_tests.cpp -o solarsystem_tests && ./solarsystem_tests
#include "Flight/OrbitalSolarSystem.h"
#include <cstdio>
#include <cstring>
using namespace Orbital;
namespace {
int g_checks = 0, g_failures = 0;
void Check(bool ok, const char* what, int line)
{ ++g_checks; if (!ok) { ++g_failures; std::printf("  FAIL line %d: %s\n", line, what); } }
#define CHECK(e) Check((e), #e, __LINE__)
bool Near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
double Length(const MapPoint& p) { return std::sqrt(p.x * p.x + p.y * p.y); }
} // namespace

int main()
{
    const auto& planets = Planets();
    CHECK(planets.size() == 8);
    CHECK(std::strcmp(planets[2].name, "Earth") == 0 && std::strcmp(planets[7].name, "Neptune") == 0);

    const double sunMuKm3 = 1.32712e11; // NSSDC Sun Fact Sheet GM, km3/s2
    for (std::size_t i = 0; i < planets.size(); ++i)
    {
        const SolarBody& b = planets[i];
        if (i > 0)
            CHECK(b.distanceMkm > planets[i - 1].distanceMkm && b.periodDays > planets[i - 1].periodDays);
        // The table agrees with Kepler's third law to 1.5 % (the fact sheet rounds; Neptune is 1.3 % off).
        const double axisKm = b.distanceMkm * 1e6;
        const double days = 2.0 * MapPi * std::sqrt(axisKm * axisKm * axisKm / sunMuKm3) / 86400.0;
        CHECK(Near(days / b.periodDays, 1.0, 0.015));
        // Every position stays between perihelion and aphelion, and a full period returns to the start.
        const double peri = b.distanceMkm * (1.0 - b.eccentricity), apo = b.distanceMkm * (1.0 + b.eccentricity);
        for (int step = 0; step <= 40; ++step)
        {
            const double r = Length(PlanetPosition(b, b.periodDays * step / 40.0));
            CHECK(r >= peri - 1e-6 && r <= apo + 1e-6);
        }
        const MapPoint start = PlanetPosition(b, 0.0), again = PlanetPosition(b, b.periodDays);
        CHECK(Near(start.x, again.x, 1e-6) && Near(start.y, again.y, 1e-6));
    }

    // Kepler's equation: the solution satisfies M = E - e sin E, including high eccentricity.
    for (double e : {0.0, 0.017, 0.206, 0.6, 0.967})
    {
        for (double m = 0.05; m < 6.28; m += 0.37)
        {
            const double E = EccentricAnomaly(m, e);
            CHECK(Near(E - e * std::sin(E), m, 1e-9));
        }
    }
    CHECK(Near(EccentricAnomaly(-0.5, 0.1), EccentricAnomaly(2.0 * MapPi - 0.5, 0.1), 1e-9));

    // Lunar transfer ellipse from the catalog constants: 200 km parking orbit to the Moon's distance.
    const double earthMu = 3.986004418e14, earthRadius = 6371000.0, moonAxis = 384400000.0;
    const double peri = earthRadius + 200000.0;
    CHECK(Near(Length(OrbitPoint(peri, moonAxis, 0.0, 0.0)), peri, 1.0));
    CHECK(Near(Length(OrbitPoint(peri, moonAxis, 0.0, MapPi)), moonAxis, 1.0));
    const MapPoint apoapsis = OrbitPoint(peri, moonAxis, MapPi / 2.0, MapPi);
    CHECK(Near(apoapsis.x, 0.0, 1.0) && Near(apoapsis.y, -moonAxis, 1.0)); // opposite the periapsis direction
    // Half the transfer period is the cruise time: about 5 days (the timeline's default).
    const double cruiseDays = OrbitPeriodS(earthMu, (peri + moonAxis) / 2.0) / 2.0 / 86400.0;
    CHECK(cruiseDays > 4.8 && cruiseDays < 5.1);
    // Kepler's second law: a quarter of the way through the cruise the craft is already past halfway out.
    const double quarter = Length(OrbitPointAtTime(peri, moonAxis, 0.0, 0.125));
    CHECK(quarter > 0.5 * moonAxis && quarter < moonAxis);
    CHECK(Near(Length(OrbitPointAtTime(peri, moonAxis, 0.0, 0.5)), moonAxis, 1.0));
    // 200 km circular orbit: about 88 minutes. Moon: about 27.3 days.
    CHECK(Near(OrbitPeriodS(earthMu, peri) / 60.0, 88.4, 0.5));
    CHECK(Near(OrbitPeriodS(earthMu, moonAxis) / 86400.0, MoonPeriodDays, 0.3));

    // Map compression keeps the order and the ends.
    CHECK(Near(MapRadius(4515.0, 4515.0, 0.5), 1.0, 1e-12));
    CHECK(MapRadius(57.9, 4515.0, 0.5) > 0.1 && MapRadius(57.9, 4515.0, 1.0) < 0.02);
    CHECK(MapRadius(149.6, 4515.0, 0.5) < MapRadius(228.0, 4515.0, 0.5));

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
