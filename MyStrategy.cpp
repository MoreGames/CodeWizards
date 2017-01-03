#include "MyStrategy.h"

#define PI 3.14159265358979323846
#define _USE_MATH_DEFINES

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <ratio>
#include <tuple>
#include <unordered_map>
#include <queue>

using namespace model;
using namespace std;
using namespace std::chrono;
using std::priority_queue;

#include <iomanip>
#include <unordered_set>
#include <array>
#include <utility>

using std::unordered_map;
using std::unordered_set;
using std::array;
using std::queue;
using std::pair;
using std::tuple;
using std::tie;
using std::string;
#include <fstream>
using std::ofstream;

#include <functional>
#include "tileadaptor.hpp"


// quick and dirty file: variables
const int debug = 0;
const int timeDebug = 0;
const int fileDebug = 0;
int tick = 0;
vector<Building> enemyBuildingsGlobal;
Building homeBase;
const double spacingBuffer = 1;
const double attackRangeThreshold = 0.5;
vector<Bonus> bonuses;
const double lowLifeFactor = 0.63;
const double maxOverallStep = 8.1;
const double stepBackFactor = 0.7;
const double minimumSaveDistance = 92; 
bool ignoreEverything = false;
bool bonusTarget = false;
int timeToNextBonusAppearance = 2501;

int activeSkillLevel = 0;
int activeSkills[25] = { 0 };
//vector<int> skillLevels;

high_resolution_clock::time_point allTime;

#define SIZEMAP 100
int scale = 4000. / SIZEMAP;
char map[SIZEMAP][SIZEMAP];
bool mapUpdated = false;
//Instantiating our path adaptor
//passing the map size and a lambda that return false if the tile is a wall
TileAdaptor adaptor({ SIZEMAP, SIZEMAP }, [](const Vectori& vec) {return map[vec.x][vec.y] == ' '; });
//This is a bit of an exageration here for the weight, but it did make my performance test go from 8s to 2s
Pathfinder pathfinder(adaptor, 100.f /*weight*/);

enum TargetType {
	_TARGET_UNKNOWN_ = -1,
	TARGET_ENEMY_BASE = 0,
	TARGET_BONUS = 1,
	TARGET_TOWER = 2,
	TARGET_WIZARD = 3,
	TARGET_MINION = 4,
	TARGET_HOME_BASE = 5,
	TARGET_DANGER_ZONE = 6,
	TARGET_PROJECTILE = 7
};

class Point : public Unit {
public:
	Point() : Unit(-1, 0.0, 0.0, 0.0, 0.0, 0.0, _FACTION_UNKNOWN_) { };
	Point(double x, double y) : Unit(-1, x, y, 0.0, 0.0, 0.0, _FACTION_UNKNOWN_) { };
	Point(double x, double y, double angle) : Unit(-1, x, y, 0.0, 0.0, angle, _FACTION_UNKNOWN_) { };
	Point(const Unit& unit) : Unit(unit) { };

	double getAbsoluteAngleTo(double x, double y) const {
		return atan2(y - this->getY(), x - this->getX());
	}
	double getAbsoluteAngleTo(const Unit& unit) const {
		return getAbsoluteAngleTo(unit.getX(), unit.getY());
	}
	double getAngleGrad() const {
		return (this->getAngle() / PI * 180);
	}
};
class AStarPoint : public CircularUnit {
public:
	AStarPoint() : CircularUnit(-1, 0.0, 0.0, 0.0, 0.0, 0.0, _FACTION_UNKNOWN_, 0) { };
	AStarPoint(const Point& p, double radius) : CircularUnit(p.getId(), p.getX(), p.getY(), p.getSpeedX(), p.getSpeedY(), p.getAngle(), p.getFaction(), radius) { };
	AStarPoint(const CircularUnit& unit) : CircularUnit(unit) { };

	bool operator==(const AStarPoint& other) const {
		int x1 = (int)(this->getX() * 1000);
		int y1 = (int)(this->getY() * 1000);
		int x2 = (int)(other.getX() * 1000);
		int y2 = (int)(other.getY() * 1000);
		return (x1 == x2 && y1 == y2);
	}
	bool operator==(const Point& other) const {
		int x1 = (int)(this->getX() * 1000);
		int y1 = (int)(this->getY() * 1000);
		int x2 = (int)(other.getX() * 1000);
		int y2 = (int)(other.getY() * 1000);
		return (x1 == x2 && y1 == y2);
	}
	bool operator<(const AStarPoint& other) const {
		return ((int)(this->getX()*1000)<(int)(other.getX() * 1000) || (!((int)(other.getX() * 1000)<(int)(this->getX() * 1000)) && (int)(this->getY() * 1000)<(int)(other.getY() * 1000)));
	}
};
class MovePoint : public Point {
private:
	double speed;
	double strafe;
	double angleRelative;
public:
	MovePoint() : Point(), speed(-1), strafe(-1) { };
	MovePoint(double x, double y, double speed, double strafe, double angle, double dist, double angleRelative) :
		Point(x, y, angle), speed(speed), strafe(strafe), angleRelative(angleRelative){ };

	double getSpeed() const {
		return speed;
	}
	double getStrafe() const {
		return strafe;
	}
	double getAngleRelative() const {
		return angleRelative;
	}
};
template<class T>
class UnitDistance {
public:
	T unit;
	double distanceToSelf;
	UnitDistance(const T& unit, double distanceToSelf) : unit(unit), distanceToSelf(distanceToSelf) {
	}

	bool operator < (const UnitDistance& p) const {
		return (this->distanceToSelf < p.distanceToSelf);
	}
};
class Action {
public:
	TargetType actionTarget;
	Point actionPoint;
	Wizard actionWizard;
	Minion actionMinion;
	Building actionBuilding;
	bool actionInAttackRange;
	bool actionInStaffRange;
	string actionDescription;
	TargetType moveTarget;
	Point movePoint;
	string moveDescription;

	Action() : actionTarget(_TARGET_UNKNOWN_), actionPoint(), actionInAttackRange(false), actionInStaffRange(false), actionDescription(""), moveTarget(_TARGET_UNKNOWN_), movePoint(), moveDescription("") {};
};

int globalLane = 0;
//vector<vector<Point>> globalPath = { 
//	{ Point(100,3900), Point(50,2693.26), Point(350,1656.75), Point(400,800), Point(4000 - 2312.13,4000 - 3950), Point(4000 - 1370.66,4000 - 3650), Point(3600,400) },
//	{ Point(100,3900), Point(902.613,2768.1), Point(1929.29,2400), Point(4000 - 1929.29,4000 - 2400), Point(4000 - 902.613,4000 - 2768.1), Point(3600,400) },
//	{ Point(100,3900), Point(1370.66,3650), Point(2312.13,3950), Point(3200,3600), Point(4000 - 350,4000 - 1656.75), Point(4000 - 50,4000 - 2693.26), Point(3600,400) } };
// TODO Enemy Base final Step
vector<vector<Point>> globalPath = { 
	{ Point(100,3900), Point(200,2693.26), Point(200,1656.75), Point(200,1200), Point(200,1000), Point(200,800), Point(235,600), Point(360,390), Point(540,260), Point(800,200), Point(4000 - 2312.13,200), Point(4000 - 1370.66,200),Point(3100,200) },
	{ Point(100,3900), Point(3200,800) },
	{ Point(100,3900), Point(1370.66,3800), Point(2312.13,3800), Point(2800,3800), Point(3000,3800), Point(3200,3800), Point(3400,3765), Point(3610,3640), Point(3740,3460), Point(3800,3200), Point(3800,4000 - 1656.75), Point(3800,4000 - 2693.26), Point(3800,900) } };

TargetType getTargetTypeGlobalPath(int index) {
	if (index == 0) return TARGET_HOME_BASE;
	if (index == 1 || index == 2 || index == 3 || index == 4) return TARGET_TOWER;
	if (index == 5) return TARGET_ENEMY_BASE;
}
int nextGlobalPathIndex(const Wizard& self, int lane) {

	int lastWaypointIndex = globalPath[lane].size() - 1;
	Point lastWaypoint = globalPath[lane][lastWaypointIndex];

	for (int waypointIndex = 0; waypointIndex < lastWaypointIndex; ++waypointIndex) {
		Point waypoint = globalPath[lane][waypointIndex];

		if (waypoint.getDistanceTo(self) <= 150) {
			return (waypointIndex + 1);
		}

		if (lastWaypoint.getDistanceTo(waypoint) < lastWaypoint.getDistanceTo(self)) {
			return waypointIndex;
		}
	}

	return lastWaypointIndex;
}
int previousGlobalPathIndex(const Wizard& self, int lane) {
	Point firstWaypoint = globalPath[lane][0];

	for (int waypointIndex = globalPath[lane].size() - 1; waypointIndex > 0; --waypointIndex) {
		Point waypoint = globalPath[lane][waypointIndex];

		if (waypoint.getDistanceTo(self) <= 150) {
			return waypointIndex - 1;
		}

		if (firstWaypoint.getDistanceTo(waypoint) < firstWaypoint.getDistanceTo(self)) {
			return waypointIndex;
		}
	}

	return 0;
}

void checkGlobalLane(const Wizard& self, const World& world) {
	if (self.getId() == 3 || self.getId() == 8) {
		int wizardsPerLane[3] = { 0,0,0 };

		auto getLane = [](int angle) {
			if (angle > 165 / 180 * PI) {
				return 0;
			} else if (angle < 105 / 180 * PI) {
				return 2;
			} else {
				return 1;
			}
		};

		for (const auto& w : world.getWizards()) {
			if (w.getFaction() != self.getFaction()) {
				wizardsPerLane[getLane(w.getAngle())] += 1;
			}
		}

		if (wizardsPerLane[1] > 3) {
			globalLane = 1;
		} else {
			globalLane = 0;
		}

	}
}
void setGlobalLane(const Wizard& self, const World& world) {

	if (self.getId() == 1 || self.getId() == 6)
		globalLane = 0;
	else if (self.getId() == 2 || self.getId() == 7)
		globalLane = 1;
	else if (self.getId() == 3 || self.getId() == 8)
		globalLane = 1;
	else if (self.getId() == 4 || self.getId() == 9)
		globalLane = 1;
	else if (self.getId() == 5 || self.getId() == 10)
		globalLane = 2;
	else
		globalLane = 1;
	return;

}

