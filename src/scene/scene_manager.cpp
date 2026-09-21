#include "scene_manager.h"
#include "core/config.h"
#include "utils/logger.h"

#include <algorithm>
#include <cmath>

namespace sakura::scene
{
SceneManager::SceneManager() = default;
SceneManager::~SceneManager() { Clear(); }

void SceneManager::Clear()
{
    m_pendingScene.reset();
    m_pendingIsPop = m_pendingIsPush = false;
    m_isTransitioning = false;
    while (!m_sceneStack.empty())
    {
        m_sceneStack.back()->OnExit();
        m_sceneStack.pop_back();
    }
    if (m_texFrom) SDL_DestroyTexture(m_texFrom);
    if (m_texTo) SDL_DestroyTexture(m_texTo);
    m_texFrom = m_texTo = nullptr;
}

Scene* SceneManager::GetCurrentScene() const
{
    return m_sceneStack.empty() ? nullptr : m_sceneStack.back().get();
}

void SceneManager::SwitchScene(std::unique_ptr<Scene> scene, TransitionType type, float duration)
{
    m_pendingScene = std::move(scene);
    m_pendingIsPush = m_pendingIsPop = false;
    m_transitionType = type;
    m_transitionDuration = std::max(0.05f, duration);
}

void SceneManager::PushScene(std::unique_ptr<Scene> scene, TransitionType type, float duration)
{
    SwitchScene(std::move(scene), type, duration);
    m_pendingIsPush = true;
}

void SceneManager::PopScene(TransitionType type, float duration)
{
    if (m_sceneStack.size() < 2) return;
    m_pendingScene.reset();
    m_pendingIsPop = true;
    m_pendingIsPush = false;
    m_transitionType = type;
    m_transitionDuration = std::max(0.05f, duration);
}

void SceneManager::ApplyPendingSwitch()
{
    const bool pop = m_pendingIsPop;
    const bool push = m_pendingIsPush;
    auto next = std::move(m_pendingScene);
    // Clear flags before callbacks, which may request a fallback scene on load failure.
    m_pendingIsPop = m_pendingIsPush = false;
    if (pop)
    {
        m_sceneStack.back()->OnExit();
        m_sceneStack.pop_back();
        return;
    }
    if (!next) return;
    if (!push)
        while (!m_sceneStack.empty())
        {
            m_sceneStack.back()->OnExit();
            m_sceneStack.pop_back();
        }
    m_sceneStack.push_back(std::move(next));
    m_sceneStack.back()->OnEnter();
}

void SceneManager::Update(float dt)
{
    if (m_isTransitioning)
    {
        m_transitionTimer += dt;
        if (!m_switched && m_transitionTimer >= m_transitionDuration * 0.5f)
        {
            m_switched = true;
            ApplyPendingSwitch();
        }
        if (m_transitionTimer < m_transitionDuration) return;
        m_isTransitioning = false;
        if (m_texFrom) SDL_DestroyTexture(m_texFrom);
        m_texFrom = nullptr;
        if(m_texTo)SDL_DestroyTexture(m_texTo);
        m_texTo=nullptr;
    }
    if (m_pendingScene || m_pendingIsPop)
    {
        if (m_transitionType == TransitionType::None || m_sceneStack.empty() ||
            sakura::core::Config::GetInstance().Get<bool>("graphics.reduced_motion", false))
            ApplyPendingSwitch();
        else
        {
            m_isTransitioning = true;
            m_switched = false;
            m_transitionTimer = 0.0f;
        }
        return;
    }
    if (auto* scene = GetCurrentScene()) scene->OnUpdate(dt);
}

void SceneManager::HandleEvent(const SDL_Event& event)
{
    if (m_isTransitioning || m_pendingScene || m_pendingIsPop) return;
    if (auto* scene = GetCurrentScene()) scene->OnEvent(event);
}

void SceneManager::RenderStack(sakura::core::Renderer& renderer)
{
    if (m_sceneStack.empty()) return;
    size_t first = m_sceneStack.size() - 1;
    while (first > 0 && m_sceneStack[first]->IsTransparent()) --first;
    for (size_t i = first; i < m_sceneStack.size(); ++i) m_sceneStack[i]->OnRender(renderer);
}

void SceneManager::Render(sakura::core::Renderer& renderer)
{
    if (m_isTransitioning) RenderTransition(renderer);
    else RenderStack(renderer);
}

void SceneManager::RenderTransition(sakura::core::Renderer& renderer)
{
    auto* native=renderer.GetSDLRenderer();
    const float width=static_cast<float>(renderer.GetScreenWidth()),height=static_cast<float>(renderer.GetScreenHeight());
    const float t=std::clamp(m_transitionTimer/m_transitionDuration,0.0f,1.0f);
    auto*& texture=m_switched?m_texTo:m_texFrom;
    if(!texture){
        texture=SDL_CreateTexture(native,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,static_cast<int>(width),static_cast<int>(height));
        if(texture){
            auto* previous=SDL_GetRenderTarget(native);
            SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);
            if(SDL_SetRenderTarget(native,texture)){
                SDL_SetRenderDrawColor(native,12,15,26,255);SDL_RenderClear(native);
                RenderStack(renderer);renderer.Flush();SDL_SetRenderTarget(native,previous);
            }else{SDL_DestroyTexture(texture);texture=nullptr;}
        }
    }
    if(!texture){RenderStack(renderer);return;}
    const float progress=m_switched?std::clamp((t-0.5f)*2,0.0f,1.0f):std::clamp(t*2,0.0f,1.0f);
    const float eased=progress*progress*(3-2*progress);
    const float amount=m_switched?1-eased:eased;
    SDL_SetRenderDrawColor(native,12,15,26,255);SDL_RenderClear(native);
    SDL_FRect destination{0,0,width,height};
    switch(m_transitionType){
    case TransitionType::SlideLeft:destination.x=(m_switched?1:-1)*amount*width;break;
    case TransitionType::SlideRight:destination.x=(m_switched?-1:1)*amount*width;break;
    case TransitionType::SlideUp:destination.y=(m_switched?1:-1)*amount*height;break;
    case TransitionType::SlideDown:destination.y=(m_switched?-1:1)*amount*height;break;
    case TransitionType::Scale:
        destination.w=width*(1-0.12f*amount);destination.h=height*(1-0.12f*amount);
        destination.x=(width-destination.w)*0.5f;destination.y=(height-destination.h)*0.5f;
        SDL_SetTextureAlphaMod(texture,static_cast<Uint8>((1-amount)*255));break;
    case TransitionType::CircleWipe:{
        constexpr int segments=96;SDL_Vertex vertices[segments+2];int indices[segments*3];
        const float radius=std::sqrt(width*width+height*height)*0.51f*(1-amount);
        vertices[0]={{width/2,height/2},{1,1,1,1},{0.5f,0.5f}};
        for(int i=0;i<=segments;++i){
            const float angle=i*6.283185307f/segments;
            const float x=width/2+std::cos(angle)*radius,y=height/2+std::sin(angle)*radius;
            vertices[i+1]={{x,y},{1,1,1,1},{x/width,y/height}};
            if(i<segments){indices[i*3]=0;indices[i*3+1]=i+1;indices[i*3+2]=i+2;}
        }
        SDL_RenderGeometry(native,texture,vertices,segments+2,indices,segments*3);return;
    }
    default:SDL_SetTextureAlphaMod(texture,static_cast<Uint8>((1-amount)*255));break;
    }
    SDL_RenderTexture(native,texture,nullptr,&destination);
    SDL_SetTextureAlphaMod(texture,255);
}
} // namespace sakura::scene
