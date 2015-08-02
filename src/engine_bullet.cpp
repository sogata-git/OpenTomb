
#include <stdio.h>
#include <stdlib.h>

#include "bullet/btBulletCollisionCommon.h"
#include "bullet/btBulletDynamicsCommon.h"
#include "bullet/BulletCollision/CollisionDispatch/btGhostObject.h"
#include "bullet/BulletCollision/CollisionDispatch/btGhostObject.h"

#include "core/gl_util.h"
#include "core/gl_font.h"
#include "core/console.h"
#include "core/obb.h"
#include "engine.h"
#include "character_controller.h"
#include "frustum.h"
#include "portal.h"
#include "engine_bullet.h"
#include "entity.h"
#include "render.h"
#include "resource.h"
#include "world.h"


btDefaultCollisionConfiguration         *bt_engine_collisionConfiguration;
btCollisionDispatcher                   *bt_engine_dispatcher;
btGhostPairCallback                     *bt_engine_ghostPairCallback;
btBroadphaseInterface                   *bt_engine_overlappingPairCache;
btSequentialImpulseConstraintSolver     *bt_engine_solver;
btDiscreteDynamicsWorld                 *bt_engine_dynamicsWorld;


render_DebugDrawer                       debugDrawer;

#define DEBUG_DRAWER_DEFAULT_BUFFER_SIZE        (128 * 1024)


// Bullet Physics initialization.
void Engine_BTInit()
{
    ///collision configuration contains default setup for memory, collision setup. Advanced users can create their own configuration.
    bt_engine_collisionConfiguration = new btDefaultCollisionConfiguration();

    ///use the default collision dispatcher. For parallel processing you can use a diffent dispatcher (see Extras/BulletMultiThreaded)
    bt_engine_dispatcher = new btCollisionDispatcher(bt_engine_collisionConfiguration);
    bt_engine_dispatcher->setNearCallback(Engine_RoomNearCallback);

    ///btDbvtBroadphase is a good general purpose broadphase. You can also try out btAxis3Sweep.
    bt_engine_overlappingPairCache = new btDbvtBroadphase();
    bt_engine_ghostPairCallback = new btGhostPairCallback();
    bt_engine_overlappingPairCache->getOverlappingPairCache()->setInternalGhostPairCallback(bt_engine_ghostPairCallback);

    ///the default constraint solver. For parallel processing you can use a different solver (see Extras/BulletMultiThreaded)
    bt_engine_solver = new btSequentialImpulseConstraintSolver;

    bt_engine_dynamicsWorld = new btDiscreteDynamicsWorld(bt_engine_dispatcher, bt_engine_overlappingPairCache, bt_engine_solver, bt_engine_collisionConfiguration);
    bt_engine_dynamicsWorld->setInternalTickCallback(Engine_InternalTickCallback);
    bt_engine_dynamicsWorld->setGravity(btVector3(0, 0, -4500.0));

    debugDrawer.setDebugMode(btIDebugDraw::DBG_DrawWireframe | btIDebugDraw::DBG_DrawConstraints);
    bt_engine_dynamicsWorld->setDebugDrawer(&debugDrawer);
}


void Engine_BTDestroy()
{
    //delete dynamics world
    delete bt_engine_dynamicsWorld;

    //delete solver
    delete bt_engine_solver;

    //delete broadphase
    delete bt_engine_overlappingPairCache;

    //delete dispatcher
    delete bt_engine_dispatcher;

    delete bt_engine_collisionConfiguration;

    delete bt_engine_ghostPairCallback;
}

/**
 * overlapping room collision filter
 */
void Engine_RoomNearCallback(btBroadphasePair& collisionPair, btCollisionDispatcher& dispatcher, const btDispatcherInfo& dispatchInfo)
{
    engine_container_p c0, c1;
    room_p r0 = NULL, r1 = NULL;

    c0 = (engine_container_p)((btCollisionObject*)collisionPair.m_pProxy0->m_clientObject)->getUserPointer();
    r0 = (c0)?(c0->room):(NULL);
    c1 = (engine_container_p)((btCollisionObject*)collisionPair.m_pProxy1->m_clientObject)->getUserPointer();
    r1 = (c1)?(c1->room):(NULL);

    if(c1 && c1 == c0)
    {
        if(((btCollisionObject*)collisionPair.m_pProxy0->m_clientObject)->isStaticOrKinematicObject() ||
           ((btCollisionObject*)collisionPair.m_pProxy1->m_clientObject)->isStaticOrKinematicObject())
        {
            return;                                                             // No self interaction
        }
        dispatcher.defaultNearCallback(collisionPair, dispatcher, dispatchInfo);
        return;
    }

    if(!r0 && !r1)
    {
        dispatcher.defaultNearCallback(collisionPair, dispatcher, dispatchInfo);// Both are out of rooms
        return;
    }

    if(r0 && r1)
    {
        if(Room_IsInNearRoomsList(r0, r1))
        {
            dispatcher.defaultNearCallback(collisionPair, dispatcher, dispatchInfo);
            return;
        }
        else
        {
            return;
        }
    }
}

/**
 * update current room of bullet object
 */
void Engine_InternalTickCallback(btDynamicsWorld *world, btScalar timeStep)
{
    for(int i=world->getNumCollisionObjects()-1;i>=0;i--)
    {
        btCollisionObject* obj = bt_engine_dynamicsWorld->getCollisionObjectArray()[i];
        btRigidBody* body = btRigidBody::upcast(obj);
        if (body && !body->isStaticObject() && body->getMotionState())
        {
            btTransform trans;
            body->getMotionState()->getWorldTransform(trans);
            engine_container_p cont = (engine_container_p)body->getUserPointer();
            if(cont && (cont->object_type == OBJECT_BULLET_MISC))
            {
                cont->room = Room_FindPosCogerrence(trans.getOrigin().m_floats, cont->room);
            }
        }
    }
}


btCollisionShape *BT_CSfromBBox(btScalar *bb_min, btScalar *bb_max)
{
    obb_p obb = OBB_Create();
    polygon_p p = obb->base_polygons;
    btTriangleMesh *trimesh = new btTriangleMesh;
    btVector3 v0, v1, v2;
    btCollisionShape* ret;
    int cnt = 0;

    OBB_Rebuild(obb, bb_min, bb_max);
    for(uint16_t i=0;i<6;i++,p++)
    {
        if(Polygon_IsBroken(p))
        {
            continue;
        }
        for(uint16_t j=1;j+1<p->vertex_count;j++)
        {
            vec3_copy(v0.m_floats, p->vertices[j + 1].position);
            vec3_copy(v1.m_floats, p->vertices[j].position);
            vec3_copy(v2.m_floats, p->vertices[0].position);
            trimesh->addTriangle(v0, v1, v2, true);
        }
        cnt ++;
    }

    if(cnt == 0)                                                                // fixed: without that condition engine may easily crash
    {
        delete trimesh;
        return NULL;
    }

    OBB_Clear(obb);
    free(obb);

    ret = new btConvexTriangleMeshShape(trimesh, true);

    return ret;
}


