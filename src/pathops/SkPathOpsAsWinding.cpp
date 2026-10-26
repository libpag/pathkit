/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "include/core/SkRect.h"
#include "src/core/SkPathPriv.h"
#include "src/pathops/SkOpEdgeBuilder.h"
#include "src/pathops/SkPathOpsCommon.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace pk {
using std::vector;

struct Contour {
    enum class Direction {  // SkPathDirection doesn't have 'none' state
        kCCW = -1,
        kNone,
        kCW,
    };

    Contour(const SkRect& bounds, int lastStart, int verbStart)
        : fBounds(bounds)
        , fVerbStart(lastStart)
        , fVerbEnd(verbStart) {
    }

    vector<Contour*> fChildren;
    const SkRect fBounds;
    SkPoint fMinXY{PK_ScalarMax, PK_ScalarMax};
    const int fVerbStart;
    const int fVerbEnd;
    Direction fDirection{Direction::kNone};
    bool fContained{false};
    bool fReverse{false};
};

static const int kPtCount[] = { 1, 1, 2, 2, 3, 0 };
static const int kPtIndex[] = { 0, 1, 1, 1, 1, 0 };

static Contour::Direction to_direction(SkScalar dy) {
    return dy > 0 ? Contour::Direction::kCCW : dy < 0 ? Contour::Direction::kCW :
            Contour::Direction::kNone;
}

static int contains_edge(SkPoint pts[4], SkPath::Verb verb, SkScalar weight, const SkPoint& edge) {
    SkRect bounds;
    bounds.setBounds(pts, kPtCount[verb] + 1);
    if (bounds.fTop > edge.fY) {
        return 0;
    }
    if (bounds.fBottom <= edge.fY) {  // check to see if y is at line end to avoid double counting
        return 0;
    }
    if (bounds.fLeft >= edge.fX) {
        return 0;
    }
    int winding = 0;
    double tVals[3];
    Contour::Direction directions[3];
    // must intersect horz ray with curve in case it intersects more than once
    int count = (*CurveIntercept[verb * 2])(pts, weight, edge.fY, tVals);
    PkASSERT(between(0, count, 3));
    // remove results to the right of edge
    for (int index = 0; index < count; ) {
        SkScalar intersectX = (*CurvePointAtT[verb])(pts, weight, tVals[index]).fX;
        if (intersectX < edge.fX) {
            ++index;
            continue;
        }
        if (intersectX > edge.fX) {
            tVals[index] = tVals[--count];
            continue;
        }
        // if intersect x equals edge x, we need to determine if pts is to the left or right of edge
        if (pts[0].fX < edge.fX && pts[kPtCount[verb]].fX < edge.fX) {
            ++index;
            continue;
        }
        // TODO : other cases need discriminating. need op angle code to figure it out
        // example: edge ends 45 degree diagonal going up. If pts is to the left of edge, keep.
        // if pts is to the right of edge, discard. With code as is, can't distiguish the two cases.
        tVals[index] = tVals[--count];
    }
    // use first derivative to determine if intersection is contributing +1 or -1 to winding
    for (int index = 0; index < count; ++index) {
        directions[index] = to_direction((*CurveSlopeAtT[verb])(pts, weight, tVals[index]).fY);
    }
    // Every intersection counts. The bounds tests above already form a half-open interval on the
    // ray's y, so of two edges sharing an endpoint only the one starting at that y reports it and
    // the other is rejected outright. Discarding endpoint intersections again here would drop the
    // only intersection a ray tangent to the curve has -- an oval's left and right extremes are
    // exactly that -- and would report the oval as not containing anything.
    for (int index = 0; index < count; ++index) {
        winding += (int) directions[index];
    }
    return winding;  // note winding indicates containership, not contour direction
}

