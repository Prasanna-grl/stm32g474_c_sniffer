#!/usr/bin/env python3
"""Generate a standalone HTML chart from g474_pd_host timeline.csv."""

from __future__ import annotations

import argparse
import csv
import html
import json
from pathlib import Path


def parse_int(value: str) -> int | None:
    if value == "":
        return None
    return int(value)


def load_timeline(path: Path) -> tuple[list[dict], list[dict]]:
    analog: list[dict] = []
    pd_events: list[dict] = []

    with path.open(newline="") as f:
        for row in csv.DictReader(f):
            timestamp_us = parse_int(row.get("timestamp_us", ""))
            if timestamp_us is None:
                continue

            event = row.get("event", "")
            if event == "analog":
                analog.append(
                    {
                        "t": timestamp_us,
                        "time": row.get("timestamp", ""),
                        "vbus_mv": parse_int(row.get("vbus_mv", "")),
                        "vbus_ma": parse_int(row.get("vbus_ma", "")),
                        "cc1_mv": parse_int(row.get("cc1_mv", "")),
                        "cc1_ma": parse_int(row.get("cc1_ma", "")),
                        "cc2_mv": parse_int(row.get("cc2_mv", "")),
                        "cc2_ma": parse_int(row.get("cc2_ma", "")),
                    }
                )
            elif event == "pd":
                pd_events.append(
                    {
                        "t": timestamp_us,
                        "time": row.get("timestamp", ""),
                        "cc": row.get("cc", ""),
                        "record": row.get("record", ""),
                        "message": row.get("message", ""),
                        "summary": row.get("summary", ""),
                        "payload": row.get("payload_hex", ""),
                    }
                )

    analog.sort(key=lambda item: item["t"])
    pd_events.sort(key=lambda item: item["t"])
    return analog, pd_events