btCollisionShape *BT_CSfromMesh(struct base_mesh_s *mesh, bool useCompression, bool buildBvh, bool is_static)
{
    uint32_t cnt = 0;
    polygon_p p;
    btTriangleMesh *trimesh = new btTriangleMesh;
    btCollisionShape* ret;
    btVector3 v0, v1, v2;

    p = mesh->polygons;
    for(uint32_t i=0;i<mesh->polygons_count;i++,p++)
    {
        if(Polygon_IsBroken(p))
        {
            continue;
        }

        for(uint16_t j=1;j+1<p->vertex_count;j++)
        {
            vec3_copy(v0.m_floats, p->vertices[j + 1].position);
            vec3_copy(v1.m_floats, p->vertices[j].position);
            vec3_copy(v2.m_floats, p->vertices[0].position);
            trimesh->addTriangle(v0, v1, v2, true);
        }
        cnt ++;
    }

    if(cnt == 0)
    {
        delete trimesh;
        return NULL;
    }

    if(is_static)
    {
        ret = new btBvhTriangleMeshShape(trimesh, useCompression, buildBvh);
    }
    else
    {
        ret = new btConvexTriangleMeshShape(trimesh, true);
    }

    return ret;
}

///@TODO: resolve cases with floor >> ceiling (I.E. floor - ceiling >= 2048)
btCollisionShape *BT_CSfromHeightmap(struct room_sector_s *heightmap, struct sector_tween_s *tweens, int tweens_size, bool useCompression, bool buildBvh)
{
    uint32_t cnt = 0;
    room_p r = heightmap->owner_room;
    btTriangleMesh *trimesh = new btTriangleMesh;
    btCollisionShape* ret;

    for(uint32_t i = 0; i < r->sectors_count; i++)
    {
        if( (heightmap[i].floor_penetration_config != TR_PENETRATION_CONFIG_GHOST) &&
            (heightmap[i].floor_penetration_config != TR_PENETRATION_CONFIG_WALL )  )
        {
            if( (heightmap[i].floor_diagonal_type == TR_SECTOR_DIAGONAL_TYPE_NONE) ||
                (heightmap[i].floor_diagonal_type == TR_SECTOR_DIAGONAL_TYPE_NW  )  )
            {
                if(heightmap[i].floor_penetration_config != TR_PENETRATION_CONFIG_DOOR_VERTICAL_A)
                {
                    trimesh->addTriangle(heightmap[i].floor_corners[3],
                                         heightmap[i].floor_corners[2],
                                         heightmap[i].floor_corners[0],
                                         true);
                    cnt++;
                }

                if(heightmap[i].floor_penetration_config != TR_PENETRATION_CONFIG_DOOR_VERTICAL_B)
                {
                    trimesh->addTriangle(heightmap[i].floor_corners[2],
                                         heightmap[i].floor_corners[1],
                                         heightmap[i].floor_corners[0],
                                         true);
                    cnt++;
                }
            }
            else
            {
                if(heightmap[i].floor_penetration_config != TR_PENETRATION_CONFIG_DOOR_VERTICAL_A)
                {
                    trimesh->addTriangle(heightmap[i].floor_corners[3],
                                         heightmap[i].floor_corners[2],
                                         heightmap[i].floor_corners[1],
                                         true);
                    cnt++;
                }

                if(heightmap[i].floor_penetration_config != TR_PENETRATION_CONFIG_DOOR_VERTICAL_B)
                {
                    trimesh->addTriangle(heightmap[i].floor_corners[3],
                                         heightmap[i].floor_corners[1],
                                         heightmap[i].floor_corners[0],
                                         true);
                    cnt++;
                }
            }
        }

        if( (heightmap[i].ceiling_penetration_config != TR_PENETRATION_CONFIG_GHOST) &&
            (heightmap[i].ceiling_penetration_config != TR_PENETRATION_CONFIG_WALL )  )
        {
            if( (heightmap[i].ceiling_diagonal_type == TR_SECTOR_DIAGONAL_TYPE_NONE) ||
                (heightmap[i].ceiling_diagonal_type == TR_SECTOR_DIAGONAL_TYPE_NW  )  )
            {
                if(heightmap[i].ceiling_penetration_config != TR_PENETRATION_CONFIG_DOOR_VERTICAL_A)
                {
                    trimesh->addTriangle(heightmap[i].ceiling_corners[0],
                                         heightmap[i].ceiling_corners[2],
                                         heightmap[i].ceiling_corners[3],
                                         true);
                    cnt++;
                }

                if(heightmap[i].ceiling_penetration_config != TR_PENETRATION_CONFIG_DOOR_VERTICAL_B)
                {
                    trimesh->addTriangle(heightmap[i].ceiling_corners[0],
                                         heightmap[i].ceiling_corners[1],
                                         heightmap[i].ceiling_corners[2],
                                         true);
                    cnt++;
                }
            }
            else
            {
                if(heightmap[i].ceiling_penetration_config != TR_PENETRATION_CONFIG_DOOR_VERTICAL_A)
                {
                    trimesh->addTriangle(heightmap[i].ceiling_corners[0],
                                         heightmap[i].ceiling_corners[1],
                                         heightmap[i].ceiling_corners[3],
                                         true);
                    cnt++;
                }

                if(heightmap[i].ceiling_penetration_config != TR_PENETRATION_CONFIG_DOOR_VERTICAL_B)
                {
                    trimesh->addTriangle(heightmap[i].ceiling_corners[1],
                                         heightmap[i].ceiling_corners[2],
                                         heightmap[i].ceiling_corners[3],
                                         true);
                    cnt++;
                }
            }
        }
    }

    for(int i=0; i<tweens_size; i++)
    {
        switch(tweens[i].ceiling_tween_type)
        {
            case TR_SECTOR_TWEEN_TYPE_2TRIANGLES:
                {
                    btScalar t = fabs((tweens[i].ceiling_corners[2].m_floats[2] - tweens[i].ceiling_corners[3].m_floats[2]) /
                                      (tweens[i].ceiling_corners[0].m_floats[2] - tweens[i].ceiling_corners[1].m_floats[2]));
                    t = 1.0 / (1.0 + t);
                    btVector3 o;
                    o.setInterpolate3(tweens[i].ceiling_corners[0], tweens[i].ceiling_corners[2], t);
                    trimesh->addTriangle(tweens[i].ceiling_corners[0],
                                         tweens[i].ceiling_corners[1],
                                         o, true);
                    trimesh->addTriangle(tweens[i].ceiling_corners[3],
                                         tweens[i].ceiling_corners[2],
                                         o, true);
                    cnt += 2;
                }
                break;

            case TR_SECTOR_TWEEN_TYPE_TRIANGLE_LEFT:
                trimesh->addTriangle(tweens[i].ceiling_corners[0],
                                     tweens[i].ceiling_corners[1],
                                     tweens[i].ceiling_corners[3],
                                     true);
                cnt++;
                break;

            case TR_SECTOR_TWEEN_TYPE_TRIANGLE_RIGHT:
                trimesh->addTriangle(tweens[i].ceiling_corners[2],
                                     tweens[i].ceiling_corners[1],
                                     tweens[i].ceiling_corners[3],
                                     true);
                cnt++;
                break;

            case TR_SECTOR_TWEEN_TYPE_QUAD:
                trimesh->addTriangle(tweens[i].ceiling_corners[0],
                                     tweens[i].ceiling_corners[1],
                                     tweens[i].ceiling_corners[3],
                                     true);
                trimesh->addTriangle(tweens[i].ceiling_corners[2],
                                     tweens[i].ceiling_corners[1],
                                     tweens[i].ceiling_corners[3],
                                     true);
                cnt += 2;
                break;
        };

        switch(tweens[i].floor_tween_type)
        {
            case TR_SECTOR_TWEEN_TYPE_2TRIANGLES:
                {
                    btScalar t = fabs((tweens[i].floor_corners[2].m_floats[2] - tweens[i].floor_corners[3].m_floats[2]) /
                                      (tweens[i].floor_corners[0].m_floats[2] - tweens[i].floor_corners[1].m_floats[2]));
                    t = 1.0 / (1.0 + t);
                    btVector3 o;
                    o.setInterpolate3(tweens[i].floor_corners[0], tweens[i].floor_corners[2], t);
                    trimesh->addTriangle(tweens[i].floor_corners[0],
                                         tweens[i].floor_corners[1],
                                         o, true);
                    trimesh->addTriangle(tweens[i].floor_corners[3],
                                         tweens[i].floor_corners[2],
                                         o, true);
                    cnt += 2;
                }
                break;

            case TR_SECTOR_TWEEN_TYPE_TRIANGLE_LEFT:
                trimesh->addTriangle(tweens[i].floor_corners[0],
                                     tweens[i].floor_corners[1],
                                     tweens[i].floor_corners[3],
                                     true);
                cnt++;
                break;

            case TR_SECTOR_TWEEN_TYPE_TRIANGLE_RIGHT:
                trimesh->addTriangle(tweens[i].floor_corners[2],
                                     tweens[i].floor_corners[1],
                                     tweens[i].floor_corners[3],
                                     true);
                cnt++;
                break;

            case TR_SECTOR_TWEEN_TYPE_QUAD:
                trimesh->addTriangle(tweens[i].floor_corners[0],
                                     tweens[i].floor_corners[1],
                                     tweens[i].floor_corners[3],
                                     true);
                trimesh->addTriangle(tweens[i].floor_corners[2],
                                     tweens[i].floor_corners[1],
                                     tweens[i].floor_corners[3],
                                     true);
                cnt += 2;
                break;
        };
    }

    if(cnt == 0)
    {
        delete trimesh;
        return NULL;
    }

    ret = new btBvhTriangleMeshShape(trimesh, useCompression, buildBvh);
    return ret;
}