static SkPoint left_edge(SkPoint pts[4], SkPath::Verb verb, SkScalar weight,
        Contour::Direction* direction) {
    PkASSERT(SkPath::kLine_Verb <= verb && verb <= SkPath::kCubic_Verb);
    SkPoint result;
    double dy;
    double t PK_INIT_TO_AVOID_WARNING;
    int roots = 0;
    // Between two endpoints at the same x the one the contour leaves from is the start, so ties go
    // to pts[0]. Taking the end instead reads the direction the boundary arrives from, which is
    // the opposite of the one it continues in: a triangle's slanted side reaches the left-most x
    // at its end, and reading it there says the contour runs the other way round.
    if (SkPath::kLine_Verb == verb) {
        result = pts[0].fX <= pts[1].fX ? pts[0] : pts[1];
        dy = pts[1].fY - pts[0].fY;
    } else if (SkPath::kQuad_Verb == verb) {
        SkDQuad quad;
        quad.set(pts);
        if (!quad.monotonicInX()) {
            roots = SkDQuad::FindExtrema(&quad[0].fX, &t);
        }
        if (roots) {
            result = quad.ptAtT(t).asSkPoint();
        } else {
            result = pts[0].fX <= pts[2].fX ? pts[0] : pts[2];
            t = pts[0].fX <= pts[2].fX ? 0 : 1;
        }
        dy = quad.dxdyAtT(t).fY;
    } else if (SkPath::kConic_Verb == verb) {
        SkDConic conic;
        conic.set(pts, weight);
        if (!conic.monotonicInX()) {
            roots = SkDConic::FindExtrema(&conic[0].fX, weight, &t);
        }
        if (roots) {
            result = conic.ptAtT(t).asSkPoint();
        } else {
            result = pts[0].fX <= pts[2].fX ? pts[0] : pts[2];
            t = pts[0].fX <= pts[2].fX ? 0 : 1;
        }
        dy = conic.dxdyAtT(t).fY;
    } else {
        PkASSERT(SkPath::kCubic_Verb == verb);
        SkDCubic cubic;
        cubic.set(pts);
        if (!cubic.monotonicInX()) {
            double tValues[2];
            roots = SkDCubic::FindExtrema(&cubic[0].fX, tValues);
            PkASSERT(roots <= 2);
            for (int index = 0; index < roots; ++index) {
                SkPoint temp = cubic.ptAtT(tValues[index]).asSkPoint();
                if (0 == index || result.fX > temp.fX) {
                    result = temp;
                    t = tValues[index];
                }
            }
        }
        if (roots) {
            result = cubic.ptAtT(t).asSkPoint();
        } else {
            result = pts[0].fX <= pts[3].fX ? pts[0] : pts[3];
            t = pts[0].fX <= pts[3].fX ? 0 : 1;
        }
        dy = cubic.dxdyAtT(t).fY;
    }
    *direction = to_direction(dy);
    return result;
}

