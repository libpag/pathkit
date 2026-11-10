/*
 * Regression tests for pk::AsWinding().
 *
 * AsWinding() rewrites an even-odd outline into a winding one without changing what it fills. The
 * inputs below are the ones it has been reported to get wrong: independent islands, more than one
 * level of nesting, curve-bounded counters and unclosed inner contours. The thin outlines are here
 * because the conversion must not rebuild the geometry -- only reverse contours -- so a counter a
 * fraction of a unit wide has to survive even though pathops quantises coordinates that finely.
 *
 * The conversion also normalizes orientation: every outermost contour ends up clockwise and every
 * counter counter-clockwise. Those cases are checked separately from the probe cases, because
 * normalizing contours that cross each other can change the filled area, which is outside what
 * this function promises to preserve.
 *
 * Every case is also run through SkOpBuilder::resolve(), which is the other pathkit entry for the
 * same conversion (Simplify followed by FixWinding). That second run is reported as information
 * only: it is what decides whether AsWinding() could simply be reimplemented on top of it, so its
 * failures are recorded rather than counted as regressions of AsWinding().
 *
 * Run with PATHKIT_BUILD_TESTS=ON; see CMakeLists.txt.
 */

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "include/core/SkPath.h"
#include "include/core/SkRect.h"
#include "include/pathops/SkPathOps.h"

