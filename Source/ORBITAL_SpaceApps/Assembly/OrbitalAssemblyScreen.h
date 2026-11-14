#pragma once

// ORBITAL v2.1 assembly screen binding (plain C++, no Unreal dependency).
// The UI talks only to AssemblyScreen: list sockets, Attach / Replace / Remove, read the
// rejection reason, and receive a fresh Snapshot() after every successful change.
// Decision (Ram, 14 Nov): the probe is built independently of the rocket. Fairing fit is
// checked only when both a bus and a fairing exist (FairingIssues / Evaluate).

#include "OrbitalAssembly.h"

#include <functional>

namespace Orbital
{
struct SocketView
{
    Id socket;      // socket type, e.g. "solar"
    int index = 0;  // index within the type
    Id name;        // stable name, e.g. "solar_2", "deck_c"
    Id partId;      // attached part, empty if free
    bool locked = false;     // parent part missing: socket does not exist yet
    bool canAccept = false;  // the selected part may be placed here
    bool wouldReplace = false; // placing here replaces partId (and its attached parts)
    Id rejectCode;           // why not, when a part is selected and canAccept is false
    std::string rejectReason;
};

struct ChangeResult
{
    bool ok = false;
    Id code;
    std::string reason; // player-facing sentence, empty on success
    Id socketName;
    int removed = 0;    // parts removed (Remove, or the old subtree on Replace)
};

// Short player-facing text for assembly rejections. Engine BLOCK/RISK/WARN codes keep
// their reviewed strings from MISSION_SYSTEMS_SPEC; these cover placement only.
inline std::string AssemblyReason(const Id& code)
{
    if (code.empty()) return {};
    if (code == "E_UNKNOWN_PART") return "This part is not in the catalog.";
    if (code == A_SLOT_MISMATCH) return "This part does not fit that socket.";
    if (code == A_MODE_MISMATCH) return "This part is for crewed missions only.";
    if (code == A_PARENT_MISSING) return "Attach the part this socket sits on first.";
    if (code == "E_SOCKET_LIMIT") return "No free socket of this type. Remove or replace one.";
    if (code == "E_BUS_INSTRUMENT_SLOTS") return "No free instrument deck on this bus.";
    if (code == "E_ENGINE_COUNT") return "This engine cannot be clustered that way.";
    if (code == A_INVALID_PLAN) return "That mission plan value is not allowed.";
    if (code == A_UNKNOWN_SOCKET) return "That socket does not exist.";
    return code;
}

class AssemblyScreen
{
public:
    using Listener = std::function<void(const AssemblySnapshot&)>;

    AssemblyScreen(const Catalog& catalog, const Mission& mission, AssemblyRules rules)
        : catalog_(&catalog), mission_(&mission), assembly_(catalog, mission, std::move(rules)),
          snapshot_(assembly_.Snapshot())
    {
    }

    // Called after every successful change with the new snapshot (dashboard refresh).
    void SetListener(Listener listener) { listener_ = std::move(listener); }

    const AssemblySnapshot& Snapshot() const { return snapshot_; }
    int Revision() const { return revision_; } // increases only on successful changes
    const Assembly& Model() const { return assembly_; }

    // Every socket in display order. Pass the part selected in the palette (or empty)
    // to fill canAccept / rejectCode for each socket.
    std::vector<SocketView> Sockets(const Id& selectedPart = {}) const
    {
        static const char* const order[] = {
            "stage1-tank", "stage1-engine", "separator", "stage2-tank", "stage2-engine", "fairing",
            "escape", "probe-core", "probe-tank", "probe-engine", "adcs", "solar", "battery",
            "antenna", "avionics", "instrument", "life-support"};
        std::vector<SocketView> views;
        for (const char* type : order)
        {
            if (!UsedInMode(type))
                continue;
            const int count = assembly_.Limit(type);
            const bool locked = assembly_.Capacity(type) == 0;
            for (int i = 0; i < count; ++i)
            {
                SocketView v;
                v.socket = type;
                v.index = i;
                v.name = Assembly::SocketName(type, i);
                v.partId = assembly_.Get(type, i);
                v.locked = locked;
                if (!selectedPart.empty())
                {
                    v.rejectCode = assembly_.CanAttachAt(type, i, selectedPart);
                    v.canAccept = v.rejectCode.empty();
                    v.wouldReplace = v.canAccept && !v.partId.empty();
                    v.rejectReason = AssemblyReason(v.rejectCode);
                }
                views.push_back(std::move(v));
            }
        }
        return views;
    }

