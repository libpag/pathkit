/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include "include/core/SkPathBuilder.h"

namespace pk {
static_assert(0 == static_cast<int>(SkPathFillType::kWinding), "fill_type_mismatch");
static_assert(1 == static_cast<int>(SkPathFillType::kEvenOdd), "fill_type_mismatch");
static_assert(2 == static_cast<int>(SkPathFillType::kInverseWinding), "fill_type_mismatch");
static_assert(3 == static_cast<int>(SkPathFillType::kInverseEvenOdd), "fill_type_mismatch");

class SkPathPriv {
public:
#ifdef PK_BUILD_FOR_ANDROID_FRAMEWORK
    static const int kPathRefGenIDBitCnt = 30; // leave room for the fill type (skbug.com/1762)
#else
    static const int kPathRefGenIDBitCnt = 32;
#endif
    /**
     *  Return the opposite of the specified direction. kUnknown is its own
     *  opposite.
     */
    static SkPathFirstDirection OppositeFirstDirection(SkPathFirstDirection dir) {
        static const SkPathFirstDirection gOppositeDir[] = {
            SkPathFirstDirection::kCCW, SkPathFirstDirection::kCW, SkPathFirstDirection::kUnknown,
        };
        return gOppositeDir[(unsigned)dir];
    }

    /**
     *  Tries to compute the direction of the outer-most non-degenerate
     *  contour. If it can be computed, return that direction. If it cannot be determined,
     *  or the contour is known to be convex, return kUnknown. If the direction was determined,
     *  it is cached to make subsequent calls return quickly.
     */
    static SkPathFirstDirection ComputeFirstDirection(const SkPath&);

    // In some scenarios (e.g. fill or convexity checking all but the last leading move to are
    // irrelevant to behavior). SkPath::injectMoveToIfNeeded should ensure that this is always at
    // least 1.
    static int LeadingMoveToCount(const SkPath& path) {
        int verbCount = path.countVerbs();
        auto verbs = path.fPathRef->verbsBegin();
        for (int i = 0; i < verbCount; i++) {
            if (verbs[i] != SkPath::Verb::kMove_Verb) {
                return i;
            }
        }
        return verbCount; // path is all move verbs
    }

    /**
      * Iterates through a raw range of path verbs, points, and conics. All values are returned
      * unaltered.
      *
      * NOTE: This class's definition will be moved into SkPathPriv once RangeIter is removed.
    */
    using RangeIter = SkPath::RangeIter;

    /**
     * Iterable object for traversing verbs, points, and conic weights in a path:
     *
     *   for (auto [verb, pts, weights] : SkPathPriv::Iterate(skPath)) {
     *       ...
     *   }
     */
    struct Iterate {
    public:
        Iterate(const SkPath& path)
                : Iterate(path.fPathRef->verbsBegin(),
                          // Don't allow iteration through non-finite points.
                          (!path.isFinite()) ? path.fPathRef->verbsBegin()
                                             : path.fPathRef->verbsEnd(),
                          path.fPathRef->points(), path.fPathRef->conicWeights()) {
        }
        Iterate(const uint8_t* verbsBegin, const uint8_t* verbsEnd, const SkPoint* points,
                const SkScalar* weights)
                : fVerbsBegin(verbsBegin), fVerbsEnd(verbsEnd), fPoints(points), fWeights(weights) {
        }
        SkPath::RangeIter begin() { return {fVerbsBegin, fPoints, fWeights}; }
        SkPath::RangeIter end() { return {fVerbsEnd, nullptr, nullptr}; }
    private:
        const uint8_t* fVerbsBegin;
        const uint8_t* fVerbsEnd;
        const SkPoint* fPoints;
        const SkScalar* fWeights;
    };

    /** Returns true if constructed by addCircle(), addOval(); and in some cases,
     addRoundRect(), addRRect(). SkPath constructed with conicTo() or rConicTo() will not
     return true though SkPath draws oval.

     rect receives bounds of oval.
     dir receives SkPathDirection of oval: kCW_Direction if clockwise, kCCW_Direction if
     counterclockwise.
     start receives start of oval: 0 for top, 1 for right, 2 for bottom, 3 for left.

     rect, dir, and start are unmodified if oval is not found.

     Triggers performance optimizations on some GPU surface implementations.

     @param rect   storage for bounding SkRect of oval; may be nullptr
     @param dir    storage for SkPathDirection; may be nullptr
     @param start  storage for start of oval; may be nullptr
     @return       true if SkPath was constructed by method that reduces to oval
     */
    static bool IsOval(const SkPath& path, SkRect* rect, SkPathDirection* dir, unsigned* start) {
        bool isCCW = false;
        bool result = path.fPathRef->isOval(rect, &isCCW, start);
        if (dir && result) {
            *dir = isCCW ? SkPathDirection::kCCW : SkPathDirection::kCW;
        }
        return result;
    }

