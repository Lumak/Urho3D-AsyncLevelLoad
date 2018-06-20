//
// Copyright (c) 2008-2016 the Urho3D project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Core/ProcessUtils.h>
#include <Urho3D/Engine/Engine.h>
#include <Urho3D/Graphics/AnimatedModel.h>
#include <Urho3D/Graphics/AnimationController.h>
#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/Light.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/Octree.h>
#include <Urho3D/Graphics/Renderer.h>
#include <Urho3D/Graphics/Zone.h>
#include <Urho3D/Graphics/Technique.h>
#include <Urho3D/Graphics/Texture2D.h>
#include <Urho3D/Graphics/Terrain.h>
#include <Urho3D/Input/Controls.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Physics/CollisionShape.h>
#include <Urho3D/Physics/PhysicsWorld.h>
#include <Urho3D/Physics/PhysicsEvents.h>
#include <Urho3D/Physics/RigidBody.h>
#include <Urho3D/IO/MemoryBuffer.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Graphics/DebugRenderer.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>
#include <Urho3D/Engine/DebugHud.h>
#include <SDL/SDL_log.h>

#include <Bullet/BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <Bullet/BulletDynamics/Dynamics/btRigidBody.h>

#include "CharacterDemo.h"
#include "Character.h"
#include "CollisionLayer.h"

#include <Urho3D/DebugNew.h>

//=============================================================================
//=============================================================================
const float CAMERA_MIN_DIST = 1.0f;
const float CAMERA_INITIAL_DIST = 5.0f;
const float CAMERA_MAX_DIST = 20.0f;

//=============================================================================
//=============================================================================
URHO3D_DEFINE_APPLICATION_MAIN(CharacterDemo)

//=============================================================================
//=============================================================================
CharacterDemo::CharacterDemo(Context* context)
    : Sample(context)
    , firstPerson_(false)
    , drawDebug_(false)
    , wireView_(false)
    , cameraMode_(false)
{
    Character::RegisterObject(context);
}

CharacterDemo::~CharacterDemo()
{
}

void CharacterDemo::Setup()
{
    engineParameters_["WindowTitle"]  = GetTypeName();
    engineParameters_["LogName"]      = GetSubsystem<FileSystem>()->GetProgramDir() + "asyncLevel.log";
    engineParameters_["FullScreen"]   = false;
    engineParameters_["Headless"]     = false;
    engineParameters_["WindowWidth"]  = 1280; 
    engineParameters_["WindowHeight"] = 720;
}

void CharacterDemo::Start()
{
    // Execute base class startup
    Sample::Start();

    ChangeDebugHudText();

    // Create the UI content
    CreateInstructions();

    // Create static scene content
    CreateScene();

    // Create the controllable character
    CreateCharacter();

    // Subscribe to necessary events
    SubscribeToEvents();

    // Set the mouse mode to use in the sample
    Sample::InitMouseMode(MM_RELATIVE);
}

void CharacterDemo::CreateScene()
{
    ResourceCache* cache = GetSubsystem<ResourceCache>();

    scene_ = new Scene(context_);

    cameraNode_ = new Node(context_);
    Camera* camera = cameraNode_->CreateComponent<Camera>();
    camera->SetFarClip(300.0f);
    cameraNode_->SetPosition(Vector3(0,100,0));
    GetSubsystem<Renderer>()->SetViewport(0, new Viewport(context_, scene_, camera));

    // load scene
    XMLFile *xmlLevel = cache->GetResource<XMLFile>("AsyncLevel/mainLevel.xml");
    scene_->LoadXML(xmlLevel->GetRoot());

    // setup for async loading
    scene_->SetAsyncLoadingMs(3);
    levelPathName_ = "AsyncLevel/";
    xmlLevel = cache->GetResource<XMLFile>(levelPathName_ + String("Level_1.xml"));
    Node *tmpNode = scene_->CreateTemporaryChild();
    SceneResolver resolver;

    if (tmpNode->LoadXML(xmlLevel->GetRoot(), resolver, false))
    {
        curLevel_ = scene_->InstantiateXML(xmlLevel->GetRoot(), tmpNode->GetWorldPosition(), tmpNode->GetWorldRotation());

        NodeRegisterLoadTriggers(curLevel_);
    }

    levelText_->SetText(String("cur level:\n") + "Level_1");

    // sub
    SubscribeToEvent(E_ASYNCLOADPROGRESS, URHO3D_HANDLER(CharacterDemo, HandleLoadProgress));
    SubscribeToEvent(E_ASYNCLEVELOADFINISHED, URHO3D_HANDLER(CharacterDemo, HandleLevelLoaded));
}

