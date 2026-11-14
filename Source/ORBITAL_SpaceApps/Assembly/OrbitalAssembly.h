#pragma once

// ORBITAL v2.1 assembly model (plain C++, no Unreal dependency).
// Implements MISSION_SYSTEMS_SPEC section 8 on top of OrbitalEngineering_v2.1.h:
// fixed sockets from mission.socket_tree, limits from mission.socket_limits,
// cascade removal, and the two snapshot JSON objects that ReadOrbitalSpacecraft /
// ReadOrbitalLauncher expect (reference_builds field names and value types).
//
// Lifetime: the Catalog and Mission passed in must outlive the Assembly.

#include "OrbitalEngineering_v2.1.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace Orbital
{
// Values that the engine's Mission struct does not carry. Fill from the catalog:
// socketTree <- mission.socket_tree, defaultOrbit <- mission.default_orbit.
struct AssemblyRules
{
    std::unordered_map<Id, Ids> socketTree;
    Id defaultOrbit;
    Id defaultGround = "single_station";
    // The catalog lists probe-core as a child of fairing. Steps 2-4 of the spec can be done
    // in any order, so by default the spacecraft is its own root and the fairing/payload link
    // is judged by the engine (E_FAIRING_MASS, E_FAIRING_SIZE). Set true to follow the tree
    // literally (removing the fairing then removes the whole spacecraft).
    bool probeRequiresFairing = false;
};

// Assembly-only codes. Engine codes are reused where they mean the same thing.
inline constexpr const char* A_OK = "";
inline constexpr const char* A_SLOT_MISMATCH = "A_SLOT_MISMATCH";   // part does not fit this socket
inline constexpr const char* A_MODE_MISMATCH = "A_MODE_MISMATCH";   // part not allowed in this mission mode
inline constexpr const char* A_PARENT_MISSING = "A_PARENT_MISSING"; // parent socket is empty
inline constexpr const char* A_INVALID_PLAN = "A_INVALID_PLAN";     // orbit, years, ground or propellant
inline constexpr const char* A_UNKNOWN_SOCKET = "A_UNKNOWN_SOCKET";

struct AttachResult
{
    bool ok = false;
    Id code;    // empty on success; otherwise E_UNKNOWN_PART, E_SOCKET_LIMIT, E_BUS_INSTRUMENT_SLOTS or A_*
    Id socket;  // socket type, equal to the part's slot
    int index = -1;
    Id socketName; // stable display/transform name, e.g. "deck_c" or "solar_2"
};

// Immutable launch snapshot (spec: "Snapshot and replay").
struct AssemblySnapshot
{
    SpacecraftDesign spacecraft;
    LauncherDesign launcher;
    std::string spacecraftJson;
    std::string launcherJson;
};

namespace AssemblyJson
{
inline void AppendString(std::string& out, const Id& value)
{
    out += '"';
    for (const char c : value)
    {
        if (c == '"' || c == '\\')
        {
            out += '\\';
            out += c;
        }
        else if (static_cast<unsigned char>(c) < 0x20)
        {
            char buffer[8];
            std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
            out += buffer;
        }
        else
        {
            out += c;
        }
    }
    out += '"';
}

inline void AppendNullable(std::string& out, const Id& value)
{
    if (value.empty())
        out += "null";
    else
        AppendString(out, value);
}

inline void AppendList(std::string& out, const Ids& values)
{
    out += '[';
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (i > 0)
            out += ',';
        AppendString(out, values[i]);
    }
    out += ']';
}

// Integers are written without a decimal point (130, not 130.0); others round-trip exactly.
inline void AppendNumber(std::string& out, double value)
{
    char buffer[40];
    if (std::isfinite(value) && std::fabs(value) < 9.0e15 && std::floor(value) == value)
    {
        std::snprintf(buffer, sizeof(buffer), "%.0f", value);
    }
    else
    {
        for (int precision = 15; precision <= 17; ++precision)
        {
            std::snprintf(buffer, sizeof(buffer), "%.*g", precision, value);
            if (std::strtod(buffer, nullptr) == value)
                break;
        }
    }
    out += buffer;
}
} // namespace AssemblyJson

