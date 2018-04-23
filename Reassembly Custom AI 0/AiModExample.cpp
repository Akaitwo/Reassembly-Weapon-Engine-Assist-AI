#include "game/StdAfx.h"
#include "AiModExample.h"

#include <game/AI.h>
#include <game/Blocks.h>
#include <game/Sector.h>
#include <game/Weapon.h>


#define ADD_ACTION(TYPE, ...)                       \
    if (TYPE::supportsConfig(ai->getConfig()))      \
        ai->addAction(new TYPE(ai, __VA_ARGS__));


static bool featuresMatch (uint64 features) {
    // not a turret?  No match.
    if (!(features & Block::TURRET))
        return false;
    // Laser (which wouldn't apply a force)
    // or autofire (point-defense)?  No match.
    if (features & (Block::LASER | Block::AUTOFIRE))
        return false;
    return true;
}



struct ATurretBoost_Aim : public AIAction
{
    bool  doingStuff    = false;
    float debugAngle    = -M_PIf * 0.5f;
    float backwardAngle = 0.0f; // the angle to fire at to push us towards our destination

    static bool supportsConfig(const AICommandConfig& cfg) {
        return cfg.hasWeapons &&
            featuresMatch(cfg.features);
    }

    ATurretBoost_Aim(AI* ai) :
        AIAction(ai, LANE_TARGET)
    {}

    uint update(uint blockedLanes) override
    {
        doingStuff    = false;
        backwardAngle = 0.0f;
        // If there's nowhere we're trying to go, don't try to go anywhere faster.
        // Keep in mind actions that process later (commonly AWander) won't trip this if they
        // *are* trying to get somewhere.
        if (!(blockedLanes & LANE_MOVEMENT))
            return LANE_NONE;

        if (!m_ai->nav.get())
            return LANE_NONE;

        const BlockCluster * cluster = m_ai->command->cluster;
        float2 pos = cluster->getAbsolutePos();
        float2 dest = m_ai->nav->dest.cfg.position; // (absolute world-space position)

        // deliberately doing this subtraction "backward" to get an angle going backward
        backwardAngle = vectorToAngle(pos - dest);

        for (Block * block : cluster->blocks) {
            FeatureE & blockFeatures = block->sb.features;

            if (!featuresMatch(blockFeatures.get()))
                continue;
            if (!block->turret)
                continue;
            //Report("TEST enumWeapons TEST -- _Aim turret (sanity check)");

            block->turret->targetAngle = backwardAngle; // just aim in one direction for now
            //block->turret->targetAngle = block->weaponAngleForTarget();

            doingStuff = true;
        }

        return doingStuff ? LANE_TARGET : LANE_NONE;
    }

    string toStringEx() const override
    {
        //return doingStuff ? "aiming at " : "";
        if (!doingStuff)
            return "";
        return str_format(
            "aiming for %0.3f deg",
            (backwardAngle * 180.0f) / M_PIf
        );
    }
};



struct ATurretBoost_Fire : public AIAction
{
    bool  doingStuff       = false;
    float minDotProdToFire = 0.8f;

    static bool supportsConfig(const AICommandConfig& cfg) {
        return ATurretBoost_Aim::supportsConfig(cfg);
    }

    ATurretBoost_Fire(AI* ai) :
        AIAction(ai, LANE_SHOOT)
    {}

    uint update(uint blockedLanes) override
    {
        doingStuff = false;
        // If there's nowhere we're trying to go, don't try to go anywhere faster
        // Keep in mind actions that process later (commonly AWander) won't trip this if they
        // *are* trying to get somewhere.
        if (!(blockedLanes & LANE_MOVEMENT))
            return LANE_NONE;

        const BlockCluster * cluster = m_ai->command->cluster;
        for (Block * block : cluster->blocks) {
            FeatureE & blockFeatures = block->sb.features;

            if (!featuresMatch(blockFeatures.get()))
                continue;
            if (!block->turret)
                continue;

            float dot = dotAngles(block->turret->angle, block->turret->targetAngle);
            bool  aimedCloseEnough = fabsf(dot) > minDotProdToFire;

            block->setWeaponEnabled(aimedCloseEnough);

            doingStuff = aimedCloseEnough || doingStuff;
        }

        return doingStuff ? LANE_SHOOT : LANE_NONE;
    }

    string toStringEx() const override
    {
        return doingStuff ? "firing" : "";
    }
};



//=============================================================================
// Exported functions
//=============================================================================

void GetApiVersion(int * major, int * minor) {
    *major = 1;
    *minor = 0;
}

// tournament mode AI
bool CreateAiActions(AI* ai) {
    const AICommandConfig & config = ai->getConfig();
    if (config.isMobile >= 2 && (config.flags & SerialCommand::DODGES)) {
        ADD_ACTION(AAvoidWeapon);
    }

    ADD_ACTION(AWeapons);

    ADD_ACTION(AFallbackTarget);
    ADD_ACTION(ATargetEnemy);
    ADD_ACTION(AAvoidCluster);
    ADD_ACTION(AAttack);
    ADD_ACTION(AHealers); // notice this isn't used by the interceptor; see macro definition
    ADD_ACTION(AInvestigate);
    ADD_ACTION(AWander);
    ADD_ACTION(ATurretBoost_Aim);
    ADD_ACTION(ATurretBoost_Fire);

    // An explanation on the order above:
    // The turret boost actions only attempt to aim/fire to assist when the
    // movement lane is busy and the ship has a destination.
    // By placing the ATurretBoost actions *after* Wander, they'll assist with
    // movement while wandering.
    // If you wanted to conserve ship energy by only turret-boosting while
    // going somewhere deliberate (never assisting wandering), you could just
    // move the turret-boost actions to right before wander.

    return true; // we handled it; no need for default AI actions
}