void CharacterDemo::CreateCharacter()
{
    ResourceCache* cache = GetSubsystem<ResourceCache>();

    Node *spawnNode = scene_->GetChild("playerSpawn");
    Node* objectNode = scene_->CreateChild("Player");
    objectNode->SetPosition(spawnNode->GetPosition());

    // spin node
    Node* adjustNode = objectNode->CreateChild("spinNode");
    adjustNode->SetRotation( Quaternion(180, Vector3(0,1,0) ) );
    
    // Create the rendering component + animation controller
    AnimatedModel* object = adjustNode->CreateComponent<AnimatedModel>();
    object->SetModel(cache->GetResource<Model>("Platforms/Models/BetaLowpoly/Beta.mdl"));
    object->SetMaterial(0, cache->GetResource<Material>("Platforms/Materials/BetaBody_MAT.xml"));
    object->SetMaterial(1, cache->GetResource<Material>("Platforms/Materials/BetaBody_MAT.xml"));
    object->SetMaterial(2, cache->GetResource<Material>("Platforms/Materials/BetaJoints_MAT.xml"));
    object->SetCastShadows(true);
    adjustNode->CreateComponent<AnimationController>();

    // Create rigidbody, and set non-zero mass so that the body becomes dynamic
    RigidBody* body = objectNode->CreateComponent<RigidBody>();
    body->SetCollisionLayer(ColLayer_Character);
    body->SetCollisionMask(ColMask_Character);
    body->SetMass(1.0f);
    body->SetRollingFriction(1.0f);

    body->SetAngularFactor(Vector3::ZERO);
    body->SetCollisionEventMode(COLLISION_ALWAYS);

    // Set a capsule shape for collision
    CollisionShape* shape = objectNode->CreateComponent<CollisionShape>();
    shape->SetCapsule(0.7f, 1.8f, Vector3(0.0f, 0.94f, 0.0f));

    // character
    character_ = objectNode->CreateComponent<Character>();
}

void CharacterDemo::CreateInstructions()
{
    ResourceCache* cache = GetSubsystem<ResourceCache>();
    Graphics *graphics = GetSubsystem<Graphics>();
    UI* ui = GetSubsystem<UI>();

    // Construct new Text object, set string to display and font to use
    Text* instructionText = ui->GetRoot()->CreateChild<Text>();
    instructionText->SetFont(cache->GetResource<Font>("Fonts/Anonymous Pro.ttf"), 14);
    instructionText->SetTextAlignment(HA_CENTER);
    instructionText->SetColor(Color::CYAN);

    // Position the text relative to the screen center
    instructionText->SetHorizontalAlignment(HA_CENTER);
    instructionText->SetPosition(0, 10);

    levelText_ = ui->GetRoot()->CreateChild<Text>();
    levelText_->SetFont(cache->GetResource<Font>("Fonts/Anonymous Pro.ttf"), 12);
    levelText_->SetTextAlignment(HA_CENTER);
    levelText_->SetColor(Color::CYAN);
    levelText_->SetPosition(graphics->GetWidth() - 100, 10);

    triggerText_ = ui->GetRoot()->CreateChild<Text>();
    triggerText_->SetFont(cache->GetResource<Font>("Fonts/Anonymous Pro.ttf"), 12);
    triggerText_->SetTextAlignment(HA_CENTER);
    triggerText_->SetColor(Color::CYAN);
    triggerText_->SetPosition(graphics->GetWidth() - 250, 10);

    progressText_ = ui->GetRoot()->CreateChild<Text>();
    progressText_->SetFont(cache->GetResource<Font>("Fonts/Anonymous Pro.ttf"), 12);
    progressText_->SetTextAlignment(HA_CENTER);
    progressText_->SetColor(Color::CYAN);
    progressText_->SetHorizontalAlignment(HA_CENTER);
    progressText_->SetPosition(0, 10);

    dbgPrimitiveText_ = ui->GetRoot()->CreateChild<Text>();
    dbgPrimitiveText_->SetFont(cache->GetResource<Font>("Fonts/Anonymous Pro.ttf"), 12);
    dbgPrimitiveText_->SetTextAlignment(HA_CENTER);
    dbgPrimitiveText_->SetColor(Color::CYAN);
    dbgPrimitiveText_->SetPosition(10, 10);
}