/*
 * =============================================================================
 */

void BT_GenEntityRigidBody(struct entity_s *ent)
{
    btScalar tr[16];
    btVector3 localInertia(0, 0, 0);
    btTransform startTransform;

    if(ent->bf.animations.model == NULL)
    {
        return;
    }

    ent->physics.bt_body = (btRigidBody**)malloc(ent->bf.bone_tag_count * sizeof(btRigidBody*));

    for(uint16_t i=0;i<ent->bf.bone_tag_count;i++)
    {
        base_mesh_p mesh = ent->bf.animations.model->mesh_tree[i].mesh_base;
        btCollisionShape *cshape = NULL;
        switch(ent->self->collision_shape)
        {
            case COLLISION_SHAPE_TRIMESH_CONVEX:
                cshape = BT_CSfromMesh(mesh, true, true, false);
                break;

            case COLLISION_SHAPE_TRIMESH:
                cshape = BT_CSfromMesh(mesh, true, true, true);
                break;

            case COLLISION_SHAPE_BOX:
                cshape = BT_CSfromBBox(mesh->bb_min, mesh->bb_max);
                break;

                ///@TODO: add other shapes implementation; may be change default;
            default:
                 cshape = BT_CSfromMesh(mesh, true, true, true);
                 break;
        };

        ent->physics.bt_body[i] = NULL;

        if(cshape)
        {
            cshape->calculateLocalInertia(0.0, localInertia);

            Mat4_Mat4_mul(tr, ent->transform, ent->bf.bone_tags[i].full_transform);
            startTransform.setFromOpenGLMatrix(tr);
            btDefaultMotionState* motionState = new btDefaultMotionState(startTransform);
            ent->physics.bt_body[i] = new btRigidBody(0.0, motionState, cshape, localInertia);
            //ent->physics.bt_body[i]->setCollisionFlags(ent->physics.bt_body[i]->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
            bt_engine_dynamicsWorld->addRigidBody(ent->physics.bt_body[i], COLLISION_GROUP_KINEMATIC, COLLISION_MASK_ALL);
            ent->physics.bt_body[i]->setUserPointer(ent->self);
            ent->physics.bt_body[i]->setUserIndex(i);
        }
    }
}


/**
 * It is from bullet_character_controller
 */
