#include "Engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace gxr
{
namespace
{
constexpr long long kCoarseBudget = 30'000;
constexpr long long kFineBudget = 600'000;
constexpr int kCoarseSubBits = 1;
constexpr int kFineSubBits = 4;
constexpr size_t kResidencyTiles = 4096;

std::atomic<uint64_t> g_payloadCounter{1};
}

Engine::Engine(int tilePx, int numWorkers) : tilePx_(tilePx)
{
    liveCancel_ = std::make_shared<std::atomic<bool>>(false);
    if (numWorkers <= 0)
    {
        const unsigned hc = std::thread::hardware_concurrency();
        numWorkers = static_cast<int>(hc > 2 ? hc - 1 : 1);
    }
    schedulerThread_ = std::thread([this] { schedulerLoop(); });
    workers_.reserve(static_cast<size_t>(numWorkers));
    for (int i = 0; i < numWorkers; ++i)
    {
        workers_.emplace_back([this, i] { workerLoop(i); });
    }
}

Engine::~Engine()
{
    stop_.store(true);
    mailCv_.notify_all();
    jobCv_.notify_all();
    if (schedulerThread_.joinable()) schedulerThread_.join();
    for (auto &t : workers_)
        if (t.joinable()) t.join();
}

void Engine::setRelation(std::shared_ptr<const Relation> rel)
{
    auto newCancel = std::make_shared<std::atomic<bool>>(false);
    {
        std::scoped_lock lock(mailMutex_);
        if (liveCancel_) liveCancel_->store(true); // cancel all in-flight prior-epoch work
        liveCancel_ = newCancel;
        mailRelation_ = std::move(rel);
        epoch_.fetch_add(1);
        mailDirty_ = true;
    }
    mailCv_.notify_all();
}

void Engine::setViewport(const Viewport &vp)
{
    {
        std::scoped_lock lock(mailMutex_);
        mailViewport_ = vp;
        mailDirty_ = true;
    }
    mailCv_.notify_all();
}

std::vector<TileKey> Engine::visibleKeys(const Viewport &vp, uint64_t epoch) const
{
    const int level = vp.activeLevel();
    const double span = tileSpanWorld(level, tilePx_);
    const WorldRect r = vp.worldBounds();
    const int64_t i0 = floorDiv(r.x0, span), i1 = floorDiv(r.x1, span);
    const int64_t j0 = floorDiv(r.y0, span), j1 = floorDiv(r.y1, span);

    std::vector<TileKey> keys;
    keys.reserve(static_cast<size_t>((i1 - i0 + 1) * (j1 - j0 + 1)));
    for (int64_t j = j0; j <= j1; ++j)
        for (int64_t i = i0; i <= i1; ++i)
            keys.push_back(TileKey{epoch, level, i, j});

    // center-out ordering so the screen paints from the focus outward
    const double cx = 0.5 * (i0 + i1), cy = 0.5 * (j0 + j1);
    std::sort(keys.begin(), keys.end(), [cx, cy](const TileKey &a, const TileKey &b) {
        const double da = (a.i - cx) * (a.i - cx) + (a.j - cy) * (a.j - cy);
        const double db = (b.i - cx) * (b.i - cx) + (b.j - cy) * (b.j - cy);
        return da < db;
    });
    return keys;
}

void Engine::enqueueVisible(const Viewport &vp, std::shared_ptr<const Relation> rel, uint64_t epoch,
                            const std::shared_ptr<std::atomic<bool>> &cancel)
{
    if (!rel) return;
    const std::vector<TileKey> keys = visibleKeys(vp, epoch);
    std::vector<Job> toEnqueue;
    toEnqueue.reserve(keys.size());
    for (const TileKey &k : keys)
    {
        if (store_.ensureQueued(k) == TileState::Missing)
        {
            toEnqueue.push_back(Job{k, rel, epoch, cancel, tileRect(k, tilePx_)});
        }
    }
    if (!toEnqueue.empty())
    {
        {
            std::scoped_lock lock(jobMutex_);
            for (auto &j : toEnqueue) jobs_.push_back(std::move(j));
        }
        jobCv_.notify_all();
    }
    store_.evictToBudget(kResidencyTiles, epoch);
}

void Engine::schedulerLoop()
{
    while (!stop_.load())
    {
        Viewport vp;
        std::shared_ptr<const Relation> rel;
        std::shared_ptr<std::atomic<bool>> cancel;
        uint64_t epoch;
        {
            std::unique_lock lock(mailMutex_);
            mailCv_.wait(lock, [this] { return mailDirty_ || stop_.load(); });
            if (stop_.load()) break;
            vp = mailViewport_;
            rel = mailRelation_;
            cancel = liveCancel_;
            epoch = epoch_.load();
            mailDirty_ = false;
        }
        enqueueVisible(vp, rel, epoch, cancel);
    }
}

void Engine::workerLoop(int)
{
    EvalScratch scratch;
    while (!stop_.load())
    {
        Job job;
        {
            std::unique_lock lock(jobMutex_);
            jobCv_.wait(lock, [this] { return !jobs_.empty() || stop_.load(); });
            if (stop_.load()) break;
            job = std::move(jobs_.front());
            jobs_.pop_front();
            jobsInFlight_.fetch_add(1);
        }

        const CancelToken ct{job.cancel.get()};
        if (!ct.cancelled())
        {
            // coarse pass -> publish immediately for fast first paint
            SolveParams coarse{tilePx_, kCoarseSubBits, kCoarseBudget, true};
            CoverageTile c = solveTile(*job.rel, job.rect, coarse, scratch, ct);
            if (!ct.cancelled())
            {
                c.payloadId = g_payloadCounter.fetch_add(1);
                store_.publish(job.key, std::make_shared<CoverageTile>(std::move(c)), false);

                // fine pass -> converge
                SolveParams fine{tilePx_, kFineSubBits, kFineBudget, true};
                CoverageTile f = solveTile(*job.rel, job.rect, fine, scratch, ct);
                if (!ct.cancelled())
                {
                    f.payloadId = g_payloadCounter.fetch_add(1);
                    store_.publish(job.key, std::make_shared<CoverageTile>(std::move(f)), true);
                }
            }
        }

        jobsCompleted_.fetch_add(1);
        if (jobsInFlight_.fetch_sub(1) == 1)
        {
            std::scoped_lock lock(jobMutex_);
            if (jobs_.empty()) jobCv_.notify_all();
        }
    }
}

size_t Engine::buildPresent(const Viewport &vp, std::vector<PresentTile> &out)
{
    out.clear();
    const uint64_t epoch = epoch_.load();
    const uint64_t frame = frameCounter_.fetch_add(1);
    const std::vector<TileKey> keys = visibleKeys(vp, epoch);

    for (const TileKey &k : keys)
    {
        store_.touch(k, frame);
        CoverageTilePtr cov = store_.snapshot(k);
        if (cov)
        {
            out.push_back(PresentTile{k, tileRect(k, tilePx_), std::move(cov), k.level, false});
            continue;
        }
        // fall back to a coarser ancestor in the same epoch (upscaled on screen)
        bool found = false;
        TileKey anc = k;
        for (int up = 0; up < 6 && !found; ++up)
        {
            anc = TileKey{epoch, anc.level + 1, anc.i >> 1, anc.j >> 1};
            CoverageTilePtr acov = store_.snapshot(anc);
            if (acov)
            {
                out.push_back(PresentTile{anc, tileRect(anc, tilePx_), std::move(acov), anc.level, true});
                found = true;
            }
        }
        // if still nothing, the tile is simply absent this frame (fills shortly)
    }
    return keys.size();
}

void Engine::waitUntilQuiescent()
{
    for (;;)
    {
        bool idle = false;
        {
            std::scoped_lock lock(mailMutex_, jobMutex_);
            idle = !mailDirty_ && jobs_.empty() && jobsInFlight_.load() == 0;
        }
        if (idle) return;
        std::unique_lock lock(jobMutex_);
        jobCv_.wait_for(lock, std::chrono::milliseconds(2));
    }
}
}