HTML_TEMPLATE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title>
<style>
  :root {{
    color-scheme: light;
    --bg: #f6f7f9;
    --panel: #ffffff;
    --ink: #16202a;
    --muted: #647286;
    --grid: #d8dee8;
    --axis: #7b8796;
    --vbus: #1346d8;
    --cc1: #14a44d;
    --cc2: #d6bf00;
    --pd: #e11931;
    --current: #7c3aed;
  }}
  * {{ box-sizing: border-box; }}
  body {{
    margin: 0;
    background: var(--bg);
    color: var(--ink);
    font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
  }}
  main {{
    height: 100vh;
    display: grid;
    grid-template-rows: auto 1fr auto;
    gap: 10px;
    padding: 12px;
  }}
  .topbar {{
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 12px;
    padding: 10px 12px;
    background: var(--panel);
    border: 1px solid #dfe4ec;
    border-radius: 6px;
  }}
  .stat {{
    display: grid;
    gap: 2px;
    min-width: 108px;
  }}
  .stat strong {{ font-size: 20px; line-height: 1.1; }}
  .stat span {{ color: var(--muted); font-size: 12px; }}
  .legend {{
    margin-left: auto;
    display: flex;
    flex-wrap: wrap;
    gap: 10px;
    color: var(--muted);
    font-size: 13px;
  }}
  .key {{ display: inline-flex; align-items: center; gap: 5px; }}
  .swatch {{ width: 18px; height: 3px; display: inline-block; border-radius: 2px; }}
  .chart-wrap {{
    position: relative;
    min-height: 360px;
    background: var(--panel);
    border: 1px solid #dfe4ec;
    border-radius: 6px;
    overflow: hidden;
  }}
  canvas {{ width: 100%; height: 100%; display: block; }}
  .tooltip {{
    position: absolute;
    pointer-events: none;
    min-width: 220px;
    max-width: 520px;
    padding: 8px 10px;
    border: 1px solid #c9d2df;
    background: rgba(255,255,255,0.96);
    color: var(--ink);
    border-radius: 5px;
    box-shadow: 0 8px 24px rgba(23, 32, 42, 0.16);
    font-size: 12px;
    display: none;
    white-space: normal;
  }}
  .events {{
    max-height: 30vh;
    overflow: auto;
    background: var(--panel);
    border: 1px solid #dfe4ec;
    border-radius: 6px;
  }}
  table {{ width: 100%; border-collapse: collapse; font-size: 12px; }}
  th, td {{
    border-bottom: 1px solid #edf0f5;
    padding: 5px 7px;
    text-align: left;
    white-space: nowrap;
  }}
  th {{ position: sticky; top: 0; background: #f8fafc; color: var(--muted); }}
  td.summary {{ white-space: normal; color: var(--muted); }}
</style>
</head>
<body>
<main>
  <section class="topbar">
    <div class="stat"><strong id="vbusNow">--</strong><span>VBUS voltage</span></div>
    <div class="stat"><strong id="vbusCurrent">--</strong><span>VBUS current</span></div>
    <div class="stat"><strong id="cc1Now">--</strong><span>CC1 voltage</span></div>
    <div class="stat"><strong id="cc2Now">--</strong><span>CC2 voltage</span></div>
    <div class="stat"><strong id="packetCount">--</strong><span>PD packets</span></div>
    <div class="legend">
      <span class="key"><span class="swatch" style="background:var(--vbus)"></span>VBUS V</span>
      <span class="key"><span class="swatch" style="background:var(--cc1)"></span>CC1 V</span>
      <span class="key"><span class="swatch" style="background:var(--cc2)"></span>CC2 V</span>
      <span class="key"><span class="swatch" style="background:var(--current)"></span>Current</span>
      <span class="key"><span class="swatch" style="background:var(--pd)"></span>PD marker</span>
    </div>
  </section>
  <section class="chart-wrap">
    <canvas id="chart"></canvas>
    <div id="tooltip" class="tooltip"></div>
  </section>
  <section class="events">
    <table>
      <thead><tr><th>#</th><th>Time</th><th>CC</th><th>Message</th><th>Record</th><th>Summary</th></tr></thead>
      <tbody id="eventRows"></tbody>
    </table>
  </section>
</main>
<script>
const analog = {analog_json};
const pdEvents = {pd_json};

const canvas = document.getElementById('chart');
const tooltip = document.getElementById('tooltip');
const ctx = canvas.getContext('2d');

const colors = {{
  vbus: '#1346d8',
  cc1: '#14a44d',
  cc2: '#d6bf00',
  current: '#7c3aed',
  pd: '#e11931',
  grid: '#d8dee8',
  axis: '#7b8796',
  ink: '#16202a',
  muted: '#647286'
}};

function fmtSeconds(us) {{
  const s = us / 1e6;
  if (s < 1) return (s * 1000).toFixed(1) + ' ms';
  return s.toFixed(3) + ' s';
}}
function fmtV(mv) {{ return mv == null ? '--' : (mv / 1000).toFixed(3) + ' V'; }}
function fmtA(ma) {{ return ma == null ? '--' : (ma / 1000).toFixed(3) + ' A'; }}

const allTimes = analog.map(a => a.t).concat(pdEvents.map(e => e.t));
const tMin = Math.min(...allTimes);
const tMax = Math.max(...allTimes);
const tSpan = Math.max(1, tMax - tMin);

function values(series) {{
  return analog.map(a => a[series]).filter(v => v != null);
}}
const volts = values('vbus_mv').concat(values('cc1_mv'), values('cc2_mv'));
const currents = values('vbus_ma').concat(values('cc1_ma'), values('cc2_ma'));
const vMin = Math.min(0, ...volts);
const vMax = Math.max(1, ...volts);
const iMin = Math.min(0, ...currents);
const iMax = Math.max(1, ...currents);

function paddedRange(min, max) {{
  const pad = Math.max(1, (max - min) * 0.08);
  return [min - pad, max + pad];
}}
const [vLo, vHi] = paddedRange(vMin, vMax);
const [iLo, iHi] = paddedRange(iMin, iMax);

function xOf(t, rect) {{
  return rect.left + ((t - tMin) / tSpan) * rect.width;
}}
function yOf(value, lo, hi, top, height) {{
  return top + height - ((value - lo) / (hi - lo)) * height;
}}

function setupCanvas() {{
  const dpr = window.devicePixelRatio || 1;
  const rect = canvas.getBoundingClientRect();
  canvas.width = Math.max(1, Math.floor(rect.width * dpr));
  canvas.height = Math.max(1, Math.floor(rect.height * dpr));
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  draw();
}}

function drawGrid(rect, top, height, lo, hi, label) {{
  ctx.strokeStyle = colors.grid;
  ctx.fillStyle = colors.muted;
  ctx.lineWidth = 1;
  ctx.font = '12px system-ui';
  for (let i = 0; i <= 4; i++) {{
    const y = top + (height * i / 4);
    ctx.beginPath();
    ctx.moveTo(rect.left, y);
    ctx.lineTo(rect.left + rect.width, y);
    ctx.stroke();
    const val = hi - (hi - lo) * i / 4;
    ctx.fillText(label(val), 8, y + 4);
  }}
}}

function drawSeries(key, color, lo, hi, top, height, rect) {{
  ctx.strokeStyle = color;
  ctx.lineWidth = 1.8;
  ctx.beginPath();
  let started = false;
  for (const point of analog) {{
    const value = point[key];
    if (value == null) continue;
    const x = xOf(point.t, rect);
    const y = yOf(value, lo, hi, top, height);
    if (!started) {{
      ctx.moveTo(x, y);
      started = true;
    }} else {{
      ctx.lineTo(x, y);
    }}
  }}
  ctx.stroke();
}}

function draw() {{
  const w = canvas.clientWidth;
  const h = canvas.clientHeight;
  ctx.clearRect(0, 0, w, h);
  const margin = {{ left: 64, right: 24, top: 24, bottom: 36 }};
  const rect = {{
    left: margin.left,
    width: Math.max(1, w - margin.left - margin.right),
  }};
  const plotH = Math.max(1, h - margin.top - margin.bottom);
  const voltageTop = margin.top;
  const voltageH = plotH * 0.55;
  const currentTop = voltageTop + voltageH + 28;
  const currentH = plotH - voltageH - 28;
  rect.top = voltageTop;
  rect.height = plotH;

  drawGrid(rect, voltageTop, voltageH, vLo, vHi, v => (v / 1000).toFixed(1) + ' V');
  drawGrid(rect, currentTop, currentH, iLo, iHi, v => (v / 1000).toFixed(1) + ' A');

  ctx.strokeStyle = colors.axis;
  ctx.beginPath();
  ctx.moveTo(rect.left, voltageTop + voltageH + 14);
  ctx.lineTo(rect.left + rect.width, voltageTop + voltageH + 14);
  ctx.stroke();

  drawSeries('vbus_mv', colors.vbus, vLo, vHi, voltageTop, voltageH, rect);
  drawSeries('cc1_mv', colors.cc1, vLo, vHi, voltageTop, voltageH, rect);
  drawSeries('cc2_mv', colors.cc2, vLo, vHi, voltageTop, voltageH, rect);
  drawSeries('vbus_ma', colors.current, iLo, iHi, currentTop, currentH, rect);

  ctx.strokeStyle = colors.pd;
  ctx.fillStyle = colors.pd;
  ctx.font = '11px system-ui';
  for (const event of pdEvents) {{
    const x = xOf(event.t, rect);
    ctx.beginPath();
    ctx.moveTo(x, voltageTop);
    ctx.lineTo(x, voltageTop + voltageH + 8);
    ctx.stroke();
    ctx.fillRect(x - 2, voltageTop + voltageH + 8, 4, 8);
  }}

  ctx.fillStyle = colors.muted;
  ctx.font = '12px system-ui';
  for (let i = 0; i <= 8; i++) {{
    const t = tMin + tSpan * i / 8;
    const x = xOf(t, rect);
    ctx.fillText(fmtSeconds(t - tMin), x - 18, h - 12);
  }}
}}

function nearestAnalog(t) {{
  if (!analog.length) return null;
  let best = analog[0];
  let bestDist = Math.abs(best.t - t);
  for (const point of analog) {{
    const dist = Math.abs(point.t - t);
    if (dist < bestDist) {{
      best = point;
      bestDist = dist;
    }}
  }}
  return best;
}}

function nearestEvent(t) {{
  if (!pdEvents.length) return null;
  let best = pdEvents[0];
  let bestDist = Math.abs(best.t - t);
  for (const event of pdEvents) {{
    const dist = Math.abs(event.t - t);
    if (dist < bestDist) {{
      best = event;
      bestDist = dist;
    }}
  }}
  return bestDist < tSpan * 0.015 ? best : null;
}}

canvas.addEventListener('mousemove', ev => {{
  const box = canvas.getBoundingClientRect();
  const marginLeft = 64;
  const marginRight = 24;
  const x = Math.min(Math.max(ev.clientX - box.left, marginLeft), box.width - marginRight);
  const t = tMin + ((x - marginLeft) / Math.max(1, box.width - marginLeft - marginRight)) * tSpan;
  const a = nearestAnalog(t);
  const e = nearestEvent(t);
  let body = '';
  if (a) {{
    body += '<b>' + htmlEscape(a.time) + '</b><br>' +
      'VBUS ' + fmtV(a.vbus_mv) + ' / ' + fmtA(a.vbus_ma) + '<br>' +
      'CC1 ' + fmtV(a.cc1_mv) + ' / ' + fmtA(a.cc1_ma) + '<br>' +
      'CC2 ' + fmtV(a.cc2_mv) + ' / ' + fmtA(a.cc2_ma);
  }}
  if (e) {{
    body += '<hr><b>' + htmlEscape(e.cc) + ' ' + htmlEscape(e.message) +
      '</b><br>' + htmlEscape(e.time) + ' rec=' + htmlEscape(e.record) +
      '<br>' + htmlEscape(e.summary);
  }}
  tooltip.innerHTML = body;
  tooltip.style.display = 'block';
  tooltip.style.left = Math.min(ev.clientX - box.left + 14, box.width - 540) + 'px';
  tooltip.style.top = Math.max(8, ev.clientY - box.top - 20) + 'px';
}});
canvas.addEventListener('mouseleave', () => tooltip.style.display = 'none');

function htmlEscape(value) {{
  return String(value ?? '').replace(/[&<>"']/g, ch => ({{
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
  }}[ch]));
}}