int Ghost_GetPenetrationFixVector(btPairCachingGhostObject *ghost, btManifoldArray *manifoldArray, btScalar correction[3])
{
    // Here we must refresh the overlapping paircache as the penetrating movement itself or the
    // previous recovery iteration might have used setWorldTransform and pushed us into an object
    // that is not in the previous cache contents from the last timestep, as will happen if we
    // are pushed into a new AABB overlap. Unhandled this means the next convex sweep gets stuck.
    //
    // Do this by calling the broadphase's setAabb with the moved AABB, this will update the broadphase
    // paircache and the ghostobject's internal paircache at the same time.    /BW

    int ret = 0;
    int num_pairs, manifolds_size;
    btBroadphasePairArray &pairArray = ghost->getOverlappingPairCache()->getOverlappingPairArray();
    btVector3 aabb_min, aabb_max, t;

    ghost->getCollisionShape()->getAabb(ghost->getWorldTransform(), aabb_min, aabb_max);
    bt_engine_dynamicsWorld->getBroadphase()->setAabb(ghost->getBroadphaseHandle(), aabb_min, aabb_max, bt_engine_dynamicsWorld->getDispatcher());
    bt_engine_dynamicsWorld->getDispatcher()->dispatchAllCollisionPairs(ghost->getOverlappingPairCache(), bt_engine_dynamicsWorld->getDispatchInfo(), bt_engine_dynamicsWorld->getDispatcher());

    vec3_set_zero(correction);
    num_pairs = ghost->getOverlappingPairCache()->getNumOverlappingPairs();
    for(int i=0;i<num_pairs;i++)
    {
        manifoldArray->clear();
        // do not use commented code: it prevents to collision skips.
        //btBroadphasePair &pair = pairArray[i];
        //btBroadphasePair* collisionPair = bt_engine_dynamicsWorld->getPairCache()->findPair(pair.m_pProxy0,pair.m_pProxy1);
        btBroadphasePair *collisionPair = &pairArray[i];

        if(!collisionPair)
        {
            continue;
        }

        if(collisionPair->m_algorithm)
        {
            collisionPair->m_algorithm->getAllContactManifolds(*manifoldArray);
        }

        manifolds_size = manifoldArray->size();
        for(int j=0;j<manifolds_size;j++)
        {
            btPersistentManifold* manifold = (*manifoldArray)[j];
            btScalar directionSign = manifold->getBody0() == ghost ? btScalar(-1.0) : btScalar(1.0);
            engine_container_p cont0 = (engine_container_p)manifold->getBody0()->getUserPointer();
            engine_container_p cont1 = (engine_container_p)manifold->getBody1()->getUserPointer();
            if((cont0->collision_type == COLLISION_TYPE_GHOST) || (cont1->collision_type == COLLISION_TYPE_GHOST))
            {
                continue;
            }
            for(int k=0;k<manifold->getNumContacts();k++)
            {
                const btManifoldPoint&pt = manifold->getContactPoint(k);
                btScalar dist = pt.getDistance();

                if(dist < 0.0)
                {
                    t = pt.m_normalWorldOnB * dist * directionSign;
                    vec3_add(correction, correction, t.m_floats)
                    ret++;
                }
            }
        }
    }

    return ret;
}


void Entity_GhostUpdate(struct entity_s *ent)
{
    if(ent->physics.ghostObjects != NULL)
    {
        if(ent->type_flags & ENTITY_TYPE_DYNAMIC)
        {
            btScalar tr[16], *v;
            btVector3 pos;

            for(uint16_t i=0;i<ent->bf.bone_tag_count;i++)
            {
                Mat4_Mat4_mul(tr, ent->transform, ent->bf.bone_tags[i].full_transform);
                v = ent->bf.animations.model->mesh_tree[i].mesh_base->centre;
                ent->physics.ghostObjects[i]->getWorldTransform().setFromOpenGLMatrix(tr);
                Mat4_vec3_mul_macro(pos.m_floats, tr, v);
                ent->physics.ghostObjects[i]->getWorldTransform().setOrigin(pos);
            }
        }
        else
        {
            btScalar tr[16], v[3];
            for(uint16_t i=0;i<ent->bf.bone_tag_count;i++)
            {
                ent->physics.bt_body[i]->getWorldTransform().getOpenGLMatrix(tr);
                Mat4_vec3_mul(v, tr, ent->bf.bone_tags[i].mesh_base->centre);
                vec3_copy(tr+12, v);
                ent->physics.ghostObjects[i]->getWorldTransform().setFromOpenGLMatrix(tr);
            }
        }
    }
}


void Entity_UpdateCurrentCollisions(struct entity_s *ent)
{
    if(ent->physics.ghostObjects != NULL)
    {
        btScalar tr[16], *v;
        btTransform orig_tr;
        btVector3 pos;

        for(uint16_t i=0;i<ent->bf.bone_tag_count;i++)
        {
            btPairCachingGhostObject *ghost = ent->physics.ghostObjects[i];
            entity_collision_node_p cn = ent->physics.last_collisions + i;

            cn->obj_count = 0;
            Mat4_Mat4_mul(tr, ent->transform, ent->bf.bone_tags[i].full_transform);
            v = ent->bf.animations.model->mesh_tree[i].mesh_base->centre;
            orig_tr = ghost->getWorldTransform();
            ghost->getWorldTransform().setFromOpenGLMatrix(tr);
            Mat4_vec3_mul_macro(pos.m_floats, tr, v);
            ghost->getWorldTransform().setOrigin(pos);

            btBroadphasePairArray &pairArray = ghost->getOverlappingPairCache()->getOverlappingPairArray();
            btVector3 aabb_min, aabb_max;

            ghost->getCollisionShape()->getAabb(ghost->getWorldTransform(), aabb_min, aabb_max);
            bt_engine_dynamicsWorld->getBroadphase()->setAabb(ghost->getBroadphaseHandle(), aabb_min, aabb_max, bt_engine_dynamicsWorld->getDispatcher());
            bt_engine_dynamicsWorld->getDispatcher()->dispatchAllCollisionPairs(ghost->getOverlappingPairCache(), bt_engine_dynamicsWorld->getDispatchInfo(), bt_engine_dynamicsWorld->getDispatcher());

            int num_pairs = ghost->getOverlappingPairCache()->getNumOverlappingPairs();
            for(int j=0;j<num_pairs;j++)
            {
                ent->physics.manifoldArray->clear();
                btBroadphasePair *collisionPair = &pairArray[j];

                if(!collisionPair)
                {
                    continue;
                }

                if(collisionPair->m_algorithm)
                {
                    collisionPair->m_algorithm->getAllContactManifolds(*ent->physics.manifoldArray);
                }

                for(int k=0;k<ent->physics.manifoldArray->size();k++)
                {
                    if(cn->obj_count < MAX_OBJECTS_IN_COLLSION_NODE - 1)
                    {
                        btPersistentManifold* manifold = (*ent->physics.manifoldArray)[k];
                        for(int c=0;c<manifold->getNumContacts();c++)               // c++ in C++
                        {
                            //const btManifoldPoint &pt = manifold->getContactPoint(c);
                            if(manifold->getContactPoint(c).getDistance() < 0.0)
                            {
                                cn->obj[cn->obj_count] = (btCollisionObject*)(*ent->physics.manifoldArray)[k]->getBody0();
                                if(ent->self == (engine_container_p)(cn->obj[cn->obj_count]->getUserPointer()))
                                {
                                    cn->obj[cn->obj_count] = (btCollisionObject*)(*ent->physics.manifoldArray)[k]->getBody1();
                                }
                                cn->obj_count++;                                    // do it once in current cycle, so condition (cn->obj_count < MAX_OBJECTS_IN_COLLSION_NODE - 1) located in correct place
                                break;
                            }
                        }
                    }
                }
            }
            ghost->setWorldTransform(orig_tr);
        }
    }
}