// Field order follows reference_builds.SC_* in PART_CATALOG.json.
inline std::string ToJson(const SpacecraftDesign& d)
{
    using namespace AssemblyJson;
    std::string out = "{\"bus\":";
    AppendNullable(out, d.bus);
    out += ",\"tanks\":";
    AppendList(out, d.tanks);
    out += ",\"engine\":";
    AppendNullable(out, d.engine);
    out += ",\"prop_kg\":";
    AppendNumber(out, d.propKg);
    out += ",\"adcs\":";
    AppendList(out, d.adcs);
    out += ",\"solar\":";
    AppendList(out, d.solar);
    out += ",\"battery\":";
    AppendList(out, d.battery);
    out += ",\"antenna\":";
    AppendNullable(out, d.antenna);
    out += ",\"computer\":";
    AppendNullable(out, d.computer);
    out += ",\"instruments\":";
    AppendList(out, d.instruments);
    out += ",\"orbit\":";
    AppendString(out, d.orbit);
    out += ",\"years\":";
    AppendNumber(out, d.years);
    out += ",\"ground\":";
    AppendString(out, d.ground);
    out += '}';
    return out;
}

// Field order follows reference_builds.LV_* in PART_CATALOG.json.
inline std::string ToJson(const LauncherDesign& d)
{
    using namespace AssemblyJson;
    std::string out = "{\"s1_tank\":";
    AppendNullable(out, d.firstTank);
    out += ",\"s1_engine\":";
    AppendNullable(out, d.firstEngine);
    out += ",\"s1_count\":";
    out += std::to_string(d.firstEngineCount);
    out += ",\"s2_tank\":";
    AppendNullable(out, d.secondTank);
    out += ",\"s2_engine\":";
    AppendNullable(out, d.secondEngine);
    out += ",\"fairing\":";
    AppendNullable(out, d.fairing);
    out += '}';
    return out;
}

class Assembly
{
public:
    Assembly(const Catalog& catalog, const Mission& mission, AssemblyRules rules)
        : catalog_(&catalog), mission_(&mission), rules_(std::move(rules))
    {
        for (const auto& [parent, children] : rules_.socketTree)
            for (const Id& child : children)
                parentOf_[child] = parent;
        orbit_ = rules_.defaultOrbit;
        if (mission.ground.count(rules_.defaultGround) == 0 && !mission.ground.empty())
            rules_.defaultGround = SmallestKey(mission.ground);
        ground_ = rules_.defaultGround;
        if (!mission.plannedYearsOptions.empty())
            years_ = mission.plannedYearsOptions.front();
    }

    // Place a part in the first free socket of its slot type.
    AttachResult Attach(const Id& partId)
    {
        AttachResult result;
        const Part* part = Find(partId);
        if (!part)
            return Fail(result, "E_UNKNOWN_PART");
        result.socket = part->slot;
        if (const char* code = CheckPlacement(*part, part->slot))
            return Fail(result, code);
        const int capacity = Capacity(part->slot);
        auto& used = sockets_[part->slot];
        for (int i = 0; i < capacity; ++i)
        {
            if (used.count(i) == 0)
                return Place(result, part->slot, i, partId);
        }
        return Fail(result, part->slot == "instrument" ? "E_BUS_INSTRUMENT_SLOTS" : "E_SOCKET_LIMIT");
    }

    // Place a part in a named socket index. An occupied socket is replaced; per the
    // contract, the old part's descendants are removed with it.
    AttachResult AttachAt(const Id& socket, int index, const Id& partId)
    {
        AttachResult result;
        result.socket = socket;
        result.index = index;
        if (!IsSocket(socket))
            return Fail(result, A_UNKNOWN_SOCKET);
        const Part* part = Find(partId);
        if (!part)
            return Fail(result, "E_UNKNOWN_PART");
        if (part->slot != socket)
            return Fail(result, A_SLOT_MISMATCH);
        if (const char* code = CheckPlacement(*part, socket))
            return Fail(result, code);
        // A replacement bus sets its own instrument capacity, so check the index against it.
        const int capacity = socket == "probe-core" ? 1 : Capacity(socket);
        if (index < 0 || index >= capacity)
            return Fail(result, socket == "instrument" ? "E_BUS_INSTRUMENT_SLOTS" : "E_SOCKET_LIMIT");
        Remove(socket, index);
        return Place(result, socket, index, partId);
    }