// Signed area of one segment under Green's theorem, integrated exactly instead of by flattening.
// The integrand of a line, quadratic or cubic is a polynomial, so a three-point Gauss-Legendre
// rule -- exact for polynomials up to degree five -- gives the true value. That matters for thin
// outlines: their area is a small difference between large terms, which sampling gets wrong.
//
// The coordinates are moved into the contour's own frame first. The sum below is translation
// invariant, but its terms are not, and large coordinates leave a thin contour's sign buried in
// cancellation. The shift has to happen in double: subtracting in the float fields first rounds
// the difference away, so two distinct vertices can collapse into one.
static double ExactSegmentArea(SkPoint pts[4], SkPath::Verb verb, SkPoint origin) {
    static constexpr double kNodes[3] = {0.5 - 0.5 * 0.7745966692414834, 0.5,
                                        0.5 + 0.5 * 0.7745966692414834};
    static constexpr double kWeights[3] = {5.0 / 18.0, 8.0 / 18.0, 5.0 / 18.0};
    double originX = origin.fX;
    double originY = origin.fY;
    double area = 0.0;
    for (int index = 0; index < 3; ++index) {
        double t = kNodes[index];
        double u = 1.0 - t;
        double x, y, dx, dy;
        if (SkPath::kLine_Verb == verb) {
            double x0 = pts[0].fX - originX, y0 = pts[0].fY - originY;
            double x1 = pts[1].fX - originX, y1 = pts[1].fY - originY;
            x = x0 + (x1 - x0) * t;
            y = y0 + (y1 - y0) * t;
            dx = x1 - x0;
            dy = y1 - y0;
        } else if (SkPath::kQuad_Verb == verb) {
            double x0 = pts[0].fX - originX, y0 = pts[0].fY - originY;
            double x1 = pts[1].fX - originX, y1 = pts[1].fY - originY;
            double x2 = pts[2].fX - originX, y2 = pts[2].fY - originY;
            x = u * u * x0 + 2.0 * u * t * x1 + t * t * x2;
            y = u * u * y0 + 2.0 * u * t * y1 + t * t * y2;
            dx = 2.0 * u * (x1 - x0) + 2.0 * t * (x2 - x1);
            dy = 2.0 * u * (y1 - y0) + 2.0 * t * (y2 - y1);
        } else {
            double x0 = pts[0].fX - originX, y0 = pts[0].fY - originY;
            double x1 = pts[1].fX - originX, y1 = pts[1].fY - originY;
            double x2 = pts[2].fX - originX, y2 = pts[2].fY - originY;
            double x3 = pts[3].fX - originX, y3 = pts[3].fY - originY;
            x = u * u * u * x0 + 3.0 * u * u * t * x1 + 3.0 * u * t * t * x2 + t * t * t * x3;
            y = u * u * u * y0 + 3.0 * u * u * t * y1 + 3.0 * u * t * t * y2 + t * t * t * y3;
            dx = 3.0 * u * u * (x1 - x0) + 6.0 * u * t * (x2 - x1) + 3.0 * t * t * (x3 - x2);
            dy = 3.0 * u * u * (y1 - y0) + 6.0 * u * t * (y2 - y1) + 3.0 * t * t * (y3 - y2);
        }
        area += kWeights[index] * 0.5 * (x * dy - y * dx);
    }
    return area;
}

// One piece of a conic, written in the frame that puts the contour's first point at the origin.
struct ConicPiece {
    double x0 = 0.0;
    double y0 = 0.0;
    double x1 = 0.0;
    double y1 = 0.0;
    double x2 = 0.0;
    double y2 = 0.0;
    double weight = 1.0;
};

// Splits a conic at t == 0.5 with rational de Casteljau. Both halves trace the same points as the
// original, so their signed areas add back up to it. The weights are rescaled to 1 at both ends,
// which reparametrises each half but leaves its trace -- and therefore its area -- untouched.
static void ChopConic(const ConicPiece& piece, ConicPiece* left, ConicPiece* right) {
    double divisor = 1.0 + piece.weight;
    double ax = (piece.x0 + piece.weight * piece.x1) / divisor;
    double ay = (piece.y0 + piece.weight * piece.y1) / divisor;
    double bx = (piece.weight * piece.x1 + piece.x2) / divisor;
    double by = (piece.weight * piece.y1 + piece.y2) / divisor;
    double weight = std::sqrt(0.5 * (1.0 + piece.weight));
    *left = {piece.x0, piece.y0, ax, ay, 0.5 * (ax + bx), 0.5 * (ay + by), weight};
    *right = {0.5 * (ax + bx), 0.5 * (ay + by), bx, by, piece.x2, piece.y2, weight};
}