///@TODO: make experiment with convexSweepTest with spheres: no more iterative cycles;
int Entity_GetPenetrationFixVector(struct entity_s *ent, btScalar reaction[3], btScalar move_global[3])
{
    int ret = 0;

    vec3_set_zero(reaction);
    if((ent->physics.ghostObjects != NULL) && (ent->physics.no_fix_all == 0))
    {
        btScalar tmp[3], orig_pos[3];
        btScalar tr[16];

        vec3_copy(orig_pos, ent->transform + 12);
        for(uint16_t i=0;i<ent->bf.animations.model->collision_map_size;i++)
        {
            btTransform tr_current;
            btVector3 from, to, curr, move;
            btScalar move_len;
            uint16_t m = ent->bf.animations.model->collision_map[i];
            ss_bone_tag_p btag = ent->bf.bone_tags + m;

            if(btag->body_part & ent->physics.no_fix_skeletal_parts)
            {
                continue;
            }

            // antitunneling condition for main body parts, needs only in move case: ((move != NULL) && (btag->body_part & (BODY_PART_BODY_LOW | BODY_PART_BODY_UPPER)))
            if((btag->parent == NULL) || ((move_global != NULL) && (btag->body_part & (BODY_PART_BODY_LOW | BODY_PART_BODY_UPPER))))
            {
                from = ent->physics.ghostObjects[m]->getWorldTransform().getOrigin();
                from.m_floats[0] += ent->transform[12+0] - orig_pos[0];
                from.m_floats[1] += ent->transform[12+1] - orig_pos[1];
                from.m_floats[2] += ent->transform[12+2] - orig_pos[2];
            }
            else
            {
                btScalar parent_from[3];
                Mat4_vec3_mul(parent_from, btag->parent->full_transform, btag->parent->mesh_base->centre);
                Mat4_vec3_mul(from.m_floats, ent->transform, parent_from);
            }

            Mat4_Mat4_mul(tr, ent->transform, btag->full_transform);
            Mat4_vec3_mul(to.m_floats, tr, btag->mesh_base->centre);
            curr = from;
            move = to - from;
            move_len = move.length();
            if((i == 0) && (move_len > 1024.0))                                 ///@FIXME: magick const 1024.0!
            {
                break;
            }
            int iter = (btScalar)(2.0 * move_len / btag->mesh_base->R) + 1;     ///@FIXME (not a critical): magick const 4.0!
            move.m_floats[0] /= (btScalar)iter;
            move.m_floats[1] /= (btScalar)iter;
            move.m_floats[2] /= (btScalar)iter;

            for(int j=0;j<=iter;j++)
            {
                vec3_copy(tr+12, curr.m_floats);
                tr_current.setFromOpenGLMatrix(tr);
                ent->physics.ghostObjects[m]->setWorldTransform(tr_current);
                if(Ghost_GetPenetrationFixVector(ent->physics.ghostObjects[m], ent->physics.manifoldArray, tmp))
                {
                    ent->transform[12+0] += tmp[0];
                    ent->transform[12+1] += tmp[1];
                    ent->transform[12+2] += tmp[2];
                    curr.m_floats[0] += tmp[0];
                    curr.m_floats[1] += tmp[1];
                    curr.m_floats[2] += tmp[2];
                    from.m_floats[0] += tmp[0];
                    from.m_floats[1] += tmp[1];
                    from.m_floats[2] += tmp[2];
                    ret++;
                }
                curr += move;
            }
        }
        vec3_sub(reaction, ent->transform+12, orig_pos);
        vec3_copy(ent->transform + 12, orig_pos);
    }

    return ret;
}


void Entity_FixPenetrations(struct entity_s *ent, btScalar move[3])
{
    if(ent->physics.ghostObjects != NULL)
    {
        btScalar t1, t2, reaction[3];

        if((move != NULL) && (ent->character != NULL))
        {
            ent->character->resp.horizontal_collide    = 0x00;
            ent->character->resp.vertical_collide      = 0x00;
        }

        if(ent->type_flags & ENTITY_TYPE_DYNAMIC)
        {
            return;
        }

        if(ent->physics.no_fix_all)
        {
            Entity_GhostUpdate(ent);
            return;
        }

        int numPenetrationLoops = Entity_GetPenetrationFixVector(ent, reaction, move);
        vec3_add(ent->transform+12, ent->transform+12, reaction);

        if(ent->character != NULL)
        {
            Character_UpdateCurrentHeight(ent);
            if((move != NULL) && (numPenetrationLoops > 0))
            {
                t1 = reaction[0] * reaction[0] + reaction[1] * reaction[1];
                t2 = move[0] * move[0] + move[1] * move[1];
                if((reaction[2] * reaction[2] < t1) && (move[2] * move[2] < t2))    // we have horizontal move and horizontal correction
                {
                    t2 *= t1;
                    t1 = (reaction[0] * move[0] + reaction[1] * move[1]) / sqrtf(t2);
                    if(t1 < ent->character->critical_wall_component)
                    {
                        ent->character->resp.horizontal_collide |= 0x01;
                    }
                }
                else if((reaction[2] * reaction[2] > t1) && (move[2] * move[2] > t2))
                {
                    if((reaction[2] > 0.0) && (move[2] < 0.0))
                    {
                        ent->character->resp.vertical_collide |= 0x01;
                    }
                    else if((reaction[2] < 0.0) && (move[2] > 0.0))
                    {
                        ent->character->resp.vertical_collide |= 0x02;
                    }
                }
            }

            if(ent->character->height_info.ceiling_hit && (reaction[2] < -0.1))
            {
                ent->character->resp.vertical_collide |= 0x02;
            }

            if(ent->character->height_info.floor_hit && (reaction[2] > 0.1))
            {
                ent->character->resp.vertical_collide |= 0x01;
            }
        }

        Entity_GhostUpdate(ent);
    }
}

/**
 * we check walls and other collision objects reaction. if reaction more then critacal
 * then cmd->horizontal_collide |= 0x01;
 * @param ent - cheked entity
 * @param cmd - here we fill cmd->horizontal_collide field
 * @param move - absolute 3d move vector
 */
int Entity_CheckNextPenetration(struct entity_s *ent, btScalar move[3])
{
    int ret = 0;
    if(ent->physics.ghostObjects != NULL)
    {
        btScalar t1, t2, reaction[3], *pos = ent->transform + 12;
        character_response_p resp = &ent->character->resp;

        Entity_GhostUpdate(ent);
        vec3_add(pos, pos, move);
        //resp->horizontal_collide = 0x00;
        ret = Entity_GetPenetrationFixVector(ent, reaction, move);
        if((ret > 0) && (ent->character != NULL))
        {
            t1 = reaction[0] * reaction[0] + reaction[1] * reaction[1];
            t2 = move[0] * move[0] + move[1] * move[1];
            if((reaction[2] * reaction[2] < t1) && (move[2] * move[2] < t2))
            {
                t2 *= t1;
                t1 = (reaction[0] * move[0] + reaction[1] * move[1]) / sqrtf(t2);
                if(t1 < ent->character->critical_wall_component)
                {
                    ent->character->resp.horizontal_collide |= 0x01;
                }
            }
        }
        vec3_sub(pos, pos, move);
        Entity_GhostUpdate(ent);
        Entity_CleanCollisionAllBodyParts(ent);
    }

    return ret;
}


