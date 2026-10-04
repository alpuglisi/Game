#pragma once
#include <cstdint>
#include <vector>
#include "sand.hpp"
#include "vec2.hpp"

enum ShapeType { SHAPE_BOX = 0, SHAPE_CIRCLE = 1 };

struct Body {
    int id = -1;
    int seq = 0;  // creation order (higher = on top)
    bool alive = false;
    ShapeType shape = SHAPE_BOX;
    Vec2 pos, vel;
    float angle = 0, w = 0;
    Vec2 half;          // box half extents
    float radius = 0;   // circle radius
    float density = 1.5f;
    float area = 0, mass = 0, invMass = 0, invI = 0, bound = 0;
    bool isStatic = false, isWheel = false, isRocket = false;
    uint32_t color = 0xe0b060;
    // fluid coupling, refreshed once per frame
    float wetFrac = 0, granFrac = 0, fluidRho = 0;
    Vec2 wetCentroid;

    Vec2 toWorld(Vec2 l) const { return pos + rotate(l, angle); }
    Vec2 toLocal(Vec2 p) const { return rotate(p - pos, -angle); }
    bool contains(Vec2 p) const;
};

enum JointType { J_PIN, J_MOTOR, J_DISTANCE, J_MOUSE };

struct Joint {
    int id = -1;
    bool alive = false;
    JointType type = J_PIN;
    int a = -1, b = -1;  // body ids; -1 = the world
    Vec2 la, lb;         // local anchors (world coordinates when body is -1)
    // distance: freq 0 = rigid rod, otherwise a soft spring
    float length = 0, freq = 0, damping = 0.4f;
    // motor
    float speed = 6.f, power = 60.f;
    bool keyed = true;
    // mouse
    float maxForce = 0;
    // solver cache
    Vec2 rA, rB, u, bias, accP;
    float k11 = 0, k12 = 0, k22 = 0;
    float effMass = 0, gamma = 0, beta = 0, accImp = 0, maxImp = 0;
};

struct Contact {
    int a, b;  // a may be -1 (terrain). Normal points from a to b.
    Vec2 p, n;
    float depth, mu;
    Vec2 rA, rB;
    float massN = 0, massT = 0, vt = 0, jn = 0, jt = 0;
    Contact(int a_, int b_, Vec2 p_, Vec2 n_, float depth_, float mu_) : a(a_), b(b_), p(p_), n(n_), depth(depth_), mu(mu_) {}
};

class Physics {
public:
    explicit Physics(World* w);

    std::vector<Body> bodies;
    std::vector<Joint> joints;
    Vec2 gravity{0.f, 260.f};
    float motorInput = 0.f;  // -1..1, drives keyed motors
    bool thrustOn = false;   // fires rockets

    void clear();
    void step(float dt);
    void stampBodies();

    int addBox(Vec2 c, Vec2 half, float angle, float density, bool stat);
    int addCircle(Vec2 c, float r, float density, bool stat, bool wheel);
    int addRocket(Vec2 c, float angle);
    int addPin(Vec2 anchor, int a, int b, bool motor, bool keyed);
    int addDistance(int a, Vec2 pa, int b, Vec2 pb, float freq);
    int addMouse(int body, Vec2 anchor);
    void setMouseTarget(int joint, Vec2 target);
    void removeJoint(int id);
    void removeBody(int id);

    int pickBody(Vec2 p, bool includeStatic, int exclude = -1) const;
    std::vector<int> bodiesAt(Vec2 p, int exclude = -1) const;  // ascending seq
    int nearestJoint(Vec2 p, float maxDist) const;
    Vec2 jointAnchorA(const Joint& j) const;
    Vec2 jointAnchorB(const Joint& j) const;
    int bodyCount() const;

private:
    World* world;
    Body worldBody;
    std::vector<Contact> contacts;
    std::vector<uint64_t> noCollide;
    int seqCounter = 0;

    Body& B(int id) { return id >= 0 ? bodies[id] : worldBody; }
    int allocBody();
    int allocJoint();
    void finalize(Body& b);
    void applyBlasts();
    void sampleFluids();
    void substep(float h);
    void buildContacts();
    void terrainContacts(Body& b);
    void collidePair(Body& a, Body& b);
    void prestepJoint(Joint& j, float h);
    void solveJoint(Joint& j, float h);
    void prestepContact(Contact& c, float h);
    void solveContact(Contact& c);
    bool connected(int a, int b) const;
    Vec2 surfaceNormal(int ix, int iy) const;
};
