#include "RangedManager.h"
#include "Util.h"
#include "CCBot.h"

RangedManager::RangedManager(CCBot & bot)
	: MicroManager(bot)
{

}

void RangedManager::executeMicro(const CUnits & targets)
{
	assignTargets(targets);
}

void RangedManager::assignTargets(const CUnits & targetsRaw)
{

	// figure out targets
	CUnits rangedUnitTargets;
	for (const auto & target : targetsRaw)
	{
		if (!target) { continue; }
		if (target->getUnitType() == sc2::UNIT_TYPEID::ZERG_EGG) { continue; }
		if (target->getUnitType() == sc2::UNIT_TYPEID::ZERG_LARVA) { continue; }
		if (target->getPos()==sc2::Point3D()) { continue; }
		rangedUnitTargets.push_back(target);
	}
	const auto sortedUnitTargets = getAttackPriority(rangedUnitTargets);
	//The idea is now to group the targets/targetPos
	std::unordered_map<CUnit_ptr, CUnits> targetsAttackedBy;
	std::unordered_map<CUnit_ptr, CUnits> targetsMovedTo;
	CUnits moveToPosition;
	//For the medivac we need either
	//Either the most injured
	std::map<float, CUnit_ptr> injuredUnits;
	//Or the soldier in the front
	CUnit_ptr frontSoldier = nullptr;
	sc2::Point2D orderPos = order.getPosition();
	float minDist = std::numeric_limits<float>::max();
	//Just checking if only medivacs available
	bool justMedivacs = true;
	const CUnits & rangedUnits = getUnits();
	//Being healed is not a buff. So we need to check every medivac if the injured unit is already healed.
	const CUnits medivacs = m_bot.UnitInfo().getUnits(Players::Self, sc2::UNIT_TYPEID::TERRAN_MEDIVAC);
	for (const auto & injured : rangedUnits)
	{
		if (!injured->isAlive())
		{
			//its too late
			continue;
		}
		if (injured->getUnitType().ToType() != sc2::UNIT_TYPEID::TERRAN_MEDIVAC)
		{
			justMedivacs = false;
		}
		else
		{
			continue;
		}
		//We can only heal biological units
		if (injured->hasAttribute(sc2::Attribute::Biological))
		{
			const float dist = Util::DistSq(injured->getPos(), orderPos);
			if (!frontSoldier || minDist > dist)
			{
				frontSoldier = injured;
				minDist = dist;
			}
			const float healthMissing = injured->getHealthMax() - injured->getHealth();
			if (healthMissing>0)
			{
				bool isHealing = false;
				for (const auto & medivac : medivacs)
				{
					if (!medivac->getOrders().empty() && medivac->getOrders().front().ability_id == sc2::ABILITY_ID::EFFECT_HEAL && medivac->getOrders().front().target_unit_tag == injured->getTag())
					{
						isHealing = true;
						break;
					}
				}
				if (!isHealing)
				{
					injuredUnits[healthMissing] = injured;
				}
			}
			
		}
	}
	// In case it were really only medivacs
	if (justMedivacs)
	{
		Micro::SmartMove(rangedUnits, m_bot.Bases().getRallyPoint(),m_bot);
		return;
	}

	//Get effects like storm
	const std::vector<sc2::Effect> effects = m_bot.Observation()->GetEffects();

	// for each Unit
	for (auto & rangedUnit : rangedUnits)
	{
		//Don't stand in a storm etc
		bool fleeYouFools = false;
		for (const auto & effect : effects)
		{
			if (Util::isBadEffect(effect.effect_id))
			{
				const float radius = m_bot.Observation()->GetEffectData()[effect.effect_id].radius;
				for (const auto & pos : effect.positions)
				{
					Drawing::drawSphere(m_bot, pos, radius, sc2::Colors::Purple);
					const float dist = Util::Dist(rangedUnit->getPos(), pos);
					if (dist<radius + 2.0f)
					{
						sc2::Point2D fleeingPos;
						if (effect.positions.size() == 1)
						{
							if (dist > 0)
							{
								fleeingPos = pos + Util::normalizeVector(rangedUnit->getPos() - pos, radius + 2.0f);
							}
							else
							{
								fleeingPos = pos + sc2::Point2D(0.1f,0.1f);
							}
						}
						else
						{
							const sc2::Point2D attackDirection = effect.positions.back() - effect.positions.front();
							//"Randomly" go right and left
							if (rangedUnit->getTag() % 2)
							{
								fleeingPos = rangedUnit->getPos() + Util::normalizeVector(sc2::Point2D(-attackDirection.x, attackDirection.y), radius + 2.0f);
							}
							else
							{
								fleeingPos = rangedUnit->getPos() - Util::normalizeVector(sc2::Point2D(-attackDirection.x, attackDirection.y), radius + 2.0f);
							}
						}
						Micro::SmartMove(rangedUnit, fleeingPos, m_bot);
						fleeYouFools = true;
						break;
					}
				}
			}
			if (fleeYouFools)
			{
				break;
			}
		}
		if (fleeYouFools)
		{
			continue;
		}
		BOT_ASSERT(rangedUnit, "ranged unit is null");
		//
		bool breach = false;
		if (order.getType() == SquadOrderTypes::Defend && m_bot.Bases().getOccupiedBaseLocations(Players::Self).size() <= 2)
		{
			CUnits Bunker = m_bot.UnitInfo().getUnits(Players::Self, sc2::UNIT_TYPEID::TERRAN_BUNKER);
			if (Bunker.size() > 0)
			{
				const BaseLocation * base = m_bot.Bases().getPlayerStartingBaseLocation(Players::Self);
				const int dist = base->getGroundDistance(Bunker.front()->getPos());
				for (const auto & target : rangedUnitTargets)
				{
					if (dist > base->getGroundDistance(target->getPos()))
					{
						breach = true;
						break;
					}
				}
			}
		}


		// if the order is to attack or defend
		if (order.getType() == SquadOrderTypes::Attack || order.getType() == SquadOrderTypes::Defend || order.getType() == SquadOrderTypes::GuardDuty)
		{
			// find the best target for this rangedUnit
			//medivacs have the other ranged units as target.
			if (rangedUnit->getUnitType() == sc2::UNIT_TYPEID::TERRAN_MEDIVAC)
			{
				//find the nearest enemy
				CUnit_ptr nearestEnemy = nullptr;
				float minDistTarget = std::numeric_limits<float>::max();
				for (const auto & target : rangedUnitTargets)
				{
					if (target->isAlive() && rangedUnit->canHitMe(target))
					{
						float dist = Util::Dist(rangedUnit->getPos(), target->getPos());
						if (dist<target->getAttackRange() && minDistTarget > dist)
						{
							nearestEnemy = target;
							minDistTarget = dist;
						}
					}
				}
				if (injuredUnits.size()>0)
				{
					CUnit_ptr mostInjured = (injuredUnits.rbegin())->second;
					if (nearestEnemy && Util::Dist(rangedUnit->getPos(), nearestEnemy->getPos()) < Util::Dist(mostInjured->getPos(), nearestEnemy->getPos()))
					{
						Micro::SmartCDAbility(rangedUnit, sc2::ABILITY_ID::EFFECT_MEDIVACIGNITEAFTERBURNERS,m_bot);
						sc2::Point2D targetPos = rangedUnit->getPos();
						sc2::Point2D runningVector = Util::normalizeVector(rangedUnit->getPos() - nearestEnemy->getPos(), nearestEnemy->getAttackRange(rangedUnit) + 2);
						targetPos += runningVector;
						Micro::SmartMove(rangedUnit, targetPos, m_bot);
					}
					else if (rangedUnit->getOrders().empty() || rangedUnit->getOrders()[0].ability_id != sc2::ABILITY_ID::EFFECT_HEAL)
					{
						if (Util::Dist(rangedUnit->getPos(), mostInjured->getPos()) > 4.0f)
						{
							Micro::SmartCDAbility(rangedUnit, sc2::ABILITY_ID::EFFECT_MEDIVACIGNITEAFTERBURNERS, m_bot);
							Micro::SmartMove(rangedUnit, mostInjured, m_bot);
						}
						else
						{

							Micro::SmartAbility(rangedUnit, sc2::ABILITY_ID::EFFECT_HEAL, mostInjured, m_bot);
							injuredUnits.erase(std::prev(injuredUnits.end())); //no idea why rbegin is not working
						}
					}
				}
				else
				{
					if (frontSoldier && (rangedUnit->getOrders().empty() ||  rangedUnit->getOrders()[0].target_unit_tag && rangedUnit->getOrders()[0].target_unit_tag != frontSoldier->getTag()))
					{
						Micro::SmartMove(rangedUnit, frontSoldier,m_bot);
					}
				}
			}
			else
			{
				//Handling disrupter shots
				if (!rangedUnit->isFlying())
				{
					const CUnits disruptorShots = m_bot.UnitInfo().getUnits(Players::Enemy, sc2::UNIT_TYPEID::PROTOSS_DISRUPTORPHASED);
					bool fleeYouFoolsPart2 = false;
					for (const auto & shot : disruptorShots)
					{
						const float dist = Util::Dist(rangedUnit->getPos(), shot->getPos());
						if (dist < 5.0f)
						{
							sc2::Point2D fleeingPos;
							const sc2::Point2D pos = rangedUnit->getPos();
							if (dist > 0)
							{
								fleeingPos = pos + Util::normalizeVector(pos - shot->getPos());
							}
							else
							{
								fleeingPos = pos + sc2::Point2D(0.1f, 0.1f);
							}
							Micro::SmartMove(rangedUnit, fleeingPos, m_bot);
							fleeYouFoolsPart2 = true;
							break;
						}
					}
					if (fleeYouFoolsPart2)
					{
						continue;
					}
				}
				
				//Search for target
				if (!rangedUnitTargets.empty() || (order.getType() == SquadOrderTypes::Defend && Util::Dist(rangedUnit->getPos(), order.getPosition()) > 7))
				{
					//CUnit_ptr target = getTarget(rangedUnit, rangedUnitTargets);
					//Highest prio in range target, in sight target, visible target
					const auto targets = getTarget(rangedUnit, sortedUnitTargets);
					if (targets[2].second)
					{
						Drawing::drawLine(m_bot, rangedUnit->getPos(), targets[2].second->getPos(), sc2::Colors::Green);
					}
					if (targets[1].second)
					{
						Drawing::drawLine(m_bot, rangedUnit->getPos(), targets[1].second->getPos(), sc2::Colors::Yellow);
					}
					if (targets[0].second)
					{
						Drawing::drawLine(m_bot, rangedUnit->getPos(), targets[0].second->getPos(), sc2::Colors::Red);
					}
					//if something goes wrong
					if (!targets[2].second)
					{
						//This can happen with vikings
						if (frontSoldier && (rangedUnit->getOrders().empty() || rangedUnit->getOrders().front().target_unit_tag && rangedUnit->getOrders().front().target_unit_tag != frontSoldier->getTag()))
						{
							Micro::SmartMove(rangedUnit, frontSoldier,m_bot);
						}
						continue;
					}
					if (order.getType() == SquadOrderTypes::Defend)
					{
						CUnits Bunker = m_bot.UnitInfo().getUnits(Players::Self, sc2::UNIT_TYPEID::TERRAN_BUNKER);
						if (Bunker.size() > 0 && Bunker.front()->isCompleted())
						{
							if (Bunker.front()->getCargoSpaceTaken() != Bunker.front()->getCargoSpaceMax())
							{
								if (!targets[0].second || Util::Dist(rangedUnit->getPos(), Bunker.front()->getPos()) < Util::Dist(rangedUnit->getPos(), targets[0].second->getPos()))
								{
									Micro::SmartRightClick(rangedUnit, Bunker.front(), m_bot);
									Micro::SmartAbility(Bunker.front(), sc2::ABILITY_ID::LOAD, rangedUnit, m_bot);
									continue;
								}
							}
							if (m_bot.Bases().getOccupiedBaseLocations(Players::Self).size() <= 2)
							{
								if (!breach)
								{
									if (targets[1].second && Util::Dist(targets[1].second->getPos(), Bunker.front()->getPos()) > 5.5f)
									{
										if (Util::Dist(targets[1].second->getPos(),rangedUnit->getPos())<=targets[1].second->getAttackRange(rangedUnit))
										{
											if (targets[0].second && rangedUnit->getWeaponCooldown())
											{
												Micro::SmartAttackUnit(rangedUnit, targets[0].second, m_bot);
											}
											else
											{
												sc2::Point2D retreatPos = m_bot.Bases().getNewestExpansion(Players::Self);
												Micro::SmartMove(rangedUnit, retreatPos, m_bot);
											}
											continue;
										}
										else
										{
											Micro::SmartAbility(rangedUnit, sc2::ABILITY_ID::HOLDPOSITION, m_bot);
											continue;
										}
									}
								}
							}
						}
					}
					//We only need fancy micro if we are in range and its not a building
					if (rangedUnit->isSelected())
					{
						int a = 1;
					}
					if (targets[0].second)
					{
						//if the target in range is really the best target
						if (targets[0].first == targets[1].first)
						{
							if (targets[0].second->isBuilding())
							{
								targetsAttackedBy[targets[0].second].push_back(rangedUnit);
							}
							else
							{
								Micro::SmartKiteTarget(rangedUnit, targets[0].second, m_bot);
							}
						}
						else
						{
							if (rangedUnit->getWeaponCooldown())
							{
								targetsMovedTo[targets[1].second].push_back(rangedUnit);
							}
							else
							{
								targetsAttackedBy[targets[0].second].push_back(rangedUnit);
							}
						}
					}
					else if (targets[1].second)
					{
						targetsAttackedBy[targets[1].second].push_back(rangedUnit);
					}
					else
					{
						targetsAttackedBy[targets[2].second].push_back(rangedUnit);
					}
				}
				// if there are no targets
				else
				{
					// if we're not near the order position
					if (Util::Dist(rangedUnit->getPos(), order.getPosition()) > 4.0f)
					{
						// move to it
						moveToPosition.push_back(rangedUnit);
					}
				}
			}
		}
	}
	//Grouped by target attack command
	for (auto & t : targetsAttackedBy)
	{
		Micro::SmartAttackUnit(t.second, t.first, m_bot);
	}
	for (auto & t : targetsMovedTo)
	{
		Micro::SmartMove(t.second, t.first->getPos(), m_bot);
	}
	//Grouped by  position Move command
	if (moveToPosition.size() > 0)
	{
		Micro::SmartAttackMove(moveToPosition, order.getPosition(), m_bot);
	}
}

