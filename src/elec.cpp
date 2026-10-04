// Electricity: battery cells fix a potential, every conducting cell (metal, solder, water, conducting rigid bodies)
// is a node of a resistor network. The network is solved each frame (conjugate gradients, warm-started), the
// battery's current limit sags the voltage, resistive heating warms the conductors, and air gaps between
// conductors at high potential difference break down into sparks that ignite fuel.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "sand.hpp"

namespace {
const int DXe[4] = {1, -1, 0, 0}, DYe[4] = {0, 0, 1, -1};
constexpr float HEAT_K = 130.f;      // joule heating -> temperature, game units
constexpr float BREAKDOWN = 3000.f;  // volts per cell of air gap at normal density
constexpr float SPARK_MIN_V = 600.f;
}  // namespace

uint8_t World::encV(float v) { return (uint8_t)std::clamp((int)std::lround(std::log(std::max(v, 1.f)) / std::log(1.045f)), 0, 255); }
float World::decV(uint8_t c) { return std::pow(1.045f, (float)c); }
uint8_t World::encA(float a) { return (uint8_t)std::clamp((int)std::lround(std::log(std::max(a, 0.001f) / 0.001f) / std::log(1.0525f)), 0, 255); }
float World::decA(uint8_t c) { return 0.001f * std::pow(1.0525f, (float)c); }

