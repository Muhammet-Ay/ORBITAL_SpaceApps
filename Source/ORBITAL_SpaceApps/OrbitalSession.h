#pragma once

#include "OrbitalDashboard.h"

#include <charconv>
#include <limits>
#include <set>
#include <string_view>

namespace Orbital
{
enum class SessionPhase { Briefing, Assembly, MissionPlan, DesignReview, Launch, Report };

struct SessionDesign
{
    SpacecraftDesign spacecraft;
    LauncherDesign launcher;
    Id primary, difficulty = "normal";
};

struct SessionLaunch
{
    SessionDesign design;
    MissionResult result;
};

struct BestScore
{
    Id designName;
    double score, science, costMCr;
    Id difficulty;
};

namespace SessionJson
{
// All string fields in the v2.1 design format are ASCII catalog IDs.
inline bool IsIdChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-';
}

class Reader
{
public:
    explicit Reader(std::string_view input) : Input(input) {}

    bool Take(char token)
    {
        Space();
        if (Input.empty() || Input.front() != token)
            return false;
        Input.remove_prefix(1);
        return true;
    }

    bool String(Id& value, bool nullable = false)
    {
        Space();
        if (nullable && Input.substr(0, 4) == "null")
        {
            Input.remove_prefix(4);
            value.clear();
            return true;
        }
        if (!Take('"'))
            return false;
        const auto length = Input.find('"');
        if (length == std::string_view::npos || length == 0)
            return false;
        const auto text = Input.substr(0, length);
        if (!std::all_of(text.begin(), text.end(), IsIdChar))
            return false;
        value.assign(text);
        Input.remove_prefix(length + 1);
        return true;
    }

    bool List(Ids& values)
    {
        if (!Take('['))
            return false;
        if (Take(']'))
            return true;
        do
        {
            Id id;
            if (!String(id))
                return false;
            values.push_back(std::move(id));
        } while (Take(','));
        return Take(']');
    }

    bool Number(double& value)
    {
        Space();
        if (Input.empty() || Input.front() < '0' || Input.front() > '9')
            return false;
        if (Input.size() > 1 && Input.front() == '0' && Input[1] >= '0' && Input[1] <= '9')
            return false;
        const auto first = Input.data();
        const auto last = first + Input.size();
        const auto parsed = std::from_chars(first, last, value);
        if (parsed.ec != std::errc{} || !std::isfinite(value))
            return false;
        const auto length = static_cast<std::size_t>(parsed.ptr - first);
        const auto dot = Input.substr(0, length).find('.');
        if (dot != std::string_view::npos &&
            (dot + 1 == length || Input[dot + 1] < '0' || Input[dot + 1] > '9'))
            return false;
        Input.remove_prefix(length);
        return true;
    }

    template<class ReadField>
    bool Object(ReadField readField)
    {
        if (!Take('{'))
            return false;
        std::set<Id> seen;
        do
        {
            Id key;
            if (!String(key) || !seen.insert(key).second || !Take(':') || !readField(key))
                return false;
        } while (Take(','));
        return Take('}');
    }