// helper functions
//-----------------
// angle in game sense, clock wise
Point getPoint(double x, double y, double angle, double distance) {
	double xp = x + distance * cos(-angle);
	double yp = y - distance * sin(-angle);
	xp = max(min(xp, 3960.), 40.);
	yp = max(min(yp, 3960.), 40.);
	return Point(xp, yp);
}
Point getPoint(const Unit& unit, double angle, double distance) {
	return getPoint(unit.getX(), unit.getY(), angle, distance);
}
Point getPoint(const Unit& unit, double distance) {
	return getPoint(unit.getX(), unit.getY(), unit.getAngle(), distance);
}
// geometry
bool isLineWithinCircle(const Unit& a, const Unit& center, double radius) {
	double d = a.getDistanceTo(center);
	if (d <= radius) {
		return true;
	}
	return false;
}
bool isLineCircleIntersectionPoint(const Unit& a, const Unit& b, const Unit& center, double radius) {
	// http://math.stackexchange.com/questions/228841/how-do-i-calculate-the-intersections-of-a-straight-line-and-a-circle
	double m = (b.getY() - a.getY()) / (b.getX() - a.getX());
	double c = a.getY() - m*a.getX();
	double p = center.getX();
	double q = center.getY();

	double A = m*m + 1;
	double B = 2 * (m*c - m*q - p);
	double C = q*q - radius*radius + p*p - 2 * c*q + c*c;

	double root = (B*B - 4 * A*C);
	if (root < 0) {
		return false;
	}
	else if (root == 0) {
		double x = -B / (2 * A);

		double d = b.getX() - a.getX();
		double dx = x - a.getX();
		if (d < 0) {
			d *= -1;
			dx *= -1;
		}
		if (d >= dx && dx >= 0) {
			return true;
		}
		return false;

	}
	else {
		double x = -B / (2 * A);
		double sqrtroot = sqrt(root) / (2 * A);
		double x1 = x + sqrtroot;
		double x2 = x - sqrtroot;

		double d = b.getX() - a.getX();
		double dx1 = x1 - a.getX();
		double dx2 = x2 - a.getX();
		if (d < 0) {
			d *= -1;
			dx1 *= -1;
			dx2 *= -1;
		}
		if ((d >= dx1 && dx1 >= 0) || (d >= dx2 && dx2 >= 0)) {
			return true;
		}
		return false;
	}
}

// output
ostream &operator<<(ostream &stream, const Point& point) {
	return stream << "x:" << point.getX() << " y:" << point.getY() << " angle:" << point.getAngleGrad();
}
ostream &operator<<(ostream &stream, const MovePoint& point) {
	return stream << "x:" << point.getX() << " y:" << point.getY() << " speed:" << point.getSpeed() << " strafe:" << point.getStrafe() << " angle:" << point.getAngleGrad();
}
ostream &operator<<(ostream &stream, const Action& action) {
	return stream << "actionTarget " << action.actionTarget << " actionPoint " << action.actionPoint << " actionDescription " << action.actionDescription << " moveTarget " << action.moveTarget << " movePoint " << action.movePoint << " moveDescription " << action.moveDescription;
}
ostream &operator<<(ostream &stream, const Unit& unit) {
	return stream << "id:" << unit.getId()  << " x:" << unit.getX() << " y:" << unit.getY() << " angle:" << (unit.getAngle() / PI * 180);
}
ostream &operator<<(ostream &stream, const Move& move) {
	return stream << "speed:" << move.getSpeed() << " strafeSpeed:" << move.getStrafeSpeed() << " turn:" << (move.getTurn() / PI * 180) << 
		" action:" << move.getAction() << " castAngle:" << move.getCastAngle() << " minCastDist:" << move.getMinCastDistance() << " maxCastDist:" << move.getMaxCastDistance();
}
ostream &operator<<(ostream &stream, const Wizard& self) {
	return stream << "id:" << self.getId() << " getX:" << self.getX() << " getY:" << self.getY() << " getTurn:" << (self.getAngle() / PI * 180) << " getSpeedX:" << self.getSpeedX() << " getSpeedY:" << self.getSpeedY();
}

// inits and tick updates

// enemy buildings
void initEnemyBuildings(const Wizard& self, const World& world) {
	Faction oppFaction = FACTION_ACADEMY;
	if (self.getFaction() == FACTION_ACADEMY) {
		oppFaction = FACTION_RENEGADES;
	}

	for (const auto &building : world.getBuildings()) {
		if (building.getFaction() == self.getFaction()) {
			Building b = { (long long)0, 4000-building.getX(), 4000-building.getY(), 0, 0, 0, oppFaction,
				building.getRadius(), building.getLife(), building.getMaxLife(), vector<Status>(), building.getType(),
				building.getVisionRange(), building.getAttackRange(),
				-1, -1, 0 };

			enemyBuildingsGlobal.push_back(b);
		}
	}
}
bool inVisionFriendlyArea(const Wizard& self, const World& world, const Building& building) {
	bool inVision = false;

	for (const auto& w : world.getWizards()) {
		if (w.getFaction() == self.getFaction()) {
			double dist = w.getDistanceTo(building);
			if (dist < w.getVisionRange()) {
				inVision = true;
				break;
			}
		}
	}

	if (!inVision) {
		for (const auto& m : world.getMinions()) {
			if (m.getFaction() == self.getFaction()) {
				double dist = m.getDistanceTo(building);
				if (dist < m.getVisionRange()) {
					inVision = true;
					break;
				}
			}
		}
	}

	return inVision;
}
void updateEnemyBuildings(const Wizard& self, const World& world) {
	// decrease cool down ticks
	for (auto& b : enemyBuildingsGlobal) {
		b = Building ( b.getId(), b.getX(), b.getY(), 0, 0, 0, b.getFaction(),
			b.getRadius(), b.getLife(), b.getMaxLife(), b.getStatuses(), b.getType(),
			b.getVisionRange(), b.getAttackRange(),
			b.getDamage(), b.getCooldownTicks(), max(b.getRemainingActionCooldownTicks()-1,0) );
	}

	// update dummy with current data
	for (const auto& building : world.getBuildings()) {
		if (building.getFaction() != self.getFaction()) {

			// find building and erase
			unsigned int i = 0;
			for (; i < (unsigned int)enemyBuildingsGlobal.size(); i++) {
				if ((int)enemyBuildingsGlobal[i].getX() == (int)building.getX() && (int)enemyBuildingsGlobal[i].getY() == (int)building.getY()) {
					break;
				}
			}
			enemyBuildingsGlobal.erase(enemyBuildingsGlobal.begin() + i);

			// add original
			enemyBuildingsGlobal.push_back(building);
		}
	}

	// update remove destroyed one
	auto isNotThereAnymore = [&self, &world](const Building& building) {
		bool inVision = inVisionFriendlyArea(self, world, building);
		if (inVision) {
			bool found = false;
			for (const auto& b : world.getBuildings()) {
				if ((int)b.getX() == (int)building.getX() && (int)b.getY() == (int)building.getY()) {
					found = true;
					if (found) break;
				}
			}
			return !found;
		}
		else {
			return false;
		}
	};
	enemyBuildingsGlobal.erase(remove_if(enemyBuildingsGlobal.begin(), enemyBuildingsGlobal.end(), isNotThereAnymore), enemyBuildingsGlobal.end());
}
// skills
void initActiveSkills() {
	for (int i = 0; i < 25; i++) {
		activeSkills[i] = 0;
	}
}
void updateActiveSkills(const Wizard& self, const World& world, const Game& game) {
	initActiveSkills();

	vector<SkillType> mySkills = self.getSkills();
	for (const auto& skill : mySkills) {
		activeSkills[skill] = 1;
	}

	for (const auto& wizard : world.getWizards()) {
		if (wizard.getFaction() == self.getFaction()) {
			double dist = self.getDistanceTo(wizard);
			if (dist <= game.getAuraSkillRange()) {
				for (const auto& skill : wizard.getSkills()) {
					if (skill == SKILL_RANGE_BONUS_AURA_1 || skill == SKILL_RANGE_BONUS_AURA_2 ||
						skill == SKILL_MAGICAL_DAMAGE_BONUS_AURA_1 || skill == SKILL_MAGICAL_DAMAGE_BONUS_AURA_2 ||
						skill == SKILL_STAFF_DAMAGE_BONUS_AURA_1 || skill == SKILL_STAFF_DAMAGE_BONUS_AURA_2 ||
						skill == SKILL_MOVEMENT_BONUS_FACTOR_AURA_1 || skill == SKILL_MOVEMENT_BONUS_FACTOR_AURA_2 ||
						skill == SKILL_MAGICAL_DAMAGE_ABSORPTION_AURA_1 || skill == SKILL_MAGICAL_DAMAGE_ABSORPTION_AURA_2) {
						activeSkills[skill] = 1;
					}
				}
			}
		}
	}
}
bool isNextSkillPossible(const Wizard& self) {
	int currentLevel = self.getLevel();
	if (activeSkillLevel < currentLevel) return true;
	return false;
}
SkillType learnNextSkill(const Wizard& self) {
	SkillType skillPath[] = {
		SKILL_RANGE_BONUS_PASSIVE_1,
		SKILL_RANGE_BONUS_AURA_1,
		SKILL_RANGE_BONUS_PASSIVE_2,
		SKILL_RANGE_BONUS_AURA_2,
		SKILL_ADVANCED_MAGIC_MISSILE,
		SKILL_MAGICAL_DAMAGE_BONUS_PASSIVE_1,
		SKILL_MAGICAL_DAMAGE_BONUS_AURA_1,
		SKILL_MAGICAL_DAMAGE_BONUS_PASSIVE_2,
		SKILL_MAGICAL_DAMAGE_BONUS_AURA_2,
		SKILL_FROST_BOLT,
		SKILL_MOVEMENT_BONUS_FACTOR_PASSIVE_1,
		SKILL_MOVEMENT_BONUS_FACTOR_AURA_1,
		SKILL_MOVEMENT_BONUS_FACTOR_PASSIVE_2,
		SKILL_MOVEMENT_BONUS_FACTOR_AURA_2,
		SKILL_HASTE,
		SKILL_STAFF_DAMAGE_BONUS_PASSIVE_1,
		SKILL_STAFF_DAMAGE_BONUS_AURA_1,
		SKILL_STAFF_DAMAGE_BONUS_PASSIVE_2,
		SKILL_STAFF_DAMAGE_BONUS_AURA_2,
		SKILL_FIREBALL,
		SKILL_MAGICAL_DAMAGE_ABSORPTION_PASSIVE_1,
		SKILL_MAGICAL_DAMAGE_ABSORPTION_AURA_1,
		SKILL_MAGICAL_DAMAGE_ABSORPTION_PASSIVE_2,
		SKILL_MAGICAL_DAMAGE_ABSORPTION_AURA_2,
		SKILL_SHIELD};

	//SkillType skillPath[] = {
	//	SKILL_MAGICAL_DAMAGE_BONUS_PASSIVE_1,
	//	SKILL_MAGICAL_DAMAGE_BONUS_AURA_1,
	//	SKILL_MAGICAL_DAMAGE_BONUS_PASSIVE_2,
	//	SKILL_MAGICAL_DAMAGE_BONUS_AURA_2,
	//	SKILL_FROST_BOLT,
	//	SKILL_RANGE_BONUS_PASSIVE_1,
	//	SKILL_RANGE_BONUS_AURA_1,
	//	SKILL_RANGE_BONUS_PASSIVE_2,
	//	SKILL_RANGE_BONUS_AURA_2,
	//	SKILL_ADVANCED_MAGIC_MISSILE,
	//	SKILL_MOVEMENT_BONUS_FACTOR_PASSIVE_1,
	//	SKILL_MOVEMENT_BONUS_FACTOR_AURA_1,
	//	SKILL_MOVEMENT_BONUS_FACTOR_PASSIVE_2,
	//	SKILL_MOVEMENT_BONUS_FACTOR_AURA_2,
	//	SKILL_HASTE,
	//	SKILL_STAFF_DAMAGE_BONUS_PASSIVE_1,
	//	SKILL_STAFF_DAMAGE_BONUS_AURA_1,
	//	SKILL_STAFF_DAMAGE_BONUS_PASSIVE_2,
	//	SKILL_STAFF_DAMAGE_BONUS_AURA_2,
	//	SKILL_FIREBALL,
	//	SKILL_MAGICAL_DAMAGE_ABSORPTION_PASSIVE_1,
	//	SKILL_MAGICAL_DAMAGE_ABSORPTION_AURA_1,
	//	SKILL_MAGICAL_DAMAGE_ABSORPTION_PASSIVE_2,
	//	SKILL_MAGICAL_DAMAGE_ABSORPTION_AURA_2,
	//	SKILL_SHIELD};

	return skillPath[activeSkillLevel];
}
// projectiles
double getProjectileMaxRange(const Projectile& projectile, const World& world, const Game& game) {
	double castRange = 600; // maximum for high leveled wizards
	for (const auto& wizard : world.getWizards()) {
		if (wizard.getId() == projectile.getOwnerUnitId()) {
			castRange = wizard.getCastRange();
		}
	}
	if (projectile.getType() == PROJECTILE_DART) {
		castRange = game.getFetishBlowdartAttackRange();
	}
	return castRange;
}
double getRemainingProjectileDistance(const Projectile& p, const Game& game, double projectileMaxRange, int firstSeenTick) {
	double speed = 40;
	if (p.getType() == PROJECTILE_DART) {
		speed = game.getDartSpeed();
	}
	if (p.getType() == PROJECTILE_FIREBALL) {
		speed = game.getFireballSpeed();
	}
	if (p.getType() == PROJECTILE_FROST_BOLT) {
		speed = game.getFrostBoltSpeed();
	}

	return projectileMaxRange - speed * (tick - firstSeenTick + 1);
}
class ProjectileData {
public:
	Projectile projectile;
	int firstSeenTick;
	Point origin;
	double minAngle; // for retreat
	double maxAngle; // for retreat
	double projectileMaxRange;
	bool targetingMe; // Im the target, but maybe out of range
	bool isOutOfRange; // Im on the save side, dont step towards
	double remainingProjectileDistance;
	double distanceToSelf;

