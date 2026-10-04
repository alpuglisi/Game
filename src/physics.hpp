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
    uint8_t mat = M_STEEL;
    float temp = AMBIENT_T;
    float density = 1.5f;
    float area = 0, mass = 0, invMass = 0, invI = 0, bound = 0;
    bool isStatic = false, isWheel = false, isRocket = false;
    bool touching = false, hasJoint = false;  // per-step bookkeeping for the rest clamp
    int group = -1;     // rigid group (weld-linked bodies act as one object); -1 = none
    uint32_t color = 0xe0b060;
    // pressure of adjacent gas/liquid, refreshed once per frame
    Vec2 fluidF;
    float fluidT = 0, fluidC = 0;
    // fluid coupling, refreshed once per frame
    float wetFrac = 0, granFrac = 0, fluidRho = 0;
    Vec2 wetCentroid;

    Vec2 toWorld(Vec2 l) const { return pos + rotate(l, angle); }
    Vec2 toLocal(Vec2 p) const { return rotate(p - pos, -angle); }
    bool contains(Vec2 p) const;
};

enum JointType { J_PIN, J_MOTOR, J_DISTANCE, J_MOUSE, J_SLIDER };

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
    int group = -1;  // >= 0: one half of a weld holding a group together
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
    float depth, mu, e;
    Vec2 rA, rB;
    float massN = 0, massT = 0, vt = 0, jn = 0, jt = 0;
    Contact(int a_, int b_, Vec2 p_, Vec2 n_, float depth_, float mu_, float e_)
        : a(a_), b(b_), p(p_), n(n_), depth(depth_), mu(mu_), e(e_) {}
};

// A closed body of liquid shared by several rigid bodies: it can not be compressed, so the volume the
// bodies sweep out must sum to zero (Pascal's law). Re-derived from the grid every frame.
struct HydroLink {
    int body;
    Vec2 a;  // sum of face normals pointing from the body into the liquid
};
struct HydroGroup {
    std::vector<HydroLink> links;
    float acc = 0.f;
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

    int addBox(Vec2 c, Vec2 half, float angle, uint8_t mat, bool stat);
    int addCircle(Vec2 c, float r, uint8_t mat, bool stat, bool wheel);
    int addRocket(Vec2 c, float angle);
    int addPin(Vec2 anchor, int a, int b, bool motor, bool keyed);
    int addDistance(int a, Vec2 pa, int b, Vec2 pb, float freq);
    int addMouse(int body, Vec2 anchor);
    int addSlider(int body, Vec2 axis);  // confine a body to a line through its centre; rotation locked
    void setMouseTarget(int joint, Vec2 target);

    // ---- groups ("layers"): members are welded into one rigid object but stay individually editable
    int groupBodies(const std::vector<int>& ids);      // returns the group id (merges existing groups)
    void ungroup(int g);
    void rebuildGroup(int g);                          // re-weld at the current poses
    std::vector<int> groupMembers(int g) const;
    // exact edits: size (half/radius), pose and static flag of one body; welds and joints follow
    void reshape(int id, Vec2 pos, Vec2 half, float radius, float angle, uint8_t mat, bool stat);
    void transformGroup(int primary, Vec2 newPos, float newAngle);  // move/rotate a whole group about `primary`
    // hollow tube between two points: two welded walls. Returns the group id.
    int addPipe(Vec2 a, Vec2 b, float outerD, float wall, uint8_t mat, bool stat);
    // flexible tube: chain of pipe segments hinged together. Returns the first segment's group id.
    int addHose(Vec2 a, Vec2 b, float outerD, float wall, int segments, uint8_t mat, bool stat);
    void removeJoint(int id);
    void removeBody(int id);

    int pickBody(Vec2 p, bool includeStatic, int exclude = -1) const;
    std::vector<int> bodiesAt(Vec2 p, int exclude = -1) const;  // ascending seq
    int nearestJoint(Vec2 p, float maxDist) const;
    Vec2 jointAnchorA(const Joint& j) const;
    Vec2 jointAnchorB(const Joint& j) const;
    int bodyCount() const;
    void dumpContacts(int body) const;  // dev

private:
    World* world;
    Body worldBody;
    std::vector<Contact> contacts;
    std::vector<HydroGroup> hydro;
    std::vector<uint64_t> noCollide;
    int seqCounter = 0;
    int groupCounter = 0;
    void weldPair(int root, int member);

    Body& B(int id) { return id >= 0 ? bodies[id] : worldBody; }
    int allocBody();
    int allocJoint();
    void finalize(Body& b);
    void applyBlasts();
    void fluidForces();
    void solveHydro();
    void sampleFluids();
    void thermalStep();
    void dissolve(Body& b);
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
