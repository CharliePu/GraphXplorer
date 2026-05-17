#include "MainThreadRequestMailbox.h"

#include <utility>
#include <variant>

namespace gx
{
bool MainThreadRequestBatch::empty() const
{
    return !resize.has_value()
        && inputEvents.empty()
        && captures.empty()
        && !frameWake
        && !refreshRequested
        && !closeRequested;
}

bool MainThreadRequestBatch::needsFrameUpdate() const
{
    return resize.has_value()
        || !inputEvents.empty()
        || !captures.empty()
        || frameWake
        || refreshRequested;
}

MainThreadRequestMailbox::MainThreadRequestMailbox(WakePoster nextWakePoster)
    : wakePoster{std::move(nextWakePoster)}
{
}

void MainThreadRequestMailbox::setWakePoster(WakePoster nextWakePoster)
{
    std::lock_guard lock(mutex);
    wakePoster = std::move(nextWakePoster);
}

void MainThreadRequestMailbox::setWakePostingEnabled(const bool enabled)
{
    std::lock_guard lock(mutex);
    wakePostingEnabled = enabled;
}

bool MainThreadRequestMailbox::submitResize(const int width,
                                            const int height,
                                            const std::string_view reason)
{
    WakePoster poster;
    auto posted = false;
    {
        std::lock_guard lock(mutex);
        if (latestResize.has_value())
        {
            ++coalescedResizeRequests;
        }
        latestResize = FramebufferResizeRequest{width, height};
        posted = markWakeLocked(reason, poster);
    }
    if (posted && poster)
    {
        poster();
    }
    return posted;
}

bool MainThreadRequestMailbox::submitInput(InputEvent event, const std::string_view reason)
{
    WakePoster poster;
    auto posted = false;
    {
        std::lock_guard lock(mutex);
        appendCoalescedInput(inputEvents, std::move(event), coalescedViewportEvents);
        posted = markWakeLocked(reason, poster);
    }
    if (posted && poster)
    {
        poster();
    }
    return posted;
}

bool MainThreadRequestMailbox::submitFrameWake(const std::string_view reason)
{
    WakePoster poster;
    auto posted = false;
    {
        std::lock_guard lock(mutex);
        frameWakeRequested = true;
        posted = markWakeLocked(reason, poster);
    }
    if (posted && poster)
    {
        poster();
    }
    return posted;
}

bool MainThreadRequestMailbox::submitRefresh(const std::string_view reason)
{
    WakePoster poster;
    auto posted = false;
    {
        std::lock_guard lock(mutex);
        refreshRequested = true;
        frameWakeRequested = true;
        posted = markWakeLocked(reason, poster);
    }
    if (posted && poster)
    {
        poster();
    }
    return posted;
}

bool MainThreadRequestMailbox::submitCapture(std::filesystem::path path,
                                             const std::string_view reason)
{
    WakePoster poster;
    auto posted = false;
    {
        std::lock_guard lock(mutex);
        captures.push_back(std::move(path));
        frameWakeRequested = true;
        posted = markWakeLocked(reason, poster);
    }
    if (posted && poster)
    {
        poster();
    }
    return posted;
}

bool MainThreadRequestMailbox::submitClose(const std::string_view reason)
{
    WakePoster poster;
    auto posted = false;
    {
        std::lock_guard lock(mutex);
        closeRequested = true;
        posted = markWakeLocked(reason, poster);
    }
    if (posted && poster)
    {
        poster();
    }
    return posted;
}

bool MainThreadRequestMailbox::hasPendingWork() const
{
    std::lock_guard lock(mutex);
    return latestResize.has_value()
        || !inputEvents.empty()
        || !captures.empty()
        || frameWakeRequested
        || refreshRequested
        || closeRequested;
}

size_t MainThreadRequestMailbox::pendingInputCount() const
{
    std::lock_guard lock(mutex);
    return inputEvents.size();
}

bool MainThreadRequestMailbox::hasPendingResize() const
{
    std::lock_guard lock(mutex);
    return latestResize.has_value();
}

MainThreadRequestBatch MainThreadRequestMailbox::drain()
{
    std::lock_guard lock(mutex);
    MainThreadRequestBatch batch{
        .resize = std::exchange(latestResize, std::nullopt),
        .inputEvents = std::move(inputEvents),
        .captures = std::move(captures),
        .frameWake = std::exchange(frameWakeRequested, false),
        .refreshRequested = std::exchange(refreshRequested, false),
        .closeRequested = std::exchange(closeRequested, false),
        .wakeReason = std::move(wakeReason),
        .coalescedResizeRequests = std::exchange(coalescedResizeRequests, size_t{0}),
        .coalescedViewportEvents = std::exchange(coalescedViewportEvents, size_t{0})
    };
    inputEvents.clear();
    captures.clear();
    wakeReason.clear();
    wakePosted = false;
    return batch;
}

bool MainThreadRequestMailbox::markWakeLocked(const std::string_view reason,
                                              WakePoster &poster)
{
    if (!wakePostingEnabled || wakePosted)
    {
        return false;
    }

    wakePosted = true;
    wakeReason = std::string{reason};
    poster = wakePoster;
    return true;
}

void MainThreadRequestMailbox::appendCoalescedInput(std::vector<InputEvent> &events,
                                                    InputEvent event,
                                                    size_t &coalesced)
{
    if (std::holds_alternative<ViewportChangedEvent>(event)
        && !events.empty()
        && std::holds_alternative<ViewportChangedEvent>(events.back()))
    {
        events.back() = std::move(event);
        ++coalesced;
        return;
    }

    events.push_back(std::move(event));
}
}