	ProjectileData(const Wizard& self, const World& world, const Game& game, const Projectile& p) : projectile(p), firstSeenTick(tick) {
		double angle = p.getAngle();
		origin = Point(p.getX() - p.getSpeedX(), p.getY() - p.getSpeedY(), angle);
		if (angle < 0) {
			angle += 2 * PI;
		}
		minAngle = angle - PI / 30.;
		maxAngle = angle + PI / 30.;
		projectileMaxRange = getProjectileMaxRange(p, world, game);
		targetingMe = false;
		isOutOfRange = true;
		remainingProjectileDistance = getRemainingProjectileDistance(p, game, projectileMaxRange, firstSeenTick);
		distanceToSelf = self.getDistanceTo(p);
		if (self.getId() != p.getOwnerPlayerId()) {
			targetingMe = isLineCircleIntersectionPoint(origin, getPoint(origin, projectileMaxRange + p.getRadius()), self, self.getRadius());
			isOutOfRange = !targetingMe;
		}
	};

	void update(const Wizard& self, const Game& game) {
		if (self.getId() != projectile.getOwnerPlayerId()) {
			remainingProjectileDistance = getRemainingProjectileDistance(projectile, game, projectileMaxRange, firstSeenTick);
			distanceToSelf = self.getDistanceTo(Point(projectile.getX() + (tick - firstSeenTick)*projectile.getSpeedX(), projectile.getY() + (tick - firstSeenTick)*projectile.getSpeedY(), projectile.getAngle()));
			isOutOfRange = !isLineCircleIntersectionPoint(origin, getPoint(origin, projectileMaxRange), self, self.getRadius());
		}
	}
};
vector<ProjectileData> projectiles;
void updateProjectileData(const Wizard& self, const World& world, const Game& game) {
	// update all old projectiles
	for (auto& projectile : projectiles) {
		projectile.update(self, game);
	}

	// insert new
	auto isNewProjectile = [](const Projectile& projectile) {
		for (const auto& p : projectiles) {
			if (projectile.getId() == p.projectile.getId()) return false;
		}
		return true;
	};
	for (const auto& projectile : world.getProjectiles()) {
		if (isNewProjectile(projectile)) {
			projectiles.push_back(ProjectileData(self, world, game, projectile));
		}
	}

	// remove old projectiles
	auto isProjectileNotInGame = [&world](const ProjectileData& projectileData) {
		for (const auto& p : world.getProjectiles()) {
			if (projectileData.projectile.getId() == p.getId()) return false;
		}
		return true;
	};
	projectiles.erase(remove_if(projectiles.begin(), projectiles.end(), isProjectileNotInGame), projectiles.end());
}
// TODO stimmt targetingMe so
bool computeProjectileRetreatAngles(double& minAngle, double& maxAngle) {
	minAngle = 0;
	maxAngle = 2 * PI;

	bool meTarget = false;
	for (const auto& p : projectiles) {
		if (p.targetingMe) {
			meTarget = true;
			if (p.minAngle > minAngle) {
				minAngle = p.minAngle;
			}
			if (p.maxAngle < maxAngle) {
				maxAngle = p.maxAngle;
			}
		}
	}
	return meTarget;
}
bool computeProjectileRetreatAngle(double& angle) {
	bool meTarget = false;
	for (const auto& p : projectiles) {
		if (p.targetingMe) {
			meTarget = true;
			if (p.minAngle > angle) {
				angle = p.minAngle;
			}
			if (p.maxAngle < angle) {
				angle = p.maxAngle;
			}
		}
	}
	return meTarget;
}

// bases
Building getEnemyBase() {
	for (const auto &building : enemyBuildingsGlobal) {
		if (building.getType() == BUILDING_FACTION_BASE) {
			return building;
		}
	}
	return Building();
}
Building getHomeBase(const LivingUnit& unit, const World& world) {
	for (auto const &building : world.getBuildings()) {
		if (building.getType() == BUILDING_FACTION_BASE && building.getFaction() == unit.getFaction()) {
			return building;
		}
	}
	return Building();
}

// remember fog of war
template<typename T>
vector<LivingUnit> getLivingUnitsWithinDistance(const CircularUnit& origin, double distance, const vector<T>& units) {
	vector<LivingUnit> returnUnits;

	for (auto const &unit : units) {
		double dist = origin.getDistanceTo(unit);
		if (dist > 1) {
			if (dist - origin.getRadius() - unit.getRadius() < distance) {
				returnUnits.push_back(unit);
			}
		}
	}

	return returnUnits;
}
vector<LivingUnit> getStaticObstaclesWithinDistance(const CircularUnit& origin, double distance, const World& world) {
	//vector<LivingUnit> obstacles = getLivingUnitsWithinDistance(origin, distance, world.getTrees());
	//vector<LivingUnit> buildings = getLivingUnitsWithinDistance(origin, distance, world.getBuildings());

	vector<LivingUnit> obstacles;
	obstacles.insert(obstacles.end(), world.getTrees().begin(), world.getTrees().end());
	obstacles.insert(obstacles.end(), world.getBuildings().begin(), world.getBuildings().end());

	for (auto const &unit : world.getMinions()) {
		if (unit.getFaction() == FACTION_NEUTRAL && unit.getSpeedX() == 0 && unit.getSpeedY() == 0) {
			obstacles.push_back(unit);
		}
	}

	return obstacles;
}
vector<LivingUnit> getAllObstaclesWithinDistance(const CircularUnit& origin, double distance, const World& world) {
	vector<LivingUnit> obstacles = getStaticObstaclesWithinDistance(origin, distance, world);
	
	//vector<LivingUnit> minions = getLivingUnitsWithinDistance(origin, distance, world.getMinions());
	//obstacles.insert(obstacles.end(), minions.begin(), minions.end());
	//vector<LivingUnit> wizards = getLivingUnitsWithinDistance(origin, distance, world.getWizards());
	//obstacles.insert(obstacles.end(), wizards.begin(), wizards.end());

	obstacles.insert(obstacles.end(), world.getMinions().begin(), world.getMinions().end());
	//obstacles.insert(obstacles.end(), world.getWizards().begin(), world.getWizards().end());
	for (auto const &unit : world.getWizards()) {
		if (unit.isMe()) continue;
		obstacles.push_back(unit);
	}

	return obstacles;
}
vector<LivingUnit> getMovingStaticObstaclesWithinDistance(const CircularUnit& origin, double distance, const World& world) {
	vector<LivingUnit> obstacles = getStaticObstaclesWithinDistance(origin, distance, world);

	for (auto const &unit : world.getMinions()) {
		if (origin.getDistanceTo(unit) - origin.getRadius() - unit.getRadius() < distance && unit.getSpeedX() == 0 && unit.getSpeedY() == 0) {
			obstacles.push_back(unit);
		}
	}
	for (auto const &unit : world.getWizards()) {
		if (origin.getDistanceTo(unit) - origin.getRadius() - unit.getRadius() < distance && unit.getSpeedX() == 0 && unit.getSpeedY() == 0) {
			obstacles.push_back(unit);
		}
	}

	return obstacles;
}
// type: 1 ... All, 2 ... Moving+Static, 3 ... Only static obstacles
vector<LivingUnit> getObstaclesWithinDistance(const CircularUnit& origin, double distance, const World& world, int type) {
	if (type == 1) {
		return getAllObstaclesWithinDistance(origin, distance, world);
	} else if (type == 2) {
		return getMovingStaticObstaclesWithinDistance(origin, distance, world);
	} else {
		return getStaticObstaclesWithinDistance(origin, distance, world);
	}
}