void Entity_CheckCollisionCallbacks(struct entity_s *ent)
{
    if(ent->physics.ghostObjects != NULL)
    {
        btCollisionObject *cobj;
        uint32_t curr_flag;
        Entity_UpdateCurrentCollisions(ent);
        while((cobj = Entity_GetRemoveCollisionBodyParts(ent, 0xFFFFFFFF, &curr_flag)) != NULL)
        {
            // do callbacks here:
            int type = -1;
            engine_container_p cont = (engine_container_p)cobj->getUserPointer();
            if(cont != NULL)
            {
                type = cont->object_type;
            }

            if(type == OBJECT_ENTITY)
            {
                entity_p activator = (entity_p)cont->object;

                if(activator->callback_flags & ENTITY_CALLBACK_COLLISION)
                {
                    // Activator and entity IDs are swapped in case of collision callback.
                    lua_ExecEntity(engine_lua, ENTITY_CALLBACK_COLLISION, activator->id, ent->id);
                    Con_Printf("char_body_flag = 0x%X, collider_bone_index = %d, collider_type = %d", curr_flag, cobj->getUserIndex(), type);
                }
            }
        }
    }
}


bool Entity_WasCollisionBodyParts(struct entity_s *ent, uint32_t parts_flags)
{
    if(ent->physics.last_collisions != NULL)
    {
        for(uint16_t i=0;i<ent->bf.bone_tag_count;i++)
        {
            if((ent->bf.bone_tags[i].body_part & parts_flags) && (ent->physics.last_collisions[i].obj_count > 0))
            {
                return true;
            }
        }
    }

    return false;
}


void Entity_CleanCollisionAllBodyParts(struct entity_s *ent)
{
    if(ent->physics.last_collisions != NULL)
    {
        for(uint16_t i=0;i<ent->bf.bone_tag_count;i++)
        {
            ent->physics.last_collisions[i].obj_count = 0;
        }
    }
}


void Entity_CleanCollisionBodyParts(struct entity_s *ent, uint32_t parts_flags)
{
    if(ent->physics.last_collisions != NULL)
    {
        for(uint16_t i=0;i<ent->bf.bone_tag_count;i++)
        {
            if(ent->bf.bone_tags[i].body_part & parts_flags)
            {
                ent->physics.last_collisions[i].obj_count = 0;
            }
        }
    }
}


btCollisionObject *Entity_GetRemoveCollisionBodyParts(struct entity_s *ent, uint32_t parts_flags, uint32_t *curr_flag)
{
    *curr_flag = 0x00;
    if(ent->physics.last_collisions != NULL)
    {
        for(uint16_t i=0;i<ent->bf.bone_tag_count;i++)
        {
            if(ent->bf.bone_tags[i].body_part & parts_flags)
            {
                entity_collision_node_p cn = ent->physics.last_collisions + i;
                if(cn->obj_count > 0)
                {
                    *curr_flag = ent->bf.bone_tags[i].body_part;
                    return cn->obj[--cn->obj_count];
                }
            }
        }
    }

    return NULL;
}

/*
 * =============================================================================
 */

/**
 * DEBUG PRIMITIVES RENDERING
 */
render_DebugDrawer::render_DebugDrawer()
:m_debugMode(0)
{
    m_max_lines = DEBUG_DRAWER_DEFAULT_BUFFER_SIZE;
    m_buffer = (GLfloat*)malloc(2 * 6 * m_max_lines * sizeof(GLfloat));

    m_lines = 0;
    m_gl_vbo = 0;
    m_need_realloc = false;
    vec3_set_zero(m_color);
    m_obb = OBB_Create();
}


render_DebugDrawer::~render_DebugDrawer()
{
    free(m_buffer);
    m_buffer = NULL;
    if(m_gl_vbo != 0)
    {
        glDeleteBuffersARB(1, &m_gl_vbo);
        m_gl_vbo = 0;
    }
    OBB_Clear(m_obb);
    m_obb = NULL;
}


void render_DebugDrawer::reset()
{
    if(m_need_realloc)
    {
        uint32_t new_buffer_size = m_max_lines * 12 * 2;
        GLfloat *new_buffer = (GLfloat*)malloc(new_buffer_size * sizeof(GLfloat));
        if(new_buffer != NULL)
        {
            free(m_buffer);
            m_buffer = new_buffer;
            m_max_lines *= 2;
        }
        m_need_realloc = false;
    }
    if(m_gl_vbo == 0)
    {
        glGenBuffersARB(1, &m_gl_vbo);
    }
    m_lines = 0;
}


void render_DebugDrawer::drawLine(const btVector3& from, const btVector3& to, const btVector3& color)
{
    GLfloat *v;

    if(m_lines < m_max_lines - 1)
    {
        v = m_buffer + 3 * 4 * m_lines;
        m_lines++;

        vec3_copy(v, from.m_floats);
        v += 3;
        vec3_copy(v, color.m_floats);
        v += 3;
        vec3_copy(v, to.m_floats);
        v += 3;
        vec3_copy(v, color.m_floats);
    }
    else
    {
        m_need_realloc = true;
    }
}


void render_DebugDrawer::setDebugMode(int debugMode)
{
   m_debugMode = debugMode;
}


void render_DebugDrawer::draw3dText(const btVector3& location, const char* textString)
{
   //glRasterPos3f(location.x(),  location.y(),  location.z());
   //BMF_DrawString(BMF_GetFont(BMF_kHelvetica10),textString);
}


void render_DebugDrawer::reportErrorWarning(const char* warningString)
{
   Con_AddLine(warningString, FONTSTYLE_CONSOLE_WARNING);
}


void render_DebugDrawer::drawContactPoint(const btVector3& pointOnB,const btVector3& normalOnB,btScalar distance,int lifeTime,const btVector3& color)
{
    if(m_lines + 2 < m_max_lines)
    {
        GLfloat *v = m_buffer + 3 * 4 * m_lines;
        m_lines += 2;
        btVector3 to = pointOnB + normalOnB * distance;

        vec3_copy(v, pointOnB.m_floats);
        v += 3;
        vec3_copy(v, color.m_floats);
        v += 3;

        vec3_copy(v, to.m_floats);
        v += 3;
        vec3_copy(v, color.m_floats);

        //glRasterPos3f(from.x(),  from.y(),  from.z());
        //char buf[12];
        //sprintf(buf," %d",lifeTime);
        //BMF_DrawString(BMF_GetFont(BMF_kHelvetica10),buf);
    }
    else
    {
        m_need_realloc = true;
    }
}


