#include "World.h"

#include "base/WorkQueue.h"
#include "microprofile.h"

World::World()
    : gravity(0)
{
}

RigidBody* World::AddBody(Coords2f coords, Vector2f size)
{
    RigidBody newbie(coords, size, 1e-5f);
    newbie.index = bodies.size();
    bodies.push_back(newbie);
    return &(bodies[bodies.size() - 1]);
}

void World::Update(WorkQueue& queue, float dt, SolveMode mode, int contactIterationsCount, int penetrationIterationsCount)
{
    MICROPROFILE_SCOPEI("Physics", "Update", 0x00ff00);

    collisionTime = mergeTime = solveTime = 0;

    ApplyGravity();
    IntegrateVelocity(dt);

    collider.UpdateBroadphase(bodies.data(), bodies.size());
    collider.UpdatePairs(queue, bodies.data(), bodies.size());
    collider.UpdateManifolds(queue);
    collider.PackManifolds();

    RefreshContactJoints();

    switch (mode)
    {
    case Solve_AoS:
        iterations = solver.SolveJointsAoS(queue, bodies.data(), bodies.size(), contactIterationsCount, penetrationIterationsCount);
        break;

    case Solve_SoA_Scalar:
        iterations = solver.SolveJointsSoA_Scalar(queue, bodies.data(), bodies.size(), collider.contactPoints.data, contactIterationsCount, penetrationIterationsCount);
        break;

    case Solve_SoA_SSE2:
        iterations = solver.SolveJointsSoA_SSE2(queue, bodies.data(), bodies.size(), collider.contactPoints.data, contactIterationsCount, penetrationIterationsCount);
        break;

#ifdef __AVX2__
    case Solve_SoA_AVX2:
        iterations = solver.SolveJointsSoA_AVX2(queue, bodies.data(), bodies.size(), collider.contactPoints.data, contactIterationsCount, penetrationIterationsCount);
        break;
#endif

    default:
        assert(!"Unknown solver mode");
        break;
    }

    IntegratePosition(dt);
}

NOINLINE void World::ApplyGravity()
{
    MICROPROFILE_SCOPEI("Physics", "ApplyGravity", -1);

    for (size_t bodyIndex = 0; bodyIndex < bodies.size(); bodyIndex++)
    {
        RigidBody* body = &bodies[bodyIndex];

        if (body->invMass > 0.0f)
        {
            body->acceleration.y += gravity;
        }
    }
}

NOINLINE void World::IntegrateVelocity(float dt)
{
    MICROPROFILE_SCOPEI("Physics", "IntegrateVelocity", -1);

    for (size_t bodyIndex = 0; bodyIndex < bodies.size(); bodyIndex++)
    {
        bodies[bodyIndex].IntegrateVelocity(dt);
    }
}

NOINLINE void World::IntegratePosition(float dt)
{
    MICROPROFILE_SCOPEI("Physics", "IntegratePosition", -1);

    for (size_t bodyIndex = 0; bodyIndex < bodies.size(); bodyIndex++)
    {
        bodies[bodyIndex].IntegratePosition(dt);
    }
}

NOINLINE void World::RefreshContactJoints()
{
    MICROPROFILE_SCOPEI("Physics", "RefreshContactJoints", -1);

    for (size_t jointIndex = 0; jointIndex < solver.contactJoints.size(); jointIndex++)
    {
        solver.contactJoints[jointIndex].collision = 0;
    }

    for (size_t manifoldIndex = 0; manifoldIndex < collider.manifolds.size(); ++manifoldIndex)
    {
        Manifold& man = collider.manifolds[manifoldIndex];

        for (int collisionIndex = 0; collisionIndex < man.pointCount; collisionIndex++)
        {
            ContactPoint& col = collider.contactPoints[man.pointIndex + collisionIndex];

            if (col.solverIndex < 0)
            {
                solver.contactJoints.push_back(ContactJoint(man.body1, man.body2, &col, man.pointIndex + collisionIndex, solver.contactJoints.size()));
            }
            else
            {
                ContactJoint& joint = solver.contactJoints[col.solverIndex];

                assert(joint.body1 == man.body1);
                assert(joint.body2 == man.body2);

                joint.collision = &col;
                joint.collisionIndex = man.pointIndex + collisionIndex;
            }
        }
    }

    for (size_t jointIndex = 0; jointIndex < solver.contactJoints.size();)
    {
        ContactJoint& joint = solver.contactJoints[jointIndex];

        if (!joint.collision)
        {
            joint = solver.contactJoints.back();
            solver.contactJoints.pop_back();
        }
        else
        {
            joint.collision->solverIndex = jointIndex;
            jointIndex++;
        }
    }
}