    bool End() { Space(); return Input.empty(); }

private:
    std::string_view Input;
    void Space()
    {
        const auto start = Input.find_first_not_of(" \t\r\n");
        Input.remove_prefix(start == std::string_view::npos ? Input.size() : start);
    }
};

inline bool ReadSpacecraft(Reader& reader, SpacecraftDesign& d)
{
    unsigned fields = 0;
    return reader.Object([&](const Id& key) {
        ++fields;
        if (key == "bus") return reader.String(d.bus, true);
        if (key == "engine") return reader.String(d.engine, true);
        if (key == "antenna") return reader.String(d.antenna, true);
        if (key == "computer") return reader.String(d.computer, true);
        if (key == "orbit") return reader.String(d.orbit);
        if (key == "ground") return reader.String(d.ground);
        if (key == "years") return reader.Number(d.years);
        if (key == "prop_kg") return reader.Number(d.propKg);
        if (key == "tanks") return reader.List(d.tanks);
        if (key == "adcs") return reader.List(d.adcs);
        if (key == "solar") return reader.List(d.solar);
        if (key == "battery") return reader.List(d.battery);
        return key == "instruments" && reader.List(d.instruments);
    }) && fields == 13;
}

inline bool ReadLauncher(Reader& reader, LauncherDesign& d)
{
    unsigned fields = 0;
    return reader.Object([&](const Id& key) {
        ++fields;
        if (key == "s1_tank") return reader.String(d.firstTank, true);
        if (key == "s1_engine") return reader.String(d.firstEngine, true);
        if (key == "s2_tank") return reader.String(d.secondTank, true);
        if (key == "s2_engine") return reader.String(d.secondEngine, true);
        if (key == "fairing") return reader.String(d.fairing, true);
        double count = 0;
        if (key != "s1_count" || !reader.Number(count) ||
            count > static_cast<double>((std::numeric_limits<int>::max)()) || std::floor(count) != count)
            return false;
        d.firstEngineCount = static_cast<int>(count);
        return true;
    }) && fields == 6;
}

inline Id String(const Id& value)
{
    return value.empty() ? "null" : '"' + value + '"';
}

inline Id List(const Ids& values)
{
    Id out = "[";
    for (const auto& value : values)
    {
        if (out.size() > 1)
            out += ',';
        out += String(value);
    }
    return out + ']';
}

inline Id Number(double value)
{
    char buffer[64];
    const auto result = std::to_chars(std::begin(buffer), std::end(buffer), value);
    return Id(buffer, result.ptr);
}

// Each nested vehicle object has the same fields as its catalog reference build.
inline Id WriteDesign(const SessionDesign& design)
{
    const auto& s = design.spacecraft;
    const auto& l = design.launcher;
    return "{\"spacecraft\":{\"bus\":" + String(s.bus) +
        ",\"tanks\":" + List(s.tanks) + ",\"engine\":" + String(s.engine) +
        ",\"prop_kg\":" + Number(s.propKg) + ",\"adcs\":" + List(s.adcs) +
        ",\"solar\":" + List(s.solar) + ",\"battery\":" + List(s.battery) +
        ",\"antenna\":" + String(s.antenna) + ",\"computer\":" + String(s.computer) +
        ",\"instruments\":" + List(s.instruments) + ",\"orbit\":" + String(s.orbit) +
        ",\"years\":" + Number(s.years) + ",\"ground\":" + String(s.ground) +
        "},\"launcher\":{\"s1_tank\":" + String(l.firstTank) +
        ",\"s1_engine\":" + String(l.firstEngine) + ",\"s1_count\":" + std::to_string(l.firstEngineCount) +
        ",\"s2_tank\":" + String(l.secondTank) + ",\"s2_engine\":" + String(l.secondEngine) +
        ",\"fairing\":" + String(l.fairing) + "},\"primary\":" + String(design.primary) +
        ",\"difficulty\":" + String(design.difficulty) + '}';
}
} // namespace SessionJson

class Session
{
public:
    // The catalog and mission must outlive the session and remain unchanged.
    Session(const Catalog& catalog, const Mission& mission) : Parts(catalog), Rules(mission) {}

    SessionPhase Phase() const { return Step; }
    const SessionDesign& Design() const { return Current; }
    const std::optional<SessionLaunch>& LaunchSnapshot() const { return Snapshot; }
    const std::vector<BestScore>& Scores() const { return Records; }

    bool Begin(SessionDesign design)
    {
        if (Step != SessionPhase::Briefing || !ValidDesign(design))
            return false;
        Current = std::move(design);
        Step = SessionPhase::Assembly;
        return true;
    }

    bool Plan()
    {
        if (Step != SessionPhase::Assembly)
            return false;
        Step = SessionPhase::MissionPlan;
        return true;
    }

    bool Update(SessionDesign design)
    {
        if (!Editable() || !ValidDesign(design))
            return false;
        Current = std::move(design);
        ResetReview();
        if (Step == SessionPhase::DesignReview)
            Step = SessionPhase::MissionPlan;
        return true;
    }

    bool Review()
    {
        if (Step != SessionPhase::MissionPlan)
            return false;
        Result = Evaluate(Parts, Rules, Current.spacecraft, Current.launcher, Current.primary, Current.difficulty);
        Findings = MakeDashboardReview(*Result);
        Step = SessionPhase::DesignReview;
        return true;
    }

    const DashboardReview* ReviewList() const { return Findings ? &*Findings : nullptr; }

    bool Go(ReviewRole role)
    {
        if (Step != SessionPhase::DesignReview)
            return false;
        const auto index = static_cast<std::size_t>(role);
        if (index >= Approvals.size() ||
            std::any_of(Findings->issues.begin(), Findings->issues.end(), [&](const ReviewIssue& issue) {
                return issue.owner == role && issue.severity == ReviewSeverity::Block;
            }))
            return false;
        Approvals[index] = true;
        return true;
    }