// move functions
//-----------------
bool isHastened(const LivingUnit& self) {
	for (const auto& s : self.getStatuses()) {
		if (s.getType() == STATUS_HASTENED) {
			return true;
		}
	}
	return false;
}
bool isShielded(const LivingUnit& self) {
	for (const auto& s : self.getStatuses()) {
		if (s.getType() == STATUS_SHIELDED) {
			return true;
		}
	}
	return false;
}
double getBonusSpeedFactor(const LivingUnit& self, const Game& game) {
	double bonusSpeedFactor = 1;

	if (activeSkills[SKILL_MOVEMENT_BONUS_FACTOR_PASSIVE_1]) bonusSpeedFactor += game.getMovementBonusFactorPerSkillLevel();
	if (activeSkills[SKILL_MOVEMENT_BONUS_FACTOR_AURA_1]) bonusSpeedFactor += game.getMovementBonusFactorPerSkillLevel();
	if (activeSkills[SKILL_MOVEMENT_BONUS_FACTOR_PASSIVE_2]) bonusSpeedFactor += game.getMovementBonusFactorPerSkillLevel();
	if (activeSkills[SKILL_MOVEMENT_BONUS_FACTOR_AURA_2]) bonusSpeedFactor += game.getMovementBonusFactorPerSkillLevel();

	if (isHastened(self)) bonusSpeedFactor += game.getHastenedMovementBonusFactor();

	return bonusSpeedFactor;
}
double getForwardSpeed(const LivingUnit& self, const Game& game) {
	double speed = game.getWizardForwardSpeed();
	double bonusSpeed = getBonusSpeedFactor(self, game);
	
	return speed * bonusSpeed;
}
double getBackwardSpeed(const LivingUnit& self, const Game& game) {
	double speed = game.getWizardBackwardSpeed();
	double bonusSpeed = getBonusSpeedFactor(self, game);

	return speed * bonusSpeed;
}
double getStrafeSpeed(const LivingUnit& self, const Game& game) {
	double speed = game.getWizardStrafeSpeed();
	double bonusSpeed = getBonusSpeedFactor(self, game);

	return speed * bonusSpeed;
}
double getTurnAngle(const LivingUnit& self, const Game& game) {
	double turnAngle = game.getWizardMaxTurnAngle();
	double bonus = 1;
	if (isHastened(self)) bonus += game.getHastenedRotationBonusFactor();

	return turnAngle * bonus;
}
MovePoint getMovePoint(const LivingUnit& self, const Game& game, double angleRelative, double dist) {
	double phi_r = angleRelative;
	double phi_g = angleRelative + self.getAngle();

	if (abs(phi_r) < PI / 2) { // ellipse
		double a = getForwardSpeed(self, game);
		double b = getStrafeSpeed(self, game);
		double cosphi_r = cos(-phi_r);
		double sinphi_r = sin(-phi_r);
		double r = a*b / (sqrt(b*b*cosphi_r*cosphi_r + a*a*sinphi_r*sinphi_r));
		r = dist < r ? dist : r;
		return MovePoint(self.getX() + r*cos(-phi_g), self.getY() - r*sin(-phi_g), r * cosphi_r, -r * sinphi_r, phi_g, r, phi_r);
	} else { // circle
		double r = dist < getBackwardSpeed(self, game) ? dist : getBackwardSpeed(self, game);
		return MovePoint(self.getX() + r*cos(-phi_g), self.getY() - r*sin(-phi_g), r * cos(-phi_r), -r * sin(-phi_r), phi_g, r, phi_r);
	}
}

// remember fog of war
bool isDirectWayToTargetWithoutObstacles(const CircularUnit& self, const World& world, const Game& game, const Point& target, int type) {
	if (target.getX() < 40 || target.getX() > 3960 || target.getY() < 40 || target.getY() > 3960) {
		return false;
	}

	double dist = self.getDistanceTo(target);
	vector<LivingUnit> obstacles = getObstaclesWithinDistance(self, dist + spacingBuffer, world, type);
	
	// any obstacles within searchDistance
	bool intersection = false;
	double radadd = game.getWizardRadius() + spacingBuffer;
	Point start = getPoint(self, self.getAngle() + self.getAngleTo(target), spacingBuffer);
	for (auto const &obstacle : obstacles) {
		if (target.getDistanceTo(obstacle) <= obstacle.getRadius()) continue;
		intersection = isLineCircleIntersectionPoint(start, target, obstacle, obstacle.getRadius() + radadd);
		if (intersection) {
			return false;
		}
	}

	return true;
}
// type: 1 ... All, 2 ... Moving, 3 ... Only static obstacles
double nextMoveTowardsTargetSearch(const Wizard& self, const World& world, const Game& game, double targetAngle, double searchDistance, int type) {
	vector<LivingUnit> obstacles = getObstaclesWithinDistance(self, searchDistance + spacingBuffer, world, type);
	if (obstacles.size() == 0) return targetAngle;

	// find new angle without obstacles
	double angleCounterClockWise = targetAngle;
	double angleClockWise = targetAngle;

	// counter clock wise
	unsigned int circlePoints = 180;
	double rad = PI / circlePoints;
	double radadd = game.getWizardRadius() + spacingBuffer;
	bool intersection1 = true;
	for (unsigned int i = 1; i <= circlePoints; i++) {
		angleCounterClockWise = targetAngle - i * rad;
		Point start = getPoint(self, angleCounterClockWise, spacingBuffer);
		Point end = getPoint(self, angleCounterClockWise, searchDistance);

		if (end.getX() < 40 || end.getX() > 3960 || end.getY() < 40 || end.getY() > 3960) {
			continue;
		}

		for (auto const &obstacle : obstacles) {
			intersection1 = isLineWithinCircle(start, obstacle, obstacle.getRadius() + radadd);
			if (intersection1) break;
			intersection1 = isLineCircleIntersectionPoint(start, end, obstacle, obstacle.getRadius() + radadd);
			if (intersection1) {
				break;
			} // sobald ein Hindernis entdeckt wird, weiter zum nächsten Winkel
		}
		if (!intersection1) {
			break;
		}
	}

	// clock wise
	bool intersection2 = true;
	for (unsigned int i = 0; i < circlePoints; i++) {
		angleClockWise = targetAngle + i * rad;
		Point start = getPoint(self, angleClockWise, spacingBuffer);
		Point end = getPoint(self, angleClockWise, searchDistance);

		if (end.getX() < 40 || end.getX() > 3960 || end.getY() < 40 || end.getY() > 3960) {
			continue;
		}

		for (auto const &obstacle : obstacles) {
			intersection2 = isLineWithinCircle(start, obstacle, obstacle.getRadius() + radadd);
			if (intersection2) break;
			intersection2 = isLineCircleIntersectionPoint(start, end, obstacle, obstacle.getRadius() + radadd);
			if (intersection2) {
				break;
			} // sobald ein Hindernis entdeckt wird, weiter zum nächsten Winkel
		}
		if (!intersection2) {
			break;
		}
	}

	// es gibt nun zwei Winkel ohne Hindernisse
	// wähle jenen Winkel mit geringer Differenz zum Ziel
	double d1 = angleCounterClockWise - targetAngle;
	double d2 = angleClockWise - targetAngle;
	double angleNew = targetAngle;
	if (abs(d1) < abs(d2)) {
		angleNew = angleCounterClockWise;
	} else {
		angleNew = angleClockWise;
	}

	if (debug) cout << "search distance:" << searchDistance << " obstacle size:" << obstacles.size() << " angleCounterClockWise:" << angleCounterClockWise / PI * 180 << " angleClockWise:" << angleClockWise / PI * 180 << " angleNew:" << angleNew / PI * 180 << endl;

	return angleNew;
}
bool isEnemyUnitAttackedByMinions(const LivingUnit& unit, const World& world, const Game& game) {
	for (auto const &minion : world.getMinions()) {
		if (unit.getFaction() != minion.getFaction() && minion.getFaction() != FACTION_NEUTRAL) {
			double dist = unit.getDistanceTo(minion);
			if ((minion.getType() == MINION_ORC_WOODCUTTER && dist <= (game.getOrcWoodcutterAttackRange() + unit.getRadius())) ||
				(minion.getType() == MINION_FETISH_BLOWDART && dist <= (game.getFetishBlowdartAttackRange() + unit.getRadius()))) {
				return true;
			}
		}
	}
	return false;
}
double getNonMovingDistanceToObstacle(const LivingUnit& obstacle, const World& world, const Game& game) {
	if (isEnemyUnitAttackedByMinions(obstacle, world, game)) {
		if (obstacle.getRadius() == 50) return 600 - 50;
		if (obstacle.getRadius() == 35) return 545 - 36;
		if (obstacle.getRadius() == 25) return game.getFetishBlowdartAttackRange();
	} else {
		if (obstacle.getRadius() == 50) return game.getGuardianTowerAttackRange();
		if (obstacle.getRadius() == 35) return game.getWizardCastRange() + game.getMagicMissileRadius() + game.getWizardRadius();
		if (obstacle.getRadius() == 25) return game.getFetishBlowdartAttackRange() + game.getDartRadius() + game.getWizardRadius();
	}
	return obstacle.getRadius();
}

