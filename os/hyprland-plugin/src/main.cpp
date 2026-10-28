// Mobarchy's Hyprland plugin (ADR 0045): the screen turns as the device does.
//
// A turn is a change of a monitor's transform (the device's rotation hook rewrites the monitor
// rules and reloads them, ADR 0020). Hyprland applies it at once: the apps then draw at the new
// size and the picture jumps. Here, for a moment after, each frame as Hyprland has drawn it
// (live, in the new orientation) is turned back to where the screen was and eases round to
// rest, shrunk to fit while it is crosswise: the screen seems to turn, as a phone's does.
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopTimer.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>

#include <chrono>
#include <cmath>
#include <map>
#include <numbers>
#include <string>

using namespace Render;

namespace {

HANDLE g_handle = nullptr;
SP<Config::Values::CIntValue> g_duration;   // ms

struct Turn {
    std::chrono::steady_clock::time_point start;
    double from = 0;        // radians the picture starts turned by
    double fit = 1;         // its scale then (crosswise: fitted in)
};
std::map<std::string, wl_output_transform> g_before;    // transforms as they were before a reload
std::map<std::string, Turn> g_turns;                     // monitors turning now
std::vector<CHyprSignalListener> g_listeners;
SP<CEventLoopTimer>              g_tick;                 // asks for the turning monitors' frames

// (a frame asked for while one is out can be lost, on the inner screen at least: frames are
// asked for on a clock, and for a moment after the turn, so the last, plain one is drawn)
constexpr auto Tick = std::chrono::milliseconds(8);
constexpr auto Tail = std::chrono::milliseconds(150);

double elapsed(const Turn& t)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t.start).count();
}

double progress(const Turn& t)
{
    return std::clamp(elapsed(t) / std::max<int64_t>(1, g_duration->value()), 0.0, 1.0);
}

void tick()
{
    for (auto it = g_turns.begin(); it != g_turns.end();)
        if (elapsed(it->second) > g_duration->value() + std::chrono::duration<double, std::milli>(Tail).count())
            it = g_turns.erase(it);
        else
            ++it;
    if (g_turns.empty()) {
        g_pHyprRenderer->m_directScanoutBlocked = false;
        return;
    }
    for (const auto& m : State::monitorState()->monitors())
        if (g_turns.contains(m->m_name))
            g_pHyprRenderer->damageMonitor(m);
    g_tick->updateTimeout(Tick);
}

// Draws the frame as rendered so far (the whole pass before it) turned and scaled about the
// middle, black around: the last element of the frame.
class CTurnElement : public IPassElement {
  public:
    CTurnElement(double angle, double scale) : m_angle(angle), m_scale(scale) {}

    std::vector<UP<IPassElement>> draw() override
    {
        const auto& rd  = g_pHyprRenderer->m_renderData;
        const auto  mon = rd.pMonitor.lock();
        auto*       from = rd.currentFB ? dynamic_cast<GL::CGLFramebuffer*>(rd.currentFB.get()) : nullptr;
        const auto  copy = mon ? mon->resources()->getUnusedWorkBuffer() : nullptr;
        auto*       to   = copy ? dynamic_cast<GL::CGLFramebuffer*>(copy.get()) : nullptr;
        if (!from || !to)
            return {};
        // the frame so far, pixel for pixel (a blit: drawing it through Hyprland's texture
        // functions left the copy empty)
        const auto px = rd.currentFB->m_size;
        glDisable(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, from->getFBID());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, to->getFBID());
        glBlitFramebuffer(0, 0, px.x, px.y, 0, 0, px.x, px.y, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        g_pHyprRenderer->bindFB(rd.currentFB);

        // black, then the copy turned (in the monitor's own coordinates: with its transform,
        // as the frame is finally put on the output)
        const auto size = mon->m_transformedSize;
        GL::g_pHyprOpenGL->renderRect({0, 0, size.x, size.y}, CHyprColor(0, 0, 0, 1), {});
        CBox turned = {size.x * (1 - m_scale) / 2, size.y * (1 - m_scale) / 2, size.x * m_scale, size.y * m_scale};
        turned.rot  = m_angle;
        g_pHyprRenderer->pushMonitorTransformEnabled(true);
        GL::g_pHyprOpenGL->renderTexture(copy->getTexture(), turned, {});
        g_pHyprRenderer->popMonitorTransformEnabled();
        return {};
    }
    bool             needsLiveBlur() override { return false; }
    bool             needsPrecomputeBlur() override { return false; }
    const char*      passName() override { return "CTurnElement"; }
    ePassElementType type() override { return EK_CUSTOM; }
    bool             undiscardable() override { return true; }
    bool             disableSimplification() override { return true; }

