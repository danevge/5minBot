#include "MapTools.h"
#include "Util.h"
#include "CCBot.h"

#include <iostream>
#include <sstream>
#include <fstream>

const size_t LegalActions = 4;
const int actionX[LegalActions] ={1, -1, 0, 0};
const int actionY[LegalActions] ={0, 0, 1, -1};

typedef std::vector<std::vector<bool>> vvb;
typedef std::vector<std::vector<int>>  vvi;
typedef std::vector<std::vector<float>>  vvf;

// constructor for MapTools
MapTools::MapTools(CCBot & bot)
    : m_bot     (bot)
    , m_width   (0)
    , m_height  (0)
    , m_maxZ    (0.0f)
    , m_frame   (0)
{

}

void MapTools::onStart()
{
    m_width  = m_bot.Observation()->GetGameInfo().width;
    m_height = m_bot.Observation()->GetGameInfo().height;

    m_walkable       = vvb(m_width, std::vector<bool>(m_height, true));
    m_buildable      = vvb(m_width, std::vector<bool>(m_height, false));
    m_ramp = vvb(m_width, std::vector<bool>(m_height, false));
    m_lastSeen       = vvi(m_width, std::vector<int>(m_height, 0));
    m_sectorNumber   = vvi(m_width, std::vector<int>(m_height, 0));
    m_terrainHeight  = vvf(m_width, std::vector<float>(m_height, 0.0f));

    // Set the boolean grid data from the Map
    for (size_t x(0); x < m_width; ++x)
    {
        for (size_t y(0); y < m_height; ++y)
        {
            m_buildable[x][y]   = Util::Placement(m_bot.Observation()->GetGameInfo(), sc2::Point2D(x+0.5f, y+0.5f));
            m_walkable[x][y]    = m_buildable[x][y] || Util::Pathable(m_bot.Observation()->GetGameInfo(), sc2::Point2D(x+0.5f, y+0.5f));
            m_terrainHeight[x][y]   = Util::TerainHeight(m_bot.Observation()->GetGameInfo(), sc2::Point2D(x+0.5f, y+0.5f));
			m_ramp[x][y] = m_walkable[x][y] || !m_buildable[x][y];
        }
    }
    for (auto & unit : m_bot.Observation()->GetUnits(sc2::Unit::Alliance::Neutral))
    {
        m_maxZ = std::max(unit->pos.z, m_maxZ);
    }

    computeConnectivity();
}

void MapTools::onFrame()
{
    m_frame++;

    for (int x=0; x<m_width; ++x)
    {
        for (int y=0; y<m_height; ++y)
        {
            if (isVisible(sc2::Point2D((float)x, (float)y)))
            {
                m_lastSeen[x][y] = m_frame;
            }
        }
    }

    draw();
}

void MapTools::computeConnectivity()
{
    // the fringe data structe we will use to do our BFS searches
    std::vector<sc2::Point2D> fringe;
    fringe.reserve(m_width*m_height);
    int sectorNumber = 0;

    // for every tile on the map, do a connected flood fill using BFS
    for (int x=0; x<m_width; ++x)
    {
        for (int y=0; y<m_height; ++y)
        {
            // if the sector is not currently 0, or the map isn't walkable here, then we can skip this tile
            if (getSectorNumber(x, y) != 0 || !isWalkable(x, y))
            {
                continue;
            }

            // increase the sector number, so that walkable tiles have sectors 1-N
            sectorNumber++;

            // reset the fringe for the search and add the start tile to it
            fringe.clear();
            fringe.push_back(sc2::Point2D(x+0.5f, y+0.5f));
            m_sectorNumber[x][y] = sectorNumber;

            // do the BFS, stopping when we reach the last element of the fringe
            for (size_t fringeIndex=0; fringeIndex<fringe.size(); ++fringeIndex)
            {
                auto & tile = fringe[fringeIndex];

                // check every possible child of this tile
                for (size_t a=0; a<LegalActions; ++a)
                {
                    sc2::Point2D nextTile(tile.x + actionX[a], tile.y + actionY[a]);

                    // if the new tile is inside the map bounds, is walkable, and has not been assigned a sector, add it to the current sector and the fringe
                    if (isValid(nextTile) && isWalkable(nextTile) && (getSectorNumber(nextTile) == 0))
                    {
                        m_sectorNumber[(int)nextTile.x][(int)nextTile.y] = sectorNumber;
                        fringe.push_back(nextTile);
                    }
                }
            }
        }
    }
}

bool MapTools::isExplored(const sc2::Point2D & pos) const
{
    if (!isValid(pos)) { return false; }

    sc2::Visibility vis = m_bot.Observation()->GetVisibility(pos);
    return vis == sc2::Visibility::Fogged || vis == sc2::Visibility::Visible;
}