// Theta*
void updateThetaStarMap(const Wizard& self, const World& world) {
	auto makeCircle = [](const Vectorf& center, float radius, int scale) {
		Vectori lt((center.x - radius - 0.00001f) / scale, (center.y - radius - 0.00001f) / scale);
		Vectori rb((center.x + radius) / scale, (center.y + radius) / scale);

		for (int x = lt.x + 1; x <= rb.x; x++) {
			for (int y = lt.y + 1; y <= rb.y; y++) {
				float dist = (x*scale - center.x)*(x*scale - center.x) + (y*scale - center.y)*(y*scale - center.y);
				if (dist <= radius*radius) {
					if (map[x][y] == ' ') {
						map[x][y] = '0';
					}
					else {
						map[x][y] += 1;
					}
				}
			}
		}
	};

	auto makeWall = [](const Vectori& pos, const Vectori& size) {
		for (int x = 0; x < size.x; x++) {
			for (int y = 0; y < size.y; y++) {
				map[pos.x + x][pos.y + y] = '#';
			}
		}
	};

	// reset map (set everything to space)
	for (auto& cs : map)
		for (auto& c : cs)
			c = ' ';

	makeWall({ 0, 0 }, { SIZEMAP, 1 });
	makeWall({ 0, 0 }, { 1, SIZEMAP });
	makeWall({ 0, SIZEMAP - 1 }, { SIZEMAP, 1 });
	makeWall({ SIZEMAP - 1, 0 }, { 1, SIZEMAP });

	for (const auto& tree : world.getTrees()) {
		makeCircle({ (const float)tree.getX(), (const float)tree.getY() }, tree.getRadius() + self.getRadius() + spacingBuffer*10, scale);
	}
	for (const auto& building : world.getBuildings()) {
		makeCircle({ (const float)building.getX(), (const float)building.getY() }, building.getRadius() + self.getRadius() + spacingBuffer*10, scale);
	}
	for (auto const &unit : world.getMinions()) {
		if (unit.getFaction() == FACTION_NEUTRAL && unit.getSpeedX() == 0 && unit.getSpeedY() == 0) {
			makeCircle({ (const float)unit.getX(), (const float)unit.getY() }, unit.getRadius() + self.getRadius() + spacingBuffer*10, scale);
		}
	}

	makeCircle({ 940, 2000 }, 400, scale);
	makeCircle({ 3040, 2000 }, 400, scale);
	makeCircle({ 2000, 940 }, 400, scale);
	makeCircle({ 2000, 3040 }, 400, scale);

	high_resolution_clock::time_point startTime = high_resolution_clock::now();

	//The map was edited so we need to regenerate the neighbors
	pathfinder.generateNodes();

	high_resolution_clock::time_point endTime = high_resolution_clock::now();
	duration<double> time_span = duration_cast<duration<double>>(endTime - startTime);
	//if (timeDebug) cout << "tick:" << tick << " time count:" << time_span.count() << " seconds." << endl;
}
double nextThetaStarAngle(const Wizard& self, const World& world, const Game& game, const Point& target, double& distance) {
	
	if (tick % 20 == 0 && !mapUpdated) {
		updateThetaStarMap(self, world);
		mapUpdated = true;
	}
	
	int mapSizeX = SIZEMAP;
	int mapSizeY = SIZEMAP;
		
	auto convert = [](const Unit& unit, int scale) {
		int x = unit.getX() / scale;
		int y = unit.getY() / scale;

		if (map[x][y] == ' ') return Vectori(x, y);
		if (map[x+1][y] == ' ') return Vectori(x+1, y);
		if (map[x][y+1] == ' ') return Vectori(x, y+1);
		return Vectori(x+1, y+1);
	};

	// start, end point
	Vectori startPoint = convert(self, scale);
	Vectori endPoint = convert(target, scale);

	//start and end point
	//map[startPoint.x][startPoint.y] = 'S';
	//map[endPoint.x][endPoint.y] = 'E';

	//draw map
	//std::cout << "after obstacles:" << std::endl;
	//for (int y = 0; y < 400; y++)
	//{
	//	for (int x = 0; x < 100; x++)
	//		std::cout << map[x][y];

	//	std::cout << std::endl;
	//}
	//std::cout << std::endl;

	high_resolution_clock::time_point startTime = high_resolution_clock::now();

	//doing the search
	//merly to show the point of how it work
	//as it would have been way easier to simply transform the vector to id and pass it to search
	auto nodePath = pathfinder.search(adaptor.posToId(startPoint), adaptor.posToId(endPoint));

	high_resolution_clock::time_point endTime = high_resolution_clock::now();
	duration<double> time_span = duration_cast<duration<double>>(endTime - startTime);
	//if (timeDebug) cout << " time count:" << time_span.count() << " seconds." << endl;

	//Convert Ids onto map position
	std::vector<Vectori> path;
	path.reserve(nodePath.size());

	for (const auto id : nodePath)
		path.push_back(adaptor.idToPos(id));


	//There is also a serach function that do the conversion between your data type
	//And the id, it take lambdas to convert between the two
	//Here's an exemple of how it would be called for this code
	//    auto path = pathfinder.search(startPoint, endPoint,
	//                                        {
	//                                            [&adaptor](const auto id)
	//                                            {
	//                                                return adaptor.idToPos(id);
	//                                            }
	//                                        },
	//                                        {
	//                                            [&adaptor](const auto& data)
	//                                            {
	//                                                return adaptor.posToId(data);
	//                                            }
	//                                        });

	distance = 0;
	for (unsigned int i = 1; i < path.size(); i++) {
		distance += path[i - 1].getDistanceTo(path[i]);
	}
	distance *= scale;

	if (debug) cout << "distance: " << distance << endl;

	//If we found a path we just want to remove the first and last node
	//Because it will be at our start and end position
	//if (path.size())
	//{
	//	path.pop_back();
	//	path.erase(path.begin());
	//}

	//if (debug) {
	//	//nodes
	//	int x = 0;
	//	for (const auto& node : path)
	//		map[node.x][node.y] = '0' + x++;

	//	//draw map
	//	std::cout << "with path:" << std::endl;
	//	for (int y = 0; y < 400; y++)
	//	{
	//		for (int x = 0; x < 100; x++)
	//			std::cout << map[x][y];

	//		std::cout << std::endl;
	//	}

	if (fileDebug) {
		if (tick % 1000 == 0) {
			ofstream stream;
			string filename = "map" + to_string(tick) + ".txt";
			stream.open(filename);

			string input;
			for (int y = 0; y < mapSizeY; y++)
			{
				for (int x = 0; x < mapSizeX; x++)
					input += map[x][y];

				input += "\n";
			}

			stream << input << endl;
			stream << "#  = walls" << std::endl;
			stream << "S  = start" << std::endl;
			stream << "E  = end" << std::endl;
			stream << "number = path nodes" << std::endl;

			stream.close();
		}
	}


	double angle = self.getAngle();
	if (path.size() > 1) {
		angle += self.getAngleTo(path[1].x*scale, path[1].y*scale);
	}
	else {
		angle = self.getAngle() + self.getAngleTo(target);
		angle = nextMoveTowardsTargetSearch(self, world, game, angle, 550, 2);
	}
	return angle;
}
MovePoint nextThetaStarMove(const Wizard& self, const World& world, const Game& game, const Point& target, double& distance) {
	double angle = nextThetaStarAngle(self, world, game, target, distance);
	return getMovePoint(self, game, angle, self.getDistanceTo(target));
}
// self != target (position wise)
double angleOptimization(const Wizard& self, const World& world, const Game& game, double startAngle, double targetAngle, double searchDistance, int type) {

	if (targetAngle-0.00001 < startAngle && startAngle < targetAngle+0.00001) return startAngle;

	// find new angle without obstacles
	vector<LivingUnit> obstacles = getObstaclesWithinDistance(self, searchDistance + spacingBuffer, world, type);

	double newAngle = startAngle;
	if (obstacles.size() > 0) {
		unsigned int np = 180;
		double d = (targetAngle - startAngle) / np;
		bool intersection = false;
		for (unsigned int i = 1; i <= np; i++) {
			double angle = startAngle + i * d;
			Point start = getPoint(self, angle, spacingBuffer);
			Point end = getPoint(self, angle, searchDistance);

			// TODO Check bounds?

			for (auto const &obstacle : obstacles) {
				intersection = isLineWithinCircle(start, obstacle, obstacle.getRadius() + game.getWizardRadius() + spacingBuffer);
				if (intersection) break;
				intersection = isLineCircleIntersectionPoint(start, end, obstacle, obstacle.getRadius() + game.getWizardRadius() + spacingBuffer);
				if (intersection) break; // sobald ein Hindernis entdeckt wird, weiter zum nächsten Winkel
			}
			if (!intersection) {
				newAngle = angle;
			}
			if (intersection) break;
		}
	}

	if (debug) cout << "search distance:" << searchDistance << " obstacle size:" << obstacles.size() << " angleCounterClockWise:" << startAngle / PI * 180 << " angleClockWise:" << targetAngle / PI * 180 << " angleNew:" << newAngle / PI * 180 << endl;

	return newAngle;
}
MovePoint nextMoveTowardsTarget(const Wizard& self, const World& world, const Game& game, const Point& target) {
	int type = 2; // obstacles are non moving units
	if (tick == 0) { // dont count wizards at the start (fine tuning)
		type = 3;
	}

	double angle = self.getAngle() + self.getAngleTo(target);
	if (!isDirectWayToTargetWithoutObstacles(self, world, game, target, 3)) {
		// there are some obstacles (trees, buildings)

		// first stage: global Search with large search distance, non-moving units are obstacles, solution has minimum angle to target
		double dist = 0;
		angle = nextThetaStarAngle(self, world, game, target, dist);

		double searchDistance = 550.; // at least 150 steps
		angle = angleOptimization(self, world, game, angle, self.getAngle() + self.getAngleTo(target), searchDistance, type);
	}
	if (!isDirectWayToTargetWithoutObstacles(self, world, game, target, 1)) {
		// there are some obstacles (trees, buildings, units)

		// second stage: lokal search with minimal search distance, all units are obstacles, solution has minimum angle to global solution
		double searchDistance = 45; // at least 2 x wizard radius + 5
		angle = nextMoveTowardsTargetSearch(self, world, game, angle, searchDistance, 1);
	}

	double angleTargetRelative = angle - self.getAngle();
	return getMovePoint(self, game, angleTargetRelative, self.getDistanceTo(target));
}

// bonus functions
//-----------------
class BonusData {
public:
	Bonus bonus;
	double distance;
	int steps;
	bool direct;

	BonusData() : bonus(), distance(0), steps(-1), direct(false) {};
	BonusData(const Bonus& bonus, const Wizard& self, const World& world, const Game& game) : bonus(bonus), direct(true) {
		steps = -1;
		distance = 0;
		nextThetaStarMove(self, world, game, bonus, distance);
		distance -= game.getWizardRadius() + game.getBonusRadius() + spacingBuffer;
		
		double d = game.isSkillsEnabled() ? 1300 : 1700;

		if (distance < d) {
			steps = (int)distance / 3.3;
			//if (!isDirectWayToTargetWithoutObstacles(self, world, game, bonus, 1)) {
			//	direct = false;
			//}
		}
	};

	bool operator < (const BonusData& b) const {
		return (this->distance < b.distance);
	}
};
ostream &operator<<(ostream &stream, const BonusData& bonusData) {
	return stream << "id:" << bonusData.bonus.getId() << " getX:" << bonusData.bonus.getX() << " getY:" << bonusData.bonus.getY() << " distance:" << bonusData.distance << " steps:" << bonusData.steps << " direct:" << bonusData.direct;
}
void initBonuses() {
	bonuses.push_back(Bonus(0, 1200, 1200, 0.0, 0.0, 0.0, _FACTION_UNKNOWN_, 0.0, _BONUS_UNKNOWN_));
	bonuses.push_back(Bonus(0, 2800, 2800, 0.0, 0.0, 0.0, _FACTION_UNKNOWN_, 0.0, _BONUS_UNKNOWN_));
}
vector<BonusData> bestBonus(const Wizard& self, const World& world, const Game& game) {
	vector<BonusData> bd;
	for (const Bonus& bonus : bonuses) {
		bd.push_back(BonusData(bonus, self, world, game));
	}
	sort(bd.begin(), bd.end());
	return bd;
}
void updateBonusData(const Wizard& self, const World& world, const Game& game) {
	// update dummy with current data
	for (const auto &bonus : world.getBonuses()) {
		// find building and erase
		unsigned int i = 0;
		for (; i < (unsigned int)bonuses.size(); i++) {
			if ((int)bonuses[i].getX() == (int)bonus.getX() && (int)bonuses[i].getY() == (int)bonus.getY()) {
				break;
			}
		}
		bonuses.erase(bonuses.begin() + i);

		// add original
		bonuses.push_back(bonus);
	}

	int nextBonusAppearance = 0;
	for (int i = 0; i < 10; i++) {
		if (nextBonusAppearance + 1 >= tick) break;
		nextBonusAppearance += game.getBonusAppearanceIntervalTicks();
	}
	nextBonusAppearance += 1;
	if (nextBonusAppearance > 20000) nextBonusAppearance *= 2;
	timeToNextBonusAppearance = nextBonusAppearance - tick;
}
vector<UnitDistance<Bonus>> getBonusesOnMap(const Wizard& self, const World& world) {
	vector<UnitDistance<Bonus>> bonuses;
	for (const auto& bonus : world.getBonuses()) {
		double dist = self.getDistanceTo(bonus);
		bonuses.push_back(UnitDistance<Bonus>(bonus, dist));
	}
	sort(bonuses.begin(), bonuses.end());
	return bonuses;
}