// get a target for the ranged unit to attack
const CUnit_ptr RangedManager::getTarget(const CUnit_ptr rangedUnit, const CUnits & targets)
{
	BOT_ASSERT(rangedUnit, "null melee unit in getTarget");
	int highPriorityFar = 0;
	int highPriorityNear = 0;
	double closestDist = std::numeric_limits<double>::max();
	double lowestHealth = std::numeric_limits<double>::max();
	CUnit_ptr closestTargetOutsideRange = nullptr;
	CUnit_ptr weakestTargetInsideRange = nullptr;
	// for each target possiblity
	// We have three levels: in range, in sight, somewhere.
	// We want to attack the weakest/highest prio target in range
	// If there is no in range, we want to attack one in sight,
	// else the one with highest prio.
	for (const auto & targetUnit : targets)
	{
		BOT_ASSERT(targetUnit, "null target unit in getTarget");
		//Ignore dead units or ones we can not hit
		if (!targetUnit->isAlive())
		{
			continue;
		}
		const float range = rangedUnit->getAttackRange(targetUnit);
		int priority = getAttackPriority(targetUnit);
		const float distance = Util::Dist(rangedUnit->getPos(), targetUnit->getPos());
		if (distance > range)
		{
			// If in sight we just add 20 to prio. This should make sure that a unit in sight has higher priority than any unit outside of range
			if (distance <= rangedUnit->getSightRange())
			{
				priority += 20;
			}
			// if it's a higher priority, or it's closer, set it
			if (!closestTargetOutsideRange || (priority > highPriorityFar) || (priority == highPriorityFar && distance < closestDist))
			{
				if (targetUnit->canHitMe(rangedUnit)) //Costly call
				{
					closestDist = distance;
					highPriorityFar = priority;
					closestTargetOutsideRange = targetUnit;
				}
			}
		}
		else
		{
			if (!weakestTargetInsideRange || (priority > highPriorityNear) || (priority == highPriorityNear && targetUnit->getHealth() < lowestHealth))
			{
				if (targetUnit->canHitMe(rangedUnit)) //Costly call
				{
					lowestHealth = targetUnit->getHealth();
					highPriorityNear = priority;
					weakestTargetInsideRange = targetUnit;
				}
			}
		}

	}
	return weakestTargetInsideRange&&highPriorityNear>1 ? weakestTargetInsideRange: closestTargetOutsideRange;
}