// Signed area of one conic, as the sum of the chords of its `level` subdivisions plus a bound on
// how far the true arc can stray from those chords. A conic with a positive weight stays inside its
// control triangle, so the region between a chord and its arc is contained in that triangle and can
// never be larger than it. Each extra level quarters the bound.
//
// That bound is what makes the sign decidable: when the chord sum is larger than it, no error
// within the bound can reach past zero. Sampling more finely instead would only shrink the same
// one-sided error without ever measuring it.
static void ConicAreaWithBound(const ConicPiece& piece, int level, double* area, double* bound) {
    double triangle = std::fabs(0.5 * (piece.x0 * (piece.y1 - piece.y2) +
                                       piece.x1 * (piece.y2 - piece.y0) +
                                       piece.x2 * (piece.y0 - piece.y1)));
    if (level <= 0) {
        *area += 0.5 * (piece.x0 * piece.y2 - piece.x2 * piece.y0);
        *bound += triangle;
        return;
    }
    ConicPiece left = {};
    ConicPiece right = {};
    ChopConic(piece, &left, &right);
    ConicAreaWithBound(left, level - 1, area, bound);
    ConicAreaWithBound(right, level - 1, area, bound);
}

// Highest subdivision level tried before the direction is given up as undecidable. Every level
// doubles the piece count and quarters the bound, so the last one already costs 4096 pieces per
// conic; a contour still undecided there is degenerate rather than merely thin.
constexpr int MAX_CONIC_LEVEL = 12;

class OpAsWinding {
public:
    enum class Edge {
        kInitial,
        kCompare,
        kArea,
    };

    OpAsWinding(const SkPath& path)
        : fPath(path) {
    }

    // Returns false when the contour's signed area cannot be pinned to a sign. A contour traced out
    // and back along the same curve encloses nothing and has an area of zero, so no direction can
    // be attributed to it: picking one would hand back an outline claiming a winding rule its
    // geometry does not follow. Reporting the failure is the only honest answer, and it is what
    // lets a caller keep the even-odd outline it already had.
    bool contourAreaDecidable(Contour& contour) {
        fConics.clear();
        fExactArea = 0.0;
        this->nextEdge(contour, Edge::kArea);
        if (fConics.empty()) {
            return true;
        }
        // Refine the chord sum until the bound on its error drops below the area itself. Two
        // samplings agreeing on a sign proved nothing: both are chord sums of the same arc and both
        // miss it on the same side, so a thin outline can push them past zero together. A bound
        // does decide it -- no error inside the bound can reach zero -- and when refinement runs
        // out, the direction is reported as undecidable rather than guessed.
        for (int level = 0; level <= MAX_CONIC_LEVEL; ++level) {
            double conicArea = 0.0;
            double conicBound = 0.0;
            for (const auto& piece : fConics) {
                ConicAreaWithBound(piece, level, &conicArea, &conicBound);
            }
            if (std::fabs(fExactArea + conicArea) > conicBound) {
                return true;
            }
        }
        return false;
    }

    void contourBounds(vector<Contour>* containers) {
        SkRect bounds;
        bounds.setEmpty();
        int lastStart = 0;
        int verbStart = 0;
        for (auto iter : SkPathPriv::Iterate(fPath)) {
            auto verb = std::get<0>(iter);
            auto pts = std::get<1>(iter);
            if (SkPathVerb::kMove == verb) {
                if (!bounds.isEmpty()) {
                    containers->emplace_back(bounds, lastStart, verbStart);
                    lastStart = verbStart;
               }
               bounds.setBounds(&pts[kPtIndex[SkPath::kMove_Verb]], kPtCount[SkPath::kMove_Verb]);
            }
            if (SkPathVerb::kLine <= verb && verb <= SkPathVerb::kCubic) {
                SkRect verbBounds;
                verbBounds.setBounds(&pts[kPtIndex[(int)verb]], kPtCount[(int)verb]);
                bounds.joinPossiblyEmptyRect(verbBounds);
            }
            ++verbStart;
        }
        if (!bounds.isEmpty()) {
            containers->emplace_back(bounds, lastStart, ++verbStart);
        }
    }