    // Tak: first free socket of the part's type.
    ChangeResult Attach(const Id& partId) { return Finish(assembly_.Attach(partId), 0); }

    // Tak / Degistir on a specific socket. An occupied socket is replaced.
    ChangeResult Replace(const Id& socket, int index, const Id& partId)
    {
        const Id code = assembly_.CanAttachAt(socket, index, partId);
        if (!code.empty())
        {
            AttachResult refused;
            refused.code = code;
            return Finish(refused, 0);
        }
        const int before = Count();
        const AttachResult result = assembly_.AttachAt(socket, index, partId);
        return Finish(result, before + 1 - Count());
    }

    // Cikar: removes the part and everything attached under it.
    ChangeResult Remove(const Id& socket, int index)
    {
        ChangeResult r;
        r.removed = assembly_.Remove(socket, index);
        r.ok = r.removed > 0;
        r.socketName = Assembly::SocketName(socket, index);
        if (!r.ok)
        {
            r.code = A_UNKNOWN_SOCKET;
            r.reason = "Nothing is attached there.";
            return r;
        }
        Changed();
        return r;
    }

    ChangeResult SetStage1EngineCount(int count) { return Plan(assembly_.SetStage1EngineCount(count)); }
    ChangeResult SetOrbit(const Id& orbit) { return Plan(assembly_.SetOrbit(orbit)); }
    ChangeResult SetYears(double years) { return Plan(assembly_.SetYears(years)); }
    ChangeResult SetGround(const Id& ground) { return Plan(assembly_.SetGround(ground)); }
    ChangeResult SetPropellant(double kg) { return Plan(assembly_.SetPropellant(kg)); }

    // True once the probe is "on the rocket": a bus and a fairing both exist.
    bool ProbeOnRocket() const { return !snapshot_.spacecraft.bus.empty() && !snapshot_.launcher.fairing.empty(); }

    // Fairing compatibility, checked only when the probe is on the rocket. Same rules and
    // codes as the engine (E_FAIRING_MASS, E_FAIRING_SIZE); empty when fine or not connected.
    Ids FairingIssues() const
    {
        Ids issues;
        if (!ProbeOnRocket())
            return issues;
        const SpacecraftDesign& d = snapshot_.spacecraft;
        const Part& fairing = catalog_->at(snapshot_.launcher.fairing);
        double wetKg = d.propKg;
        for (const Id& id : {d.bus, d.engine, d.antenna, d.computer})
            if (!id.empty())
                wetKg += catalog_->at(id).massKg;
        for (const Ids* group : {&d.tanks, &d.adcs, &d.solar, &d.battery, &d.instruments})
            for (const Id& id : *group)
                wetKg += catalog_->at(id).massKg;
        if (wetKg > fairing.maxPayloadKg)
            issues.emplace_back("E_FAIRING_MASS");
        if (std::find(fairing.fitsBus.begin(), fairing.fitsBus.end(), d.bus) == fairing.fitsBus.end())
            issues.emplace_back("E_FAIRING_SIZE");
        return issues;
    }

private:
    const Catalog* catalog_;
    const Mission* mission_;
    Assembly assembly_;
    AssemblySnapshot snapshot_;
    Listener listener_;
    int revision_ = 0;

    bool UsedInMode(const Id& socket) const
    {
        for (const auto& [id, part] : *catalog_)
            if (FitsSlot(part, socket, mission_->mode))
                return true;
        return false;
    }

    int Count() const
    {
        int n = 0;
        for (const SocketView& v : Sockets())
            n += v.partId.empty() ? 0 : 1;
        return n;
    }

    void Changed()
    {
        snapshot_ = assembly_.Snapshot();
        ++revision_;
        if (listener_)
            listener_(snapshot_);
    }

    ChangeResult Finish(const AttachResult& result, int removed)
    {
        ChangeResult r;
        r.ok = result.ok;
        r.code = result.code;
        r.reason = AssemblyReason(result.code);
        r.socketName = result.socketName;
        r.removed = removed;
        if (r.ok)
            Changed();
        return r;
    }

    ChangeResult Plan(const Id& code)
    {
        ChangeResult r;
        r.ok = code.empty();
        r.code = code;
        r.reason = AssemblyReason(code);
        if (r.ok)
            Changed();
        return r;
    }
};
} // namespace Orbital
