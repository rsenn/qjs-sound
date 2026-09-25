/* Screen geometry of the tonnetz lattice: three rows of triangles, node (i, j) sits at x0 + (i + j/2) * s. */

import { clamp, mod12, cid, triNodes, triSel, nodeSel, edgeSel } from './theory.js';

function segDist(p, a, b) {
  const dx = b[0] - a[0], dy = b[1] - a[1];
  const t = clamp(((p.x - a[0]) * dx + (p.y - a[1]) * dy) / (dx * dx + dy * dy), 0, 1);
  return Math.hypot(p.x - (a[0] + t * dx), p.y - (a[1] + t * dy));
}

const centroid = pts => [(pts[0][0] + pts[1][0] + pts[2][0]) / 3, (pts[0][1] + pts[1][1] + pts[2][1]) / 3];

export class TonnetzGeometry {
  constructor() {
    this.width = 0; this.s = 0; this.hh = 0; this.x0 = 0; this.y0 = 0;
  }

  /* `top` is the y of the lattice area's upper edge; `s` the triangle side length in pixels.
     With `frame` {x, y} the lattice runs down the screen instead: `width` is then the length along y, `top` the offset across x. */
  layout(width, s, top, frame = null) {
    this.width = width; this.s = s; this.hh = s * 0.866; this.frame = frame;
    this.x0 = width / 2; this.y0 = top + s * 0.4 + 3 * this.hh;
  }

  vnp(i, j) { return [this.x0 + (i + j / 2) * this.s, this.y0 - j * this.hh]; }

  np(i, j) {
    const [x, y] = this.vnp(i, j);
    return this.frame ? [this.frame.x + y, this.frame.y + x] : [x, y];
  }

  triPts(i, j, up) { return triNodes(i, j, up).map(([a, b]) => this.np(a, b)); }

  latticeRange() {
    return { imin: Math.floor(-this.x0 / this.s) - 3, imax: Math.ceil((this.width - this.x0) / this.s) + 1 };
  }

  /* Every on-screen copy of a chord: chord identity is a pitch-class fact, so a chord appears several times across the lattice. */
  copiesOf(id) {
    const out = [];
    if (!this.width) return out;
    const { imin, imax } = this.latticeRange();
    for (let j = 0; j < 3; j++) for (let i = imin; i <= imax; i++) for (const up of [true, false]) {
      if (cid(mod12(7 * i + 4 * j + (up ? 0 : 4)), !up) === id) out.push({ i, j, up });
    }
    return out;
  }

  posOf(s) {
    if (s.kind === 'node') return this.np(...s.at);
    if (s.kind === 'edge') {
      const [a, b] = s.edge.map(n => this.np(...n));
      return [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2];
    }
    return centroid(this.triPts(s.tri.i, s.tri.j, s.tri.up));
  }

  /* The copy nearest `from` keeps the on-screen jump small. */
  findTri(id, from) {
    const [px, py] = this.posOf(from);
    let best = null, bd = Infinity;
    for (const c of this.copiesOf(id)) {
      const [cx, cy] = centroid(this.triPts(c.i, c.j, c.up));
      const d = Math.hypot(cx - px, cy - py);
      if (d < bd) { bd = d; best = c; }
    }
    return best && triSel(best.i, best.j, best.up);
  }

  hit(p) {
    if (this.frame) p = { x: p.y - this.frame.y, y: p.x - this.frame.x };
    let best = null, bd = this.s * 0.3;
    for (let j = 0; j <= 3; j++) {
      const i = Math.round((p.x - this.x0) / this.s - j / 2);
      const [x, y] = this.vnp(i, j), d = Math.hypot(p.x - x, p.y - y);
      if (d < bd) { bd = d; best = [i, j]; }
    }
    if (best) return nodeSel(...best);
    const jr = clamp((this.y0 - p.y) / this.hh, 0.001, 2.999);
    const u = (p.x - this.x0) / this.s - jr / 2;
    const fi = Math.floor(u), fj = Math.floor(jr);
    const up = (u - fi) + (jr - fj) < 1;
    const pts = triNodes(fi, fj, up);
    let edge = null, ed = this.s * 0.2;
    for (let a = 0; a < 3; a++) {
      const A = pts[a], B = pts[(a + 1) % 3];
      const d = segDist(p, this.vnp(...A), this.vnp(...B));
      if (d < ed) { ed = d; edge = [A, B]; }
    }
    return edge ? edgeSel(...edge) : triSel(fi, fj, up);
  }
}