    /** Returns true if constructed by addRoundRect(), addRRect(); and if construction
     is not empty, not SkRect, and not oval. SkPath constructed with other calls
     will not return true though SkPath draws SkRRect.

     rrect receives bounds of SkRRect.
     dir receives SkPathDirection of oval: kCW_Direction if clockwise, kCCW_Direction if
     counterclockwise.
     start receives start of SkRRect: 0 for top, 1 for right, 2 for bottom, 3 for left.

     rrect, dir, and start are unmodified if SkRRect is not found.

     Triggers performance optimizations on some GPU surface implementations.

     @param rrect  storage for bounding SkRect of SkRRect; may be nullptr
     @param dir    storage for SkPathDirection; may be nullptr
     @param start  storage for start of SkRRect; may be nullptr
     @return       true if SkPath contains only SkRRect
     */
    static bool IsRRect(const SkPath& path, SkRRect* rrect, SkPathDirection* dir,
                        unsigned* start) {
        bool isCCW = false;
        bool result = path.fPathRef->isRRect(rrect, &isCCW, start);
        if (dir && result) {
            *dir = isCCW ? SkPathDirection::kCCW : SkPathDirection::kCW;
        }
        return result;
    }

    // Returns number of valid points for each verb, not including the "starter"
    // point that the Iterator adds for line/quad/conic/cubic
    static int PtsInVerb(unsigned verb) {
        static const uint8_t gPtsInVerb[] = {
            1,  // kMove    pts[0]
            1,  // kLine    pts[0..1]
            2,  // kQuad    pts[0..2]
            2,  // kConic   pts[0..2]
            3,  // kCubic   pts[0..3]
            0,  // kClose
            0   // kDone
        };

        return gPtsInVerb[verb];
    }

    static bool IsAxisAligned(const SkPath& path);

    static bool AllPointsEq(const SkPoint pts[], int count) {
        for (int i = 1; i < count; ++i) {
            if (pts[0] != pts[i]) {
                return false;
            }
        }
        return true;
    }

    static bool IsRectContour(const SkPath&, bool allowPartial, int* currVerb,
                              const SkPoint** ptsPtr, bool* isClosed, SkPathDirection* direction,
                              SkRect* rect);
    static void SetConvexity(const SkPath& path, SkPathConvexity c) {
        path.setConvexity(c);
    }
    static void SetConvexity(SkPathBuilder* builder, SkPathConvexity c) {
        builder->privateSetConvexity(c);
    }
    static void ReverseAddPath(SkPathBuilder* builder, const SkPath& reverseMe) {
        builder->privateReverseAddPath(reverseMe);
    }
};

// Lightweight variant of SkPath::Iter that only returns segments (e.g. lines/conics).
// Does not return kMove or kClose.
// Always "auto-closes" each contour.
// Roughly the same as SkPath::Iter(path, true), but does not return moves or closes
//
class SkPathEdgeIter {
    const uint8_t*  fVerbs;
    const uint8_t*  fVerbsStop;
    const SkPoint*  fPts;
    const SkPoint*  fMoveToPtr;
    const SkScalar* fConicWeights;
    SkPoint         fScratch[2];    // for auto-close lines
    bool            fNeedsCloseLine;
    bool            fNextIsNewContour;

    enum {
        kIllegalEdgeValue = 99
    };

public:
    SkPathEdgeIter(const SkPath& path);

    SkScalar conicWeight() const {
        return *fConicWeights;
    }

    enum class Edge {
        kLine  = SkPath::kLine_Verb,
        kQuad  = SkPath::kQuad_Verb,
        kConic = SkPath::kConic_Verb,
        kCubic = SkPath::kCubic_Verb,
    };

    static SkPath::Verb EdgeToVerb(Edge e) {
        return SkPath::Verb(e);
    }

    struct Result {
        const SkPoint*  fPts;   // points for the segment, or null if done
        Edge            fEdge;
        bool            fIsNewContour;

        // Returns true when it holds an Edge, false when the path is done.
        operator bool() { return fPts != nullptr; }
    };

    Result next() {
        auto closeline = [&]() {
            fScratch[0] = fPts[-1];
            fScratch[1] = *fMoveToPtr;
            fNeedsCloseLine = false;
            fNextIsNewContour = true;
            return Result{ fScratch, Edge::kLine, false };
        };

        for (;;) {
            if (fVerbs == fVerbsStop) {
                return fNeedsCloseLine
                    ? closeline()
                    : Result{ nullptr, Edge(kIllegalEdgeValue), false };
            }

            const auto v = *fVerbs++;
            switch (v) {
                case SkPath::kMove_Verb: {
                    if (fNeedsCloseLine) {
                        auto res = closeline();
                        fMoveToPtr = fPts++;
                        return res;
                    }
                    fMoveToPtr = fPts++;
                    fNextIsNewContour = true;
                } break;
                case SkPath::kClose_Verb:
                    if (fNeedsCloseLine) return closeline();
                    break;
                default: {
                    // Actual edge.
                    const int pts_count = (v+2) / 2,
                              cws_count = (v & (v-1)) / 2;
                    fNeedsCloseLine = true;
                    fPts           += pts_count;
                    fConicWeights  += cws_count;

                    bool isNewContour = fNextIsNewContour;
                    fNextIsNewContour = false;
                    return { &fPts[-(pts_count + 1)], Edge(v), isNewContour };
                }
            }
        }
    }
};
}  // namespace pk