void render_DebugDrawer::render()
{
    if((m_lines > 0) && (m_gl_vbo != 0))
    {
        glBindBufferARB(GL_ARRAY_BUFFER_ARB, m_gl_vbo);
        glBufferDataARB(GL_ARRAY_BUFFER_ARB, m_lines * 12 * sizeof(GLfloat), m_buffer, GL_STREAM_DRAW);
        glVertexPointer(3, GL_FLOAT, 6 * sizeof(GLfloat), (void*)0);
        glColorPointer(3, GL_FLOAT, 6 * sizeof(GLfloat),  (void*)(3 * sizeof(GLfloat)));
        glDrawArrays(GL_LINES, 0, 2 * m_lines);
        glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
    }

    vec3_set_zero(m_color);
    m_lines = 0;
}


void render_DebugDrawer::drawAxis(btScalar r, btScalar transform[16])
{
    GLfloat *v0, *v;

    if(m_lines + 3 >= m_max_lines)
    {
        m_need_realloc = true;
        return;
    }

    v0 = v = m_buffer + 3 * 4 * m_lines;
    m_lines += 3;
    vec3_copy(v0, transform + 12);

    // OX
    v += 3;
    v[0] = 1.0;
    v[1] = 0.0;
    v[2] = 0.0;
    v += 3;
    vec3_add_mul(v, v0, transform + 0, r);
    v += 3;
    v[0] = 1.0;
    v[1] = 0.0;
    v[2] = 0.0;
    v += 3;

    // OY
    vec3_copy(v, v0);
    v += 3;
    v[0] = 0.0;
    v[1] = 1.0;
    v[2] = 0.0;
    v += 3;
    vec3_add_mul(v, v0, transform + 4, r);
    v += 3;
    v[0] = 0.0;
    v[1] = 1.0;
    v[2] = 0.0;
    v += 3;

    // OZ
    vec3_copy(v, v0);
    v += 3;
    v[0] = 0.0;
    v[1] = 0.0;
    v[2] = 1.0;
    v += 3;
    vec3_add_mul(v, v0, transform + 8, r);
    v += 3;
    v[0] = 0.0;
    v[1] = 0.0;
    v[2] = 1.0;
}


void render_DebugDrawer::drawFrustum(struct frustum_s *f)
{
    if(f != NULL)
    {
        GLfloat *v, *v0;
        btScalar *fv = f->vertex;

        if(m_lines + f->vertex_count >= m_max_lines)
        {
            m_need_realloc = true;
            return;
        }

        v = v0 = m_buffer + 3 * 4 * m_lines;
        m_lines += f->vertex_count;

        for(uint16_t i=0;i<f->vertex_count-1;i++,fv += 3)
        {
            vec3_copy(v, fv);
            v += 3;
            vec3_copy(v, m_color);
            v += 3;

            vec3_copy(v, fv + 3);
            v += 3;
            vec3_copy(v, m_color);
            v += 3;
        }

        vec3_copy(v, fv);
        v += 3;
        vec3_copy(v, m_color);
        v += 3;
        vec3_copy(v, v0);
        v += 3;
        vec3_copy(v, m_color);
    }
}


void render_DebugDrawer::drawPortal(struct portal_s *p)
{
    if(p != NULL)
    {
        GLfloat *v, *v0;
        btScalar *pv = p->vertex;

        if(m_lines + p->vertex_count >= m_max_lines)
        {
            m_need_realloc = true;
            return;
        }

        v = v0 = m_buffer + 3 * 4 * m_lines;
        m_lines += p->vertex_count;

        for(uint16_t i=0;i<p->vertex_count-1;i++,pv += 3)
        {
            vec3_copy(v, pv);
            v += 3;
            vec3_copy(v, m_color);
            v += 3;

            vec3_copy(v, pv + 3);
            v += 3;
            vec3_copy(v, m_color);
            v += 3;
        }

        vec3_copy(v, pv);
        v += 3;
        vec3_copy(v, m_color);
        v += 3;
        vec3_copy(v, v0);
        v += 3;
        vec3_copy(v, m_color);
    }
}


void render_DebugDrawer::drawBBox(btScalar bb_min[3], btScalar bb_max[3], btScalar *transform)
{
    if(m_lines + 12 < m_max_lines)
    {
        OBB_Rebuild(m_obb, bb_min, bb_max);
        m_obb->transform = transform;
        OBB_Transform(m_obb);
        drawOBB(m_obb);
    }
    else
    {
        m_need_realloc = true;
    }
}


void render_DebugDrawer::drawOBB(struct obb_s *obb)
{
    GLfloat *v, *v0;
    polygon_p p = obb->polygons;

    if(m_lines + 12 >= m_max_lines)
    {
        m_need_realloc = true;
        return;
    }

    v = v0 = m_buffer + 3 * 4 * m_lines;
    m_lines += 12;

    vec3_copy(v, p->vertices[0].position);
    v += 3;
    vec3_copy(v, m_color);
    v += 3;
    vec3_copy(v, (p+1)->vertices[0].position);
    v += 3;
    vec3_copy(v, m_color);
    v += 3;

    vec3_copy(v, p->vertices[1].position);
    v += 3;
    vec3_copy(v, m_color);
    v += 3;
    vec3_copy(v, (p+1)->vertices[3].position);
    v += 3;
    vec3_copy(v, m_color);
    v += 3;

    vec3_copy(v, p->vertices[2].position);
    v += 3;
    vec3_copy(v, m_color);
    v += 3;
    vec3_copy(v, (p+1)->vertices[2].position);
    v += 3;
    vec3_copy(v, m_color);
    v += 3;

    vec3_copy(v, p->vertices[3].position);
    v += 3;
    vec3_copy(v, m_color);
    v += 3;
    vec3_copy(v, (p+1)->vertices[1].position);
    v += 3;
    vec3_copy(v, m_color);
    v += 3;

    for(uint16_t i=0; i<2; i++,p++)
    {
        vertex_p pv = p->vertices;
        v0 = v;
        for(uint16_t j=0;j<p->vertex_count-1;j++,pv++)
        {
            vec3_copy(v, pv->position);
            v += 3;
            vec3_copy(v, m_color);
            v += 3;

            vec3_copy(v, (pv+1)->position);
            v += 3;
            vec3_copy(v, m_color);
            v += 3;
        }

        vec3_copy(v, pv->position);
        v += 3;
        vec3_copy(v, m_color);
        v += 3;
        vec3_copy(v, v0);
        v += 3;
        vec3_copy(v, m_color);
        v += 3;
    }
}


