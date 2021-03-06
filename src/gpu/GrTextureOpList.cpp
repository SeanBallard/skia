/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "GrTextureOpList.h"

#include "GrAuditTrail.h"
#include "GrGpu.h"
#include "GrResourceAllocator.h"
#include "GrTextureProxy.h"
#include "SkStringUtils.h"
#include "ops/GrCopySurfaceOp.h"

////////////////////////////////////////////////////////////////////////////////

GrTextureOpList::GrTextureOpList(GrResourceProvider* resourceProvider,
                                 GrTextureProxy* proxy,
                                 GrAuditTrail* auditTrail)
    : INHERITED(resourceProvider, proxy, auditTrail) {
}

GrTextureOpList::~GrTextureOpList() {
}

////////////////////////////////////////////////////////////////////////////////

#ifdef SK_DEBUG
void GrTextureOpList::dump() const {
    INHERITED::dump();

    SkDebugf("ops (%d):\n", fRecordedOps.count());
    for (int i = 0; i < fRecordedOps.count(); ++i) {
        SkDebugf("*******************************\n");
        SkDebugf("%d: %s\n", i, fRecordedOps[i]->name());
        SkString str = fRecordedOps[i]->dumpInfo();
        SkDebugf("%s\n", str.c_str());
        const SkRect& clippedBounds = fRecordedOps[i]->bounds();
        SkDebugf("ClippedBounds: [L: %.2f, T: %.2f, R: %.2f, B: %.2f]\n",
                    clippedBounds.fLeft, clippedBounds.fTop, clippedBounds.fRight,
                    clippedBounds.fBottom);
    }
}

#endif

void GrTextureOpList::onPrepare(GrOpFlushState* flushState) {
    SkASSERT(this->isClosed());

    // Loop over the ops that haven't yet generated their geometry
    for (int i = 0; i < fRecordedOps.count(); ++i) {
        if (fRecordedOps[i]) {
            // We do not call flushState->setDrawOpArgs as this op list does not support GrDrawOps.
            fRecordedOps[i]->prepare(flushState);
        }
    }
}

bool GrTextureOpList::onExecute(GrOpFlushState* flushState) {
    if (0 == fRecordedOps.count()) {
        return false;
    }

    std::unique_ptr<GrGpuTextureCommandBuffer> commandBuffer(
                         flushState->gpu()->createCommandBuffer(fTarget.get()->priv().peekTexture(),
                                                                fTarget.get()->origin()));
    flushState->setCommandBuffer(commandBuffer.get());

    for (int i = 0; i < fRecordedOps.count(); ++i) {
        // We do not call flushState->setDrawOpArgs as this op list does not support GrDrawOps.
        fRecordedOps[i]->execute(flushState);
    }

    commandBuffer->submit();
    flushState->setCommandBuffer(nullptr);

    return true;
}

void GrTextureOpList::reset() {
    fRecordedOps.reset();
    INHERITED::reset();
}

////////////////////////////////////////////////////////////////////////////////

// This closely parallels GrRenderTargetOpList::copySurface but renderTargetOpList
// stores extra data with the op
bool GrTextureOpList::copySurface(const GrCaps& caps,
                                  GrSurfaceProxy* dst,
                                  GrSurfaceProxy* src,
                                  const SkIRect& srcRect,
                                  const SkIPoint& dstPoint) {
    SkASSERT(dst == fTarget.get());

    std::unique_ptr<GrOp> op = GrCopySurfaceOp::Make(dst, src, srcRect, dstPoint);
    if (!op) {
        return false;
    }

    auto addDependency = [ &caps, this ] (GrSurfaceProxy* p) {
        this->addDependency(p, caps);
    };
    op->visitProxies(addDependency);

    this->recordOp(std::move(op));
    return true;
}

void GrTextureOpList::gatherProxyIntervals(GrResourceAllocator* alloc) const {
    unsigned int cur = alloc->numOps();

    // Add the interval for all the writes to this opList's target
    alloc->addInterval(fTarget.get(), cur, cur+fRecordedOps.count()-1);

    auto gather = [ alloc ] (GrSurfaceProxy* p) {
        alloc->addInterval(p);
    };
    for (int i = 0; i < fRecordedOps.count(); ++i) {
        SkASSERT(alloc->curOp() == cur+i);

        const GrOp* op = fRecordedOps[i].get(); // only diff from the GrRenderTargetOpList version
        op->visitProxies(gather);

        alloc->incOps();
    }
}

void GrTextureOpList::recordOp(std::unique_ptr<GrOp> op) {
    SkASSERT(fTarget.get());
    // A closed GrOpList should never receive new/more ops
    SkASSERT(!this->isClosed());

    GR_AUDIT_TRAIL_ADD_OP(fAuditTrail, op.get(), fTarget.get()->uniqueID());
    GrOP_INFO("Re-Recording (%s, opID: %u)\n"
        "\tBounds LRTB (%f, %f, %f, %f)\n",
        op->name(),
        op->uniqueID(),
        op->bounds().fLeft, op->bounds().fRight,
        op->bounds().fTop, op->bounds().fBottom);
    GrOP_INFO(SkTabString(op->dumpInfo(), 1).c_str());
    GR_AUDIT_TRAIL_OP_RESULT_NEW(fAuditTrail, op.get());

    fRecordedOps.emplace_back(std::move(op));
}