    // Dry run of AttachAt: empty when this part could go in this socket (replacing any occupant).
    Id CanAttachAt(const Id& socket, int index, const Id& partId) const
    {
        if (!IsSocket(socket))
            return A_UNKNOWN_SOCKET;
        const Part* part = Find(partId);
        if (!part)
            return "E_UNKNOWN_PART";
        if (part->slot != socket)
            return A_SLOT_MISMATCH;
        if (const char* code = CheckPlacement(*part, socket))
            return code;
        if (index < 0 || index >= Capacity(socket))
            return socket == "instrument" ? "E_BUS_INSTRUMENT_SLOTS" : "E_SOCKET_LIMIT";
        return A_OK;
    }

    // Socket count of this type when its parent is present (for drawing locked sockets).
    int Limit(const Id& socket) const
    {
        if (!IsSocket(socket))
            return 0;
        if (socket == "instrument")
        {
            const Part* bus = Find(Get("probe-core", 0));
            return bus ? bus->maxInstruments : 0;
        }
        const auto limit = mission_->socketLimits.find(socket);
        return limit != mission_->socketLimits.end() ? limit->second : 1;
    }

    // Remove the part in a socket and everything attached under it. Returns parts removed.
    int Remove(const Id& socket, int index)
    {
        const auto type = sockets_.find(socket);
        if (type == sockets_.end())
            return 0;
        const auto slot = type->second.find(index);
        if (slot == type->second.end())
            return 0;
        type->second.erase(slot);
        int removed = 1;
        // Only single sockets have children in the catalog tree, so emptying one clears its subtree.
        const auto children = rules_.socketTree.find(socket);
        if (children != rules_.socketTree.end() && Get(socket, 0).empty())
        {
            for (const Id& child : children->second)
            {
                if (child == "probe-core" && !rules_.probeRequiresFairing)
                    continue;
                removed += RemoveAll(child);
            }
        }
        return removed;
    }

    // Remove the first socket holding this part ID (convenience for UI drag-off).
    int RemovePart(const Id& partId)
    {
        for (auto& [socket, used] : sockets_)
            for (const auto& [index, id] : used)
                if (id == partId)
                    return Remove(socket, index);
        return 0;
    }

    Id SetStage1EngineCount(int count)
    {
        const Part* engine = Find(Get("stage1-engine", 0));
        if (count < 1 || (engine && count > engine->maxCount))
            return "E_ENGINE_COUNT";
        engineCount_ = count;
        return A_OK;
    }

    Id SetOrbit(const Id& orbit)
    {
        if (mission_->orbits.count(orbit) == 0)
            return A_INVALID_PLAN;
        orbit_ = orbit;
        return A_OK;
    }

    Id SetYears(double years)
    {
        const auto& options = mission_->plannedYearsOptions;
        if (std::find(options.begin(), options.end(), years) == options.end())
            return A_INVALID_PLAN;
        years_ = years;
        return A_OK;
    }

    Id SetGround(const Id& ground)
    {
        if (mission_->ground.count(ground) == 0)
            return A_INVALID_PLAN;
        ground_ = ground;
        return A_OK;
    }

    // Propellant above tank capacity is allowed here; the engine reports E_PROP_OVER_CAPACITY.
    Id SetPropellant(double kg)
    {
        if (!std::isfinite(kg) || kg < 0)
            return A_INVALID_PLAN;
        propKg_ = kg;
        return A_OK;
    }

    // Part ID in a socket, or empty.
    Id Get(const Id& socket, int index) const
    {
        const auto type = sockets_.find(socket);
        if (type == sockets_.end())
            return {};
        const auto slot = type->second.find(index);
        return slot == type->second.end() ? Id{} : slot->second;
    }

    // Number of sockets of this type that currently exist (0 when the parent is missing).
    int Capacity(const Id& socket) const
    {
        if (!IsSocket(socket) || !ParentPresent(socket))
            return 0;
        if (socket == "instrument")
        {
            const Part* bus = Find(Get("probe-core", 0));
            return bus ? bus->maxInstruments : 0;
        }
        const auto limit = mission_->socketLimits.find(socket);
        return limit != mission_->socketLimits.end() ? limit->second : 1;
    }

    // Stable socket name for transforms and UI: instruments use the bus deck sockets.
    static Id SocketName(const Id& socket, int index)
    {
        if (socket == "instrument")
            return "deck_" + std::string(1, static_cast<char>('a' + index));
        return socket + "_" + std::to_string(index + 1);
    }