void render_DebugDrawer::drawMeshDebugLines(struct base_mesh_s *mesh, btScalar transform[16], const btScalar *overrideVertices, const btScalar *overrideNormals)
{
    if((!m_need_realloc) && (renderer.style & R_DRAW_NORMALS))
    {
        GLfloat *v = m_buffer + 3 * 4 * m_lines;
        btScalar n[3];

        if(m_lines + mesh->vertex_count >= m_max_lines)
        {
            m_need_realloc = true;
            return;
        }

        setColor(0.8, 0.0, 0.9);
        m_lines += mesh->vertex_count;
        if(overrideVertices)
        {
            btScalar *ov = (btScalar*)overrideVertices;
            btScalar *on = (btScalar*)overrideNormals;
            for(uint32_t i=0; i<mesh->vertex_count; i++,ov+=3,on+=3,v+=12)
            {
                Mat4_vec3_mul_macro(v, transform, ov);
                Mat4_vec3_rot_macro(n, transform, on);

                v[6 + 0] = v[0] + n[0] * 128.0;
                v[6 + 1] = v[1] + n[1] * 128.0;
                v[6 + 2] = v[2] + n[2] * 128.0;
                vec3_copy(v+3, m_color);
                vec3_copy(v+9, m_color);
            }
        }
        else
        {
            vertex_p mv = mesh->vertices;
            for (uint32_t i = 0; i < mesh->vertex_count; i++,mv++,v+=12)
            {
                Mat4_vec3_mul_macro(v, transform, mv->position);
                Mat4_vec3_rot_macro(n, transform, mv->normal);

                v[6 + 0] = v[0] + n[0] * 128.0;
                v[6 + 1] = v[1] + n[1] * 128.0;
                v[6 + 2] = v[2] + n[2] * 128.0;
                vec3_copy(v+3, m_color);
                vec3_copy(v+9, m_color);
            }
        }
    }
}


void render_DebugDrawer::drawSkeletalModelDebugLines(struct ss_bone_frame_s *bframe, btScalar transform[16])
{
    if((!m_need_realloc) && renderer.style & R_DRAW_NORMALS)
    {
        btScalar tr[16];

        ss_bone_tag_p btag = bframe->bone_tags;
        for(uint16_t i=0; i<bframe->bone_tag_count; i++,btag++)
        {
            Mat4_Mat4_mul(tr, transform, btag->full_transform);
            drawMeshDebugLines(btag->mesh_base, tr, NULL, NULL);
        }
    }
}


void render_DebugDrawer::drawEntityDebugLines(struct entity_s *entity)
{
    if(m_need_realloc || entity->was_rendered_lines || !(renderer.style & (R_DRAW_AXIS | R_DRAW_NORMALS | R_DRAW_BOXES)) ||
       !(entity->state_flags & ENTITY_STATE_VISIBLE) || (entity->bf.animations.model->hide && !(renderer.style & R_DRAW_NULLMESHES)))
    {
        return;
    }

    if(renderer.style & R_DRAW_BOXES)
    {
        debugDrawer.setColor(0.0, 0.0, 1.0);
        debugDrawer.drawOBB(entity->obb);
    }

    if(renderer.style & R_DRAW_AXIS)
    {
        // If this happens, the lines after this will get drawn with random colors. I don't care.
        debugDrawer.drawAxis(1000.0, entity->transform);
    }

    if(entity->bf.animations.model && entity->bf.animations.model->animations)
    {
        debugDrawer.drawSkeletalModelDebugLines(&entity->bf, entity->transform);
    }

    entity->was_rendered_lines = 1;
}


void render_DebugDrawer::drawSectorDebugLines(struct room_sector_s *rs)
{
    if(m_lines + 12 < m_max_lines)
    {
        btScalar bb_min[3] = {(btScalar)(rs->pos[0] - TR_METERING_SECTORSIZE / 2.0), (btScalar)(rs->pos[1] - TR_METERING_SECTORSIZE / 2.0), (btScalar)rs->floor};
        btScalar bb_max[3] = {(btScalar)(rs->pos[0] + TR_METERING_SECTORSIZE / 2.0), (btScalar)(rs->pos[1] + TR_METERING_SECTORSIZE / 2.0), (btScalar)rs->ceiling};

        drawBBox(bb_min, bb_max, NULL);
    }
    else
    {
        m_need_realloc = true;
    }
}


void render_DebugDrawer::drawRoomDebugLines(struct room_s *room, struct render_s *render)
{
    uint32_t flag;
    frustum_p frus;
    engine_container_p cont;
    entity_p ent;

    if(m_need_realloc)
    {
        return;
    }

    flag = render->style & R_DRAW_ROOMBOXES;
    if(flag)
    {
        debugDrawer.setColor(0.0, 0.1, 0.9);
        debugDrawer.drawBBox(room->bb_min, room->bb_max, NULL);
        /*for(uint32_t s=0;s<room->sectors_count;s++)
        {
            drawSectorDebugLines(room->sectors + s);
        }*/
    }

    flag = render->style & R_DRAW_PORTALS;
    if(flag)
    {
        debugDrawer.setColor(0.0, 0.0, 0.0);
        for(uint16_t i=0; i<room->portal_count; i++)
        {
            debugDrawer.drawPortal(room->portals+i);
        }
    }

    flag = render->style & R_DRAW_FRUSTUMS;
    if(flag)
    {
        debugDrawer.setColor(1.0, 0.0, 0.0);
        for(frus=room->frustum; frus; frus=frus->next)
        {
            debugDrawer.drawFrustum(frus);
        }
    }

    if(!(renderer.style & R_SKIP_ROOM) && (room->mesh != NULL))
    {
        debugDrawer.drawMeshDebugLines(room->mesh, room->transform, NULL, NULL);
    }

    flag = render->style & R_DRAW_BOXES;
    for(uint32_t i=0; i<room->static_mesh_count; i++)
    {
        if(room->static_mesh[i].was_rendered_lines || !Frustum_IsOBBVisibleInRoom(room->static_mesh[i].obb, room) ||
          ((room->static_mesh[i].hide == 1) && !(renderer.style & R_DRAW_DUMMY_STATICS)))
        {
            continue;
        }

        if(flag)
        {
            debugDrawer.setColor(0.0, 1.0, 0.1);
            debugDrawer.drawOBB(room->static_mesh[i].obb);
        }

        if(render->style & R_DRAW_AXIS)
        {
            debugDrawer.drawAxis(1000.0, room->static_mesh[i].transform);
        }

        debugDrawer.drawMeshDebugLines(room->static_mesh[i].mesh, room->static_mesh[i].transform, NULL, NULL);

        room->static_mesh[i].was_rendered_lines = 1;
    }

    for(cont=room->containers; cont; cont=cont->next)
    {
        switch(cont->object_type)
        {
        case OBJECT_ENTITY:
            ent = (entity_p)cont->object;
            if(ent->was_rendered_lines == 0)
            {
                if(Frustum_IsOBBVisibleInRoom(ent->obb, room))
                {
                    debugDrawer.drawEntityDebugLines(ent);
                }
                ent->was_rendered_lines = 1;
            }
            break;
        };
    }
}


