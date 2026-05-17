#ifndef MAINTHREADREQUESTMAILBOX_H
#define MAINTHREADREQUESTMAILBOX_H

#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../App/AppState.h"

namespace gx
{
struct FramebufferResizeRequest
{
    int width{0};
    int height{0};
};

struct MainThreadRequestBatch
{
    std::optional<FramebufferResizeRequest> resize{};
    std::vector<InputEvent> inputEvents{};
    std::vector<std::filesystem::path> captures{};
    bool frameWake{false};
    bool refreshRequested{false};
    bool closeRequested{false};
    std::string wakeReason{};
    size_t coalescedResizeRequests{0};
    size_t coalescedViewportEvents{0};

    [[nodiscard]] bool empty() const;
    [[nodiscard]] bool needsFrameUpdate() const;
};

class MainThreadRequestMailbox
{
public:
    using WakePoster = std::function<void()>;

    explicit MainThreadRequestMailbox(WakePoster wakePoster = {});

    void setWakePoster(WakePoster wakePoster);
    void setWakePostingEnabled(bool enabled);

    bool submitResize(int width, int height, std::string_view reason = "resize");
    bool submitInput(InputEvent event, std::string_view reason = "input");
    bool submitFrameWake(std::string_view reason = "wake");
    bool submitRefresh(std::string_view reason = "refresh");
    bool submitCapture(std::filesystem::path path, std::string_view reason = "capture");
    bool submitClose(std::string_view reason = "close");

    [[nodiscard]] bool hasPendingWork() const;
    [[nodiscard]] size_t pendingInputCount() const;
    [[nodiscard]] bool hasPendingResize() const;
    [[nodiscard]] MainThreadRequestBatch drain();

private:
    [[nodiscard]] bool markWakeLocked(std::string_view reason, WakePoster &poster);
    static void appendCoalescedInput(std::vector<InputEvent> &events,
                                     InputEvent event,
                                     size_t &coalescedViewportEvents);

    mutable std::mutex mutex;
    WakePoster wakePoster;
    bool wakePostingEnabled{true};
    bool wakePosted{false};
    bool frameWakeRequested{false};
    bool refreshRequested{false};
    bool closeRequested{false};
    std::string wakeReason{};
    std::optional<FramebufferResizeRequest> latestResize{};
    size_t coalescedResizeRequests{0};
    size_t coalescedViewportEvents{0};
    std::vector<InputEvent> inputEvents{};
    std::vector<std::filesystem::path> captures{};
};
}

#endif // MAINTHREADREQUESTMAILBOX_H
