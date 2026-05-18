#include "GeomUtils.h"
#include <cmath>

namespace GeomUtils {

double heading(double x1, double y1, double x2, double y2)
{
    return std::atan2(y2 - y1, x2 - x1);
}

std::vector<XodrGeometry> buildGeometryChain(
    const std::vector<OsmId>& nodeIds,
    const NodeXYMap&          nodeXY,
    double                    minLength)
{
    std::vector<XodrGeometry> result;
    if (nodeIds.size() < 2) return result;

    // Look up first and last node XY — endpoints we must preserve
    auto itFirst = nodeXY.find(nodeIds.front());
    auto itLast  = nodeXY.find(nodeIds.back());
    if (itFirst == nodeXY.end() || itLast == nodeXY.end()) return result;

    const XY& pFirst = itFirst->second;
    const XY& pLast  = itLast->second;

    double s = 0.0;

    for (std::size_t i = 0; i + 1 < nodeIds.size(); ++i) {
        auto it1 = nodeXY.find(nodeIds[i]);
        auto it2 = nodeXY.find(nodeIds[i + 1]);
        if (it1 == nodeXY.end() || it2 == nodeXY.end()) continue;

        const XY& p1 = it1->second;
        const XY& p2 = it2->second;

        double dx  = p2.x - p1.x;
        double dy  = p2.y - p1.y;
        double len = std::hypot(dx, dy);
        if (len < minLength) continue;

        XodrGeometry g;
        g.s      = s;
        g.x      = p1.x;
        g.y      = p1.y;
        g.hdg    = std::atan2(dy, dx);
        g.length = len;
        result.push_back(g);
        s += len;
    }

    if (result.empty()) return result;

    // Ensure the chain starts at the first node
    {
        auto& first = result.front();
        double errX = pFirst.x - first.x;
        double errY = pFirst.y - first.y;
        if (std::hypot(errX, errY) > 1e-9) {
            // Recompute first segment from pFirst to its original endpoint
            double endX = first.x + first.length * std::cos(first.hdg);
            double endY = first.y + first.length * std::sin(first.hdg);
            first.x = pFirst.x;
            first.y = pFirst.y;
            double dx2 = endX - pFirst.x;
            double dy2 = endY - pFirst.y;
            double newLen = std::hypot(dx2, dy2);
            if (newLen >= 1e-9) {
                first.hdg    = std::atan2(dy2, dx2);
                first.length = newLen;
            }
        }
    }

    // Ensure the chain ends at the last node
    {
        auto& last = result.back();
        double endX = last.x + last.length * std::cos(last.hdg);
        double endY = last.y + last.length * std::sin(last.hdg);
        double errX = pLast.x - endX;
        double errY = pLast.y - endY;
        if (std::hypot(errX, errY) > 1e-9) {
            double dx2 = pLast.x - last.x;
            double dy2 = pLast.y - last.y;
            double newLen = std::hypot(dx2, dy2);
            if (newLen >= 1e-9) {
                last.hdg    = std::atan2(dy2, dx2);
                last.length = newLen;
            }
        }
    }

    return result;
}

double chainLength(const std::vector<XodrGeometry>& chain)
{
    if (chain.empty()) return 0.0;
    const auto& last = chain.back();
    return last.s + last.length;
}

PosHdg evaluateChain(const std::vector<XodrGeometry>& chain, double s)
{
    if (chain.empty()) return {0, 0, 0};

    // Clamp s to valid range
    double totalLen = chainLength(chain);
    if (s <= 0.0) return {chain.front().x, chain.front().y, chain.front().hdg};
    if (s >= totalLen) {
        const auto& last = chain.back();
        double ex = last.x + last.length * std::cos(last.hdg);
        double ey = last.y + last.length * std::sin(last.hdg);
        return {ex, ey, last.hdg};
    }

    // Find the segment containing s
    for (const auto& g : chain) {
        if (s >= g.s && s <= g.s + g.length) {
            double ds = s - g.s;
            double x = g.x + ds * std::cos(g.hdg);
            double y = g.y + ds * std::sin(g.hdg);
            return {x, y, g.hdg};
        }
    }
    // Fallback: end of chain
    const auto& last = chain.back();
    double ex = last.x + last.length * std::cos(last.hdg);
    double ey = last.y + last.length * std::sin(last.hdg);
    return {ex, ey, last.hdg};
}

PosHdg trimChainEnd(std::vector<XodrGeometry>& chain, double dist)
{
    double totalLen = chainLength(chain);
    double newLen = totalLen - dist;
    if (newLen < 0.01) newLen = 0.01; // keep a minimal stub

    // Evaluate position at the new endpoint
    PosHdg ep = evaluateChain(chain, newLen);

    // Remove segments that start beyond newLen
    while (!chain.empty() && chain.back().s >= newLen)
        chain.pop_back();

    // Truncate the last remaining segment
    if (!chain.empty()) {
        auto& last = chain.back();
        last.length = newLen - last.s;
        if (last.length < 1e-9) last.length = 1e-9;
    }

    return ep;
}

PosHdg trimChainStart(std::vector<XodrGeometry>& chain, double dist)
{
    double totalLen = chainLength(chain);
    if (dist >= totalLen - 0.01) dist = totalLen - 0.01;

    // Evaluate position at the new startpoint
    PosHdg sp = evaluateChain(chain, dist);

    // Remove segments that end before dist
    while (!chain.empty() && (chain.front().s + chain.front().length) <= dist)
        chain.erase(chain.begin());

    // Adjust the first remaining segment
    if (!chain.empty()) {
        auto& first = chain.front();
        double offset = dist - first.s;
        if (offset > 0) {
            first.x += offset * std::cos(first.hdg);
            first.y += offset * std::sin(first.hdg);
            first.length -= offset;
            if (first.length < 1e-9) first.length = 1e-9;
        }
        // Re-base all s-offsets so chain starts at s=0
        double sShift = first.s + offset;
        first.s = 0.0;
        for (std::size_t i = 1; i < chain.size(); ++i)
            chain[i].s -= sShift;
    }

    return sp;
}

} // namespace GeomUtils