void World::electricity() {
    const int N = W * H;
    if ((int)volt.size() != N) { volt.assign(N, 0.f); curr.assign(N, 0.f); elecRaw.assign(N, 0.f); elecIdx.assign(N, -1); }
    // decay old arcs
    for (auto& a : arcs) --a.frames;
    arcs.erase(std::remove_if(arcs.begin(), arcs.end(), [](const Arc& a) { return a.frames <= 0; }), arcs.end());
    {   // spark cooldowns are (cell, frames) pairs
        size_t w = 0;
        for (size_t k = 0; k + 1 < elecCool.size(); k += 2)
            if (elecCool[k + 1] > 1) { elecCool[w++] = elecCool[k]; elecCool[w++] = elecCool[k + 1] - 1; }
        elecCool.resize(w);
    }

    bool anyBatt = false;
    for (int i = 0; i < N && !anyBatt; ++i) anyBatt = cells[i].t == M_BATT_POS;
    if (!anyBatt) {
        if (hadElec) { std::fill(volt.begin(), volt.end(), 0.f); std::fill(curr.begin(), curr.end(), 0.f); std::fill(elecRaw.begin(), elecRaw.end(), 0.f); hadElec = false; }
        vMax = iSource = 0.f;
        return;
    }
    hadElec = true;

    // ---- nodes
    auto sigmaAt = [&](int i) -> float {
        int b = bodyMask[i];
        if (b >= 0) return b < (int)bodySigma.size() ? bodySigma[b] : 0.f;
        uint8_t t = cells[i].t;
        if (t == M_EMPTY || t == M_MOLTEN) return 0.f;
        return MATS[t].elec;
    };
    std::vector<int>& idx = elecIdx;
    std::fill(idx.begin(), idx.end(), -1);
    std::vector<int> cellOf;
    std::vector<float> sig;
    for (int i = 0; i < N; ++i) {
        float s = sigmaAt(i);
        if (s > 0.f) { idx[i] = (int)cellOf.size(); cellOf.push_back(i); sig.push_back(s); }
    }
    const int n = (int)cellOf.size();
    std::vector<int> nb(4 * (size_t)n, -1);
    std::vector<float> g(4 * (size_t)n, 0.f);
    std::vector<char> isD(n, 0);
    std::vector<float> dval(n, 0.f);
    for (int a = 0; a < n; ++a) {
        int i = cellOf[a], x = i % W, y = i / W;
        for (int k = 0; k < 4; ++k) {
            int nx = x + DXe[k], ny = y + DYe[k];
            if (!inb(nx, ny)) continue;
            int b = idx[ny * W + nx];
            if (b < 0) continue;
            nb[4 * a + k] = b;
            g[4 * a + k] = 2.f * sig[a] * sig[b] / (sig[a] + sig[b]);
        }
        if (bodyMask[i] < 0) {
            if (cells[i].t == M_BATT_POS) { isD[a] = 1; dval[a] = decV(cells[i].life); }
            else if (cells[i].t == M_BATT_NEG) { isD[a] = 1; dval[a] = 0.f; }
        }
    }

    // ---- connected components (so floating networks stay at 0 V)
    std::vector<int> comp(n, -1), stack;
    std::vector<char> compHasD;
    int nc = 0;
    for (int a = 0; a < n; ++a) {
        if (comp[a] >= 0) continue;
        bool hasD = false;
        comp[a] = nc; stack.push_back(a);
        while (!stack.empty()) {
            int p = stack.back(); stack.pop_back();
            hasD |= isD[p] != 0;
            for (int k = 0; k < 4; ++k) { int q = nb[4 * p + k]; if (q >= 0 && comp[q] < 0) { comp[q] = nc; stack.push_back(q); } }
        }
        compHasD.push_back(hasD);
        ++nc;
    }

    // ---- solve (A x = b) for the free nodes of components that contain a source
    std::vector<float> x(n, 0.f);
    float vTop = 1.f;
    for (int a = 0; a < n; ++a) {
        if (isD[a]) { x[a] = dval[a]; vTop = std::max(vTop, dval[a]); }
        else if (compHasD[comp[a]]) x[a] = elecRaw[cellOf[a]];
    }
    std::vector<char> free_(n, 0);
    for (int a = 0; a < n; ++a) free_[a] = !isD[a] && compHasD[comp[a]];
    std::vector<float> r(n, 0.f), pdir(n, 0.f), Ap(n, 0.f), bvec(n, 0.f);
    for (int a = 0; a < n; ++a) {
        if (!free_[a]) continue;
        for (int k = 0; k < 4; ++k) { int q = nb[4 * a + k]; if (q >= 0 && isD[q]) bvec[a] += g[4 * a + k] * dval[q]; }
    }
    // one conjugate-gradient solve per connected network, each to its own relative tolerance
    std::vector<std::vector<int>> members(nc);
    for (int a = 0; a < n; ++a) if (free_[a]) members[comp[a]].push_back(a);
    double rr = 0, tol = 0;
    for (int c = 0; c < nc; ++c) {
        const std::vector<int>& m = members[c];
        if (m.empty()) continue;
        auto applyL = [&](const std::vector<float>& p, std::vector<float>& out) {
            for (int a : m) {
                float sum = 0.f;
                for (int k = 0; k < 4; ++k) {
                    int q = nb[4 * a + k];
                    if (q < 0) continue;
                    float gg = g[4 * a + k];
                    sum += gg * p[a];
                    if (free_[q]) sum -= gg * p[q];
                }
                out[a] = sum;
            }
        };
        applyL(x, Ap);
        rr = 0;
        double rr0 = 0;
        for (int a : m) { r[a] = bvec[a] - Ap[a]; pdir[a] = r[a]; rr += (double)r[a] * r[a]; rr0 += (double)bvec[a] * bvec[a]; }
        tol = std::max(1e-18, rr0 * 1e-12);
        int maxIt = std::min(800, 4 * (int)m.size() + 20);
        for (int it = 0; it < maxIt && rr > tol; ++it) {
            applyL(pdir, Ap);
            double pAp = 0;
            for (int a : m) pAp += (double)pdir[a] * Ap[a];
            if (pAp <= 1e-30) break;
            float alpha = (float)(rr / pAp);
            double rrNew = 0;
            for (int a : m) { x[a] += alpha * pdir[a]; r[a] -= alpha * Ap[a]; rrNew += (double)r[a] * r[a]; }
            float beta = (float)(rrNew / rr);
            for (int a : m) pdir[a] = r[a] + beta * pdir[a];
            rr = rrNew;
        }
    }
    std::fill(elecRaw.begin(), elecRaw.end(), 0.f);
    for (int a = 0; a < n; ++a) elecRaw[cellOf[a]] = x[a];

    // ---- source current and the current limit (the battery sags to its rated amperage)
    std::vector<int> blobOf(n, -1);
    std::vector<float> compI(nc, 0.f), compImax(nc, 0.f);
    {
        int nb_ = 0;
        for (int a = 0; a < n; ++a) {
            if (!isD[a] || cells[cellOf[a]].t != M_BATT_POS || blobOf[a] >= 0) continue;
            float amax = decA(cells[cellOf[a]].aux);
            blobOf[a] = nb_; stack.push_back(a);
            while (!stack.empty()) {
                int p = stack.back(); stack.pop_back();
                for (int k = 0; k < 4; ++k) { int q = nb[4 * p + k]; if (q >= 0 && blobOf[q] < 0 && isD[q] && cells[cellOf[q]].t == M_BATT_POS) { blobOf[q] = nb_; stack.push_back(q); } }
            }
            compImax[comp[a]] += amax;
            ++nb_;
        }
        for (int a = 0; a < n; ++a) {
            if (!isD[a] || cells[cellOf[a]].t != M_BATT_POS) continue;
            for (int k = 0; k < 4; ++k) {
                int q = nb[4 * a + k];
                if (q < 0 || (isD[q] && cells[cellOf[q]].t != M_BATT_NEG)) continue;   // (a + cell touching a - cell is a dead short, and counts)
                float cur = g[4 * a + k] * (x[a] - x[q]);
                if (cur > 0) compI[comp[a]] += cur;
            }
        }
    }
    std::vector<float> sag(nc, 1.f);
    iSource = 0.f;
    for (int c = 0; c < nc; ++c) {
        if (compI[c] > compImax[c] && compI[c] > 0) sag[c] = compImax[c] / compI[c];
        iSource += compI[c] * sag[c];
    }

    // ---- publish potentials, currents, heating
    std::fill(volt.begin(), volt.end(), 0.f);
    std::fill(curr.begin(), curr.end(), 0.f);
    bodyHeat.assign(bodySigma.size(), 0.f);
    vMax = 0.f;
    for (int a = 0; a < n; ++a) {
        float v = x[a] * sag[comp[a]];
        volt[cellOf[a]] = v;
        vMax = std::max(vMax, std::fabs(v));
    }
    for (int a = 0; a < n; ++a) {
        int i = cellOf[a];
        for (int k = 0; k < 4; k += 2) {  // right and down edges, each once
            int q = nb[4 * a + k];
            if (q < 0) continue;
            float gg = g[4 * a + k];
            float dv = volt[i] - volt[cellOf[q]];
            float cur = std::fabs(gg * dv);
            curr[i] = std::max(curr[i], cur);
            curr[cellOf[q]] = std::max(curr[cellOf[q]], cur);
            float P = gg * dv * dv;
            if (P < 1e-4f) continue;
            for (int end = 0; end < 2; ++end) {
                int ci = end ? cellOf[q] : i;
                if (bodyMask[ci] >= 0) {   // a conducting body heats too: the energy goes to the body, which spreads it over its whole volume
                    int bi = bodyMask[ci];
                    if (bi < (int)bodyHeat.size()) bodyHeat[bi] += HEAT_K * P * 0.5f / 60.f;
                    continue;
                }
                if (cells[ci].t == M_BATT_POS || cells[ci].t == M_BATT_NEG) continue;
                Cell& c = cells[ci];
                c.temp += HEAT_K * P * 0.5f / 60.f / cellCap(c);
            }
        }
    }

    // ---- sparks across air gaps
    if (vMax < SPARK_MIN_V) return;
    for (int a = 0; a < n; ++a) {
        int i = cellOf[a];
        float va = volt[i];
        if (va < SPARK_MIN_V) continue;
        int x0 = i % W, y0 = i / W;
        bool cooling = false;
        for (size_t k = 0; k < elecCool.size(); k += 2) if (elecCool[k] == i && elecCool[k + 1] > 0) { cooling = true; break; }
        if (cooling) continue;
        for (int d = 0; d < 8; ++d) {
            static const int DX8[8] = {1, -1, 0, 0, 1, 1, -1, -1}, DY8[8] = {0, 0, 1, -1, 1, -1, 1, -1};
            int dx = DX8[d], dy = DY8[d];
            float dens = 0.f;
            int gap = 0, hit = -1;
            for (int k = 1; k <= 9; ++k) {
                int nx = x0 + dx * k, ny = y0 + dy * k;
                if (!inb(nx, ny)) break;
                int j = ny * W + nx;
                if (idx[j] >= 0) { if (k > 1) hit = j; break; }
                if (bodyMask[j] >= 0) break;
                const Cell& c = cells[j];
                if (c.t == M_EMPTY) dens += 1.f;
                else if (MATS[c.t].kind == K_GAS) dens += std::clamp(c.amt, 0.3f, 4.f);
                else break;
                ++gap;
            }
            if (hit < 0 || gap < 1) continue;
            float vb = volt[hit];
            float dV = va - vb;
            float len = (float)gap * ((dx && dy) ? 1.414f : 1.f);
            float vbd = BREAKDOWN * len * std::clamp(dens / gap, 0.4f, 4.f);
            if (dV < vbd) continue;
            // the arc: ignite along the channel and heat it
            int hx = hit % W, hy = hit / W;
            for (int k = 1; k <= gap; ++k) {
                int cx = x0 + dx * k, cy = y0 + dy * k;
                Cell& c = at(cx, cy);
                if (c.t != M_EMPTY) c.temp = std::max(c.temp, 450.f);
                sparkAt(cx, cy);
                for (int q = 0; q < 4; ++q) { int sx = cx + DXe[q], sy = cy + DYe[q]; if (inb(sx, sy) && bodyMask[sy * W + sx] < 0) sparkAt(sx, sy); }
            }
            arcs.push_back({x0, y0, hx, hy, 3});
            ++arcCount;
            elecCool.push_back(i); elecCool.push_back(6);
            break;
        }
    }
}