    // Feeds one edge to the query in progress: either accumulating into the winding for a
    // containment test, or updating the contour's left-most edge and its direction.
    int handleEdge(Contour& contour, Edge edge, SkPoint pts[4], SkPath::Verb verb,
                   SkScalar weight) {
        bool horizontal = true;
        for (int index = 1; index <= kPtCount[verb]; ++index) {
            if (pts[0].fY != pts[index].fY) {
                horizontal = false;
                break;
            }
        }
        if (horizontal) {
            return 0;
        }
        if (edge == Edge::kCompare) {
            return contains_edge(pts, verb, weight, contour.fMinXY);
        }
        PkASSERT(edge == Edge::kInitial);
        Contour::Direction direction;
        SkPoint minXY = left_edge(pts, verb, weight, &direction);
        if (minXY.fX > contour.fMinXY.fX) {
            return 0;
        }
        if (minXY.fX < contour.fMinXY.fX) {
            contour.fMinXY = minXY;
            contour.fDirection = Contour::Direction::kNone;
        }
        // A contour continues in the direction the boundary leaves the left-most point, which is
        // the direction at the start of the edge that starts there. left_edge() reports the
        // direction at whichever endpoint is left-most, so an edge that only reaches that x at its
        // end describes where the boundary came from -- the opposite of where it goes next. A
        // triangle's slanted side is such an edge, and reading it there says the contour runs the
        // other way round.
        if (pts[0].fX == contour.fMinXY.fX) {
            contour.fDirection = to_direction((*CurveSlopeAtT[verb])(pts, weight, 0).fY);
        } else if (contour.fDirection == Contour::Direction::kNone) {
            // The left-most point is an extreme inside this curve, so no edge starts there and the
            // tangent at it is the direction the boundary leaves in.
            contour.fDirection = direction;
        }
        return 0;
    }

    int nextEdge(Contour& contour, Edge edge) {
        // The verb range recorded by contourBounds() indexes the raw verbs, so this walks them the
        // same way. SkPath::Iter with forceClose inserts the closing edge as an extra line, which
        // desynchronises the two counts: the first contour still sees its closing edge through the
        // range it was given, but every later one is short by the edges inserted before it and
        // loses its closing edge entirely.
        int verbCount = 0;
        int winding = 0;
        SkPoint startPoint = {};
        SkPoint lastPoint = {};
        bool started = false;
        for (auto iter : SkPathPriv::Iterate(fPath)) {
            auto verb = std::get<0>(iter);
            auto iterPts = std::get<1>(iter);
            int current = verbCount++;
            if (current < contour.fVerbStart || current >= contour.fVerbEnd) {
                continue;
            }
            if (SkPathVerb::kMove == verb) {
                startPoint = iterPts[0];
                lastPoint = iterPts[0];
                started = true;
                continue;
            }
            if (SkPathVerb::kLine > verb || verb > SkPathVerb::kCubic) {
                continue;
            }
            SkPoint pts[4];
            int ptCount = kPtCount[(int) verb] + 1;
            for (int index = 0; index < ptCount; ++index) {
                pts[index] = iterPts[index];
            }
            // The conic weight lives in a parallel array that only advances for conics, so it is
            // only meaningful for this verb.
            SkScalar weight = SkPathVerb::kConic == verb ? *std::get<2>(iter) : 1;
            if (edge == Edge::kArea) {
                // A horizontal edge still contributes here -- unlike a winding or a containment
                // test, an area picks up -y * dx along one -- so it cannot be skipped the way
                // handleEdge() skips it.
                if (SkPathVerb::kConic == verb) {
                    fConics.push_back({pts[0].fX - startPoint.fX, pts[0].fY - startPoint.fY,
                                       pts[1].fX - startPoint.fX, pts[1].fY - startPoint.fY,
                                       pts[2].fX - startPoint.fX, pts[2].fY - startPoint.fY,
                                       (double) weight});
                } else {
                    fExactArea += ExactSegmentArea(pts, (SkPath::Verb) verb, startPoint);
                }
                lastPoint = pts[kPtCount[(int) verb]];
                continue;
            }
            winding += handleEdge(contour, edge, pts, (SkPath::Verb) verb, weight);
            lastPoint = pts[kPtCount[(int) verb]];
        }
        if (edge == Edge::kArea) {
            // The gap back to the start closes the contour but adds no area: measured with the
            // start at the origin, its cross product is zero along the whole of it. Sending it
            // through handleEdge() would also be wrong, since that treats anything that is not a
            // containment test as a direction query and would overwrite the contour's direction.
            return winding;
        }
        // The edge back to the start is not in the verb list, but both rasterizing and contains()
        // treat it as a real boundary. addRect() leaves an entire side of a rectangle to it, so
        // leaving it out picks the wrong left-most edge and reports the wrong direction -- which is
        // what decides whether a counter gets reversed.
        if (started && (lastPoint.fX != startPoint.fX || lastPoint.fY != startPoint.fY)) {
            SkPoint pts[4] = {lastPoint, startPoint, {}, {}};
            winding += handleEdge(contour, edge, pts, SkPath::kLine_Verb, 1);
        }
        return winding;
    }

