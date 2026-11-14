#pragma once

// ORBITAL game flow (plain C++, no Unreal dependency): one object that owns the player's
// design and moves it through assembly -> design review -> flight -> report -> assembly.
// It wraps Ram's Orbital::Session (OrbitalSession.h) and drives it from the assembly screen:
//   - every successful edit updates the session and re-runs its review, so the dashboard
//     rows (Session::ReviewList) are always live;
//   - Launch is refused while any BLOCK code is present, asks for confirmation when RISK or
//     WARN codes are present, and otherwise takes the launch snapshot;
//   - the launch snapshot (design + Evaluate result) is what the flight view plays.
// Single-player simplification: the four review roles sign off together at launch.
//
// Lifetime: catalog and mission must outlive this object.

#include "../OrbitalSession.h"
#include "OrbitalAssemblyScreen.h"

#include <map>

namespace Orbital
{
enum class GamePhase { Assembly, Review, Flight, Report };
enum class LaunchAnswer { Refused, NeedsConfirmation, Launched };

struct FlowPresets
{
    std::map<Id, SpacecraftDesign> spacecraft; // reference_builds SC_*
    std::map<Id, LauncherDesign> launchers;    // reference_builds LV_*
};

// Mission cards on the main menu. Each is a preset of primary objective and budget level on
// the same lunar polar scenario: same parts, same physics, different goal and money.
struct MissionCard
{
    Id id, title;
    std::string summary;
    Id primary, difficulty;
};

inline std::vector<MissionCard> MissionCards()
{
    return {
        {"ice_prospector", "Ice Prospector",
         "Search the lunar poles for water ice on a tight budget. Carry only what the search needs.",
         "O1_ICE", "hard"},
        {"landing_scout", "Landing Site Scout",
         "Image candidate landing sites near the pole. Cameras make a lot of data, so plan how to send it home.",
         "O4_IMAGING", "normal"},
        {"polar_survey", "Full Polar Survey",
         "Fly all five instruments with a larger budget: ice, terrain, temperature, landing sites and shadowed craters.",
         "O1_ICE", "easy"},
    };
}

class GameFlow
{
public:
    GameFlow(const Catalog& catalog, const Mission& mission, AssemblyRules rules, FlowPresets presets,
             Id primary = "O1_ICE", Id difficulty = "normal")
        : catalog_(&catalog), mission_(&mission), presets_(std::move(presets)), primary_(std::move(primary)),
          difficulty_(std::move(difficulty)), screen_(catalog, mission, std::move(rules)), session_(catalog, mission)
    {
        Sync();
    }

    GamePhase Phase() const { return phase_; }
    const AssemblyScreen& Screen() const { return screen_; }
    const Session& SessionState() const { return session_; }
    const Id& Primary() const { return primary_; }
    const Id& Difficulty() const { return difficulty_; }

    // Live Evaluate() result for the current design (score preview, codes).
    const MissionResult& Preview() const { return preview_; }
    // Ram's dashboard rows and review issues for the current design. Null only if the
    // session refused the design.
    const DashboardReview* Review() const { return session_.ReviewList(); }
    // Design and result frozen at launch. Empty until a launch succeeds.
    const std::optional<SessionLaunch>& LaunchSnapshot() const { return session_.LaunchSnapshot(); }
    // Last refusal shown to the player (attach refused, launch refused). Empty after a success.
    const ChangeResult& LastChange() const { return last_; }

    // ---- editing (assembly phase only) ----
    ChangeResult Attach(const Id& partId) { return Edit([&] { return screen_.Attach(partId); }); }
    ChangeResult Replace(const Id& socket, int index, const Id& partId)
    {
        return Edit([&] { return screen_.Replace(socket, index, partId); });
    }
    ChangeResult Remove(const Id& socket, int index) { return Edit([&] { return screen_.Remove(socket, index); }); }
    ChangeResult SetStage1EngineCount(int count) { return Edit([&] { return screen_.SetStage1EngineCount(count); }); }
    ChangeResult SetOrbit(const Id& orbit) { return Edit([&] { return screen_.SetOrbit(orbit); }); }
    ChangeResult SetYears(double years) { return Edit([&] { return screen_.SetYears(years); }); }
    ChangeResult SetGround(const Id& ground) { return Edit([&] { return screen_.SetGround(ground); }); }
    ChangeResult SetPropellant(double kg) { return Edit([&] { return screen_.SetPropellant(kg); }); }

    bool SetPrimary(const Id& objective)
    {
        if (phase_ != GamePhase::Assembly || mission_->objectiveWeights.count(objective) == 0)
            return false;
        primary_ = objective;
        return Sync();
    }

    bool SetDifficulty(const Id& difficulty)
    {
        if (phase_ != GamePhase::Assembly || mission_->budgets.count(difficulty) == 0)
            return false;
        difficulty_ = difficulty;
        return Sync();
    }

    // Main menu mission card: sets the primary objective and the budget level. The design is kept.
    bool StartMission(const MissionCard& card)
    {
        if (phase_ != GamePhase::Assembly || mission_->objectiveWeights.count(card.primary) == 0 ||
            mission_->budgets.count(card.difficulty) == 0)
            return false;
        primary_ = card.primary;
        difficulty_ = card.difficulty;
        last_ = ChangeResult{};
        last_.ok = true;
        return Sync();
    }

