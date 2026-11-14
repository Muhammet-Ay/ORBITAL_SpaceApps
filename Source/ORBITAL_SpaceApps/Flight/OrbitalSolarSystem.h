// ORBITAL solar system and map-view maths: the Sun, eight planets and the Moon, plus the
// orbit shapes the map view draws (parking orbit, lunar transfer, lunar orbit).
// Plain C++ (no Unreal types). Tested by Tools/StandaloneTests/solarsystem_tests.cpp.
//
// Sources
//   Distance from the Sun, orbital period, eccentricity and diameter: NASA NSSDC Planetary
//   Fact Sheet (metric), https://nssdc.gsfc.nasa.gov/planetary/factsheet/ , read 2026-11-14.
//   Sun diameter: NSSDC Sun Fact Sheet (volumetric mean radius 695,700 km).
// Display assumptions (not NASA values, do not quote them as data)
//   - orbits are drawn in one plane (inclinations ignored)
//   - startDeg, where each planet sits when the map opens, and perihelionDeg, which way each
//     ellipse points, are rounded illustrative angles
//   - map radii may be compressed with MapRadius() so the inner planets stay visible
#pragma once

#include <cmath>
#include <vector>

namespace Orbital
{
struct SolarBody
{
    const char* name;
    double distanceMkm;  // mean distance from the Sun (semi-major axis), million km
    double periodDays;   // orbital period
    double eccentricity;
    double diameterKm;
    double startDeg, perihelionDeg; // display assumptions, see the header comment
    unsigned char r, g, b;          // map colour
};

inline const std::vector<SolarBody>& Planets()
{
    static const std::vector<SolarBody> bodies = {
        {"Mercury", 57.9, 88.0, 0.206, 4879.0, 252.0, 77.0, 170, 160, 150},
        {"Venus", 108.2, 224.7, 0.007, 12104.0, 182.0, 132.0, 230, 200, 140},
        {"Earth", 149.6, 365.2, 0.017, 12756.0, 100.0, 103.0, 90, 150, 255},
        {"Mars", 228.0, 687.0, 0.094, 6792.0, 355.0, 336.0, 230, 110, 80},
        {"Jupiter", 778.5, 4331.0, 0.049, 142984.0, 34.0, 15.0, 220, 180, 140},
        {"Saturn", 1432.0, 10747.0, 0.052, 120536.0, 50.0, 92.0, 230, 210, 160},
        {"Uranus", 2867.0, 30589.0, 0.047, 51118.0, 313.0, 171.0, 160, 220, 230},
        {"Neptune", 4515.0, 59800.0, 0.010, 49528.0, 305.0, 45.0, 90, 120, 240},
    };
    return bodies;
}

inline constexpr double SunDiameterKm = 1391400.0;
inline constexpr double MoonPeriodDays = 27.3, MoonEccentricity = 0.055, MoonDiameterKm = 3475.0;
inline constexpr double MapPi = 3.14159265358979323846;

struct MapPoint
{
    double x = 0, y = 0;
};

// Solves Kepler's equation M = E - e sin E for the eccentric anomaly E (radians).
inline double EccentricAnomaly(double meanAnomaly, double eccentricity)
{
    const double twoPi = 2.0 * MapPi;
    double m = std::fmod(meanAnomaly, twoPi);
    if (m < 0)
        m += twoPi;
    double e = eccentricity < 0.8 ? m : MapPi;
    for (int i = 0; i < 30; ++i)
    {
        const double step = (e - eccentricity * std::sin(e) - m) / (1.0 - eccentricity * std::cos(e));
        e -= step;
        if (std::fabs(step) < 1e-12)
            break;
    }
    return e;
}

// Point on an ellipse with the focus (the body being orbited) at the origin.
// periapsisRad is the direction of the closest point; eccentricAnomaly runs 0 at periapsis to pi at apoapsis.
inline MapPoint EllipsePoint(double semiMajorAxis, double eccentricity, double periapsisRad, double eccentricAnomaly)
{
    const double px = semiMajorAxis * (std::cos(eccentricAnomaly) - eccentricity);
    const double py = semiMajorAxis * std::sqrt(1.0 - eccentricity * eccentricity) * std::sin(eccentricAnomaly);
    const double c = std::cos(periapsisRad), s = std::sin(periapsisRad);
    return {px * c - py * s, px * s + py * c};
}

// The same ellipse given the way mission designers quote it: closest and farthest distance from the centre.
inline MapPoint OrbitPoint(double periapsisRadius, double apoapsisRadius, double periapsisRad, double eccentricAnomaly)
{
    const double axis = (periapsisRadius + apoapsisRadius) / 2.0;
    const double ecc = axis > 0 ? (apoapsisRadius - periapsisRadius) / (apoapsisRadius + periapsisRadius) : 0.0;
    return EllipsePoint(axis, ecc, periapsisRad, eccentricAnomaly);
}

// Where a body is on its ellipse after elapsedFraction of one period, starting from periapsis.
// Bodies move fastest near periapsis (Kepler's second law), so this is not a constant-speed sweep.
inline MapPoint OrbitPointAtTime(double periapsisRadius, double apoapsisRadius, double periapsisRad, double elapsedFraction)
{
    const double ecc = (apoapsisRadius - periapsisRadius) / (apoapsisRadius + periapsisRadius);
    return OrbitPoint(periapsisRadius, apoapsisRadius, periapsisRad, EccentricAnomaly(2.0 * MapPi * elapsedFraction, ecc));
}

// Planet position around the Sun, million km, daysFromStart after the map opened.
inline MapPoint PlanetPosition(const SolarBody& body, double daysFromStart)
{
    const double perihelion = body.perihelionDeg * MapPi / 180.0;
    const double meanAnomaly = (body.startDeg - body.perihelionDeg) * MapPi / 180.0
        + 2.0 * MapPi * daysFromStart / body.periodDays;
    return EllipsePoint(body.distanceMkm, body.eccentricity, perihelion, EccentricAnomaly(meanAnomaly, body.eccentricity));
}

// Orbital period from Kepler's third law, seconds. mu in m3/s2, semi-major axis in metres.
inline double OrbitPeriodS(double mu, double semiMajorAxisM)
{
    return 2.0 * MapPi * std::sqrt(semiMajorAxisM * semiMajorAxisM * semiMajorAxisM / mu);
}

// Map radius compression. power = 1 keeps true scale; 0.5 (square root) keeps Mercury visible
// on the same screen as Neptune. The map must say "not to scale" when power is not 1.
inline double MapRadius(double trueRadius, double outerRadius, double power)
{
    return outerRadius > 0 ? std::pow(trueRadius / outerRadius, power) : 0.0;
}
} // namespace Orbital