  private:
    double m_angle, m_scale;
};

void remember()
{
    g_before.clear();
    for (const auto& m : State::monitorState()->monitors())
        g_before[m->m_name] = m->m_transform;
}

// After a reload: the monitors whose transform changed start turning.
void compare()
{
    for (const auto& m : State::monitorState()->monitors()) {
        const auto was = g_before.find(m->m_name);
        if (was == g_before.end() || was->second == m->m_transform)
            continue;
        // quarter turns from the old transform to the new (wl_output: counter-clockwise),
        // the shorter way round
        int quarters = ((int(m->m_transform) & 3) - (int(was->second) & 3) + 4) % 4;
        if (quarters == 3)
            quarters = -1;
        const double ratio = m->m_transformedSize.x / std::max(1.0, m->m_transformedSize.y);
        g_turns[m->m_name] = {std::chrono::steady_clock::now(), quarters * std::numbers::pi / 2,
                              quarters % 2 ? std::min(ratio, 1 / ratio) : 1.0};
        g_pHyprRenderer->m_directScanoutBlocked = true;
        g_pHyprRenderer->damageMonitor(m);
        g_tick->updateTimeout(Tick);
    }
    g_before.clear();
}

// (a whole frame each time while turning)
void pre(PHLMONITOR mon)
{
    if (mon && g_turns.contains(mon->m_name))
        mon->m_forceFullFrames = std::max(mon->m_forceFullFrames, 1);
}

void stage(eRenderStage stage)
{
    if (stage != RENDER_LAST_MOMENT)
        return;
    const auto mon = g_pHyprRenderer->m_renderData.pMonitor.lock();
    const auto it  = mon ? g_turns.find(mon->m_name) : g_turns.end();
    if (it == g_turns.end())
        return;
    const double p = progress(it->second);
    if (p >= 1)
        return;
    const double ease  = 1 - std::pow(1 - p, 3);
    const double angle = it->second.from * (1 - ease);
    const double scale = it->second.fit + (1 - it->second.fit) * ease;
    g_pHyprRenderer->m_renderPass.add(makeUnique<CTurnElement>(angle, scale));
}

} // namespace

APICALL EXPORT std::string PLUGIN_API_VERSION()
{
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle)
{
    g_handle = handle;
    if (std::string(__hyprland_api_get_hash()) != std::string(__hyprland_api_get_client_hash())) {
        HyprlandAPI::addNotification(g_handle, "[mobarchy] built for another Hyprland: not loaded", CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[mobarchy] version mismatch");
    }
    g_duration = makeShared<Config::Values::CIntValue>("plugin:mobarchy:turn_ms", "How long the screen takes to turn (ms)", 350);
    HyprlandAPI::addConfigValueV2(g_handle, g_duration);

    g_tick = makeShared<CEventLoopTimer>(std::nullopt, [](SP<CEventLoopTimer>, void*) { tick(); }, nullptr);
    g_pEventLoopManager->addTimer(g_tick);

    g_listeners = {
        Event::bus()->m_events.config.preReload.listen([] { remember(); }),
        Event::bus()->m_events.monitor.layoutChanged.listen([] { compare(); }),
        Event::bus()->m_events.config.reloaded.listen([] { compare(); }),
        Event::bus()->m_events.render.stage.listen([](eRenderStage s) { stage(s); }),
        Event::bus()->m_events.render.pre.listen([](PHLMONITOR m) { pre(m); }),
    };

    return {"mobarchy", "Mobarchy: the screen turns as the device does", "Mobarchy", "0.1"};
}

APICALL EXPORT void PLUGIN_EXIT()
{
    // (the listeners go with the plugin: one left behind would call into its unloaded code)
    g_listeners.clear();
    g_pEventLoopManager->removeTimer(g_tick);
    g_tick.reset();
    g_turns.clear();
    g_pHyprRenderer->m_directScanoutBlocked = false;
}