// get the attack priority of a type in relation to a zergling
int RangedManager::getAttackPriority(const CUnit_ptr & unit)
{
	BOT_ASSERT(unit, "null unit in getAttackPriority");

	if (unit->isCombatUnit())
	{
		if (!unit->isVisible())
		{
			return 3;
		}
		if (unit->getUnitType() == sc2::UNIT_TYPEID::ZERG_BANELING)
		{
			return 9;
		}
		if (unit->getUnitType() == sc2::UNIT_TYPEID::ZERG_LURKERMPBURROWED)
		{
			return 8;
		}
		if (unit->isType(sc2::UNIT_TYPEID::ZERG_INFESTEDTERRANSEGG) || unit->isType(sc2::UNIT_TYPEID::ZERG_BROODLING) || unit->isType(sc2::UNIT_TYPEID::ZERG_INFESTORTERRAN) || unit->isType(sc2::UNIT_TYPEID::PROTOSS_INTERCEPTOR))
		{
			return 6;
		}
		return 7;
	}
	if (unit->getUnitType() == sc2::UNIT_TYPEID::PROTOSS_PHOTONCANNON || unit->getUnitType() == sc2::UNIT_TYPEID::ZERG_SPINECRAWLER || unit->isType(sc2::UNIT_TYPEID::TERRAN_BUNKER))
	{
		return 7;
	}
	if (unit->isWorker())
	{
		return 7;
	}
	if (unit->getUnitType() == sc2::UNIT_TYPEID::PROTOSS_PYLON || unit->getUnitType() == sc2::UNIT_TYPEID::ZERG_SPORECRAWLER || unit->getUnitType() == sc2::UNIT_TYPEID::TERRAN_MISSILETURRET)
	{
		return 5;
	}
	if (unit->isTownHall())
	{
		return 4;
	}
	return 1;
}