vector<UnitDistance<Minion>> getEnemyMinionsWithinDistance(const LivingUnit& unit, const World& world, double distance) {
	vector<UnitDistance<Minion>> enemyMinions;
	for (const auto& minion : world.getMinions()) {
		if ((unit.getFaction() != minion.getFaction() && minion.getFaction() != FACTION_NEUTRAL) ||
			(minion.getFaction() == FACTION_NEUTRAL && (minion.getSpeedX() != 0 || minion.getSpeedY() != 0 || minion.getLife() < minion.getMaxLife() || minion.getRemainingActionCooldownTicks() > 0))) {
			double dist = unit.getDistanceTo(minion);
			if (dist < distance) {
				enemyMinions.push_back(UnitDistance<Minion>(minion, dist));
			}
		}
	}
	sort(enemyMinions.begin(), enemyMinions.end());
	return enemyMinions;
}
vector<UnitDistance<Minion>> getEnemyMinionsInVision(const Wizard& self, const World& world) {
	return getEnemyMinionsWithinDistance(self, world, self.getVisionRange());
}
//vector<UnitDistance<Minion>> getEnemyMinionsInAttackRange(const Wizard& self, const World& world) {
//	// attack range for minions
//	// < 500 + 10 + 20 (only for magic missle)
//	// - 13 * 3 (remove, minions dont retreat)
//
//	return getEnemyMinionsWithinDistance(self, world, );
//}
vector<UnitDistance<Wizard>> getEnemyWizardsWithinDistance(const LivingUnit& unit, const World& world, double distance) {
	vector<UnitDistance<Wizard>> enemyWizards;
	for (const auto& wizard : world.getWizards()) {
		if (unit.getFaction() != wizard.getFaction()) {
			double dist = unit.getDistanceTo(wizard);
			if (dist < distance) {
				enemyWizards.push_back(UnitDistance<Wizard>(wizard, dist));
			}
		}
	}
	sort(enemyWizards.begin(), enemyWizards.end());
	return enemyWizards;
}
vector<UnitDistance<Wizard>> getEnemyWizardsInVision(const Wizard& self, const World& world) {
	return getEnemyWizardsWithinDistance(self, world, self.getVisionRange());
}

//vector<UnitDistance<Wizard>> getEnemyWizardsInAttackRange(const Wizard& self, const World& world) {
//	return getEnemyWizardsWithinDistance(self, world, getAttackRangeEnemyUnit(self, world));
//}
vector<UnitDistance<Building>> getEnemyBuildingsWithinDistance(const Wizard& self, double distance) {
	vector<UnitDistance<Building>> eB;
	for (auto const &building : enemyBuildingsGlobal) {
		double dist = self.getDistanceTo(building);
		if (dist <= distance) {
			eB.push_back(UnitDistance<Building>(building, dist));
		}
	}
	return eB;
}
double getBestAttackRangeEnemyUnit(const Wizard& self, const Game& game, const Wizard& unit) {

	return self.getCastRange() + unit.getRadius() + game.getMagicMissileRadius() - attackRangeThreshold - ((int)(self.getCastRange() / game.getMagicMissileSpeed()) + 1) * getBackwardSpeed(unit, game);
}
double getAttackRangeEnemyUnit(const Wizard& self, const Game& game, const LivingUnit& unit) {

	return self.getCastRange() + unit.getRadius() + game.getMagicMissileRadius() - attackRangeThreshold;
}
double getStaffAttackRangeEnemyUnit(const LivingUnit& unit, const Game& game) {
	return unit.getRadius() + game.getStaffRange() - 0.5;
}

//vector<UnitDistance<Building>> getEnemyBuildingsInAttackRange(const Wizard& self, const Game& game) {
//	// attack buildings always with magic missiles
//	vector<UnitDistance<Building>> enemyBuildings = getEnemyBuildingsWithinDistance(self, self.getCastRange() + 100 + game.getMagicMissileRadius() - attackRangeThreshold);
//
//	auto isNotInAttackRange = [&self, &game](const UnitDistance<Building>& enemy) {
//		if (enemy.unit.getType() == BUILDING_GUARDIAN_TOWER) {
//			if (enemy.distanceToSelf > getAttackRangeEnemyUnit(self, game, enemy.unit)) {
//				return true;
//			}
//			else {
//				return false;
//			}
//		}
//		else {
//			return false;
//		}
//	};
//	enemyBuildings.erase(remove_if(enemyBuildings.begin(), enemyBuildings.end(), isNotInAttackRange), enemyBuildings.end());
//
//	return enemyBuildings;
//}

//bool isEnemyUnitInAttackRange(const Wizard& self, const World& world) {
//	if (getEnemyBuildingsInAttackRange(self).size() > 0)
//		return true;
//	if (getEnemyWizardsInAttackRange(self, world).size() > 0)
//		return true;
//	if (getEnemyMinionsInAttackRange(self, world).size() > 0)
//		return true;
//
//	return false;
//}

vector<UnitDistance<LivingUnit>> getEnemiesInVision(const Wizard& self, const World& world) {
	vector<UnitDistance<LivingUnit>> enemies;

	for (const auto& unit : getEnemyMinionsInVision(self, world)) {
		enemies.push_back(UnitDistance<LivingUnit>(unit.unit,unit.distanceToSelf));
	}
	for (const auto& unit : getEnemyWizardsInVision(self, world)) {
		enemies.push_back(UnitDistance<LivingUnit>(unit.unit, unit.distanceToSelf));
	}
	for (const auto& unit : getEnemyBuildingsWithinDistance(self, 610)) {
		enemies.push_back(UnitDistance<LivingUnit>(unit.unit, unit.distanceToSelf));
	}

	return enemies;
}

//bool isEnemyBuildingCanAttackMe(const Wizard& self, const Game& game, const Building& unit) {
//	double saveDist = getSaveDistanceBuilding(unit);
//	double currentDist = self.getDistanceTo(unit);
//	int steps = (saveDist - currentDist) / getBackwardSpeed(self, game) + 1;
//
//	if (steps < unit.getCooldownTicks()) false;
//	return true;
//}
//MovePoint saveMoveTowardsBonus() {
	// Alle Obstacles im UmKreis von 650
	// movedist 35+10
	// start angle ist Target (z.B. Enemy Base)
	// Suchrichtung in Richtung Bonus
	//stopp wenn kein Schnittpunkt
	// default stay
	// wenn in Danger zone -> retreat (2 Winkel des Rückzugsbereiche
//}
// 6.1 zur Gefahrenzone