    bool LoadDesign(const SpacecraftDesign& spacecraft, const LauncherDesign& launcher)
    {
        if (phase_ != GamePhase::Assembly || !screen_.Load(spacecraft, launcher))
            return false;
        last_ = ChangeResult{};
        last_.ok = true;
        return Sync();
    }

    // Reference designs from TEST_CASES.md: TC-A, TC-B, TC-D, TC-E, TC-F.
    bool LoadCase(const Id& name)
    {
        const bool caseA = name == "TC-A";
        const auto probe = presets_.spacecraft.find(caseA ? "SC_PROSPECTOR" : "SC_SURVEYOR");
        const auto rocket = presets_.launchers.find(caseA ? "LV_LIGHT" : "LV_MID");
        if (probe == presets_.spacecraft.end() || rocket == presets_.launchers.end())
            return false;
        SpacecraftDesign design = probe->second;
        if (name == "TC-D")
            design.propKg = 150.0;
        else if (name == "TC-E")
            design.battery = {"battery_s"};
        else if (name == "TC-F")
            design.solar = {"solar_s"};
        else if (!caseA && name != "TC-B")
            return false;
        return LoadDesign(design, rocket->second);
    }

    // ---- launch ----
    LaunchAnswer RequestLaunch()
    {
        if (phase_ != GamePhase::Assembly)
            return LaunchAnswer::Refused;
        if (!preview_.block.empty())
        {
            last_ = ChangeResult{};
            last_.code = preview_.block.front();
            last_.reason = "Launch refused: fix the red items first.";
            return LaunchAnswer::Refused;
        }
        if (!preview_.risk.empty() || !preview_.warn.empty())
        {
            phase_ = GamePhase::Review;
            return LaunchAnswer::NeedsConfirmation;
        }
        return DoLaunch() ? LaunchAnswer::Launched : LaunchAnswer::Refused;
    }

    // Design review answers: fly with the listed risks, or go back and fix them.
    bool ConfirmLaunch() { return phase_ == GamePhase::Review && DoLaunch(); }
    bool CancelReview()
    {
        if (phase_ != GamePhase::Review)
            return false;
        phase_ = GamePhase::Assembly;
        return true;
    }

    // ---- flight and report ----
    bool FlightFinished()
    {
        if (phase_ != GamePhase::Flight || !session_.Finish())
            return false;
        phase_ = GamePhase::Report;
        return true;
    }

    // Report "Redesign": back to assembly with the same design, ready to change and relaunch.
    bool Redesign()
    {
        if (phase_ == GamePhase::Flight)
            FlightFinished();
        if (phase_ != GamePhase::Report || !session_.BackToAssembly())
            return false;
        phase_ = GamePhase::Assembly;
        last_ = ChangeResult{};
        last_.ok = true;
        return Sync();
    }

private:
    const Catalog* catalog_;
    const Mission* mission_;
    FlowPresets presets_;
    Id primary_, difficulty_;
    AssemblyScreen screen_;
    Session session_;
    MissionResult preview_;
    ChangeResult last_;
    GamePhase phase_ = GamePhase::Assembly;

    template <typename Operation>
    ChangeResult Edit(Operation operation)
    {
        if (phase_ != GamePhase::Assembly)
        {
            ChangeResult refused;
            refused.code = A_INVALID_PLAN;
            refused.reason = "The design can only be changed on the assembly screen.";
            return last_ = refused;
        }
        last_ = operation();
        if (last_.ok)
        {
            // The design format has no separator field: it comes with the stage 1 tank.
            const Assembly& model = screen_.Model();
            if (!model.Get("stage1-tank", 0).empty() && model.Get("separator", 0).empty() &&
                !screen_.SeparatorId().empty())
                screen_.Attach(screen_.SeparatorId());
            Sync();
        }
        return last_;
    }

    // Pushes the assembly snapshot into the session and refreshes review and preview.
    bool Sync()
    {
        const AssemblySnapshot& snapshot = screen_.Snapshot();
        SessionDesign design;
        design.spacecraft = snapshot.spacecraft;
        design.launcher = snapshot.launcher;
        design.primary = primary_;
        design.difficulty = difficulty_;
        preview_ = Evaluate(*catalog_, *mission_, design.spacecraft, design.launcher, primary_, difficulty_);
        const bool accepted = session_.Phase() == SessionPhase::Briefing ? session_.Begin(design)
                                                                         : session_.Update(design);
        if (!accepted)
            return false;
        if (session_.Phase() == SessionPhase::Assembly)
            session_.Plan();
        return session_.Phase() == SessionPhase::MissionPlan && session_.Review();
    }

    bool DoLaunch()
    {
        for (const ReviewRole role : {ReviewRole::MissionLead, ReviewRole::PropulsionEngineer,
                                      ReviewRole::ScienceOfficer, ReviewRole::PowerAndCommsEngineer})
            session_.Go(role);
        if (!session_.Launch())
        {
            phase_ = GamePhase::Assembly;
            last_ = ChangeResult{};
            last_.reason = "Launch refused by the design review.";
            return false;
        }
        phase_ = GamePhase::Flight;
        last_ = ChangeResult{};
        last_.ok = true;
        return true;
    }
};
} // namespace Orbital
