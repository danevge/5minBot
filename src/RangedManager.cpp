#include "RangedManager.h"
#include "Util.h"
#include "CCBot.h"

RangedManager::RangedManager(CCBot & bot)
    : MicroManager(bot)
{

}

void RangedManager::executeMicro(const std::vector<const sc2::Unit *> & targets)
{
    assignTargets(targets);
}

void RangedManager::assignTargets(const std::vector<const sc2::Unit *> & targets)
{
    const std::vector<const sc2::Unit *> & rangedUnits = getUnits();

    // figure out targets
    std::vector<const sc2::Unit *> rangedUnitTargets;
    for (auto target : targets)
    {
        if (!target) { continue; }
        if (target->unit_type == sc2::UNIT_TYPEID::ZERG_EGG) { continue; }
        if (target->unit_type == sc2::UNIT_TYPEID::ZERG_LARVA) { continue; }

        rangedUnitTargets.push_back(target);
    }
	//The idea is now to group the targets/targetPos
	std::unordered_map<const sc2::Unit *, sc2::Units > targetsAttackedBy;
	sc2::Units moveToPosition;
	//For the medivac we need either
	//Either the most injured
	std::map<int, const sc2::Unit *> injuredUnits;
	//Or the soldier in the front
	const sc2::Unit * frontSoldier = nullptr;
	sc2::Point2D orderPos = order.getPosition();
	float minDist = std::numeric_limits<float>::max();
	//Just checking if only medivacs available
	bool justMedivacs = true;
	for (auto & injured : rangedUnits)
	{
		if (!m_bot.GetUnit(injured->tag) || !injured->is_alive)
		{
			//its too late
			continue;
		}
		if (injured->unit_type.ToType() != sc2::UNIT_TYPEID::TERRAN_MEDIVAC)
		{
			justMedivacs = false;
		}
		else
		{
			continue;
		}
		auto attributes = m_bot.Observation()->GetUnitTypeData()[injured->unit_type].attributes;
		//We can only heal biological units
		if (std::find(attributes.begin(), attributes.end(), sc2::Attribute::Biological) != attributes.end())
		{
			int healthMissing = injured->health_max - injured->health;
			if (healthMissing>0)
			{
				injuredUnits[healthMissing] = injured;
			}
			float dist = Util::DistSq(injured->pos, orderPos);
			if (!frontSoldier || minDist > dist)
			{
				frontSoldier = injured;
				minDist = dist;
			}
		}
	}
	// In case it were really only medivacs
	if (justMedivacs)
	{
		Micro::SmartMove(rangedUnits, m_bot.Bases().getRallyPoint(),m_bot);
		return;
	}
    // for each Unit
    for (auto rangedUnit : rangedUnits)
    {
        BOT_ASSERT(rangedUnit, "ranged unit is null");
        // if the order is to attack or defend
		if (order.getType() == SquadOrderTypes::Attack || order.getType() == SquadOrderTypes::Defend)
		{
			// find the best target for this rangedUnit
			//medivacs have the other ranged units as target.
			if (rangedUnit->unit_type == sc2::UNIT_TYPEID::TERRAN_MEDIVAC)
			{
				if (rangedUnit->is_selected)
				{
					int a = 1;
				}
				//find the nearest enemy
				const sc2::Unit * nearestEnemy = nullptr;
				float minDistTarget = std::numeric_limits<float>::max();
				for (auto & target : rangedUnitTargets)
				{
					if (target->is_alive && Util::canHitMe(rangedUnit, target,m_bot))
					{
						float dist = Util::Dist(rangedUnit->pos, target->pos);
						if (!nearestEnemy || minDistTarget > dist)
						{
							nearestEnemy = target;
							minDistTarget = dist;
						}
					}
				}
				
				if (injuredUnits.size()>0)
				{
					const sc2::Unit* mostInjured = (injuredUnits.rbegin())->second;
					if (nearestEnemy && Util::Dist(rangedUnit->pos, nearestEnemy->pos) < Util::Dist(mostInjured->pos, nearestEnemy->pos))
					{
						m_bot.Actions()->UnitCommand(rangedUnit, sc2::ABILITY_ID::EFFECT_MEDIVACIGNITEAFTERBURNERS);
						sc2::Point2D targetPos = rangedUnit->pos;
						sc2::Point2D runningVector = mostInjured->pos - nearestEnemy->pos;
						runningVector *= (Util::GetAttackRange(rangedUnit->unit_type,m_bot) - 1) / (std::sqrt(Util::DistSq(runningVector)));
						targetPos += runningVector;
						Micro::SmartMove(rangedUnit, targetPos, m_bot);
					}
					else if (Util::Dist(rangedUnit->pos, mostInjured->pos) > 5)
					{
						m_bot.Actions()->UnitCommand(rangedUnit, sc2::ABILITY_ID::EFFECT_MEDIVACIGNITEAFTERBURNERS);
						if (rangedUnit->orders.empty() || rangedUnit->orders[0].target_unit_tag != mostInjured->tag)
						{
							m_bot.Actions()->UnitCommand(rangedUnit, sc2::ABILITY_ID::MOVE, mostInjured);
						}
					}
					else
					{
						if (rangedUnit->orders.empty() || rangedUnit->orders[0].ability_id != sc2::ABILITY_ID::EFFECT_HEAL)
						{
							m_bot.Actions()->UnitCommand(rangedUnit, sc2::ABILITY_ID::EFFECT_HEAL, mostInjured);
							injuredUnits.erase(std::prev(injuredUnits.end())); //no idea why rbegin is not working
						}
					}
				}
				else
				{
					if (rangedUnit->orders.empty() || frontSoldier && rangedUnit->orders[0].target_unit_tag && rangedUnit->orders[0].target_unit_tag != frontSoldier->tag)
					{
						m_bot.Actions()->UnitCommand(rangedUnit, sc2::ABILITY_ID::MOVE, frontSoldier);
					}
				}
			}
			else
			{
				if (!rangedUnitTargets.empty())
				{
					const sc2::Unit * target = getTarget(rangedUnit, rangedUnitTargets);
					//if something goes wrong
					if (!target)
					{
						return;
					}
					//We only need fancy micro if we are in range and its not a building
					if (!m_bot.Data(target->unit_type).isBuilding && Util::Dist(rangedUnit->pos, target->pos) <= Util::GetAttackRange(rangedUnit->unit_type.ToType(), m_bot))
					{
						Micro::SmartKiteTarget(rangedUnit, target, m_bot);
					}
					//else we batch the attack comand first
					else
					{
						targetsAttackedBy[target].push_back(rangedUnit);
					}
				}
				// if there are no targets
				else
				{
					// if we're not near the order position
					if (Util::Dist(rangedUnit->pos, order.getPosition()) > 4)
					{
						// move to it
						moveToPosition.push_back(rangedUnit);
					}
				}
			}
		}

        if (m_bot.Config().DrawUnitTargetInfo)
        {
            // TODO: draw the line to the unit's target
        }
    }
	//Grouped by target attack command
	for (auto & t : targetsAttackedBy)
	{
		Micro::SmartAttackUnit(t.second, t.first, m_bot);
	}
	//Grouped by  position Move command
	if (moveToPosition.size() > 0)
	{
		Micro::SmartAttackMove(moveToPosition, order.getPosition(), m_bot);
	}
}