double getSaveDistance(const Building& building) {
	double dist = building.getAttackRange() + 2;

	if (building.getLife() < 0.10 * building.getMaxLife()) {
		dist = 0;
	}

	return dist;
}
double getSaveDistance(const Wizard& wizard, const Game& game) {
	double dist = wizard.getCastRange() + game.getFireballRadius() + wizard.getRadius() + 1;
	return dist;
}
double getSaveDistance(const Minion& minion, const Game& game) {
	double dist = game.getOrcWoodcutterAttackRange() + game.getWizardRadius() + 1;
	if (minion.getType() == MINION_FETISH_BLOWDART) {
		dist = game.getFetishBlowdartAttackRange() + game.getDartRadius() + game.getWizardRadius() + 1;
	}

	return dist;
}
bool buildingHasOtherTarget(const Wizard& self, const World& world, const Building& building) {
	for (const auto& m : world.getMinions()) {
		if (m.getFaction() != building.getFaction() && m.getFaction() != FACTION_NEUTRAL) {
			double dist = m.getDistanceTo(building);
			if (dist < building.getAttackRange()) {
				if ((building.getDamage() < m.getLife() && m.getLife() < self.getLife()) ||
					(building.getDamage() > m.getLife() && m.getLife() > self.getLife())) {
					return true;
				}
			}
		}
	}

	for (const auto& w : world.getWizards()) {
		if (w.getFaction() != building.getFaction() && !w.isMe()) {
			double dist = w.getDistanceTo(building);
			if (dist < building.getAttackRange()) {
				if ((building.getDamage() < w.getLife() && w.getLife() < self.getLife()) ||
					(building.getDamage() > w.getLife() && w.getLife() > self.getLife())) {
					return true;
				}
			}
		}
	}

	return false;
}
double getDangerZoneDistance(const Wizard& self, const World& world, const Game& game, const Building& building) {
	if (buildingHasOtherTarget(self, world, building)) {
		return minimumSaveDistance;
	}

	double saveDistance = getSaveDistance(building);
	saveDistance += maxOverallStep;

	int cool = building.getRemainingActionCooldownTicks();
	//if (building.getType() == BUILDING_GUARDIAN_TOWER) {
	//	cool = min(cool, game.getGuardianTowerCooldownTicks()/2);
	//} else {
	//	cool = min(cool, game.getFactionBaseCooldownTicks()/2);
	//}

	saveDistance = saveDistance - cool * getBackwardSpeed(self, game) * stepBackFactor;
	saveDistance = max(saveDistance, minimumSaveDistance);
	return saveDistance;

	// verfeinern ob target des turms
}
bool hasSkill(const Wizard& wizard, int skill) {
	for (const auto& s : wizard.getSkills()) {
		if (skill == s) {
			return true;
		}
	}
	return false;
}
// TODO Achtung Wizards und Minions können sich auch auf mich zubewegen, d.h. die Danger Zone wird sehr rasch wieder anwachsen, ev. schneller
// als sich mein Wizard zurückbewegen kann
// ev. nur 50% anrechnen
double getDangerZoneDistance(const Wizard& self, const World& world, const Game& game, const Wizard& wizard) {
	double saveDistance = getSaveDistance(wizard, game);
	saveDistance += maxOverallStep;

	int cool = self.getRemainingActionCooldownTicks();
	int coolAction = self.getRemainingCooldownTicksByAction()[ACTION_MAGIC_MISSILE];

	if (hasSkill(self, SKILL_FIREBALL)) {
		coolAction = min(coolAction, self.getRemainingCooldownTicksByAction()[ACTION_FIREBALL]);
	}
	if (hasSkill(wizard, SKILL_FROST_BOLT)) {
		coolAction = min(coolAction, self.getRemainingCooldownTicksByAction()[ACTION_FROST_BOLT]);
	}

	cool = max((int)(self.getCastRange() / game.getMagicMissileSpeed()) - max(cool, coolAction), 0);
	
	// works only with static units: - cool * getBackwardSpeed(self, game) * stepBackFactor
	// crap:
	//return saveDistance - - (int)(wizard.getCastRange() / game.getMagicMissileSpeed()) * getBackwardSpeed(self, game);
	//saveDistance = saveDistance - cool * getBackwardSpeed(self, game) - (int)(wizard.getCastRange()/game.getMagicMissileSpeed()) * getBackwardSpeed(self, game);

	double bestAttackRange = getAttackRangeEnemyUnit(self, game, wizard) - 14 * getBackwardSpeed(self, game);
	if (self.getLife() > 0.41 * self.getMaxLife()) {
		if (saveDistance > bestAttackRange) {
			saveDistance -= cool * (getForwardSpeed(self, game) + 0.7);
		}

		if (isShielded(self)) {
			saveDistance = bestAttackRange;
		}
	}
	else {
		if (saveDistance > bestAttackRange) {
			saveDistance -= cool * (getBackwardSpeed(self, game) + 0.7);
		}
	}


	saveDistance = max(saveDistance, minimumSaveDistance);
	return saveDistance;
}
bool hasOtherTarget(const LivingUnit& self, const Minion& minion, const World& world) {

	double distMinion = self.getDistanceTo(minion);

	for (const auto& u : world.getMinions()) {
		if (u.getId() != minion.getId() && u.getFaction() != minion.getFaction() && u.getFaction() != FACTION_NEUTRAL) {
			double dist = minion.getDistanceTo(u);
			if (dist < distMinion-0.1) {
				return true;
			}
		}
	}
	for (const auto& u : world.getWizards()) {
		if (u.getFaction() != minion.getFaction() && !u.isMe()) {
			double dist = minion.getDistanceTo(u);
			if (dist < distMinion-0.1) {
				return true;
			}
		}
	}
	for (const auto& u : world.getBuildings()) {
		if (u.getFaction() != minion.getFaction()) {
			double dist = minion.getDistanceTo(u);
			if (dist < distMinion) {
				return true;
			}
		}
	}

	return false;
}
double getDangerZoneDistance(const Wizard& self, const World& world, const Game& game, const Minion& minion) {
	double saveDistance = getSaveDistance(minion, game);
	saveDistance += maxOverallStep+3;

	if (hasOtherTarget(self, minion, world)) {
		return minimumSaveDistance;
	}

	if (minion.getType() == MINION_FETISH_BLOWDART) {
		int cool = minion.getRemainingActionCooldownTicks();
		// works only with static units: - cool * getBackwardSpeed(self, game) * stepBackFactor
		saveDistance = saveDistance - ((int)(game.getFetishBlowdartAttackRange() / game.getDartSpeed())-1) * getBackwardSpeed(self, game);
	}

	saveDistance = max(saveDistance, minimumSaveDistance);
	return saveDistance;
}
template<class T>
void filterDangerZone(vector < UnitDistance<T> >& enemies, const Wizard& self, const World& world, const Game& game) {
	auto isNoDanger = [&self, &world, &game](const UnitDistance<T>& enemy) {
		if (enemy.distanceToSelf < getDangerZoneDistance(self, world, game, enemy.unit)) {
			return false;
		}
		return true;
	};
	enemies.erase(remove_if(enemies.begin(), enemies.end(), isNoDanger), enemies.end());
}
bool checkDangerZoneEnemies(const Wizard& self, const World& world, const Game& game, Action& action) {
	double searchDistance = 811; // enemy base + 1

	vector<UnitDistance<Building>> enemyBuildings = getEnemyBuildingsWithinDistance(self, searchDistance);
	vector<UnitDistance<Wizard>> enemyWizards = getEnemyWizardsWithinDistance(self, world, searchDistance);
	vector<UnitDistance<Minion>> enemyMinions = getEnemyMinionsWithinDistance(self, world, searchDistance);
	
	filterDangerZone(enemyBuildings, self, world, game);
	filterDangerZone(enemyWizards, self, world, game);
	filterDangerZone(enemyMinions, self, world, game);

	// all enemies within danger zone
	// if empty return false

	if (enemyBuildings.size() == 0 && enemyWizards.size() == 0 && enemyMinions.size() == 0) {
		return false;
	}

	// now compute retreat angle and set Move

	class RetreatAngle {
	public:
		double angle;
		double angleToNext;
		RetreatAngle(double a) : angle(a), angleToNext(0) {};
	};

	int size = 0;
	vector<RetreatAngle> angles;
	for (const auto& e : enemyBuildings) {
		double angle = self.getAngle() + self.getAngleTo(e.unit);
		if (angle < 0) {
			angle += 2 * PI;
		}
		angles.push_back(RetreatAngle(angle));
		size += 1;
	}
	for (const auto& e : enemyWizards) {
		double angle = self.getAngle() + self.getAngleTo(e.unit);
		if (angle < 0) {
			angle += 2 * PI;
		}
		angles.push_back(RetreatAngle(angle));
		size += 1;
	}
	for (const auto& e : enemyMinions) {
		double angle = self.getAngle() + self.getAngleTo(e.unit);
		if (angle < 0) {
			angle += 2 * PI;
		}
		angles.push_back(RetreatAngle(angle));
		size += 1;
	}

	sort(angles.begin(), angles.end(), [](const RetreatAngle& a, const RetreatAngle& b) {
		return a.angle < b.angle;
	});
	angles.push_back(RetreatAngle(angles[0].angle + 2 * PI));
	for (unsigned int i = 0; i < angles.size()-1; i++) {
		double angleToNext = angles[i + 1].angle - angles[i].angle;
		angles[i].angleToNext = angleToNext;
	}
	sort(angles.begin(), angles.end(), [](const RetreatAngle& a, const RetreatAngle& b) {
		return a.angleToNext > b.angleToNext;
	});

	double retreatAngle = angles[0].angle + angles[0].angleToNext/2;
	while (retreatAngle > 2 * PI) {
		retreatAngle -= 2.0 * PI;
	}

	// focus on the diagonale home - enemy base
	// +/-30° relaxtion
	int index = previousGlobalPathIndex(self, globalLane);
	Point previousWaypoint = globalPath[globalLane][index];
	double dist = 0;
	double angle = nextThetaStarAngle(self, world, game, previousWaypoint, dist);

	double angleTarget = angle;
	//TODO Verbessern Winkel zur home base über die Pfadsuche Theta*
	if (angleTarget < 0) {
		angleTarget += 2.0 * PI;
	}
	angleTarget = min(angleTarget, retreatAngle + PI / 30.);
	angleTarget = max(angleTarget, retreatAngle - PI / 30.);

	// 50 steps back
	Point target = getPoint(self, angleTarget, getBackwardSpeed(self, game) * 50);
	action.movePoint = target;
	action.moveTarget = TARGET_DANGER_ZONE;
	//MovePoint movePoint = nextMoveTowardsTarget(self, world, game, target);

	return true;
}
bool checkProjectiles(const Wizard& self, const World& world, const Game& game, Move& move, Action& action) {
	double minAngle = 0;
	double maxAngle = 2 * PI;
	computeProjectileRetreatAngles(minAngle, maxAngle);

	// compute next move towards bonuses, home base, middle of retreat area, etc.
	
	double angleToTarget = self.getAngleTo(action.movePoint);
	if (angleToTarget < 0) {
		angleToTarget += 2 * PI;
	}
	if (!computeProjectileRetreatAngle(angleToTarget)) {
		return false;
	}

	// TODO Verbesserung:
	// Unterscheidung je nach target, gibt es Fälle wo ausweichen weniger wichtig ist als das Target (z.B. sichbarer oder erreichbarer Bonus)
	Point target = getPoint(self, angleToTarget, getBackwardSpeed(self, game) * 50);
	action.movePoint = target;
	action.moveTarget = TARGET_PROJECTILE;
	//MovePoint movePoint = nextMoveTowardsTarget(self, world, game, target);

	return true;
}

void setOverallTarget(const Wizard& self, const World& world, const Game& game, Action& action) {
	int index = nextGlobalPathIndex(self, globalLane);
	action.moveTarget = getTargetTypeGlobalPath(index);
	action.movePoint = globalPath[globalLane][index];
}
void setBonusOnMapTarget(const Wizard& self, const World& world, const Game& game, Action& action) {
	vector<UnitDistance<Bonus>> bonusesOnMap = getBonusesOnMap(self, world);
	// wizard has bonus in vision, get it
	if (bonusesOnMap.size() > 0 && bonusesOnMap[0].distanceToSelf < self.getVisionRange()) {
		bonusTarget = true;
		// TODO verfeinern, was ist wenn von der anderen Seite ein Gegner auch auf den Bonus zuläuft
		// mit nachfolgenden checkDangerZoneEnemies und checkProjectiles funktionen könnte man den Bonus nicht 
		// erreichen
		// ev. so: bin ich der näheste Wizard zum Bonus -> ignoreEverything = true
		action.moveTarget = TARGET_BONUS; action.movePoint = bonusesOnMap[0].unit;
	}
}
Point getBonusMoveTarget(const Wizard& self, const Bonus& bonus) {
	return getPoint(self, (self.getAngle() + self.getAngleTo(bonus)), (self.getDistanceTo(bonus) - 57));
}
void setBonusPrepareTarget(const Wizard& self, const World& world, const Game& game, Action& action) {
	if (timeToNextBonusAppearance > 1000) {
		bonusTarget = false;
	}
	else {
		vector<BonusData> bonusData = bestBonus(self, world, game);
		if (bonusTarget) {
			action.moveTarget = TARGET_BONUS;
			action.movePoint = getBonusMoveTarget(self, bonusData[0].bonus);
		}
		else {
			// TODO passt das so mit -50 < x < +10
			if ((bonusData[0].steps - 50) < timeToNextBonusAppearance && timeToNextBonusAppearance < (bonusData[0].steps + 10)) {
				bonusTarget = true;

				action.moveTarget = TARGET_BONUS;
				action.movePoint = getBonusMoveTarget(self, bonusData[0].bonus);
			}
		}
	}

	// TODO analog zu oben bei freundlichen und feindlichen Wizards die sich auch richtung bonus bewegen
}

bool baseDefence(const Wizard& self, const World& world) {
	Building home = getHomeBase(self, world);
	if (getEnemyMinionsWithinDistance(home, world, home.getVisionRange()).size() > 0) return true;
	if (getEnemyWizardsWithinDistance(home, world, home.getVisionRange()).size() > 0) return true;
	return false;
}

bool isTargetedByMinion(const Minion& minion, const World& world) {
	for (const auto& u : world.getMinions()) {
		if (u.getId() != minion.getId() && u.getFaction() != minion.getFaction() && u.getFaction() != FACTION_NEUTRAL) {
			if (!hasOtherTarget(minion, u, world)) {
				return true;
			}
		}
	}
	return false;
}
void setActionTarget(const Wizard& self, const World& world, const Game& game, Action& action) {
	// set target to nearest enemy
	// not necessarily in attackRange
	// maximum attack range = 600 (max cast range) + 100 (max radius: enemy base) + 10  + 15 * getForwardSpeed
	double searchDistance = 710 + 15 * getForwardSpeed(self, game);

	// TODO verbessern
	// one shoot, relevant für Wizards, Buildings
	// verschwunden bevor das Projectile die unit trifft, relevant für Minions, Buildings

	vector<UnitDistance<Building>> enemyBuildings = getEnemyBuildingsWithinDistance(self, searchDistance);
	vector<UnitDistance<Wizard>> enemyWizards = getEnemyWizardsWithinDistance(self, world, searchDistance);
	vector<UnitDistance<Minion>> enemyMinions = getEnemyMinionsWithinDistance(self, world, searchDistance);

	// action.actionPoint;
	action.actionTarget = _TARGET_UNKNOWN_;
	action.actionInAttackRange = false;
	double distanceToEnemy = game.getMapSize();

	for (const auto& enemy : enemyBuildings) {
		if (enemy.distanceToSelf < distanceToEnemy) {
			distanceToEnemy = enemy.distanceToSelf;
			if (distanceToEnemy < getAttackRangeEnemyUnit(self, game, enemy.unit)) {
				action.actionInAttackRange = true;
			}
			if (distanceToEnemy < getStaffAttackRangeEnemyUnit(enemy.unit, game)) {
				action.actionInStaffRange = true;
			}
			action.actionBuilding = enemy.unit;
			if (enemy.unit.getType() == BUILDING_FACTION_BASE) {
				action.actionTarget = TARGET_ENEMY_BASE;
			}
			else {
				action.actionTarget = TARGET_TOWER;
			}
			action.actionPoint = Point(enemy.unit);
		}
	}
	for (const auto& enemy : enemyWizards) {
		if (enemy.distanceToSelf < distanceToEnemy) {
			distanceToEnemy = enemy.distanceToSelf;
			if (distanceToEnemy < getAttackRangeEnemyUnit(self, game, enemy.unit)) {
				action.actionInAttackRange = true;
			}
			if (distanceToEnemy < getStaffAttackRangeEnemyUnit(enemy.unit, game)) {
				action.actionInStaffRange = true;
			}
			action.actionWizard = enemy.unit;
			action.actionTarget = TARGET_WIZARD;
			action.actionPoint = Point(enemy.unit);
		}
	}
	for (const auto& enemy : enemyMinions) {
		if (enemy.distanceToSelf < distanceToEnemy) {
			if (!isTargetedByMinion(enemy.unit, world) || !hasOtherTarget(self, enemy.unit, world)) {
				distanceToEnemy = enemy.distanceToSelf;
				if (distanceToEnemy < getAttackRangeEnemyUnit(self, game, enemy.unit)) {
					action.actionInAttackRange = true;
				}
				if (distanceToEnemy < getStaffAttackRangeEnemyUnit(enemy.unit, game)) {
					action.actionInStaffRange = true;
				}
				action.actionMinion = enemy.unit;
				action.actionTarget = TARGET_MINION;
				action.actionPoint = Point(enemy.unit);
			}
		}
	}
}

