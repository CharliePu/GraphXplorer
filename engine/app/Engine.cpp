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

constexpr double kCullMargin = 0.5; // keep a half-viewport ring around the screen

// Does a tile rect fall within the viewport expanded by a margin ring?
bool keepForViewport(const WorldRect &r, const Viewport &vp, double marginFrac)
{
    const WorldRect b = vp.worldBounds();
    const double mx = (b.x1 - b.x0) * marginFrac;
    const double my = (b.y1 - b.y0) * marginFrac;
    return !(r.x1 < b.x0 - mx || r.x0 > b.x1 + mx || r.y1 < b.y0 - my || r.y0 > b.y1 + my);
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
    // publish the latest viewport (for worker culling) and bump the generation
    // (for job priority) before waking the scheduler.
    currentVp_.store(std::make_shared<const Viewport>(vp), std::memory_order_release);
    viewportGen_.fetch_add(1, std::memory_order_release);
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
        for (auto &j : jobs)
        {
            j.seq = jobSeq_.fetch_add(1, std::memory_order_relaxed);
            jobs_.push(std::move(j));
        }
    }
    jobCv_.notify_all();
}

void Engine::enqueueVisible(const Viewport &vp, std::shared_ptr<const Relation> rel, uint64_t epoch,
                            const std::shared_ptr<std::atomic<bool>> &cancel)
{
    if (!rel) return;
    const int detail = vp.activeLevel();
    const uint64_t gen = viewportGen_.load(std::memory_order_acquire);
    const std::vector<TileKey> keys = startNodes(vp, epoch);
    std::vector<Job> toEnqueue;
    toEnqueue.reserve(keys.size());
    for (const TileKey &k : keys)
    {
        if (store_.ensureQueued(k) == TileState::Missing)
        {
            // start nodes are above the detail level; if Mixed they render a
            // coarse placeholder raster (the no-holes base layer) and spawn children.
            Job j{k, rel, epoch, cancel, tileRect(k, tilePx_), detail, true};
            j.viewportGen = gen;
            toEnqueue.push_back(std::move(j));
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
        bool dirty;
        std::vector<TileKey> resolve;
        {
            std::unique_lock lock(mailMutex_);
            mailCv_.wait(lock, [this] { return mailDirty_ || resolvePending_ || stop_.load(); });
            if (stop_.load()) break;
            vp = mailViewport_;
            rel = mailRelation_;
            cancel = liveCancel_;
            epoch = epoch_.load();
            dirty = mailDirty_;
            mailDirty_ = false;
            resolve.swap(resolveReq_);
            resolvePending_ = false;
        }
        if (dirty) enqueueVisible(vp, rel, epoch, cancel);
        if (!resolve.empty() && rel) serviceResolve(resolve, vp, rel, epoch, cancel);
    }
}

void Engine::serviceResolve(const std::vector<TileKey> &keys, const Viewport &vp,
                            const std::shared_ptr<const Relation> &rel, uint64_t epoch,
                            const std::shared_ptr<std::atomic<bool>> &cancel)
{
    (void)vp; // use the compositor's detail level (resolveDetail_), not the lagging mailViewport
    const int detail = resolveDetail_.load(std::memory_order_relaxed);
    const uint64_t gen = viewportGen_.load(std::memory_order_acquire);
    std::vector<Job> jobs;
    for (const TileKey &k : keys)
    {
        if (k.epoch != epoch) continue;          // drop stale-epoch requests
        if (!store_.claimForResolve(k)) continue; // already Done / in flight
        // Solve at the COMPOSITOR's detail level (the level it was requesting), not
        // the lagging mailViewport's. A detail tile (k.level <= detail) -> atDetail
        // true -> fine raster + Done (fixes the persistent black band / blur). An
        // Unknown INTERMEDIATE node (k.level > detail) -> atDetail false -> it gets
        // classified and CASCADES its children down toward the detail level, so a
        // deep zoom continues past nodes a shallower prior view already classified.
        Job j{k, rel, epoch, cancel, tileRect(k, tilePx_), detail, false};
        j.viewportGen = gen;
        jobs.push_back(std::move(j));
    }
    pushJobs(jobs);
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
            job = jobs_.top(); // priority: newest viewport, coarsest, FIFO
            jobs_.pop();
            jobsInFlight_.fetch_add(1);
        }

        const std::shared_ptr<const Viewport> vpNow = currentVp_.load(std::memory_order_acquire);
        const CancelToken ct{job.cancel.get()};

        // Viewport cull: discard un-started DETAIL-or-finer work that is now off
        // screen (panned away). Coarse nodes (level > detail) are always processed
        // -- they cover the viewport and feed the no-holes fallback chain, so
        // dropping them would punch holes. Already-published tiles are untouched.
        bool culled = false;
        if (vpNow && job.key.level <= vpNow->activeLevel()
            && !keepForViewport(job.rect, *vpNow, kCullMargin))
        {
            culled = true;
            store_.resetToMissing(job.key); // re-claimable if it returns to view
        }

        if (!culled && !ct.cancelled())
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
                        const int curDetail = vpNow ? vpNow->activeLevel() : job.detailLevel;
                        TileKey ch[4];
                        childKeys(job.key, ch);
                        std::vector<Job> kids;
                        for (const TileKey &c : ch)
                        {
                            // expand the cascade only into coarse children (needed for
                            // the tree/fallback) or on-screen detail children; off-screen
                            // detail children are never enqueued (cull at the source).
                            const WorldRect cr = tileRect(c, tilePx_);
                            const bool keep =
                                !vpNow || c.level > curDetail || keepForViewport(cr, *vpNow, kCullMargin);
                            if (keep && store_.ensureQueued(c) == TileState::Missing)
                            {
                                Job kid{c, job.rel, job.epoch, job.cancel, cr, job.detailLevel, false};
                                kid.viewportGen = job.viewportGen; // inherit -> old cascades stay low priority
                                kids.push_back(std::move(kid));
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
    std::vector<TileKey> stuck; // detail tiles that need a (re)solve (see below)

    // Bake a coarse-ancestor STAND-IN into a detail tile, so the presenter can draw
    // it (resident, zero new upload) for the few frames the tile's OWN texture takes
    // to upload -> no hole on a tile swap (immersion). Same world-aligned ancestor
    // walk as emitFallback. UniformFalse ancestor -> no stand-in (background is right).
    auto fillStandin = [&](const TileKey &node, PresentTile &p) {
        TileKey anc = node;
        for (int up = 1; up <= kMaxAncestorUp; ++up)
        {
            anc = parentKey(anc);
            const NodeClass ac = store_.classOf(anc);
            if (ac == NodeClass::UniformTrue)
            {
                p.standinFlat = true; // proven-true ancestor fills solid
                store_.touch(anc, frame);
                return;
            }
            if (ac == NodeClass::UniformFalse) return; // proven empty -> background
            CoverageTilePtr acov = store_.snapshot(anc);
            if (acov)
            {
                const int64_t span = int64_t{1} << up;
                const int64_t li = node.i - (anc.i << up);
                const int64_t lj = node.j - (anc.j << up);
                const float inv = 1.0f / static_cast<float>(span);
                p.standinCov = std::move(acov);
                p.su0 = static_cast<float>(li) * inv;
                p.sv0 = static_cast<float>(lj) * inv;
                p.su1 = static_cast<float>(li + 1) * inv;
                p.sv1 = static_cast<float>(lj + 1) * inv;
                store_.touch(anc, frame);
                return;
            }
        }
    };

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

    // Is there ready content to reuse at `n` or within `depth` finer levels? (A
    // proven-uniform node or any published raster.) Lets the compositor bridge
    // MULTIPLE zoom-out levels down to the cached finer tiles instead of giving up
    // after one level and flashing a hole over a region it actually still has.
    std::function<bool(const TileKey &, int)> hasReusable = [&](const TileKey &n, int depth) -> bool {
        const NodeClass c = store_.classOf(n);
        if (c == NodeClass::UniformTrue || c == NodeClass::UniformFalse) return true;
        if (store_.snapshot(n)) return true;
        if (depth <= 0) return false;
        TileKey ch[4];
        childKeys(n, ch);
        for (const TileKey &k : ch)
            if (hasReusable(k, depth - 1)) return true;
        return false;
    };

    // `down` bounds how many levels we may descend into FINER cached children when
    // this node has no ready leaf of its own -> reuses the pre-zoom-out detail tiles
    // until the larger greedy tile is ready (immersion: no flash on zoom-out).
    std::function<void(const TileKey &, int)> emit = [&](const TileKey &node, int down) {
        // Skip nodes outside the viewport (+ the same margin the worker culls by).
        // The start-node region extends past the screen; without this the traversal
        // would emit fallback for, and request re-solves of, off-screen tiles that
        // the worker then culls -> an infinite request/cull churn and a frame that
        // never settles. Keeping the two cull boundaries identical fixes both.
        if (!keepForViewport(tileRect(node, tilePx_), vp, kCullMargin)) return;
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
            // The node's OWN raster for this exact region. If converged (Done), emit
            // it final. If it is mid-(re)solve -- Coarse/Queued/Missing after a pan
            // away-and-back or a coarser-zoom reuse -- its published snapshot is STILL
            // the last good raster for THIS region (snapshots are immutable and
            // world-aligned), so emit it as a stand-in and still request the refined
            // tile. Dropping a non-null snapshot merely because state != Done was the
            // few-frames-of-emptiness on tile swap (immersion: show the old correct
            // content, never a hole).
            CoverageTilePtr cov = store_.snapshot(node);
            if (cov)
            {
                const bool done = store_.state(node) == TileState::Done;
                PresentTile p{};
                p.key = node;
                p.rect = tileRect(node, tilePx_);
                p.cov = std::move(cov);
                p.level = node.level;
                p.state = done ? TileState::Done : TileState::Coarse;
                p.fallback = !done; // keep refining while it is the stale raster
                // Attach a resident coarse-ancestor stand-in so the presenter never
                // shows a hole while this tile's own texture is still uploading.
                fillStandin(node, p);
                out.push_back(std::move(p));
                if (!done && store_.state(node) != TileState::Queued) stuck.push_back(node);
                return;
            }
        }

        // Not a ready leaf (Unknown, or detail-Mixed still solving). If this is a
        // detail-level tile, it needs its own raster -- request a (re)solve. This is
        // what un-sticks an intermediate node reused at a coarser zoom (Coarse, no
        // raster) or a culled node back in view (Missing): neither is re-enqueued by
        // discovery or the cascade, so the compositor asks for it directly.
        // Request a (re)solve only for tiles not already queued, so a multi-frame
        // settle doesn't re-append hundreds of in-flight keys every frame. Request:
        //  - a detail tile (level <= detail): it needs its own raster; AND
        //  - an UNKNOWN intermediate node (level > detail): it blocks the descent to
        //    the detail level. This happens when a shallower prior view classified the
        //    parent but never cascaded below its own detail, so the deeper children
        //    were never created -> the descent dead-ends here. Requesting it makes the
        //    worker classify + CASCADE its children down toward the detail level.
        if (store_.state(node) != TileState::Queued
            && (node.level <= detail || nc == NodeClass::Unknown))
            stuck.push_back(node);

        // Prefer reusing finer cached children (zoom-out / refinement); else fall
        // back to a coarser ancestor (zoom-in / pan). Every region is covered once.
        if (down > 0)
        {
            TileKey ch[4];
            childKeys(node, ch);
            bool any = false;
            for (const TileKey &c : ch)
                if (hasReusable(c, down - 1)) // look deeper, not just direct children
                {
                    any = true;
                    break;
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

    // Hand any stuck detail tiles to the scheduler, which re-solves them with a
    // consistent relation/epoch/cancel. The main thread does no solving itself.
    if (!stuck.empty())
    {
        resolveDetail_.store(detail, std::memory_order_relaxed); // level to solve them at
        {
            std::scoped_lock lock(mailMutex_);
            resolveReq_.insert(resolveReq_.end(), stuck.begin(), stuck.end());
            resolvePending_ = true;
        }
        mailCv_.notify_all();
    }
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
            idle = !mailDirty_ && !resolvePending_ && jobs_.empty() && jobsInFlight_.load() == 0;
        }
        if (idle) return;
        std::unique_lock lock(jobMutex_);
        jobCv_.wait_for(lock, std::chrono::milliseconds(2));
    }
}
}