bool MapTools::isVisible(const sc2::Point2D & pos) const
{
    if (!isValid(pos)) { return false; }

    return m_bot.Observation()->GetVisibility(pos) == sc2::Visibility::Visible;
}

bool MapTools::isPowered(const sc2::Point2D & pos) const
{
    for (auto & powerSource : m_bot.Observation()->GetPowerSources())
    {
        if (Util::Dist(pos, powerSource.position) < powerSource.radius)
        {
            return true;
        }
    }

    return false;
}

float MapTools::terrainHeight(float x, float y) const
{
    return m_terrainHeight[(int)x][(int)y];
}

//int MapTools::getGroundDistance(const sc2::Point2D & src, const sc2::Point2D & dest) const
//{
//    return (int)Util::Dist(src, dest);
//}

int MapTools::getGroundDistance(const sc2::Point2D & src, const sc2::Point2D & dest) const
{
    if (_allMaps.size() > 50)
    {
        _allMaps.clear();
    }

    return getDistanceMap(dest).getDistance(src);
}

const DistanceMap & MapTools::getDistanceMap(const sc2::Point2D & tile) const
{
    std::pair<int, int> intTile((int)tile.x, (int)tile.y);

    if (_allMaps.find(intTile) == _allMaps.end())
    {
        _allMaps[intTile] = DistanceMap();
        _allMaps[intTile].computeDistanceMap(m_bot, tile);
    }

    return _allMaps[intTile];
}

int MapTools::getSectorNumber(int x, int y) const
{
    if (!isValid(x, y))
    {
        return 0;
    }

    return m_sectorNumber[x][y];
}

int MapTools::getSectorNumber(const sc2::Point2D & pos) const
{
    return getSectorNumber((int)pos.x, (int)pos.y);
}

bool MapTools::isValid(int x, int y) const
{
    return x >= 0 && y >= 0 && x < m_width && y < m_height;
}

bool MapTools::isValid(const sc2::Point2D & pos) const
{
    return isValid((int)pos.x, (int)pos.y);
}

void MapTools::draw() const
{
    sc2::Point2D camera = m_bot.Observation()->GetCameraPos();
    for (float x = camera.x - 16.0f; x < camera.x + 16.0f; ++x)
    {
        for (float y = camera.y - 16.0f; y < camera.y + 16.0f; ++y)
        {
            if (!isValid((int)x, (int)y))
            {
                continue;
            }

            if (m_bot.Config().DrawWalkableSectors)
            {
                std::stringstream ss;
                ss << getSectorNumber((int)x, (int)y);
                m_bot.Debug()->DebugTextOut(ss.str(), sc2::Point3D(x + 0.5f, y + 0.5f, m_maxZ + 0.1f), sc2::Colors::Yellow);
            }

            if (m_bot.Config().DrawTileInfo)
            {
                sc2::Color color = isWalkable((int)x, (int)y) ? sc2::Colors::Green : sc2::Colors::Red;
                if (isWalkable((int)x, (int)y) && !isBuildable((int)x, (int)y))
                {
                    color = sc2::Colors::Yellow;
                }

                drawSquare(x, y, x+1, y+1, color);
            }
        }
    }
}

void MapTools::drawLine(float x1, float y1, float x2, float y2, const sc2::Color & color) const
{
    m_bot.Debug()->DebugLineOut(sc2::Point3D(x1, y1, m_maxZ + 0.2f), sc2::Point3D(x2, y2, m_maxZ + 0.2f), color);
}

void MapTools::drawLine(const sc2::Point2D & min, const sc2::Point2D max, const sc2::Color & color) const
{
    m_bot.Debug()->DebugLineOut(sc2::Point3D(min.x, min.y, m_maxZ + 0.2f), sc2::Point3D(max.x, max.y, m_maxZ + 0.2f), color);
}

void MapTools::drawSquare(float x1, float y1, float x2, float y2, const sc2::Color & color) const
{
    m_bot.Debug()->DebugLineOut(sc2::Point3D(x1, y1, m_maxZ), sc2::Point3D(x1+1, y1, m_maxZ), color);
    m_bot.Debug()->DebugLineOut(sc2::Point3D(x1, y1, m_maxZ), sc2::Point3D(x1, y1+1, m_maxZ), color);
    m_bot.Debug()->DebugLineOut(sc2::Point3D(x1+1, y1+1, m_maxZ), sc2::Point3D(x1+1, y1, m_maxZ), color);
    m_bot.Debug()->DebugLineOut(sc2::Point3D(x1+1, y1+1, m_maxZ), sc2::Point3D(x1, y1+1, m_maxZ), color);
}