    SpacecraftDesign ToSpacecraftDesign() const
    {
        SpacecraftDesign d;
        d.bus = Get("probe-core", 0);
        d.engine = Get("probe-engine", 0);
        d.antenna = Get("antenna", 0);
        d.computer = Get("avionics", 0);
        d.tanks = List("probe-tank");
        d.adcs = List("adcs");
        d.solar = List("solar");
        d.battery = List("battery");
        d.instruments = List("instrument");
        d.orbit = orbit_;
        d.ground = ground_;
        d.years = years_;
        d.propKg = propKg_;
        return d;
    }

    LauncherDesign ToLauncherDesign() const
    {
        LauncherDesign d;
        d.firstTank = Get("stage1-tank", 0);
        d.firstEngine = Get("stage1-engine", 0);
        d.firstEngineCount = engineCount_;
        d.secondTank = Get("stage2-tank", 0);
        d.secondEngine = Get("stage2-engine", 0);
        d.fairing = Get("fairing", 0);
        return d;
    }

    AssemblySnapshot Snapshot() const
    {
        AssemblySnapshot s;
        s.spacecraft = ToSpacecraftDesign();
        s.launcher = ToLauncherDesign();
        s.spacecraftJson = ToJson(s.spacecraft);
        s.launcherJson = ToJson(s.launcher);
        return s;
    }

private:
    const Catalog* catalog_;
    const Mission* mission_;
    AssemblyRules rules_;
    std::unordered_map<Id, Id> parentOf_;
    std::map<Id, std::map<int, Id>> sockets_; // ordered: export lists follow socket index
    int engineCount_ = 1;
    Id orbit_, ground_;
    double years_ = 1, propKg_ = 0;

    template <typename Map>
    static Id SmallestKey(const Map& map)
    {
        Id best = map.begin()->first;
        for (const auto& entry : map)
            best = std::min(best, entry.first);
        return best;
    }

    const Part* Find(const Id& id) const
    {
        if (id.empty())
            return nullptr;
        const auto part = catalog_->find(id);
        return part == catalog_->end() ? nullptr : &part->second;
    }

    bool IsSocket(const Id& socket) const
    {
        return parentOf_.count(socket) > 0;
    }

    bool ParentPresent(const Id& socket) const
    {
        const auto parent = parentOf_.find(socket);
        if (parent == parentOf_.end())
            return false;
        if (parent->second == "launch-mount")
            return true;
        if (socket == "probe-core" && !rules_.probeRequiresFairing)
            return true;
        return !Get(parent->second, 0).empty();
    }

    const char* CheckPlacement(const Part& part, const Id& socket) const
    {
        if (!IsSocket(socket))
            return A_SLOT_MISMATCH;
        const auto& modes = part.missionModes;
        if (std::find(modes.begin(), modes.end(), mission_->mode) == modes.end())
            return A_MODE_MISMATCH;
        if (!ParentPresent(socket))
            return A_PARENT_MISSING;
        return nullptr;
    }

    AttachResult& Fail(AttachResult& result, const char* code) const
    {
        result.ok = false;
        result.code = code;
        return result;
    }

    AttachResult& Place(AttachResult& result, const Id& socket, int index, const Id& partId)
    {
        sockets_[socket][index] = partId;
        if (socket == "stage1-engine")
        {
            const Part* engine = Find(partId);
            if (engine && engineCount_ > engine->maxCount)
                engineCount_ = std::max(1, engine->maxCount);
        }
        result.ok = true;
        result.code.clear();
        result.socket = socket;
        result.index = index;
        result.socketName = SocketName(socket, index);
        return result;
    }

    int RemoveAll(const Id& socket)
    {
        const auto type = sockets_.find(socket);
        if (type == sockets_.end() || type->second.empty())
            return 0;
        int removed = 0;
        while (!type->second.empty())
            removed += Remove(socket, type->second.begin()->first);
        return removed;
    }

    Ids List(const Id& socket) const
    {
        Ids ids;
        const auto type = sockets_.find(socket);
        if (type != sockets_.end())
            for (const auto& [index, id] : type->second)
                ids.push_back(id);
        return ids;
    }
};
} // namespace Orbital