// get a target for the ranged unit to attack
// TODO: this is the melee targeting code, replace it with something better for ranged units
const sc2::Unit * RangedManager::getTarget(const sc2::Unit * rangedUnit, const std::vector<const sc2::Unit *> & targets)
{
    BOT_ASSERT(rangedUnit, "null melee unit in getTarget");

    int highPriorityFar = 0;
	int highPriorityNear = 0;
    double closestDist = std::numeric_limits<double>::max();
	double lowestHealth = std::numeric_limits<double>::max();
    const sc2::Unit * closestTargetOutsideRange = nullptr;
	const sc2::Unit * weakestTargetInsideRange = nullptr;
	const float range = Util::GetAttackRange(rangedUnit->unit_type,m_bot);
    // for each target possiblity
	for (auto targetUnit : targets)
	{
		if (targetUnit->cloak==1)
		{
			int a = 1;
		}
		BOT_ASSERT(targetUnit, "null target unit in getTarget");
		if (!targetUnit->is_alive)
		{
			continue;
		}
		int priority = getAttackPriority(rangedUnit, targetUnit);
		float distance = Util::Dist(rangedUnit->pos, targetUnit->pos);
		if (distance > range)
		{
			// if it's a higher priority, or it's closer, set it
			if (!closestTargetOutsideRange || (priority > highPriorityFar) || (priority == highPriorityFar && distance < closestDist))
			{
				closestDist = distance;
				highPriorityFar = priority;
				closestTargetOutsideRange = targetUnit;
			}
		}
		else
		{
			if (!weakestTargetInsideRange || (priority > highPriorityNear) || (priority == highPriorityNear && targetUnit->health+targetUnit->shield < lowestHealth))
			{
				lowestHealth = targetUnit->health + targetUnit->shield;
				highPriorityNear = priority;
				weakestTargetInsideRange = targetUnit;
			}
		}

	}
    return weakestTargetInsideRange&&highPriorityNear>1 ? weakestTargetInsideRange: closestTargetOutsideRange;
}

// get the attack priority of a type in relation to a zergling
int RangedManager::getAttackPriority(const sc2::Unit * attacker, const sc2::Unit * unit)
{
    BOT_ASSERT(unit, "null unit in getAttackPriority");

    if (Util::IsCombatUnit(unit, m_bot))
    {
		if (unit->unit_type == sc2::UNIT_TYPEID::ZERG_BANELING)
		{
			return 11;
		}
        return 10;
    }
	if (unit->unit_type == sc2::UNIT_TYPEID::PROTOSS_PHOTONCANNON || unit->unit_type == sc2::UNIT_TYPEID::ZERG_SPINECRAWLER)
	{
		return 10;
	}
    if (Util::IsWorker(unit))
    {
        return 10;
    }
	if (unit->unit_type == sc2::UNIT_TYPEID::PROTOSS_PYLON || unit->unit_type == sc2::UNIT_TYPEID::ZERG_SPORECRAWLER)
	{
		return 5;
	}

    return 1;
}