void MapTools::drawBox(float x1, float y1, float x2, float y2, const sc2::Color & color) const
{
    m_bot.Debug()->DebugBoxOut(sc2::Point3D(x1, y1, m_maxZ + 2.0f), sc2::Point3D(x2, y2, m_maxZ-5.0f), color);
}

void MapTools::drawBox(const sc2::Point2D & min, const sc2::Point2D max, const sc2::Color & color) const
{
    m_bot.Debug()->DebugBoxOut(sc2::Point3D(min.x, min.y, m_maxZ + 2.0f), sc2::Point3D(max.x, max.y, m_maxZ-5.0f), color);
}

void MapTools::drawSphere(const sc2::Point2D & pos, float radius, const sc2::Color & color) const
{
    m_bot.Debug()->DebugSphereOut(sc2::Point3D(pos.x, pos.y, m_maxZ), radius, color);
}

void MapTools::drawSphere(float x, float y, float radius, const sc2::Color & color) const
{
    m_bot.Debug()->DebugSphereOut(sc2::Point3D(x, y, m_maxZ), radius, color);
}

void MapTools::drawText(const sc2::Point2D & pos, const std::string & str, const sc2::Color & color) const
{
    m_bot.Debug()->DebugTextOut(str, sc2::Point3D(pos.x, pos.y, m_maxZ), color);
}

void MapTools::drawTextScreen(const sc2::Point2D & pos, const std::string & str, const sc2::Color & color) const
{
    m_bot.Debug()->DebugTextOut(str, pos, color);
}

bool MapTools::isConnected(int x1, int y1, int x2, int y2) const
{
    if (!isValid(x1, y1) || !isValid(x2, y2))
    {
        return false;
    }

    int s1 = getSectorNumber(x1, y1);
    int s2 = getSectorNumber(x2, y2);

    return s1 != 0 && (s1 == s2);
}

bool MapTools::isConnected(const sc2::Point2D & p1, const sc2::Point2D & p2) const
{
    return isConnected((int)p1.x, (int)p1.y, (int)p2.x, (int)p2.y);
}

bool MapTools::isBuildable(int x, int y) const
{
    if (!isValid(x, y))
    {
        return false;
    }

    return m_buildable[x][y];
}

bool MapTools::canBuildTypeAtPosition(int x, int y, sc2::UnitTypeID type) const
{
    return m_bot.Query()->Placement(m_bot.Data(type).buildAbility, sc2::Point2D((float)x, (float)y));
}

bool MapTools::isBuildable(const sc2::Point2D & tile) const
{
    return isBuildable((int)tile.x, (int)tile.y);
}

void MapTools::printMap()
{
    std::stringstream ss;
    for (int y(0); y < m_height; ++y)
    {
        for (int x(0); x < m_width; ++x)
        {
            ss << isWalkable(x, y);
        }

        ss << "\n";
    }

    std::ofstream out("map.txt");
    out << ss.str();
    out.close();
}

bool MapTools::isDepotBuildableTile(const sc2::Point2D & tile) const
{
    if (!isValid(tile))
    {
        return false;
    }

    return m_ramp[(int)tile.x][(int)tile.y];
}

bool MapTools::isWalkable(int x, int y) const
{
    if (!isValid(x, y))
    {
        return false;
    }

    return m_walkable[x][y];
}

bool MapTools::isWalkable(const sc2::Point2D & tile) const
{
    return isWalkable((int)tile.x, (int)tile.y);
}

int MapTools::width() const
{
    return m_width;
}

int MapTools::height() const
{
    return m_height;
}

const std::vector<sc2::Point2D> & MapTools::getClosestTilesTo(const sc2::Point2D & pos) const
{
    return getDistanceMap(pos).getSortedTiles();
}

const sc2::Point2D MapTools::getClosestWalkableTo(const sc2::Point2D & pos) const
{
	// get the precomputed vector of tile positions which are sorted closes to this location
	auto & closestToPos = m_bot.Map().getClosestTilesTo(pos);


	// iterate through the list until we've found a suitable location
	for (size_t i(0); i < closestToPos.size(); ++i)
	{
		auto & pos = closestToPos[i];

		if (isWalkable(pos))
		{
			return pos;
		}
	}
	return sc2::Point2D(0,0);
}


void MapTools::drawBoxAroundUnit(const UnitTag & unitTag, sc2::Color color) const
{
    const sc2::Unit * unit = m_bot.GetUnit(unitTag);

    if (!unit) { return; }

    sc2::Point3D p_min = unit->pos;
    p_min.x -= 2.0f;
    p_min.y -= 2.0f;
    p_min.z -= 2.0f;

    sc2::Point3D p_max = unit->pos;
    p_max.x += 2.0f;
    p_max.y += 2.0f;
    p_max.z += 2.0f;

    drawSquare(unit->pos.x - 2.0f, unit->pos.y - 2.0f, unit->pos.x + 2.0f, unit->pos.y + 2.0f, color);
}