std::map<float, CUnits, std::greater<float>> RangedManager::getAttackPriority(const CUnits & enemies)
{
	std::map<float, CUnits, std::greater<float>> sortedEnemies;
	const CUnits & rangedUnits = getUnits();
	const float numUnits = static_cast<float>(rangedUnits.size());
	for (const auto & enemy : enemies)
	{
		uint32_t OpportunityLevel = 0;
		for (const auto & unit : rangedUnits)
		{
			if (enemy->canHitMe(unit))
			{
				const float dist = Util::Dist(unit->getPos(), enemy->getPos());
				if (dist <= unit->getAttackRange())
				{
					//One point for being in range
					++OpportunityLevel;
					if (unit->getEngagedTargetTag() == enemy->getTag())
					{
						//Second point for being already a target
						++OpportunityLevel;
					}
				}
			}
		}
		//						basic priority	+	alpha*how many are already attacking it	+	beta*health
		const float priority = getAttackPriority(enemy) + 0.5f*(static_cast<float>(OpportunityLevel) / (2.0f*numUnits)) + 0.5f*(1-enemy->getHealth()/enemy->getHealthMax());
		sortedEnemies[priority].push_back(enemy);
	}
	return sortedEnemies;
}

std::vector<std::pair<float, CUnit_ptr>> RangedManager::getTarget(const CUnit_ptr & unit, const std::map<float, CUnits, std::greater<float>> & sortedEnemies)
{
	std::vector<std::pair<float, CUnit_ptr>> targets = { {-1.0f,nullptr},{ -1.0f,nullptr },{ -1.0f,nullptr } };
	const float unitAttackRange = unit->getAttackRange();
	const float unitSightRange = unit->getSightRange();

	for (const auto & enemies : sortedEnemies)
	{
		const float priority = enemies.first;
		for (const auto & enemy : enemies.second)
		{
			if (enemy->canHitMe(unit))
			{
				const float dist = Util::Dist(enemy->getPos(), unit->getPos());

				if (targets[2].first <= priority)
				{
					if (targets[2].second)
					{
						const float distOld = Util::Dist(targets[2].second->getPos(), unit->getPos());
						if (distOld > dist)
						{
							targets[2].second = enemy;
						}
					}
					else
					{
						targets[2] = { enemies.first,enemy };
					}
				}
				if (targets[1].first <= priority && dist < unitSightRange) // < because sometime just on the edge they are still invisible
				{
					if (targets[1].second)
					{
						const float distOld = Util::Dist(targets[1].second->getPos(), unit->getPos());
						if (distOld > dist)
						{
							targets[1].second = enemy;
						}
					}
					else
					{
						targets[1] = { enemies.first,enemy };
					}
				}
				if (targets[0].first <= priority && dist <= unitAttackRange)
				{
					if (targets[0].second)
					{
						const float distOld = Util::Dist(targets[0].second->getPos(), unit->getPos());
						if (distOld > dist)
						{
							targets[0].second = enemy;
						}
					}
					else
					{
						targets[0] = { enemies.first,enemy };
					}
				}
			}
		}
		if (targets[0].second)
		{
			break;
		}
	}
	return targets;
}