function fillStats() {{
  const last = analog[analog.length - 1];
  document.getElementById('packetCount').textContent = String(pdEvents.length);
  if (last) {{
    document.getElementById('vbusNow').textContent = fmtV(last.vbus_mv);
    document.getElementById('vbusCurrent').textContent = fmtA(last.vbus_ma);
    document.getElementById('cc1Now').textContent = fmtV(last.cc1_mv);
    document.getElementById('cc2Now').textContent = fmtV(last.cc2_mv);
  }}
  const body = document.getElementById('eventRows');
  body.innerHTML = pdEvents.map((e, index) =>
    '<tr><td>' + (index + 1) + '</td><td>' + htmlEscape(e.time) + '</td>' +
    '<td>' + htmlEscape(e.cc) + '</td><td>' + htmlEscape(e.message) + '</td>' +
    '<td>' + htmlEscape(e.record) + '</td><td class="summary">' +
    htmlEscape(e.summary) + '</td></tr>'
  ).join('');
}}

window.addEventListener('resize', setupCanvas);
fillStats();
setupCanvas();
</script>
</body>
</html>
"""


def build_html(title: str, analog: list[dict], pd_events: list[dict]) -> str:
    return HTML_TEMPLATE.format(
        title=html.escape(title),
        analog_json=json.dumps(analog, separators=(",", ":")),
        pd_json=json.dumps(pd_events, separators=(",", ":")),
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", nargs="?", default="timeline.csv", type=Path)
    parser.add_argument("--out", default="timeline.html", type=Path)
    args = parser.parse_args()

    analog, pd_events = load_timeline(args.csv)
    if not analog and not pd_events:
        raise SystemExit(f"No timeline rows found in {args.csv}")

    title = f"{args.csv.name} ({len(analog)} analog, {len(pd_events)} PD)"
    args.out.write_text(build_html(title, analog, pd_events))
    print(f"wrote {args.out} analog={len(analog)} pd={len(pd_events)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