void CharacterDemo::ChangeDebugHudText()
{
    // change profiler text
    if (GetSubsystem<DebugHud>())
    {
        Text *dbgText = GetSubsystem<DebugHud>()->GetProfilerText();
        dbgText->SetColor(Color::CYAN);
        dbgText->SetTextEffect(TE_NONE);

        dbgText = GetSubsystem<DebugHud>()->GetStatsText();
        dbgText->SetColor(Color::CYAN);
        dbgText->SetTextEffect(TE_NONE);

        dbgText = GetSubsystem<DebugHud>()->GetMemoryText();
        dbgText->SetColor(Color::CYAN);
        dbgText->SetTextEffect(TE_NONE);

        dbgText = GetSubsystem<DebugHud>()->GetModeText();
        dbgText->SetColor(Color::CYAN);
        dbgText->SetTextEffect(TE_NONE);
    }
}

void CharacterDemo::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(CharacterDemo, HandleUpdate));
    SubscribeToEvent(E_POSTUPDATE, URHO3D_HANDLER(CharacterDemo, HandlePostUpdate));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(CharacterDemo, HandlePostRenderUpdate));
    UnsubscribeFromEvent(E_SCENEUPDATE);
}

void CharacterDemo::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;

    Input* input = GetSubsystem<Input>();

    if (character_)
    {
        // Clear previous controls
        character_->controls_.Set(CTRL_FORWARD | CTRL_BACK | CTRL_LEFT | CTRL_RIGHT | CTRL_JUMP, false);

        // Update controls using keys
        UI* ui = GetSubsystem<UI>();
        if (!ui->GetFocusElement())
        {
            character_->controls_.Set(CTRL_FORWARD, input->GetKeyDown(KEY_W));
            character_->controls_.Set(CTRL_BACK, input->GetKeyDown(KEY_S));
            character_->controls_.Set(CTRL_LEFT, input->GetKeyDown(KEY_A));
            character_->controls_.Set(CTRL_RIGHT, input->GetKeyDown(KEY_D));
            character_->controls_.Set(CTRL_JUMP, input->GetKeyDown(KEY_SPACE));

            character_->controls_.yaw_ += (float)input->GetMouseMoveX() * YAW_SENSITIVITY;
            character_->controls_.pitch_ += (float)input->GetMouseMoveY() * YAW_SENSITIVITY;

            // Limit pitch
            character_->controls_.pitch_ = Clamp(character_->controls_.pitch_, -80.0f, 80.0f);
            // Set rotation already here so that it's updated every rendering frame instead of every physics frame
            character_->GetNode()->SetRotation(Quaternion(character_->controls_.yaw_, Vector3::UP));
        }
    }

    // Toggle debug geometry with space
    if (input->GetKeyPress(KEY_F5))
    {
        drawDebug_ = !drawDebug_;
    }
}

