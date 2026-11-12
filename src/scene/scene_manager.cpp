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
    SDL_SetRenderDrawColor(native,8,11,22,255);SDL_RenderClear(native);
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
    case TransitionType::SilkShutter:{
        destination.w=width*(1.0f-0.025f*amount);destination.h=height*(1.0f-0.025f*amount);
        destination.x=(width-destination.w)*0.5f;destination.y=(height-destination.h)*0.5f;
        SDL_SetTextureAlphaMod(texture,static_cast<Uint8>((1.0f-amount*0.35f)*255));
        SDL_RenderTexture(native,texture,nullptr,&destination);
        SDL_SetTextureAlphaMod(texture,255);
        constexpr int kBlades=7;
        const float skew=0.14f;
        const float span=1.0f+skew;
        for(int i=0;i<kBlades;++i){
            const float stagger=static_cast<float>(i)/static_cast<float>(kBlades-1)*0.22f;
            const float local=std::clamp((amount*(1.0f+0.22f)-stagger),0.0f,1.0f);
            if(local<=0.002f)continue;
            const float slotW=span/static_cast<float>(kBlades);
            const float center=(static_cast<float>(i)+0.5f)*slotW-skew*0.5f;
            const float halfW=slotW*0.54f*local;
            const sakura::core::Color cTop{18,24,46,static_cast<uint8_t>(248*local)};
            const sakura::core::Color cBot{28,22,48,static_cast<uint8_t>(252*local)};
            renderer.DrawQuad(
                {center-halfW+skew*0.5f,0.0f},{center+halfW+skew*0.5f,0.0f},
                {center+halfW-skew*0.5f,1.0f},{center-halfW-skew*0.5f,1.0f},
                cTop,cTop,cBot,cBot);
            renderer.DrawLine(center+halfW+skew*0.5f,0.0f,center+halfW-skew*0.5f,1.0f,
                {250,216,157,static_cast<uint8_t>(185*local)},0.0018f);
        }
        renderer.Flush();
        return;
    }
    case TransitionType::SakuraSweep:{
        destination.x=(m_switched?0.045f:-0.045f)*amount*width;
        SDL_SetTextureAlphaMod(texture,static_cast<Uint8>((1.0f-amount*0.45f)*255));
        SDL_RenderTexture(native,texture,nullptr,&destination);
        SDL_SetTextureAlphaMod(texture,255);
        const float sweepCenter=1.25f-2.50f*(t*t*(3.0f-2.0f*t));
        const float bandHalf=0.38f+0.28f*amount;
        const float skew=0.18f;
        renderer.DrawQuad(
            {sweepCenter-bandHalf+skew,0.0f},{sweepCenter+bandHalf+skew,0.0f},
            {sweepCenter+bandHalf-skew,1.0f},{sweepCenter-bandHalf-skew,1.0f},
            {14,18,36,static_cast<uint8_t>(252*amount)},{26,20,44,static_cast<uint8_t>(252*amount)},
            {22,18,40,static_cast<uint8_t>(252*amount)},{12,16,32,static_cast<uint8_t>(252*amount)});
        renderer.SetBlendMode(sakura::core::BlendMode::Additive);
        renderer.DrawLine(sweepCenter-bandHalf+skew,0.0f,sweepCenter-bandHalf-skew,1.0f,
            {246,172,200,static_cast<uint8_t>(210*amount)},0.006f);
        renderer.DrawLine(sweepCenter+bandHalf+skew,0.0f,sweepCenter+bandHalf-skew,1.0f,
            {250,216,157,static_cast<uint8_t>(190*amount)},0.004f);
        renderer.DrawRadialGradient(0.5f,0.5f,0.28f*amount,0.28f*amount,
            {246,178,206,static_cast<uint8_t>(95*amount)},{246,178,206,0},40);
        renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
        renderer.Flush();
        return;
    }
    default:SDL_SetTextureAlphaMod(texture,static_cast<Uint8>((1-amount)*255));break;
    }
    SDL_RenderTexture(native,texture,nullptr,&destination);
    SDL_SetTextureAlphaMod(texture,255);
}
} // namespace sakura::scene