int getMinimumTicksUntilNextAction(const Wizard& self) {
	int minTicks = self.getRemainingCooldownTicksByAction()[ACTION_MAGIC_MISSILE];
	if (activeSkills[SKILL_FIREBALL]) {
		minTicks = min(minTicks, self.getRemainingCooldownTicksByAction()[ACTION_FIREBALL]);
	}
	if (activeSkills[SKILL_FROST_BOLT]) {
		minTicks = min(minTicks, self.getRemainingCooldownTicksByAction()[ACTION_FROST_BOLT]);
	}

	return minTicks;
}
void setMove(const Wizard& self, const World& world, const Game& game, Move& move, const Action& action) {
	// set move
	MovePoint nextMove = nextMoveTowardsTarget(self, world, game, action.movePoint);
	move.setSpeed(nextMove.getSpeed());
	move.setStrafeSpeed(nextMove.getStrafe());

	// set turn angle and action
	// maximum attack range = 600 (max cast range) + 100 (max radius: enemy base) + 10  + 15 * getForwardSpeed

	// default
	double angleMove = self.getAngleTo(nextMove);
	// turn towards movePoint
	move.setTurn(angleMove);
	move.setAction(ACTION_NONE);

	if (action.actionTarget != _TARGET_UNKNOWN_) {
		double dist = self.getDistanceTo(action.actionPoint);
		// greater attackRange + 15 * speed (15 * 6° = 90°)

		double radius = 0;
		double attackRange = 0;
		if (action.actionTarget == TARGET_ENEMY_BASE || action.actionTarget == TARGET_TOWER) {
			attackRange = getAttackRangeEnemyUnit(self, game, action.actionBuilding);
			radius = action.actionBuilding.getRadius();
		}
		if (action.actionTarget == TARGET_WIZARD) {
			attackRange = getAttackRangeEnemyUnit(self, game, action.actionWizard);
			radius = action.actionWizard.getRadius();
		}
		if (action.actionTarget == TARGET_MINION) {
			attackRange = getAttackRangeEnemyUnit(self, game, action.actionMinion);
			radius = action.actionMinion.getRadius();
		}

		if (dist < attackRange + 15 * getForwardSpeed(self, game)) {
			// turn towards actionPoint
			double angleAction = self.getAngleTo(action.actionPoint);
			double neededTurns = abs(angleAction) / game.getWizardMaxTurnAngle();
			double availableTurns = getMinimumTicksUntilNextAction(self) + 1.5;

			if (neededTurns > availableTurns) {
				move.setTurn(angleAction);

				// check in attack range
				double castAngle = self.getAngleTo(action.actionPoint);
				if (action.actionInStaffRange && abs(castAngle) < (game.getStaffSector() / 2.) && self.getRemainingActionCooldownTicks() == 0 && self.getRemainingCooldownTicksByAction()[ACTION_STAFF] == 0) {
					move.setAction(ACTION_STAFF);
				} else if (action.actionInAttackRange) {
					double castAngle = self.getAngleTo(action.actionPoint);

					// check in cast angle range
					if (abs(castAngle) < (game.getStaffSector() / 2.)) {
						move.setCastAngle(castAngle);

						// check any action ready
						if (self.getRemainingActionCooldownTicks() == 0) {

							//  use fireball and frost bolt only for wizards and minions
							if (activeSkills[SKILL_FIREBALL] && self.getRemainingCooldownTicksByAction()[ACTION_FIREBALL] == 0) {
								move.setAction(ACTION_FIREBALL);
								move.setCastAngle(angleAction);
								move.setMinCastDistance(dist - radius + game.getFireballRadius());
							}
							else if (activeSkills[SKILL_FROST_BOLT] && self.getRemainingCooldownTicksByAction()[ACTION_FROST_BOLT] == 0 && (action.actionTarget == TARGET_WIZARD || action.actionTarget == TARGET_MINION)) {
								move.setAction(ACTION_FROST_BOLT);
								move.setCastAngle(angleAction);
								move.setMinCastDistance(dist - radius + game.getFrostBoltRadius());
								
							}
							else if (self.getRemainingCooldownTicksByAction()[ACTION_MAGIC_MISSILE] == 0) {
								move.setAction(ACTION_MAGIC_MISSILE);
								move.setCastAngle(angleAction);
								move.setMinCastDistance(dist - radius + game.getMagicMissileRadius());
							}

							move.setMaxCastDistance(dist + 61);
						}
					}
				}
			}
		}
	}
	// FIREBALL & FROST nur auf Wizards und Minions die nicht durch diesen Schuss verschwinden

	// bei actionTarget = wizard, berücksichtigen wie wahrscheinlich es ist das der Schuss trifft im Verhältnis zum aktuellen Mana
	// und zum verwendeten Typ (es zahlt sich nicht aus einen Wizard anzugreifen wenn er mit einem Schritt aus der Gefahrenzone ist
	// und dadurch ziemlich viel Mana verbraucht wird weil Fire oder Frost verwendet worden ist)

	// Mana berücksichtigen

	// status des Targets berücksichtigen, wenn schon gefrostet nicht nochmals
}

bool bonusPointInVision(const Wizard& self, const Action& action) {
	double dist = self.getDistanceTo(action.movePoint);

	if (action.moveTarget == TARGET_BONUS) {
		if (dist < self.getVisionRange()) {
			return true;
		}
	}

	return false;
}
bool check5_2(const World& world) {
	string me = "OneMoreTurn";
	int count = 0;
	for (const auto& p : world.getPlayers()) {
		if (p.getName().find(me) != std::string::npos) {
			count += 1;
		}
	}
	return count > 1;
}

MyStrategy::MyStrategy() {
	// some tests at the beginning
	//Point a(3, 5);
	//Point b(1, 3);
	//Point c(4, 6);
	//double r = 1.5;
	//cout << isLineCircleIntersectionPoint(a, b, c, r) << endl;

	initBonuses();
}
void MyStrategy::move(const Wizard& self,  const World& world, const Game& game, Move& move) {
	tick = world.getTickIndex();
	mapUpdated = false;
	ignoreEverything = false;

	high_resolution_clock::time_point startTime = high_resolution_clock::now();

	if (debug) cout << "input " << move << endl;
	if (debug) cout << "input " << self << endl;

	if (tick == 0) {
		// setups
		initEnemyBuildings(self, world);
		homeBase = getHomeBase(self, world);
		setGlobalLane(self, world);
	} else {
		// updates
		updateEnemyBuildings(self, world);
		updateBonusData(self, world, game);
		updateActiveSkills(self, world, game);
		updateProjectileData(self, world, game);
	}

	if (tick <= 200 && tick >= 100 && tick%10 == 0) {
		checkGlobalLane(self, world);
	}

	// learn skill
	if (game.isSkillsEnabled() && isNextSkillPossible(self)) {
		move.setSkillToLearn(learnNextSkill(self));
		activeSkillLevel += 1;
	}

	// set move target
	Action action;
	setOverallTarget(self, world, game, action);
	//TODO setze target auf enemies (towers, wizards) die gleich untergehen und dadurch punkte bringen, vorallem die Türme am weg
	
	//setBonusPrepareTarget(self, world, game, action);
	//setBonusOnMapTarget(self, world, game, action);

	// set overall targets
	//if (baseDefence(self, world)) {
	//	// TODO check 650, setze Targets richtig wenn in base vision radius
	//	if (self.getDistanceTo(getHomeBase(self, world)) > 650) {
	//		ignoreEverything = true;
	//	}
	//	action.moveTarget = TARGET_HOME_BASE; action.movePoint = Point(getHomeBase(self,world));
	//}

	// check
	// check Danger Zone
	// check Projectiles
	if (!ignoreEverything && !bonusPointInVision(self, action)) {
		if (checkDangerZoneEnemies(self, world, game, action)) {
			;
		}
		if (checkProjectiles(self, world, game, move, action)) {
			;
		}
	}

	// Idee
	// einzel fall betrachtung, one and two shot unit
	// Bonus holen trotz DangerZone und Projectiles

	// nach MovePoint den ActionPoint bestimmen
	setActionTarget(self, world, game, action);

	// Idee
	// Danger Zone aufheben wenn die unit nur mehr wenig leben hat, wie genau z.B. Tower < 24.01 oder abhängig von MAgic Missle schaden von mir, darf nur mehr 2 Schüsse überleben
	// damit ich ja sicher auch Punkte bekomme wenn gekillt


	// Idee
	// noch diverse Checks und dadurch andere Wege (z.B. nicht in die Schusslinie von Türme, wenn man der einzige im Radius ist
	// Türme braucht man nicht näher als 500-x, können sich nicht bewegen
	// nicht in die Nähe von anderen Wizards

	setMove(self, world, game, move, action);
	//if (debug) cout << "output " << move << endl;

	high_resolution_clock::time_point endTime = high_resolution_clock::now();
	duration<double> time_span = duration_cast<duration<double>>(endTime - startTime);
	if (timeDebug) if (tick%500==0) cout << "tick:" << tick << " time count:" << time_span.count() << " seconds " << " all time:" << endl;
	//if (debug) cout << "------------------------" << endl;
}

// Idee
// A. target idenitifizieren
// die nächste gegnerische Einheit
// ev. check ob die nächste gegnerische Einheit nicht sowieso von Minions erledigt wird -> Mana sparen




bool minionNextRoundNotAliveForSure(const Minion& minion, const World& world) {
	// alle gegnerischen Minions


	return true;
}