    bool Launch()
    {
        if (Step != SessionPhase::DesignReview || !Result->block.empty() ||
            !std::all_of(Approvals.begin(), Approvals.end(), [](bool go) { return go; }))
            return false;
        Snapshot = SessionLaunch{Current, *Result};
        Step = SessionPhase::Launch;
        return true;
    }

    bool Finish(Id designName)
    {
        if (Step != SessionPhase::Launch || designName.empty())
            return false;
        const auto& result = Snapshot->result;
        const BestScore entry{std::move(designName), result.score, result.science,
                              result.totalCostMCr, Snapshot->design.difficulty};
        const auto place = std::find_if(Records.begin(), Records.end(),
            [&](const BestScore& saved) { return saved.score < entry.score; });
        Records.insert(place, entry);
        Step = SessionPhase::Report;
        return true;
    }

    bool BackToAssembly()
    {
        if (Step != SessionPhase::Report)
            return false;
        Snapshot.reset();
        ResetReview();
        Step = SessionPhase::Assembly;
        return true;
    }

    std::optional<Id> SaveDesignJson() const
    {
        if (Step == SessionPhase::Briefing)
            return std::nullopt;
        return SessionJson::WriteDesign(Current);
    }

    bool LoadDesignJson(std::string_view json)
    {
        SessionDesign candidate;
        candidate.difficulty.clear();
        SessionJson::Reader reader(json);
        unsigned fields = 0;
        if (!reader.Object([&](const Id& key) {
            ++fields;
            if (key == "spacecraft") return SessionJson::ReadSpacecraft(reader, candidate.spacecraft);
            if (key == "launcher") return SessionJson::ReadLauncher(reader, candidate.launcher);
            if (key == "primary") return reader.String(candidate.primary);
            return key == "difficulty" && reader.String(candidate.difficulty);
        }) || !reader.End() || fields != 4)
            return false;
        return Step == SessionPhase::Briefing ? Begin(std::move(candidate)) : Update(std::move(candidate));
    }

private:
    const Catalog& Parts;
    const Mission& Rules;
    SessionPhase Step = SessionPhase::Briefing;
    SessionDesign Current;
    std::array<bool, 4> Approvals{};
    std::optional<MissionResult> Result;
    std::optional<DashboardReview> Findings;
    std::optional<SessionLaunch> Snapshot;
    std::vector<BestScore> Records;

    bool Editable() const
    {
        return Step == SessionPhase::Assembly || Step == SessionPhase::MissionPlan || Step == SessionPhase::DesignReview;
    }

    void ResetReview()
    {
        Approvals.fill(false);
        Result.reset();
        Findings.reset();
    }

    bool ValidDesign(const SessionDesign& design) const
    {
        const auto& s = design.spacecraft;
        const auto& l = design.launcher;
        const auto fits = [&](const Id& id, const char* slot) {
            const auto part = Parts.find(id);
            return part != Parts.end() && FitsSlot(part->second, slot, Rules.mode);
        };
        const auto single = [&](const Id& id, const char* slot) { return id.empty() || fits(id, slot); };
        const auto list = [&](const Ids& ids, const char* slot) {
            return std::all_of(ids.begin(), ids.end(), [&](const Id& id) { return fits(id, slot); });
        };
        return Rules.objectiveWeights.count(design.primary) && Rules.budgets.count(design.difficulty) &&
            Rules.orbits.count(s.orbit) && Rules.ground.count(s.ground) &&
            std::find(Rules.plannedYearsOptions.begin(), Rules.plannedYearsOptions.end(), s.years) != Rules.plannedYearsOptions.end() &&
            std::isfinite(s.propKg) && s.propKg >= 0 && l.firstEngineCount >= 0 &&
            single(s.bus, "probe-core") && single(s.engine, "probe-engine") &&
            single(s.antenna, "antenna") && single(s.computer, "avionics") &&
            list(s.tanks, "probe-tank") && list(s.adcs, "adcs") &&
            list(s.solar, "solar") && list(s.battery, "battery") && list(s.instruments, "instrument") &&
            single(l.firstTank, "stage1-tank") && single(l.firstEngine, "stage1-engine") &&
            single(l.secondTank, "stage2-tank") && single(l.secondEngine, "stage2-engine") &&
            single(l.fairing, "fairing");
    }
};
} // namespace Orbital