namespace {

int gFailures = 0;

void Report(bool passed, const char* name, const char* detail) {
  if (passed) {
    std::printf("ok   %s\n", name);
    return;
  }
  ++gFailures;
  std::printf("FAIL %s: %s\n", name, detail);
}

// Reported, but not counted: see the file comment.
void ReportInfo(bool passed, const char* name, const char* detail) {
  std::printf("%s %s%s%s\n", passed ? "ok  " : "fail", name, passed ? "" : ": ",
              passed ? "" : detail);
}

// The conversion has to preserve the filled area, so every probe is compared against what the
// source filled at the same point. The source is left under its own fill type: forcing it to
// even-odd here would invert the inverse cases and compare the wrong area.
bool FilledTheSame(const pk::SkPath& source, const pk::SkPath& winding, float x, float y) {
  return source.contains(x, y) == winding.contains(x, y);
}

bool KeepsProbes(const pk::SkPath& source, const pk::SkPath& winding,
                 const std::vector<float>& probes) {
  for (size_t index = 0; index + 1 < probes.size(); index += 2) {
    if (!FilledTheSame(source, winding, probes[index], probes[index + 1])) {
      return false;
    }
  }
  return true;
}

struct Case {
  const char* name;
  pk::SkPath source;
  std::vector<float> probes;
};

pk::SkPath MakeRing(float outerLeft, float outerTop, float outerRight, float outerBottom,
                    float innerLeft, float innerTop, float innerRight, float innerBottom) {
  pk::SkPath path;
  path.addRect(pk::SkRect::MakeLTRB(outerLeft, outerTop, outerRight, outerBottom),
               pk::SkPathDirection::kCW);
  path.addRect(pk::SkRect::MakeLTRB(innerLeft, innerTop, innerRight, innerBottom),
               pk::SkPathDirection::kCW);
  path.setFillType(pk::SkPathFillType::kEvenOdd);
  return path;
}

// 品: three rings side by side. Each keeps its own counter, so no direction chosen for the whole
// path can be what opens them.
pk::SkPath MakeIslands() {
  pk::SkPath path;
  path.setFillType(pk::SkPathFillType::kEvenOdd);
  for (int index = 0; index < 3; ++index) {
    auto left = static_cast<float>(index) * 400.0f;
    path.addRect(pk::SkRect::MakeLTRB(left, 0, left + 380, 380), pk::SkPathDirection::kCW);
    path.addRect(pk::SkRect::MakeLTRB(left + 80, 80, left + 300, 300), pk::SkPathDirection::kCW);
  }
  return path;
}

// 回: three levels of nesting. The counter between the outer and the middle square has to stay
// open while the island inside it stays filled.
pk::SkPath MakeNested() {
  pk::SkPath path;
  path.setFillType(pk::SkPathFillType::kEvenOdd);
  path.addRect(pk::SkRect::MakeLTRB(0, 0, 1000, 1000), pk::SkPathDirection::kCW);
  path.addRect(pk::SkRect::MakeLTRB(150, 150, 850, 850), pk::SkPathDirection::kCW);
  path.addRect(pk::SkRect::MakeLTRB(300, 300, 700, 700), pk::SkPathDirection::kCW);
  return path;
}

// addOval() emits conics, so this exercises the conic branch of the conversion.
pk::SkPath MakeConic() {
  pk::SkPath path;
  path.setFillType(pk::SkPathFillType::kEvenOdd);
  path.addOval(pk::SkRect::MakeLTRB(0, 0, 1000, 1000), pk::SkPathDirection::kCW);
  path.addOval(pk::SkRect::MakeLTRB(250, 250, 750, 750), pk::SkPathDirection::kCW);
  return path;
}

// An inner contour that was never closed. Rasterizing treats the gap as an implicit edge, so the
// contour is still a counter.
pk::SkPath MakeUnclosedInner() {
  pk::SkPath path;
  path.setFillType(pk::SkPathFillType::kEvenOdd);
  path.addRect(pk::SkRect::MakeLTRB(0, 0, 1000, 1000), pk::SkPathDirection::kCW);
  path.moveTo(200, 200);
  path.lineTo(800, 200);
  path.lineTo(800, 800);
  path.lineTo(200, 800);
  return path;
}

// An outer triangle holding an inner contour that never calls close(). The gap back to the start is
// an edge and contributes to the area; dropping it makes the counter look like it already runs
// against the outline around it, so it is left alone and its interior turns solid.
pk::SkPath MakeUnclosedTriangleCounter() {
  pk::SkPath path;
  path.setFillType(pk::SkPathFillType::kEvenOdd);
  path.moveTo(0, 0);
  path.lineTo(10, 0);
  path.lineTo(0, 10);
  path.close();
  path.moveTo(2, 6);
  path.lineTo(2, 2);
  path.lineTo(6, 2);
  return path;
}

// The band between f(x) = 2x - x^2/512 and (4095/4096) * f(x): about a tenth of a unit thick at its
// widest while spanning 512 units. The two sides are split differently -- one quadratic for the
// upper edge, two for the lower -- because with matching splits the sampling errors scale together
// and the sign never flips.
pk::SkPath MakeThinQuadBand() {
  pk::SkPath path;
  path.setFillType(pk::SkPathFillType::kEvenOdd);
  path.addRect(pk::SkRect::MakeLTRB(0, 0, 1100, 1100), pk::SkPathDirection::kCW);
  path.moveTo(0, 0);
  path.quadTo(512, 1024, 1024, 0);
  path.quadTo(768, 511.875f, 512, 511.875f);
  path.quadTo(256, 511.875f, 0, 0);
  path.close();
  return path;
}

// A counter about one unit thick whose two sides are not the same kind of curve: a conic along the
// top, a quad along the bottom. The weight is deliberately not 1, because a weight of exactly 1 is
// rewritten as a quad and would leave the conic branch untested.
pk::SkPath MakeThinMixedConic() {
  pk::SkPath path;
  path.setFillType(pk::SkPathFillType::kEvenOdd);
  path.addRect(pk::SkRect::MakeLTRB(0, 0, 1000, 1000), pk::SkPathDirection::kCW);
  path.moveTo(400, 400);
  path.conicTo(500, 401, 600, 400, 2.0f);
  path.quadTo(500, 398.5f, 400, 400);
  path.close();
  return path;
}

// addOval() emits conics, so this is the same three levels as 回 but bounded by curves: every
// level is decided by a chord sum plus an error bound instead of by a cross product.
pk::SkPath MakeCurvedNested() {
  pk::SkPath path;
  path.setFillType(pk::SkPathFillType::kEvenOdd);
  path.addOval(pk::SkRect::MakeLTRB(0, 0, 1000, 1000), pk::SkPathDirection::kCW);
  path.addOval(pk::SkRect::MakeLTRB(150, 150, 850, 850), pk::SkPathDirection::kCW);
  path.addOval(pk::SkRect::MakeLTRB(300, 300, 700, 700), pk::SkPathDirection::kCW);
  return path;
}

// The left-most point of the inner contour is a cusp: both edges meeting there run to the right,
// so the edge leaving it heads into the shape. Reading the tangent at that point reports where the
// boundary came from rather than the way the contour runs, which made the counter look like it
// already ran against the outline around it and left it filled in.
pk::SkPath MakeLeftmostCusp() {
  pk::SkPath path;
  path.setFillType(pk::SkPathFillType::kEvenOdd);
  path.addRect(pk::SkRect::MakeLTRB(0, 0, 1000, 1000), pk::SkPathDirection::kCW);
  path.moveTo(200, 300);
  path.quadTo(400, 320, 600, 400);
  path.lineTo(600, 700);
  path.lineTo(350, 700);
  path.close();
  return path;
}

// The same ring, but with the inner contour already running against the outer one, so the outline
// is winding-correct before the conversion and nothing has to be turned around.
pk::SkPath MakeOppositeRing() {
  pk::SkPath path;
  path.setFillType(pk::SkPathFillType::kEvenOdd);
  path.addRect(pk::SkRect::MakeLTRB(0, 0, 1000, 1000), pk::SkPathDirection::kCW);
  path.addRect(pk::SkRect::MakeLTRB(200, 200, 800, 800), pk::SkPathDirection::kCCW);
  return path;
}

// A pentagram after Simplify(): a decagon outline with a pentagon at its centre, the two touching
// at the decagon's five concave vertices. The pentagon is the hole in the middle of the star. Its
// left-most point is one of those shared vertices, so casting a containment ray from it puts the
// ray's end on the decagon's boundary: the hole used to read as an independent outline, kept its
// clockwise direction and was filled in.
pk::SkPath MakeTouchingContours() {
  constexpr float kCenter = 500.0f;
  constexpr float kOuter = 400.0f;
  constexpr float kInner = 200.0f;
  constexpr double kPi = 3.14159265358979323846;
  pk::SkPath path;
  path.setFillType(pk::SkPathFillType::kEvenOdd);
  std::vector<pk::SkPoint> decagon;
  std::vector<pk::SkPoint> pentagon;
  for (int index = 0; index < 10; ++index) {
    const double angle = -kPi / 2 + index * kPi / 5;
    const float radius = index % 2 == 0 ? kOuter : kInner;
    auto point = pk::SkPoint::Make(kCenter + radius * static_cast<float>(std::cos(angle)),
                                   kCenter + radius * static_cast<float>(std::sin(angle)));
    decagon.push_back(point);
    if (index % 2 != 0) {
      pentagon.push_back(point);
    }
  }
  path.moveTo(decagon.front());
  for (size_t index = 1; index < decagon.size(); ++index) {
    path.lineTo(decagon[index]);
  }
  path.close();
  path.moveTo(pentagon.front());
  for (size_t index = 1; index < pentagon.size(); ++index) {
    path.lineTo(pentagon[index]);
  }
  path.close();
  return path;
}

std::vector<Case> MakeCases() {
  std::vector<Case> cases;
  cases.push_back({"SingleRing", MakeRing(0, 0, 1000, 1000, 200, 200, 800, 800),
                   {500, 500, 100, 500, 900, 900}});
  cases.push_back({"OppositeRing", MakeOppositeRing(), {500, 500, 100, 500}});
  cases.push_back({"Islands", MakeIslands(),
                   {190, 190, 590, 190, 990, 190, 20, 190, 420, 190, 820, 190}});
  cases.push_back({"NestedThreeLevels", MakeNested(), {75, 500, 220, 500, 500, 500}});
  cases.push_back({"Conic", MakeConic(), {500, 500, 80, 500}});
  cases.push_back({"UnclosedInner", MakeUnclosedInner(), {500, 500, 100, 500}});
  cases.push_back({"UnclosedTriangleCounter", MakeUnclosedTriangleCounter(), {3, 3, 1, 1}});
  {
    const float origin = 1e7f;
    // Coordinates far from the origin make the cross products feeding any area far larger than the
    // area itself, so a single-precision sum cancels out and the direction becomes noise.
    cases.push_back({"FarFromOrigin",
                     MakeRing(origin - 1000, origin - 1000, origin + 1000, origin + 1000,
                              origin - 500, origin - 500, origin + 500, origin + 500),
                     {origin, origin, origin, origin - 990}});
  }
  cases.push_back({"ThinQuadBand", MakeThinQuadBand(), {512, 511.9375f, 512, 300}});
  cases.push_back({"ThinMixedConic", MakeThinMixedConic(), {500, 400, 500, 100}});
  cases.push_back({"LeftmostCusp", MakeLeftmostCusp(), {450, 600, 100, 100, 900, 900}});
  cases.push_back({"CurvedNestedThreeLevels", MakeCurvedNested(), {60, 500, 220, 500, 500, 500}});
  cases.push_back({"TouchingContours", MakeTouchingContours(),
                   {500, 500, 500, 150, 850, 500, 150, 500, 0, 0}});
  return cases;
}

void RunAsWinding(const Case& testCase) {
  pk::SkPath winding;
  if (!pk::AsWinding(testCase.source, &winding)) {
    Report(false, testCase.name, "AsWinding() returned false");
    return;
  }
  Report(KeepsProbes(testCase.source, winding, testCase.probes), testCase.name,
         "the filled area changed");
}

// The sign of each contour's signed area, in path order: +1 for clockwise, -1 for the other way.
// The outlines checked below are polygons, so a curve's endpoints are enough to read its
// direction.
std::vector<int> ContourDirections(const pk::SkPath& path) {
  std::vector<int> directions;
  pk::SkPath::Iter iter(path, false);
  pk::SkPoint pts[4];
  double area = 0.0;
  bool started = false;
  pk::SkPath::Verb verb;
  while ((verb = iter.next(pts)) != pk::SkPath::kDone_Verb) {
    if (verb == pk::SkPath::kMove_Verb) {
      if (started) {
        directions.push_back(area > 0 ? 1 : area < 0 ? -1 : 0);
      }
      area = 0.0;
      started = true;
      continue;
    }
    if (verb == pk::SkPath::kClose_Verb) {
      continue;
    }
    const int end = verb == pk::SkPath::kCubic_Verb ? 3
                  : verb == pk::SkPath::kLine_Verb ? 1 : 2;
    area += static_cast<double>(pts[0].fX) * pts[end].fY
          - static_cast<double>(pts[end].fX) * pts[0].fY;
  }
  if (started) {
    directions.push_back(area > 0 ? 1 : area < 0 ? -1 : 0);
  }
  return directions;
}

// resolve() hands back an even-odd result, so probing it without switching the fill type would
// report "kept" for every counter: even-odd keeps counters by parity whether or not the conversion
// did anything. The fill type is switched here so the probe says something about the conversion.
void RunOpBuilder(const Case& testCase) {
  auto name = std::string("[opbuilder] ") + testCase.name;
  pk::SkOpBuilder builder;
  builder.add(testCase.source, pk::SkPathOp::kUnion_SkPathOp);
  pk::SkPath resolved;
  if (!builder.resolve(&resolved)) {
    ReportInfo(false, name.c_str(), "resolve() returned false");
    return;
  }
  resolved.setFillType(pk::SkPathFillType::kWinding);
  ReportInfo(KeepsProbes(testCase.source, resolved, testCase.probes), name.c_str(),
             "the filled area changed");
}

void TestInverseFillType() {
  auto source = MakeRing(0, 0, 1000, 1000, 200, 200, 800, 800);
  source.setFillType(pk::SkPathFillType::kInverseEvenOdd);
  pk::SkPath winding;
  if (!pk::AsWinding(source, &winding)) {
    Report(false, "InverseFillType", "AsWinding() returned false");
    return;
  }
  Report(winding.getFillType() == pk::SkPathFillType::kInverseWinding, "InverseFillType",
         "inverse even-odd was not mapped to inverse winding");
  Report(KeepsProbes(source, winding, {500, 500, 2000, 2000}), "InverseFillType",
         "the inverse area changed what it fills");
}

void TestWindingIsLeftAlone() {
  auto source = MakeRing(0, 0, 1000, 1000, 200, 200, 800, 800);
  source.setFillType(pk::SkPathFillType::kWinding);
  pk::SkPath winding;
  if (!pk::AsWinding(source, &winding)) {
    Report(false, "WindingIsLeftAlone", "AsWinding() returned false");
    return;
  }
  Report(winding == source, "WindingIsLeftAlone", "a winding path was rewritten");
}

// Every outermost contour has to come back clockwise, even when it is the only contour, when
// nothing is nested inside any of them, or when it arrived running the other way. A counter runs
// the other way than the outermost contour it ended up inside.
void TestNormalizedDirections() {
  auto check = [](const pk::SkPath& source, const std::vector<int>& expected, const char* name) {
    pk::SkPath winding;
    if (!pk::AsWinding(source, &winding)) {
      Report(false, name, "AsWinding() returned false");
      return;
    }
    auto directions = ContourDirections(winding);
    if (winding.getFillType() != pk::SkPathFillType::kWinding || directions != expected) {
      std::string detail = "contour directions were:";
      for (int direction : directions) {
        detail += " " + std::to_string(direction);
      }
      Report(false, name, detail.c_str());
      return;
    }
    Report(true, name, "");
  };
  {
    pk::SkPath source;
    source.setFillType(pk::SkPathFillType::kEvenOdd);
    source.addRect(pk::SkRect::MakeLTRB(0, 0, 1000, 1000), pk::SkPathDirection::kCCW);
    check(source, {1}, "NormalizedSingleContour");
  }
  {
    pk::SkPath source;
    source.setFillType(pk::SkPathFillType::kEvenOdd);
    source.addRect(pk::SkRect::MakeLTRB(0, 0, 400, 400), pk::SkPathDirection::kCW);
    source.addRect(pk::SkRect::MakeLTRB(600, 0, 1000, 400), pk::SkPathDirection::kCCW);
    check(source, {1, 1}, "NormalizedIsolatedOutermost");
  }
  {
    pk::SkPath source;
    source.setFillType(pk::SkPathFillType::kEvenOdd);
    source.addRect(pk::SkRect::MakeLTRB(0, 0, 1000, 1000), pk::SkPathDirection::kCW);
    source.addRect(pk::SkRect::MakeLTRB(200, 200, 800, 800), pk::SkPathDirection::kCCW);
    source.addRect(pk::SkRect::MakeLTRB(1200, 0, 2200, 1000), pk::SkPathDirection::kCCW);
    source.addRect(pk::SkRect::MakeLTRB(1400, 200, 2000, 800), pk::SkPathDirection::kCW);
    check(source, {1, -1, 1, -1}, "NormalizedCountersFollowParent");
  }
  // The decagon and pentagon touch at five vertices; the pentagon is a hole, not an island.
  check(MakeTouchingContours(), {1, -1}, "NormalizedTouchingContours");
}

// The same conic traced out and back, so the two arcs cancel. No amount of refinement can put the
// error bound below an area of zero, so the direction has to be reported as undecidable rather than
// guessed, and the result has to be left alone -- a silent failure would hand back an outline that
// still depends on the even-odd rule while claiming to be winding.
void TestRejectsUndecidableConicDirection() {
  pk::SkPath source;
  source.setFillType(pk::SkPathFillType::kEvenOdd);
  source.moveTo(0, 0);
  source.conicTo(20, 100, 40, 0, 2.0f);
  source.conicTo(20, 100, 0, 0, 2.0f);
  pk::SkPath winding = MakeRing(0, 0, 100, 100, 20, 20, 80, 80);
  auto before = winding;
  Report(!pk::AsWinding(source, &winding), "UndecidableConicDirection", "returned true");
  Report(winding == before, "UndecidableConicDirection", "a failed call overwrote the result");
}

// Nothing about an outline with non-finite coordinates can be reasoned about, so the call has to
// fail and leave the result untouched.
void TestNonFiniteFails() {
  pk::SkPath source;
  source.setFillType(pk::SkPathFillType::kEvenOdd);
  source.moveTo(std::numeric_limits<float>::infinity(), 0);
  pk::SkPath winding = MakeRing(0, 0, 100, 100, 20, 20, 80, 80);
  auto before = winding;
  Report(!pk::AsWinding(source, &winding), "NonFiniteFails", "AsWinding() returned true");
  Report(winding == before, "NonFiniteFails", "a failed call overwrote the result");
}

}  // namespace

int main() {
  auto cases = MakeCases();
  std::printf("--- pk::AsWinding (counted) ---\n");
  for (const auto& testCase : cases) {
    RunAsWinding(testCase);
  }
  TestInverseFillType();
  TestWindingIsLeftAlone();
  TestNormalizedDirections();
  TestRejectsUndecidableConicDirection();
  TestNonFiniteFails();
  std::printf("--- pk::SkOpBuilder::resolve (informational) ---\n");
  for (const auto& testCase : cases) {
    RunOpBuilder(testCase);
  }
  if (gFailures == 0) {
    std::printf("all AsWinding tests passed\n");
    return 0;
  }
  std::printf("%d AsWinding test(s) failed\n", gFailures);
  return 1;
}
