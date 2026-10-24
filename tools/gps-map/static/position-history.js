/* Small, browser-local GPS history. Timestamps are bridge receipt times. */
class PositionHistory {
  static interval = 10 * 60 * 1000;
  static retention = 3 * 60 * 60 * 1000;
  static key = 'hab.position-history.v1';

  constructor(storage, now = Date.now()) {
    this.storage = storage;
    this.points = [];
    this.latest = null;
    this.error = false;
    try {
      const saved = JSON.parse(storage.getItem(PositionHistory.key) || 'null');
      if (saved) {
        this.points = Array.isArray(saved.points) ? saved.points.filter(p => this.valid(p)) : [];
        this.points.sort((a,b) => a.time-b.time);
        this.points = this.points.filter((p,i,all) => !i || p.time-all[i-1].time >= PositionHistory.interval).slice(-18);
        this.latest = this.valid(saved.latest) ? saved.latest : null;
      }
    } catch (_) { this.error = true; }
    this.prune(now);
  }

  valid(p) {
    return p && Number.isFinite(p.time) && Number.isFinite(p.lat) && Math.abs(p.lat)<=90 &&
      Number.isFinite(p.lon) && Math.abs(p.lon)<=180;
  }

  prune(now = Date.now()) {
    const count = this.points.length;
    this.points = this.points.filter(p => p.time > now-PositionHistory.retention && p.time <= now);
    if (count !== this.points.length) this.save();
  }

  accept(point, now = Date.now()) {
    this.prune(now);
    if (!this.valid(point) || point.time > now || point.time <= now-PositionHistory.retention ||
        (this.latest && point.time <= this.latest.time)) return;
    this.latest = point;
    const last = this.points.at(-1);
    if (!last || point.time-last.time >= PositionHistory.interval) this.points.push(point);
    this.save();
  }

  save() {
    try {
      this.storage.setItem(PositionHistory.key, JSON.stringify({points:this.points, latest:this.latest}));
      this.error = false;
    } catch (_) { this.error = true; }
  }

  static color(time, now = Date.now()) {
    const age = Math.max(0, Math.min(1, (now-time)/PositionHistory.retention));
    return `hsl(${Math.round(155*(1-age))}, 72%, 38%)`;
  }
}
if (typeof module !== 'undefined') module.exports = PositionHistory;