void CharacterDemo::HandlePostUpdate(StringHash eventType, VariantMap& eventData)
{
    if (!character_)
        return;

    Node* characterNode = character_->GetNode();
    Quaternion rot = characterNode->GetRotation();
    Quaternion dir = rot * Quaternion(character_->controls_.pitch_, Vector3::RIGHT);

    {
        Vector3 aimPoint = characterNode->GetPosition() + rot * Vector3(0.0f, 1.7f, 0.0f);
        Vector3 rayDir = dir * Vector3::BACK;
        float rayDistance = CAMERA_INITIAL_DIST;
        PhysicsRaycastResult result;

        scene_->GetComponent<PhysicsWorld>()->RaycastSingle(result, Ray(aimPoint, rayDir), rayDistance, ColMask_Camera);
        if (result.body_)
            rayDistance = Min(rayDistance, result.distance_);
        rayDistance = Clamp(rayDistance, CAMERA_MIN_DIST, CAMERA_MAX_DIST);

        cameraNode_->SetPosition(aimPoint + rayDir * rayDistance);
        cameraNode_->SetRotation(dir);
    }
}

void CharacterDemo::HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    Graphics *graphics = GetSubsystem<Graphics>();

    unsigned primitives = graphics->GetNumPrimitives();
    unsigned batches = graphics->GetNumBatches();
    dbgPrimitiveText_->SetText(ToString("Tri:%u, batches:%u", primitives, batches));
}

void CharacterDemo::MoveCamera(float timeStep)
{
    // Do not move if the UI has a focused element (the console)
    if (GetSubsystem<UI>()->GetFocusElement())
        return;

    Input* input = GetSubsystem<Input>();

    // Movement speed as world units per second
    const float MOVE_SPEED = 20.0f;
    // Mouse sensitivity as degrees per pixel
    const float MOUSE_SENSITIVITY = 0.1f;

    // Use this frame's mouse motion to adjust camera node yaw and pitch. Clamp the pitch between -90 and 90 degrees
    IntVector2 mouseMove = input->GetMouseMove();
    yaw_ += MOUSE_SENSITIVITY * mouseMove.x_;
    pitch_ += MOUSE_SENSITIVITY * mouseMove.y_;
    pitch_ = Clamp(pitch_, -90.0f, 90.0f);

    // Construct new orientation for the camera scene node from yaw and pitch. Roll is fixed to zero
    cameraNode_->SetRotation(Quaternion(pitch_, yaw_, 0.0f));

    // Read WASD keys and move the camera scene node to the corresponding direction if they are pressed
    // Use the Translate() function (default local space) to move relative to the node's orientation.
    if (input->GetKeyDown(KEY_W))
        cameraNode_->Translate(Vector3::FORWARD * MOVE_SPEED * timeStep);
    if (input->GetKeyDown(KEY_S))
        cameraNode_->Translate(Vector3::BACK * MOVE_SPEED * timeStep);
    if (input->GetKeyDown(KEY_A))
        cameraNode_->Translate(Vector3::LEFT * MOVE_SPEED * timeStep);
    if (input->GetKeyDown(KEY_D))
        cameraNode_->Translate(Vector3::RIGHT * MOVE_SPEED * timeStep);
}

void CharacterDemo::NodeRegisterLoadTriggers(Node *node)
{
    if (node)
    {
        PODVector<Node *> result;
        node->GetChildrenWithTag(result, "levelLoadTrigger", true);

        for ( unsigned i = 0; i < result.Size(); ++i )
        {
            SubscribeToEvent(result[i], E_NODECOLLISIONSTART, URHO3D_HANDLER(CharacterDemo, HandleLoadTriggerEntered));
        }
    }
    else
    {
        URHO3D_LOGERROR("NodeRegisterLoadTriggers - node is NULL.");
    }
}