    bool containerContains(Contour& contour, Contour& test) {
        // find outside point on lesser contour
        // arbitrarily, choose non-horizontal edge where point <= bounds left
        // note that if leftmost point is control point, may need tight bounds
            // to find edge with minimum-x
        if (PK_ScalarMax == test.fMinXY.fX) {
            this->nextEdge(test, Edge::kInitial);
        }
        // find all edges on greater equal or to the left of one on lesser
        contour.fMinXY = test.fMinXY;
        int winding = this->nextEdge(contour, Edge::kCompare);
        // if edge is up, mark contour cw, otherwise, ccw
        // sum of greater edges direction should be cw, 0, ccw
        test.fContained = winding != 0;
        return -1 <= winding && winding <= 1;
    }

    void inParent(Contour& contour, Contour& parent) {
        // move contour into sibling list contained by parent
        for (auto test : parent.fChildren) {
            if (test->fBounds.contains(contour.fBounds)) {
                inParent(contour, *test);
                return;
            }
        }
        // move parent's children into contour's children if contained by contour
        for (auto iter = parent.fChildren.begin(); iter != parent.fChildren.end(); ) {
            if (contour.fBounds.contains((*iter)->fBounds)) {
                contour.fChildren.push_back(*iter);
                iter = parent.fChildren.erase(iter);
                continue;
            }
            ++iter;
        }
        parent.fChildren.push_back(&contour);
    }

    bool checkContainerChildren(Contour* parent, Contour* child) {
        // Initialize this contour before it is used as a container: containment queries reuse
        // the container's fMinXY as scratch storage, not as its initial-edge sentinel.
        if (parent && !containerContains(*parent, *child)) {
            return false;
        }
        for (auto grandChild : child->fChildren) {
            if (!checkContainerChildren(child, grandChild)) {
                return false;
            }
        }
        return true;
    }

    bool markReverse(Contour* parent, Contour* child) {
        bool reversed = false;
        if (parent && parent->fDirection == child->fDirection) {
            child->fReverse = true;
            child->fDirection = (Contour::Direction) -(int) child->fDirection;
            reversed = true;
        }
        // Descendants must be compared against their parent's final direction.
        for (auto grandChild : child->fChildren) {
            reversed |= markReverse(grandChild->fContained ? child : parent, grandChild);
        }
        return reversed;
    }