void MapTools::drawSphereAroundUnit(const UnitTag & unitTag, sc2::Color color) const
{
    const sc2::Unit * unit = m_bot.GetUnit(unitTag);

    if (!unit) { return; }

    drawSphere(unit->pos, 1, color);
}

sc2::Point2D MapTools::getLeastRecentlySeenPosition() const
{
    int minSeen = std::numeric_limits<int>::max();
    sc2::Point2D leastSeen(0.0f, 0.0f);
    const BaseLocation * baseLocation = m_bot.Bases().getPlayerStartingBaseLocation(Players::Self);

    for (auto & tile : baseLocation->getClosestTiles())
    {
        BOT_ASSERT(isValid(tile), "How is this tile not valid?");

        int lastSeen = m_lastSeen[(int)tile.x][(int)tile.y];
        if (lastSeen < minSeen)
        {
            minSeen = lastSeen;
            leastSeen = tile;
        }
    }

    return leastSeen;
}

const sc2::Point2D MapTools::findNearestValidWalkable(const sc2::Point2D currentPos,const sc2::Point2D targetPos) const
{
	//Easiest case
	if (isWalkable(targetPos) && isValid(targetPos)) { return targetPos; }
	// ToDo: Find alternative Positions.

	return m_bot.Bases().getPlayerStartingBaseLocation(Players::Self)->getPosition();
}

sc2::Point2D MapTools::getWallPosition(sc2::UnitTypeID type) const
{
	sc2::Point2D bestPosition(0.0f, 0.0f);
	return bestPosition;
	float maxDist = 0;
	const BaseLocation *startBase=m_bot.Bases().getPlayerStartingBaseLocation(Players::Self);
	sc2::Point2D startLocation = startBase->getPosition();
	
	//if it is farer away than our exe we don't care
	sc2::Point2D expansionLocation = m_bot.Bases().getNextExpansion(Players::Self);
	float ExpDist = Util::Dist(startLocation, expansionLocation);
	//if it is not the same height as the bases height
	float startBaseHeight = terrainHeight(startLocation.x, startLocation.y);
	for (int x(0); x < m_width; ++x)
	{
		for (int y(0); y < m_height; ++y)
		{
			//We do not need to check anything if it is not the correct height anyway.
			float height = terrainHeight(x, y);
			if (height == startBaseHeight)
			{
				//We do not need to check anything else if it is too far away anyway.
				sc2::Point2D position = sc2::Point2D(x + 0.5f, y + 0.5f);
				float distance = Util::Dist(startLocation, position);
				if (maxDist<distance && distance < ExpDist)
				{
					if (m_bot.Query()->Placement(sc2::ABILITY_ID::BUILD_SUPPLYDEPOT, position))
					{
						if (isNextToRamp(x,y))
						{
							bestPosition = position;
							maxDist = distance;
						}
					}
				}
			}
		}
	}
	return bestPosition;
	
}

bool MapTools::isNextToRamp(int x, int y) const
{
	if (m_ramp[x][y + 1]) { return true; } //above
	if (m_ramp[x][y - 1]) { return true; }//Below
	if (m_ramp[x - 1][y]) { return true; }//left
	if (m_ramp[x + 1][y]) { return true; }//right
	return false;
}

const sc2::Point2D MapTools::getClosestBorderPoint(sc2::Point2D pos,int margin) const
{
	int x_min = m_bot.Observation()->GetGameInfo().playable_min.x + margin;
	int x_max = m_bot.Observation()->GetGameInfo().playable_max.x - margin;
	int y_min = m_bot.Observation()->GetGameInfo().playable_min.y + margin;
	int y_max = m_bot.Observation()->GetGameInfo().playable_max.y - margin;
	if (pos.x < x_max - pos.x)
	{
		if (pos.y < y_max - pos.y)
		{
			if (pos.x < pos.y)
			{
				return sc2::Point2D(x_min, pos.y);
			}
			else
			{
				return sc2::Point2D(pos.x, y_min);
			}
		}
		else
		{
			if (pos.x < y_max - pos.y)
			{
				return sc2::Point2D(x_min, pos.y);
			}
			else
			{
				return sc2::Point2D(pos.x, y_max);
			}
		}
	}
	else
	{
		if (pos.y < y_max - pos.y)
		{
			if (x_max - pos.x < pos.y)
			{
				return sc2::Point2D(x_max, pos.y);
			}
			else
			{
				return sc2::Point2D(pos.x, y_min);
			}
		}
		else
		{
			if (x_max - pos.x < y_max-pos.y)
			{
				return sc2::Point2D(x_max, pos.y);
			}
			else
			{
				return sc2::Point2D(pos.x, y_max);
			}
		}
	}
}