void CharacterDemo::HandleLoadTriggerEntered(StringHash eventType, VariantMap& eventData)
{
    using namespace NodeCollisionStart;

    // prevent the trigger to reload while already loading
    if (!levelLoadPending_.Empty())
    {
        return;
    }

    Node *node = ((RigidBody*)eventData[P_BODY].GetVoidPtr())->GetNode();
    StringVector tagVec = node->GetTags();
    String levelName;
    String loadLevel;

    // get trigger tags
    for ( unsigned i = 0; i < tagVec.Size(); ++i )
    {
        if (tagVec[i].StartsWith("levelName="))
        {
            const unsigned nameLen = String("levelName=").Length();
            levelName = tagVec[i].Substring(nameLen, tagVec[i].Length() - nameLen);
        }
        else if (tagVec[i].StartsWith("loadLevel="))
        {
            const unsigned loadLen = String("loadLevel=").Length();
            loadLevel = tagVec[i].Substring(loadLen, tagVec[i].Length() - loadLen);
        }
    }

    if (!levelName.Empty() && !loadLevel.Empty())
    {
        levelText_->SetText(String("cur level:\n") + levelName);
        triggerText_->SetText(String("trig info:\n") + "level=" + levelName + "\nload =" + loadLevel);

        String curLevelName = curLevel_?curLevel_->GetName():String::EMPTY;
        String loadLevelName = nextLevel_?nextLevel_->GetName():String::EMPTY;

        if (curLevelName != levelName)
        {
            // swap nodes
            if (curLevelName == loadLevel && loadLevelName == levelName)
            {
                Node *tmpNode = curLevel_;
                curLevel_ = nextLevel_;
                nextLevel_ = tmpNode;
            }
            else
            {
                URHO3D_LOGERROR("Trigger level and load names out of sequence.");
            }
        }
        else if (loadLevelName != loadLevel)
        {
            // remove any existing level
            if (nextLevel_)
            {
                nextLevel_->Remove();
                nextLevel_ = NULL;
            }

            // async load
            ResourceCache* cache = GetSubsystem<ResourceCache>();
            String levelPathFile = levelPathName_ + loadLevel + String(".xml");
            URHO3D_LOGINFO("cache file " + levelPathFile);
            XMLFile *xmlLevel = cache->GetResource<XMLFile>(levelPathFile);

            if (xmlLevel)
            {
                SceneResolver resolver;
                Node *tmpNode = scene_->CreateTemporaryChild();

                URHO3D_LOGINFO("LoadXML loading " + levelPathFile);
                if (tmpNode->LoadXML(xmlLevel->GetRoot(), resolver, false))
                {
                    URHO3D_LOGINFO("Async loading " + levelPathFile);
                    if (scene_->InstantiateXMLAync(xmlLevel->GetRoot(), tmpNode->GetWorldPosition(), tmpNode->GetWorldRotation()))
                    {
                        // prevent the trigger to reload while already loading
                        levelLoadPending_ = levelPathFile;
                        progressText_->SetText("");
                    }
                    else
                    {
                        URHO3D_LOGERROR("InstantiateXMLAsync failed to init, level=" + levelPathFile);
                    }
                }
            }
            else
            {
                URHO3D_LOGERROR("Load level file= " + levelPathFile + " not found!");
            }
        }
    }
}

void CharacterDemo::HandleLoadProgress(StringHash eventType, VariantMap& eventData)
{
    using namespace AsyncLoadProgress;

    float progress = eventData[P_PROGRESS].GetFloat();
    int loadedNode = eventData[P_LOADEDNODES].GetInt();
    int totalNodes = eventData[P_TOTALNODES].GetInt();
    int loadedResources = eventData[P_LOADEDRESOURCES].GetInt();
    int totalResources = eventData[P_TOTALRESOURCES].GetInt();

    String progressStr = ToString("progress=%d%%", (int)(progress * 100.0f)) +
                         ToString("\nnodes: %d/%d", loadedNode, totalNodes) +
                         ToString("\nresources: %d/%d", loadedResources, totalResources);

    progressText_->SetText(progressStr);
}

void CharacterDemo::HandleLevelLoaded(StringHash eventType, VariantMap& eventData)
{
    using namespace AsyncLevelLoadFinished;
    nextLevel_ = (Node*)eventData[P_NODE].GetVoidPtr();

    // register triggers from new level node and clear loading flag
    NodeRegisterLoadTriggers(nextLevel_);
    levelLoadPending_.Clear();
}