    SkPath reverseMarkedContours(vector<Contour>& contours, SkPathFillType fillType) {
        SkPathPriv::Iterate iterate(fPath);
        auto iter = iterate.begin();
        int verbCount = 0;

        SkPathBuilder result;
        result.setFillType(fillType);
        for (const Contour& contour : contours) {
            SkPathBuilder reverse;
            SkPathBuilder* temp = contour.fReverse ? &reverse : &result;
            for (; iter != iterate.end() && verbCount < contour.fVerbEnd; ++iter, ++verbCount) {
                auto verb = std::get<0>(*iter);
                auto pts = std::get<1>(*iter);
                auto w = std::get<2>(*iter);
                switch (verb) {
                    case SkPathVerb::kMove:
                        temp->moveTo(pts[0]);
                        break;
                    case SkPathVerb::kLine:
                        temp->lineTo(pts[1]);
                        break;
                    case SkPathVerb::kQuad:
                        temp->quadTo(pts[1], pts[2]);
                        break;
                    case SkPathVerb::kConic:
                        temp->conicTo(pts[1], pts[2], *w);
                        break;
                    case SkPathVerb::kCubic:
                        temp->cubicTo(pts[1], pts[2], pts[3]);
                        break;
                    case SkPathVerb::kClose:
                        temp->close();
                        break;
                }
            }
            if (contour.fReverse) {
                PkASSERT(temp == &reverse);
                SkPathPriv::ReverseAddPath(&result, reverse.detach());
            }
        }
        return result.detach();
    }

private:
    const SkPath& fPath;
    // Scratch for the area pass: the exact part accumulated as it walks, and the conics held back
    // because their area needs refining before its sign can be trusted.
    vector<ConicPiece> fConics = {};
    double fExactArea = 0.0;
};

static bool set_result_path(SkPath* result, const SkPath& path, SkPathFillType fillType) {
    *result = path;
    result->setFillType(fillType);
    return true;
}

bool AsWinding(const SkPath& path, SkPath* result) {
    if (!path.isFinite()) {
        return false;
    }
    SkPathFillType fillType = path.getFillType();
    if (fillType == SkPathFillType::kWinding
            || fillType == SkPathFillType::kInverseWinding ) {
        return set_result_path(result, path, fillType);
    }
    fillType = path.isInverseFillType() ? SkPathFillType::kInverseWinding :
            SkPathFillType::kWinding;
    if (path.isEmpty() || path.isConvex()) {
        return set_result_path(result, path, fillType);
    }
    // count contours
    vector<Contour> contours;   // one per contour
    OpAsWinding winder(path);
    winder.contourBounds(&contours);
    // Checked before the single-contour shortcut below, because that shortcut is exactly the case
    // that cannot be decided: one contour has nothing to be nested in, so it is taken as already
    // consistent and returned untouched. A contour that doubles back on itself is one contour too.
    for (auto& contour : contours) {
        if (!winder.contourAreaDecidable(contour)) {
            return false;
        }
    }
    if (contours.size() <= 1) {
        return set_result_path(result, path, fillType);
    }
    // create contour bounding box tree
    Contour sorted(SkRect(), 0, 0);
    for (auto& contour : contours) {
        winder.inParent(contour, sorted);
    }
    // if sorted has no grandchildren, no child has to fix its children's winding
    if (std::all_of(sorted.fChildren.begin(), sorted.fChildren.end(),
            [](const Contour* contour) -> bool { return !contour->fChildren.size(); } )) {
        return set_result_path(result, path, fillType);
    }
    // starting with outermost and moving inward, see if one path contains another
    for (auto contour : sorted.fChildren) {
        winder.nextEdge(*contour, OpAsWinding::Edge::kInitial);
        if (!winder.checkContainerChildren(nullptr, contour)) {
            return false;
        }
    }
    // starting with outermost and moving inward, mark paths to reverse
    bool reversed = false;
    for (auto contour : sorted.fChildren) {
        reversed |= winder.markReverse(nullptr, contour);
    }
    if (!reversed) {
        return set_result_path(result, path, fillType);
    }
    *result = winder.reverseMarkedContours(contours, fillType);
    return true;
}
}  // namespace pk