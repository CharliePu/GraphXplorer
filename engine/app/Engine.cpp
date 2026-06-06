#include "Engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace gxr
{
namespace
{
constexpr long long kCoarseBudget = 20'000;  // fast first paint (~few ms/tile)
constexpr long long kFineBudget = 200'000;    // bounded refinement (~tens of ms/tile)
constexpr int kCoarseSubBits = 1;
constexpr int kFineSubBits = 4;
constexpr size_t kResidencyTiles = 8192;
constexpr int kMaxAncestorUp = 16; // fallback search depth for a covering ancestor

std::atomic<uint64_t> g_payloadCounter{1};

TileKey parentKey(const TileKey &k) { return {k.epoch, k.level + 1, k.i >> 1, k.j >> 1}; }

void childKeys(const TileKey &k, TileKey out[4])
{
    const int cl = k.level - 1;
    out[0] = {k.epoch, cl, k.i * 2, k.j * 2};
    out[1] = {k.epoch, cl, k.i * 2 + 1, k.j * 2};
    out[2] = {k.epoch, cl, k.i * 2, k.j * 2 + 1};
    out[3] = {k.epoch, cl, k.i * 2 + 1, k.j * 2 + 1};
}
}

Engine::Engine(int tilePx, int numWorkers, std::function<void()> wake)
    : tilePx_(tilePx), wake_(std::move(wake))
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

int Engine::chooseStartLevel(const Viewport &vp) const
{
    const int detail = vp.activeLevel();
    const WorldRect r = vp.worldBounds();
    int level = detail;
    for (int extra = 0; extra < 24; ++extra)
    {
        const double span = tileSpanWorld(level, tilePx_);
        const int64_t nx = floorDiv(r.x1, span) - floorDiv(r.x0, span) + 1;
        const int64_t ny = floorDiv(r.y1, span) - floorDiv(r.y0, span) + 1;
        if (nx <= 2 && ny <= 2) break; // ~2x2 coarse roots cover the viewport
        ++level;
    }
    return level;
}

std::vector<TileKey> Engine::startNodes(const Viewport &vp, uint64_t epoch) const
{
    const int level = chooseStartLevel(vp);
    const double span = tileSpanWorld(level, tilePx_);
    const WorldRect r = vp.worldBounds();
    const int64_t i0 = floorDiv(r.x0, span), i1 = floorDiv(r.x1, span);
    const int64_t j0 = floorDiv(r.y0, span), j1 = floorDiv(r.y1, span);
    std::vector<TileKey> keys;
    keys.reserve(static_cast<size_t>((i1 - i0 + 1) * (j1 - j0 + 1)));
    for (int64_t j = j0; j <= j1; ++j)
        for (int64_t i = i0; i <= i1; ++i)
            keys.push_back(TileKey{epoch, level, i, j});
    return keys;
}

void Engine::pushJobs(std::vector<Job> &jobs)
{
    if (jobs.empty()) return;
    {
        std::scoped_lock lock(jobMutex_);
        for (auto &j : jobs) jobs_.push_back(std::move(j));
    }
    jobCv_.notify_all();
}

void Engine::enqueueVisible(const Viewport &vp, std::shared_ptr<const Relation> rel, uint64_t epoch,
                            const std::shared_ptr<std::atomic<bool>> &cancel)
{
    if (!rel) return;
    const int detail = vp.activeLevel();
    const std::vector<TileKey> keys = startNodes(vp, epoch);
    std::vector<Job> toEnqueue;
    toEnqueue.reserve(keys.size());
    for (const TileKey &k : keys)
    {
        if (store_.ensureQueued(k) == TileState::Missing)
        {
            // start nodes are above the detail level; if Mixed they render a
            // coarse placeholder raster (the no-holes base layer) and spawn children.
            toEnqueue.push_back(Job{k, rel, epoch, cancel, tileRect(k, tilePx_), detail, true});
        }
    }
    pushJobs(toEnqueue);
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
            // 1) Greedy classification of the whole node (sound: uniform only if proven).
            const NodeClass nc = classifyRegion(*job.rel, job.rect, scratch, ct);
            if (!ct.cancelled())
            {
                store_.setClass(job.key, nc);
                const bool atDetail = job.key.level <= job.detailLevel;

                if (nc == NodeClass::UniformTrue || nc == NodeClass::UniformFalse)
                {
                    // greedy leaf: no raster; the flat is drawn from the classification.
                    store_.publish(job.key, nullptr, true);
                    if (wake_) wake_();
                }
                else // Mixed: needs a raster (fine at detail level) and, if coarse, children.
                {
                    const bool wantRaster = atDetail || job.baseRaster;
                    if (wantRaster)
                    {
                        const int sub = atDetail ? kFineSubBits : kCoarseSubBits;
                        const long long budget = atDetail ? kFineBudget : kCoarseBudget;
                        SolveParams p{tilePx_, sub, budget, true};
                        CoverageTile c = solveTile(*job.rel, job.rect, p, scratch, ct);
                        if (!ct.cancelled())
                        {
                            c.payloadId = g_payloadCounter.fetch_add(1);
                            store_.publish(job.key, std::make_shared<CoverageTile>(std::move(c)),
                                           /*done=*/atDetail);
                            if (wake_) wake_();
                        }
                    }
                    else
                    {
                        // intermediate Mixed node: classified, no raster; just descend.
                        store_.publish(job.key, nullptr, false);
                    }

                    if (!atDetail && !ct.cancelled())
                    {
                        TileKey ch[4];
                        childKeys(job.key, ch);
                        std::vector<Job> kids;
                        for (const TileKey &c : ch)
                        {
                            if (store_.ensureQueued(c) == TileState::Missing)
                            {
                                kids.push_back(Job{c, job.rel, job.epoch, job.cancel,
                                                   tileRect(c, tilePx_), job.detailLevel, false});
                            }
                        }
                        pushJobs(kids);
                    }
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
    const int detail = vp.activeLevel();

    // Fallback for a node with no ready leaf: draw the node's OWN footprint sampling
    // the nearest ready ancestor's content (UV sub-rect) -> exactly one quad per
    // region, no overlap, no holes. A proven-uniform ancestor is exact (final).
    auto emitFallback = [&](const TileKey &node) {
        TileKey anc = node;
        for (int up = 1; up <= kMaxAncestorUp; ++up)
        {
            anc = parentKey(anc);
            store_.touch(anc, frame); // keep fallback ancestors resident
            const NodeClass ac = store_.classOf(anc);
            if (ac == NodeClass::UniformTrue)
            {
                PresentTile p{};
                p.key = node;
                p.rect = tileRect(node, tilePx_);
                p.level = node.level;
                p.state = TileState::Done; // a uniform ancestor is exact -> final
                p.flat = true;
                p.flatValue = 1.0f;
                out.push_back(std::move(p));
                return;
            }
            if (ac == NodeClass::UniformFalse) return; // proven empty -> draw nothing (final)
            CoverageTilePtr acov = store_.snapshot(anc);
            if (acov)
            {
                const int64_t span = int64_t{1} << up;
                const int64_t li = node.i - (anc.i << up);
                const int64_t lj = node.j - (anc.j << up);
                const float inv = 1.0f / static_cast<float>(span);
                PresentTile p{};
                p.key = node;
                p.rect = tileRect(node, tilePx_);
                p.cov = std::move(acov);
                p.level = node.level;
                p.fallback = true; // a coarse stand-in until this node's own tile is ready
                p.state = TileState::Coarse;
                p.u0 = static_cast<float>(li) * inv;
                p.v0 = static_cast<float>(lj) * inv;
                p.u1 = static_cast<float>(li + 1) * inv;
                p.v1 = static_cast<float>(lj + 1) * inv;
                out.push_back(std::move(p));
                return;
            }
        }
        // nothing ready anywhere up the chain: mark a (brief) not-final gap.
        PresentTile p{};
        p.key = node;
        p.rect = tileRect(node, tilePx_);
        p.level = node.level;
        p.fallback = true;
        out.push_back(std::move(p));
    };

    // `down` bounds how many levels we may descend into FINER cached children when
    // this node has no ready leaf of its own -> reuses the pre-zoom-out detail tiles
    // until the larger greedy tile is ready (immersion: no flash on zoom-out).
    std::function<void(const TileKey &, int)> emit = [&](const TileKey &node, int down) {
        store_.touch(node, frame);
        const NodeClass nc = store_.classOf(node);
        if (nc == NodeClass::UniformTrue)
        {
            PresentTile p{};
            p.key = node;
            p.rect = tileRect(node, tilePx_);
            p.level = node.level;
            p.state = TileState::Done;
            p.flat = true;
            p.flatValue = 1.0f;
            out.push_back(std::move(p));
            return;
        }
        if (nc == NodeClass::UniformFalse) return; // background

        if (nc == NodeClass::Mixed && node.level > detail)
        {
            TileKey ch[4];
            childKeys(node, ch);
            for (const TileKey &c : ch) emit(c, down);
            return;
        }
        if (nc == NodeClass::Mixed && node.level <= detail)
        {
            CoverageTilePtr cov = store_.snapshot(node);
            if (cov && store_.state(node) == TileState::Done)
            {
                PresentTile p{};
                p.key = node;
                p.rect = tileRect(node, tilePx_);
                p.cov = std::move(cov);
                p.level = node.level;
                p.state = TileState::Done;
                out.push_back(std::move(p));
                return;
            }
        }

        // Not a ready leaf (Unknown, or detail-Mixed still solving). Prefer reusing
        // finer cached children (zoom-out / refinement); else fall back to a coarser
        // ancestor (zoom-in / pan). Either way every region is covered exactly once.
        if (down > 0)
        {
            TileKey ch[4];
            childKeys(node, ch);
            bool any = false;
            for (const TileKey &c : ch)
            {
                const NodeClass cc = store_.classOf(c);
                if (cc == NodeClass::UniformTrue || cc == NodeClass::UniformFalse
                    || store_.snapshot(c))
                {
                    any = true;
                    break;
                }
            }
            if (any)
            {
                for (const TileKey &c : ch) emit(c, down - 1);
                return;
            }
        }
        emitFallback(node);
    };

    constexpr int kReuseDown = 4; // levels of finer-tile reuse on zoom-out
    for (const TileKey &s : startNodes(vp, epoch)) emit(s, kReuseDown);
    return out.size();
}

void Engine::debugTiles(const Viewport &vp, std::vector<DebugTile> &out)
{
    out.clear();
    const uint64_t epoch = epoch_.load();
    const int detail = vp.activeLevel();

    // Mirror the compositor traversal, emitting one box per leaf so the overlay
    // shows the ACTUAL variable-size greedy tiling (big boxes for uniform regions,
    // fine boxes along boundaries).
    std::function<void(const TileKey &)> visit = [&](const TileKey &node) {
        const NodeClass nc = store_.classOf(node);
        if (nc == NodeClass::UniformTrue || nc == NodeClass::UniformFalse)
        {
            out.push_back(DebugTile{tileRect(node, tilePx_), TileState::Done}); // greedy leaf
            return;
        }
        if (nc == NodeClass::Mixed && node.level > detail)
        {
            TileKey ch[4];
            childKeys(node, ch);
            for (const TileKey &c : ch) visit(c);
            return;
        }
        out.push_back(DebugTile{tileRect(node, tilePx_), store_.state(node)}); // leaf (detail/unknown)
    };
    for (const TileKey &s : startNodes(vp, epoch)) visit(s);
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
