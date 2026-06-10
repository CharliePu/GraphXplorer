#include "Engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace gxr
{
namespace
{
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

// Two compositing boundaries (immersion vs O(visible)):
//  - kCullMargin: the pan-ahead ring. Tiles here stay resident (touched, kept by
//    the LRU) and get a first-paint pass, so a small pan reveals content
//    instantly instead of a hole.
//  - kDrawMargin: the draw set. Only tiles here are emitted, given stand-ins,
//    re-requested, and REFINED past first paint -- so per-frame main-thread work
//    and the workers' refinement effort stay proportional to what is on screen.
constexpr double kCullMargin = 0.5;
constexpr double kDrawMargin = 0.1;

// Does a tile rect fall within the viewport expanded by a margin ring?
bool keepForViewport(const WorldRect &r, const Viewport &vp, double marginFrac)
{
    const WorldRect b = vp.worldBounds();
    const double mx = (b.x1 - b.x0) * marginFrac;
    const double my = (b.y1 - b.y0) * marginFrac;
    return !(r.x1 < b.x0 - mx || r.x0 > b.x1 + mx || r.y1 < b.y0 - my || r.y0 > b.y1 + my);
}

// THE draw-model predicate (objective 2, generation), generation-free: would the LATEST
// viewport ever draw output that depends on this (level, refinePass) tile?
// Abandon the work iff not. A job's right to RUN is "does the latest viewport
// draw it", independent of when it was scheduled (generation only orders
// priority in JobCmp -- "will it be drawn" and "how soon" are different
// questions).
//  - a COVERING node (coarser than detail) is the no-holes fallback chain.
//    Its coarse BASE (pass 0) is kept UNCONDITIONALLY -- never abandoned on a
//    position test, because the cascade builds the coarse tree past the margin
//    and an abandoned base could leave a descendant with no fallback ancestor.
//    Its REFINEMENT is invisible work (detail tiles draw over it): never wanted.
//  - off-screen (outside the cull ring) detail-or-finer: nothing below it is
//    visible either (a descendant's rect is contained in its ancestor's).
//  - finer than the current detail level: a zoom-out replaced it; the detail
//    tile at the coarser level covers this region.
//  - the pan-ahead ring between the draw and cull margins gets first paint
//    only, so workers focus refinement on what the user actually sees.
bool wantsTile(const TileKey &key, const WorldRect &rect, int refinePass, const Viewport &vp)
{
    const int detail = vp.activeLevel();
    if (key.level > detail) return refinePass == 0;
    if (!keepForViewport(rect, vp, kCullMargin)) return false;
    if (key.level < detail) return false;
    return keepForViewport(rect, vp, kDrawMargin) ? true : refinePass == 0;
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
    inflight_.reserve(static_cast<size_t>(numWorkers));
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

// Arm the abort flag of every in-flight job whose output the latest viewport
// will not draw. Runs on the SCHEDULER thread (off the main-thread path). The
// solver polls the flag inside its subdivision AND sampling loops, so even a
// pathological tile frees its worker in well under a millisecond -- the new
// viewport's jobs never wait behind stale expensive work (objective 2).
void Engine::abandonStaleInflight()
{
    const std::shared_ptr<const Viewport> vp = currentVp_.load(std::memory_order_acquire);
    if (!vp) return;
    std::scoped_lock lock(inflightMutex_);
    for (const Inflight &f : inflight_)
    {
        if (!wantsTile(f.key, f.rect, f.refinePass, *vp) &&
            !f.abort->exchange(true, std::memory_order_acq_rel))
        {
            abortsArmed_.fetch_add(1, std::memory_order_relaxed);
        }
    }
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
            const WorldRect r = tileRect(k, tilePx_);
            Job j{k, rel, epoch, cancel, r, detail, true};
            j.viewportGen = gen;
            j.onScreen = keepForViewport(r, vp, 0.0);
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
        if (dirty)
        {
            // Free workers stuck on stale tiles and re-true the queue order
            // BEFORE scheduling the new viewport, so the current region's
            // top-priority jobs run immediately instead of waiting for stale
            // work to drain or sorting below stale-flagged entries (obj 2).
            abandonStaleInflight();
            requeueForViewport(vp);
            enqueueVisible(vp, rel, epoch, cancel);
        }
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
    const std::shared_ptr<const Viewport> vpNow = currentVp_.load(std::memory_order_acquire);
    std::vector<Job> jobs;
    for (const TileKey &k : keys)
    {
        if (k.epoch != epoch) continue; // drop stale-epoch requests
        // A request generated for a now-superseded viewport (the user moved
        // between buildPresent and here) would be stamped current-gen and grind
        // off-screen; drop it -- the new viewport's buildPresent re-requests
        // exactly what it needs, and the fallback chain covers the gap meanwhile.
        if (vpNow && !keepForViewport(tileRect(k, tilePx_), *vpNow, kCullMargin)) continue;
        if (!store_.claimForResolve(k)) continue; // already Done / in flight
        // Solve at the COMPOSITOR's detail level (the level it was requesting), not
        // the lagging mailViewport's. A detail tile (k.level <= detail) -> atDetail
        // true -> raster ladder + Done (fixes the persistent black band / blur). An
        // Unknown INTERMEDIATE node (k.level > detail) -> atDetail false -> it gets
        // classified and CASCADES its children down toward the detail level, so a
        // deep zoom continues past nodes a shallower prior view already classified.
        Job j{k, rel, epoch, cancel, tileRect(k, tilePx_), detail, false};
        j.viewportGen = gen;
        // Resume a broken/partial refine chain where it left off (-1 -> pass 0).
        j.refinePass = std::clamp(store_.bestPass(k) + 1, 0, kMaxRefinePass);
        j.onScreen = !vpNow || keepForViewport(j.rect, *vpNow, 0.0);
        jobs.push_back(std::move(j));
    }
    pushJobs(jobs);
}

// Rebuild the job queue against the latest viewport. Enqueue-time priority
// flags (onScreen) go stale the moment the user moves; a buried stale-flag job
// cannot be re-claimed (its slot is Queued, so claimForResolve fails) and
// starves behind fresher work -- e.g. a panned-in strip's first paints stuck
// under the overlap region's refines. Recomputing the flags, and dropping jobs
// the draw model says will never be drawn (freeing their slots for re-claim),
// keeps the queue order true to the CURRENT viewport. Scheduler thread;
// O(N log N) in queued jobs, far from the main-thread path. Workers briefly
// block on the queue mutex, which is fine -- they are grinding solves.
void Engine::requeueForViewport(const Viewport &vp)
{
    const uint64_t epochNow = epoch_.load();
    std::vector<Job> keep;
    {
        std::scoped_lock lock(jobMutex_);
        keep.reserve(jobs_.size());
        while (!jobs_.empty())
        {
            Job j = jobs_.top();
            jobs_.pop();
            if (j.epoch != epochNow) continue; // dead epoch: drop (slot left for eviction)
            if (!wantsTile(j.key, j.rect, j.refinePass, vp))
            {
                store_.abandonIfUnfinished(j.key); // re-claimable if it returns to view
                continue;
            }
            j.onScreen = keepForViewport(j.rect, vp, 0.0);
            keep.push_back(std::move(j));
        }
        for (auto &j : keep) jobs_.push(std::move(j));
    }
    jobCv_.notify_all();
}

void Engine::registerInflight(const Job &job, const std::shared_ptr<std::atomic<bool>> &abort)
{
    std::scoped_lock lock(inflightMutex_);
    inflight_.push_back(Inflight{job.key, job.rect, job.refinePass, abort});
}

void Engine::unregisterInflight(const TileKey &key)
{
    std::scoped_lock lock(inflightMutex_);
    for (size_t i = 0; i < inflight_.size(); ++i)
    {
        if (inflight_[i].key == key)
        {
            inflight_[i] = std::move(inflight_.back());
            inflight_.pop_back();
            return;
        }
    }
}

// After publishing pass p, the publisher (and only it: claim-gated) schedules
// pass p+1 -- unless the latest viewport would not draw that refinement (the
// pan-ahead ring and covering nodes stop at first paint; the compositor's
// stuck-request path resumes the ladder if the tile later enters the draw set).
void Engine::selfEnqueueRefine(const Job &done)
{
    const int next = done.refinePass + 1;
    const std::shared_ptr<const Viewport> vp = currentVp_.load(std::memory_order_acquire);
    if (vp && !wantsTile(done.key, done.rect, next, *vp)) return;
    if (!store_.claimForResolve(done.key)) return; // raced by a resolve request / Done
    Job j{done.key, done.rel, done.epoch, done.cancel, done.rect, done.detailLevel, false};
    j.refinePass = next;
    j.viewportGen = viewportGen_.load(std::memory_order_acquire); // latest tier
    j.onScreen = !vp || keepForViewport(done.rect, *vp, 0.0);
    std::vector<Job> one;
    one.push_back(std::move(j));
    pushJobs(one);
}

void Engine::workerLoop(int)
{
#ifdef _WIN32
    // Workers below normal priority: with hardware_concurrency-1 of them plus
    // the scheduler, every logical core is busy; at equal priority the OS
    // occasionally parks the render thread for a quantum (observed as 100-200ms
    // inter-frame gaps). Below-normal workers still consume all idle cores but
    // always yield the next slice to the UI. Throughput cost is a few percent.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
    EvalScratch scratch;
    while (!stop_.load())
    {
        Job job;
        {
            std::unique_lock lock(jobMutex_);
            jobCv_.wait(lock, [this] { return !jobs_.empty() || stop_.load(); });
            if (stop_.load()) break;
            job = jobs_.top(); // priority: newest viewport, visible, first paint, coarse
            jobs_.pop();
            jobsInFlight_.fetch_add(1);
        }

        const std::shared_ptr<const Viewport> vpNow = currentVp_.load(std::memory_order_acquire);

        // Dequeue-time cull: drop work whose output the latest viewport will not
        // draw (panned/zoomed away). The slot returns to Missing so the tile is
        // re-claimable if it ever comes back into view.
        if (vpNow && !wantsTile(job.key, job.rect, job.refinePass, *vpNow))
        {
            store_.abandonIfUnfinished(job.key);
            jobsCompleted_.fetch_add(1);
            if (jobsInFlight_.fetch_sub(1) == 1)
            {
                std::scoped_lock lock(jobMutex_);
                if (jobs_.empty()) jobCv_.notify_all();
            }
            continue;
        }

        // Run with a per-job abort flag registered for viewport-change aborts.
        auto abortFlag = std::make_shared<std::atomic<bool>>(false);
        registerInflight(job, abortFlag);
        const CancelToken ct{job.cancel.get(), abortFlag.get()};
        bool published = false;

        if (!ct.cancelled())
        {
            if (job.refinePass > 0)
            {
                // Refinement pass of a known-Mixed detail tile: solve and publish
                // (monotone: a stale coarser publish can never downgrade a finer one).
                const SolveParams p = refinePassParams(tilePx_, job.refinePass);
                CoverageTile c = solveTile(*job.rel, job.rect, p, scratch, ct);
                if (!ct.cancelled())
                {
                    c.payloadId = g_payloadCounter.fetch_add(1);
                    const bool fin = job.refinePass >= kMaxRefinePass;
                    published = store_.publishRefine(
                        job.key, std::make_shared<CoverageTile>(std::move(c)), job.refinePass, fin);
                    if (published)
                    {
                        if (wake_) wake_();
                        if (!fin) selfEnqueueRefine(job);
                    }
                }
            }
            else
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
                        published = true;
                        if (wake_) wake_();
                    }
                    else // Mixed: needs a raster (ladder at detail level) and, if coarse, children.
                    {
                        const bool wantRaster = atDetail || job.baseRaster;
                        if (wantRaster)
                        {
                            // Equality curves render via the band model, which is
                            // subBits-independent -- every ladder pass would emit the
                            // same bytes. Solve them ONCE at final quality instead.
                            const bool equality = job.rel->isEquality();
                            const int pass = (atDetail && equality) ? kMaxRefinePass : 0;
                            // Pass-0 otherwise: a detail tile's FIRST paint and a
                            // covering node's stand-in raster both need to land in
                            // a few ms, not after a full fine solve. Detail tiles
                            // then sharpen through the ladder (self-enqueued).
                            const SolveParams p = refinePassParams(tilePx_, pass);
                            CoverageTile c = solveTile(*job.rel, job.rect, p, scratch, ct);
                            if (!ct.cancelled())
                            {
                                c.payloadId = g_payloadCounter.fetch_add(1);
                                if (atDetail)
                                {
                                    const bool fin = pass >= kMaxRefinePass;
                                    published = store_.publishRefine(
                                        job.key, std::make_shared<CoverageTile>(std::move(c)),
                                        pass, fin);
                                    if (published)
                                    {
                                        if (wake_) wake_();
                                        if (!fin) selfEnqueueRefine(job);
                                    }
                                }
                                else
                                {
                                    // Base raster == ladder pass 0: publish through the
                                    // same watermark so a later zoom-out that turns this
                                    // node into a detail tile RESUMES at pass 1 instead
                                    // of re-running the pass it already has.
                                    published = store_.publishRefine(
                                        job.key, std::make_shared<CoverageTile>(std::move(c)),
                                        0, /*done=*/false);
                                    if (published && wake_) wake_();
                                }
                            }
                        }
                        else
                        {
                            // intermediate Mixed node: classified, no raster; just descend.
                            store_.publish(job.key, nullptr, false);
                            published = true;
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
                                    kid.onScreen = !vpNow || keepForViewport(cr, *vpNow, 0.0);
                                    kids.push_back(std::move(kid));
                                }
                            }
                            pushJobs(kids);
                        }
                    }
                }
            }
        }

        unregisterInflight(job.key);
        // Aborted before publishing: free the slot so the tile is re-claimable
        // (its previous snapshot, if any, stays drawable -- no downgrade).
        // Epoch-guard: a stale-EPOCH cancel (relation changed) must not touch a
        // slot that is now only display fallback awaiting eviction.
        if (ct.cancelled() && !published && job.epoch == epoch_.load())
            store_.abandonIfUnfinished(job.key);

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

    {
    // ONE shared lock + ONE hash lookup per visited node for the whole walk
    // (released before the stuck hand-off below -- no nested lock orders).
    // Everything in this scope reads slot atomics through Handles; workers'
    // publishes (shared lock + atomic stores) proceed concurrently, untorn.
    TileStore::ReadAccess ra(store_);

    // The nearest ready ancestor payload, THREADED down the descent (resolved
    // once per start node + updated at each raster-carrying intermediate)
    // instead of a per-leaf upward walk. Used both for fallback quads (no own
    // raster yet) and as the baked stand-in a detail tile's presenter draws
    // until its own texture uploads (no hole on a tile swap).
    struct Standin
    {
        CoverageTilePtr cov; // ancestor raster (with ancKey for the UV sub-rect)
        TileKey ancKey{};
        bool flat{false};     // proven-TRUE ancestor: solid fill
        bool falseAnc{false}; // proven-FALSE ancestor: background is correct (final)
    };

    auto resolveStandin = [&](const TileKey &start) -> Standin {
        Standin s;
        TileKey anc = start;
        for (int up = 1; up <= kMaxAncestorUp; ++up)
        {
            anc = parentKey(anc);
            const TileStore::Handle h = ra.find(anc);
            if (!h) continue;
            h.touch(frame); // keep fallback ancestors resident
            const NodeClass ac = h.klass();
            if (ac == NodeClass::UniformTrue)
            {
                s.flat = true;
                return s;
            }
            if (ac == NodeClass::UniformFalse)
            {
                s.falseAnc = true;
                return s;
            }
            if (CoverageTilePtr c = h.snapshot())
            {
                s.cov = std::move(c);
                s.ancKey = anc;
                return s;
            }
        }
        return s;
    };

    // UV sub-rect of `node` within ancestor `anc` (world-aligned power-of-two).
    auto uvFor = [](const TileKey &node, const TileKey &anc, float &u0, float &v0, float &u1,
                    float &v1) {
        const int up = anc.level - node.level;
        const int64_t li = node.i - (anc.i << up);
        const int64_t lj = node.j - (anc.j << up);
        const float inv = 1.0f / static_cast<float>(int64_t{1} << up);
        u0 = static_cast<float>(li) * inv;
        v0 = static_cast<float>(lj) * inv;
        u1 = static_cast<float>(li + 1) * inv;
        v1 = static_cast<float>(lj + 1) * inv;
    };

    // Fallback for a node with no ready leaf: draw the node's OWN footprint
    // sampling the threaded stand-in -> exactly one quad per region, no overlap,
    // no holes. A proven-uniform ancestor is exact (final).
    auto emitFallback = [&](const TileKey &node, const Standin &sa) {
        if (sa.falseAnc) return; // proven empty -> draw nothing (final)
        PresentTile p{};
        p.key = node;
        p.rect = tileRect(node, tilePx_);
        p.level = node.level;
        if (sa.flat)
        {
            p.state = TileState::Done; // a uniform ancestor is exact -> final
            p.flat = true;
            p.flatValue = 1.0f;
        }
        else if (sa.cov)
        {
            p.cov = sa.cov;
            p.fallback = true; // a coarse stand-in until this node's own tile is ready
            p.state = TileState::Coarse;
            uvFor(node, sa.ancKey, p.u0, p.v0, p.u1, p.v1);
            // Mirror the ancestor into the stand-in fields so the presenter's
            // residency-continuity chain can fall back to whatever this ancestor
            // KEY last drew (an older payload of the same region) when this exact
            // payload is not resident and uploads are rationed -- no hole.
            p.standinCov = sa.cov;
            p.standinKey = sa.ancKey;
            p.su0 = p.u0;
            p.sv0 = p.v0;
            p.su1 = p.u1;
            p.sv1 = p.v1;
        }
        else
        {
            p.fallback = true; // nothing ready anywhere up the chain: brief not-final gap
        }
        out.push_back(std::move(p));
    };

    std::function<void(const TileKey &, Standin)> emit = [&](const TileKey &node, Standin sa) {
        // Visit out to the CULL ring (touch keeps the pan-ahead set resident);
        // draw/request only within the DRAW ring (O(visible) main-thread work).
        if (!keepForViewport(tileRect(node, tilePx_), vp, kCullMargin)) return;
        const TileStore::Handle h = ra.find(node);
        h.touch(frame);
        const bool draw = keepForViewport(tileRect(node, tilePx_), vp, kDrawMargin);
        const NodeClass nc = h.klass();
        if (nc == NodeClass::UniformTrue)
        {
            if (!draw) return;
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
            // Descend; this node's OWN raster (a base/stand-in layer) becomes the
            // nearest ready ancestor for the whole subtree. Capturing it here is
            // what keeps a former start/detail node's raster in the fallback
            // chain after a zoom turns it into an intermediate.
            if (CoverageTilePtr own = h.snapshot())
            {
                sa.cov = std::move(own);
                sa.ancKey = node;
                sa.flat = false;
                sa.falseAnc = false;
            }
            TileKey ch[4];
            childKeys(node, ch);
            for (const TileKey &c : ch) emit(c, sa);
            return;
        }
        if (nc == NodeClass::Mixed && node.level <= detail)
        {
            // The node's OWN raster for this exact region. If converged (Done), emit
            // it final. If it is mid-ladder or mid-(re)solve, its published snapshot
            // is STILL the last good raster for THIS region (snapshots are immutable
            // and world-aligned), so emit it and keep requesting the refined tile.
            if (CoverageTilePtr cov = h.snapshot())
            {
                if (!draw) return; // resident + touched; not drawn from the ring
                const TileState state = h.state();
                const bool done = state == TileState::Done;
                PresentTile p{};
                p.key = node;
                p.rect = tileRect(node, tilePx_);
                p.cov = std::move(cov);
                p.level = node.level;
                p.state = done ? TileState::Done : TileState::Coarse;
                p.fallback = !done; // keep refining while not final
                // Attach the threaded stand-in so the presenter never shows a hole
                // while this tile's own texture is still uploading.
                if (sa.flat) p.standinFlat = true;
                else if (sa.cov)
                {
                    p.standinCov = sa.cov;
                    p.standinKey = sa.ancKey;
                    uvFor(node, sa.ancKey, p.su0, p.sv0, p.su1, p.sv1);
                }
                out.push_back(std::move(p));
                if (!done && state != TileState::Queued) stuck.push_back(node);
                return;
            }
        }

        // Not a ready leaf (Unknown, or detail-Mixed still solving). Request a
        // (re)solve for: a detail tile (needs its own raster) or an UNKNOWN
        // intermediate (blocks the descent -- requesting it makes the worker
        // classify + CASCADE its children toward the detail level). Ring tiles
        // are not requested (first paint arrives via the cascade; the request
        // path re-arms if they enter the draw set). Cover the region with the
        // threaded stand-in meanwhile -- exactly one quad per region.
        if (draw && h.state() != TileState::Queued
            && (node.level <= detail || nc == NodeClass::Unknown))
            stuck.push_back(node);

        // Nothing above covers this region and this node is not classified yet
        // (a zoom-out's freshly-born coarser root, mid classify round-trip).
        // The only published content, if any, lies BELOW: follow the EXISTING
        // subtree -- a missing slot has no descendants, so the recursion is
        // bounded by what was actually built. The previously-painted area keeps
        // drawing (no black flash on a scale transition); only genuinely-new
        // territory shows background until its first paint (the carve-out).
        if (node.level > detail && !sa.flat && !sa.cov && !sa.falseAnc)
        {
            TileKey ch[4];
            childKeys(node, ch);
            bool anyExisting = false;
            for (const TileKey &c : ch) anyExisting = anyExisting || bool(ra.find(c));
            if (anyExisting)
            {
                for (const TileKey &c : ch)
                {
                    if (ra.find(c)) emit(c, sa);
                    else if (keepForViewport(tileRect(c, tilePx_), vp, kDrawMargin))
                        emitFallback(c, sa); // truly-cold sibling: one gap quad
                }
                return;
            }
        }
        if (draw) emitFallback(node, sa);
    };

    for (const TileKey &s : startNodes(vp, epoch)) emit(s, resolveStandin(s));
    }

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
    TileStore::ReadAccess ra(store_);

    // Mirror the compositor traversal, emitting one box per leaf so the overlay
    // shows the ACTUAL variable-size greedy tiling (big boxes for uniform regions,
    // fine boxes along boundaries).
    std::function<void(const TileKey &)> visit = [&](const TileKey &node) {
        const TileStore::Handle h = ra.find(node);
        const NodeClass nc = h.klass();
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
        out.push_back(DebugTile{tileRect(node, tilePx_), h.state()}); // leaf (detail/unknown)
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
