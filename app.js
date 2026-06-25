class RNG {
  constructor(seed) {
    this.seed = seed >>> 0;
  }

  next() {
    this.seed = (1664525 * this.seed + 1013904223) >>> 0;
    return this.seed / 4294967296;
  }

  range(min, max) {
    return min + (max - min) * this.next();
  }

  int(min, max) {
    return Math.floor(this.range(min, max + 1));
  }

  pick(items) {
    return items[Math.floor(this.next() * items.length)];
  }
}

class DSU {
  constructor(items) {
    this.parent = new Map(items.map((x) => [x, x]));
    this.rank = new Map(items.map((x) => [x, 0]));
  }

  find(x) {
    let p = this.parent.get(x);
    if (p !== x) {
      p = this.find(p);
      this.parent.set(x, p);
    }
    return p;
  }

  union(a, b) {
    let ra = this.find(a);
    let rb = this.find(b);
    if (ra === rb) return false;
    if (this.rank.get(ra) < this.rank.get(rb)) {
      [ra, rb] = [rb, ra];
    }
    this.parent.set(rb, ra);
    if (this.rank.get(ra) === this.rank.get(rb)) {
      this.rank.set(ra, this.rank.get(ra) + 1);
    }
    return true;
  }
}

const ui = {
  nodeCount: document.getElementById("nodeCount"),
  maxRounds: document.getElementById("maxRounds"),
  commRange: document.getElementById("commRange"),
  lambdaPriority: document.getElementById("lambdaPriority"),
  gamma: document.getElementById("gamma"),
  seed: document.getElementById("seed"),
  speed: document.getElementById("speed"),
  resetBtn: document.getElementById("resetBtn"),
  stepBtn: document.getElementById("stepBtn"),
  prevRoundBtn: document.getElementById("prevRoundBtn"),
  playBtn: document.getElementById("playBtn"),
  pauseBtn: document.getElementById("pauseBtn"),
  roundBadge: document.getElementById("roundBadge"),
  modeBadge: document.getElementById("modeBadge"),
  commRangeOut: document.getElementById("commRangeOut"),
  lambdaOut: document.getElementById("lambdaOut"),
  gammaOut: document.getElementById("gammaOut"),
  activeNodes: document.getElementById("activeNodes"),
  avgSoc: document.getElementById("avgSoc"),
  socVariance: document.getElementById("socVariance"),
  avgDelay: document.getElementById("avgDelay"),
  delivered: document.getElementById("delivered"),
  dropped: document.getElementById("dropped"),
  roundLog: document.getElementById("roundLog"),
  networkCanvas: document.getElementById("networkCanvas"),
  nodeTooltip: document.getElementById("nodeTooltip"),
  chartCanvas: document.getElementById("chartCanvas"),
};

const ctx = ui.networkCanvas.getContext("2d");
const chartCtx = ui.chartCanvas.getContext("2d");

let sim = null;
let timer = null;
const drag = {
  active: false,
  lastX: 0,
  lastY: 0,
};

function cfgFromUI() {
  return {
    nodeCount: clamp(parseInt(ui.nodeCount.value, 10) || 80, 20, 160),
    maxRounds: clamp(parseInt(ui.maxRounds.value, 10) || 120, 20, 400),
    area: 100,
    commRange: parseFloat(ui.commRange.value),
    lambda: parseFloat(ui.lambdaPriority.value),
    gamma: parseFloat(ui.gamma.value),
    alpha: 0.35,
    seed: parseInt(ui.seed.value, 10) || 42,
    initialSoc: 100,
    deadThreshold: 2,
    sourceFraction: 0.18,
    txBaseCost: 0.1,
    txDistanceCost: 0.01,
    rxCost: 0.045,
    queueService: 8,
    packetBits: 1024,
    dataRate: 250000,
    processingDelay: 1.5,
    decisionDelay: 1.8,
  };
}

function clamp(v, lo, hi) {
  return Math.max(lo, Math.min(hi, v));
}

function dist(a, b) {
  return Math.hypot(a.x - b.x, a.y - b.y, 0.55 * ((a.z || 0) - (b.z || 0)));
}

function edgeKey(a, b) {
  return a < b ? `${a}-${b}` : `${b}-${a}`;
}

function avg(values) {
  return values.length ? values.reduce((a, b) => a + b, 0) / values.length : 0;
}

function variance(values) {
  const m = avg(values);
  return values.length ? avg(values.map((v) => (v - m) ** 2)) : 0;
}

function energyBin(soc) {
  if (soc < 20) return 0;
  if (soc < 40) return 1;
  if (soc < 60) return 2;
  if (soc < 80) return 3;
  return 4;
}

function queueBin(q) {
  if (q <= 1) return 0;
  if (q <= 4) return 1;
  return 2;
}

function nodeColor(node) {
  if (!node.alive) return "#111827";
  if (node.soc >= 70) return "#16a34a";
  if (node.soc >= 40) return "#f59e0b";
  return "#dc2626";
}

function createSimulation() {
  const cfg = cfgFromUI();
  const rng = new RNG(cfg.seed);
  const nodes = [];
  for (let i = 0; i < cfg.nodeCount; i++) {
    nodes.push({
      id: i,
      x: rng.range(5, cfg.area - 5),
      y: rng.range(5, cfg.area - 5),
      z: rng.range(5, cfg.area - 5),
      soc: cfg.initialSoc,
      queue: 0,
      alive: true,
    });
  }
  return {
    cfg,
    rng,
    nodes,
    q: new Map(),
    round: 0,
    history: [],
    lastRoutes: [],
    lastSources: [],
    lastTransmitter: null,
    lastFusedEdges: new Set(),
    snapshots: [],
    viewRound: 0,
    camera: {
      rotX: -0.58,
      rotY: 0.72,
      zoom: 1,
      hoverNode: null,
      projected: new Map(),
    },
    logs: [],
  };
}

function cloneNodes(nodes) {
  return nodes.map((node) => ({ ...node }));
}

function cloneRoutes(routes) {
  return routes.map((path) => path.slice());
}

function captureSnapshot() {
  return {
    round: sim.round,
    nodes: cloneNodes(sim.nodes),
    qEntries: Array.from(sim.q.entries()),
    rngSeed: sim.rng.seed,
    history: sim.history.map((item) => ({ ...item })),
    lastRoutes: cloneRoutes(sim.lastRoutes),
    lastSources: sim.lastSources.slice(),
    lastTransmitter: sim.lastTransmitter,
    lastFusedEdges: Array.from(sim.lastFusedEdges),
    logs: sim.logs.slice(),
  };
}

function storeSnapshot() {
  sim.snapshots[sim.round] = captureSnapshot();
  sim.viewRound = sim.round;
}

function restoreSnapshot(roundIndex) {
  const snapshot = sim.snapshots[roundIndex];
  if (!snapshot) return;
  hideTooltip();
  sim.viewRound = snapshot.round;
  sim.nodes = cloneNodes(snapshot.nodes);
  sim.q = new Map(snapshot.qEntries);
  sim.rng.seed = snapshot.rngSeed;
  sim.history = snapshot.history.map((item) => ({ ...item }));
  sim.lastRoutes = cloneRoutes(snapshot.lastRoutes);
  sim.lastSources = snapshot.lastSources.slice();
  sim.lastTransmitter = snapshot.lastTransmitter;
  sim.lastFusedEdges = new Set(snapshot.lastFusedEdges);
  sim.logs = snapshot.logs.slice();
  render();
}

function buildGraph(nodes, cfg) {
  const graph = new Map();
  nodes.filter((n) => n.alive).forEach((n) => graph.set(n.id, []));
  const alive = nodes.filter((n) => n.alive);
  for (let i = 0; i < alive.length; i++) {
    for (let j = i + 1; j < alive.length; j++) {
      const a = alive[i];
      const b = alive[j];
      const d = dist(a, b);
      if (d <= cfg.commRange) {
        const quality = 1 - d / cfg.commRange;
        if (quality >= 0.05) {
          graph.get(a.id).push(b.id);
          graph.get(b.id).push(a.id);
        }
      }
    }
  }
  return graph;
}

function allEdges(graph) {
  const edges = [];
  const seen = new Set();
  for (const [a, nbrs] of graph.entries()) {
    nbrs.forEach((b) => {
      const key = edgeKey(a, b);
      if (!seen.has(key)) {
        seen.add(key);
        edges.push([a, b]);
      }
    });
  }
  return edges;
}

function mstEdges(graph, nodes) {
  const nodeMap = new Map(nodes.map((n) => [n.id, n]));
  const edges = allEdges(graph)
    .map(([a, b]) => ({ a, b, w: dist(nodeMap.get(a), nodeMap.get(b)) }))
    .sort((x, y) => x.w - y.w);
  const dsu = new DSU([...graph.keys()]);
  const mst = new Set();
  for (const e of edges) {
    if (dsu.union(e.a, e.b)) mst.add(edgeKey(e.a, e.b));
  }
  return mst;
}

function fusedWeights(graph, nodes, cfg) {
  const nodeMap = new Map(nodes.map((n) => [n.id, n]));
  const edges = allEdges(graph);
  const mst = mstEdges(graph, nodes);
  const maxDist = Math.max(...edges.map(([a, b]) => dist(nodeMap.get(a), nodeMap.get(b))), 1);
  const weights = new Map();
  const preferred = new Set();
  edges.forEach(([a, b]) => {
    const na = nodeMap.get(a);
    const nb = nodeMap.get(b);
    const mera = (1 / (na.soc / 100 + 0.05) + 1 / (nb.soc / 100 + 0.05)) / 40;
    const mstWeight = dist(na, nb) / maxDist + (mst.has(edgeKey(a, b)) ? 0 : 0.75);
    const w = cfg.lambda * mera + (1 - cfg.lambda) * mstWeight;
    weights.set(edgeKey(a, b), w);
    if (w < 0.38 || mst.has(edgeKey(a, b))) preferred.add(edgeKey(a, b));
  });
  return { weights, preferred };
}

function chooseTransmitter(nodes, cfg) {
  const alive = nodes.filter((n) => n.alive);
  const sink = { x: cfg.area / 2, y: cfg.area / 2, z: cfg.area / 2 };
  return alive.reduce((best, n) => {
    const score = n.soc - 0.12 * dist(n, sink) - 0.8 * n.queue;
    return score > best.score ? { id: n.id, score } : best;
  }, { id: alive[0]?.id ?? 0, score: -Infinity }).id;
}

function stateKey(node, transmitter, graph, nodes, cfg) {
  const tx = nodes[transmitter];
  const nbrs = graph.get(node.id) || [];
  const neighborSoc = nbrs.length ? avg(nbrs.map((id) => nodes[id].soc)) : 0;
  const hopEstimate = dist(node, tx) / Math.max(cfg.commRange, 1);
  const hopBin = hopEstimate < 1 ? 0 : hopEstimate < 2.5 ? 1 : 2;
  return `${energyBin(node.soc)}|${queueBin(node.queue)}|${hopBin}|${energyBin(neighborSoc)}`;
}

function qGet(sim, nodeId, state, action) {
  return sim.q.get(`${nodeId}|${state}|${action}`) || 0;
}

function qSet(sim, nodeId, state, action, value) {
  sim.q.set(`${nodeId}|${state}|${action}`, value);
}

function dijkstra(start, target, graph, costFn) {
  const heap = [{ cost: 0, node: start, path: [start] }];
  const seen = new Set();
  while (heap.length) {
    heap.sort((a, b) => a.cost - b.cost);
    const item = heap.shift();
    if (seen.has(item.node)) continue;
    seen.add(item.node);
    if (item.node === target) return item.path;
    (graph.get(item.node) || []).forEach((nbr) => {
      if (!seen.has(nbr)) {
        heap.push({
          cost: item.cost + costFn(item.node, nbr),
          node: nbr,
          path: item.path.concat(nbr),
        });
      }
    });
  }
  return null;
}

function packetDelay(path, nodes, cfg) {
  if (!path || path.length <= 1) return 0;
  const txDelay = (cfg.packetBits / cfg.dataRate) * 1000;
  let total = cfg.decisionDelay;
  path.slice(0, -1).forEach((id) => {
    total += txDelay + cfg.processingDelay + (nodes[id].queue / cfg.queueService) * 1000;
  });
  return total;
}

function reward(path, delivered, nodes, delay) {
  if (!delivered || !path) return -2;
  const socs = path.map((id) => nodes[id].soc);
  const qPenalty = avg(path.map((id) => nodes[id].queue));
  return 2.5 + 1.2 * (avg(socs) / 100) - 0.012 * variance(socs) - 0.08 * (path.length - 1) - 0.03 * qPenalty - 0.002 * delay;
}

function applyEnergy(path, nodes, cfg) {
  for (let i = 0; i < path.length - 1; i++) {
    const a = nodes[path[i]];
    const b = nodes[path[i + 1]];
    const d = dist(a, b);
    a.soc -= cfg.txBaseCost + cfg.txDistanceCost * d + 0.01;
    b.soc -= 0.045;
    a.queue = Math.max(0, a.queue - 1);
    if (i + 1 < path.length - 1) b.queue += 1;
  }
  nodes.forEach((n) => {
    if (n.soc <= cfg.deadThreshold) {
      n.alive = false;
      n.soc = Math.max(0, n.soc);
    }
  });
}

function stepRound() {
  if (!sim) return;
  if (sim.viewRound < sim.round) {
    restoreSnapshot(sim.viewRound + 1);
    return;
  }
  if (sim.round >= sim.cfg.maxRounds) return;
  const { cfg, rng, nodes } = sim;
  const graph = buildGraph(nodes, cfg);
  const { weights, preferred } = fusedWeights(graph, nodes, cfg);
  const transmitter = chooseTransmitter(nodes, cfg);
  const aliveIds = nodes.filter((n) => n.alive).map((n) => n.id);
  const sourceCount = Math.max(1, Math.floor(aliveIds.length * cfg.sourceFraction));
  const shuffled = aliveIds.slice().sort(() => rng.next() - 0.5);
  const sources = shuffled.filter((id) => id !== transmitter).slice(0, sourceCount);
  let delivered = 0;
  let dropped = 0;
  let totalDelay = 0;
  let totalReward = 0;
  const routes = [];
  const eventLines = [];

  sources.forEach((source) => {
    const packets = rng.int(1, 3);
    nodes[source].queue += packets;
    for (let p = 0; p < packets; p++) {
      const path = dijkstra(source, transmitter, graph, (a, b) => {
        const s = stateKey(nodes[a], transmitter, graph, nodes, cfg);
        const qBonus = 0.03 * qGet(sim, a, s, b);
        return Math.max(0.01, (weights.get(edgeKey(a, b)) || 1) - qBonus);
      });

      if (!path) {
        dropped += 1;
        totalReward -= 2;
        continue;
      }

      const delay = packetDelay(path, nodes, cfg);
      const r = reward(path, true, nodes, delay);
      totalDelay += delay;
      totalReward += r;
      delivered += 1;
      routes.push(path);

      for (let i = 0; i < path.length - 1; i++) {
        const a = path[i];
        const b = path[i + 1];
        const s = stateKey(nodes[a], transmitter, graph, nodes, cfg);
        const nextS = stateKey(nodes[b], transmitter, graph, nodes, cfg);
        const old = qGet(sim, a, s, b);
        const nextActions = graph.get(b) || [];
        const future = nextActions.length
          ? Math.max(...nextActions.map((n) => qGet(sim, b, nextS, n)))
          : 0;
        qSet(sim, a, s, b, old + cfg.alpha * (r + cfg.gamma * future - old));
      }

      applyEnergy(path, nodes, cfg);
    }
  });

  const alive = nodes.filter((n) => n.alive).length;
  const socs = nodes.map((n) => n.soc);
  const metric = {
    round: sim.round + 1,
    active: alive,
    avgSoc: avg(socs),
    variance: variance(socs),
    reward: totalReward,
    delay: delivered ? totalDelay / delivered : 0,
    delivered,
    dropped,
    transmitter,
  };
  sim.history.push(metric);
  sim.lastRoutes = routes;
  sim.lastSources = sources;
  sim.lastTransmitter = transmitter;
  sim.lastFusedEdges = preferred;
  sim.round += 1;

  const sampleRoute = routes[0] ? routes[0].join(" → ") : "không tìm được route";
  eventLines.push(`<b>Round ${sim.round}</b>: transmitter = node ${transmitter}.`);
  eventLines.push(`Sources: ${sources.slice(0, 8).join(", ")}${sources.length > 8 ? "..." : ""}.`);
  eventLines.push(`Route mẫu: ${sampleRoute}.`);
  eventLines.push(`Delivered ${delivered}, dropped ${dropped}, active ${alive}/${cfg.nodeCount}.`);
  eventLines.push(`λ=${cfg.lambda.toFixed(2)} ưu tiên MERA; γ=${cfg.gamma.toFixed(2)} coi trọng reward tương lai.`);
  sim.logs.unshift(`<p>${eventLines.join("<br>")}</p>`);
  sim.logs = sim.logs.slice(0, 40);

  storeSnapshot();
  render();
}

function render() {
  if (!sim) return;
  drawNetwork();
  drawChart();
  const latest = sim.history[sim.history.length - 1] || {
    active: sim.nodes.length,
    avgSoc: 100,
    variance: 0,
    delay: 0,
    delivered: 0,
    dropped: 0,
  };
  ui.roundBadge.textContent = sim.viewRound === sim.round
    ? `Round ${sim.round}`
    : `Round ${sim.viewRound}/${sim.round}`;
  ui.prevRoundBtn.disabled = sim.viewRound <= 0;
  ui.stepBtn.textContent = sim.viewRound < sim.round ? "Next round" : "Next round";
  ui.activeNodes.textContent = `${latest.active}/${sim.cfg.nodeCount}`;
  ui.avgSoc.textContent = `${latest.avgSoc.toFixed(1)}%`;
  ui.socVariance.textContent = latest.variance.toFixed(1);
  ui.avgDelay.textContent = `${latest.delay.toFixed(1)} ms`;
  ui.delivered.textContent = latest.delivered;
  ui.dropped.textContent = latest.dropped;
  ui.roundLog.innerHTML = sim.logs.join("");
}

function toCanvas(node) {
  const w = ui.networkCanvas.width;
  const h = ui.networkCanvas.height;
  const half = sim.cfg.area / 2;
  const cx = node.x - half;
  const cy = node.y - half;
  const cz = (node.z || 0) - half;
  const cosY = Math.cos(sim.camera.rotY);
  const sinY = Math.sin(sim.camera.rotY);
  const x1 = cx * cosY + cz * sinY;
  const z1 = -cx * sinY + cz * cosY;
  const cosX = Math.cos(sim.camera.rotX);
  const sinX = Math.sin(sim.camera.rotX);
  const y2 = cy * cosX - z1 * sinX;
  const z2 = cy * sinX + z1 * cosX;
  const viewDistance = 280;
  const perspective = clamp(viewDistance / (viewDistance + z2), 0.55, 1.8);
  const scale = 4.15 * sim.camera.zoom * perspective;
  return {
    x: w / 2 + x1 * scale,
    y: h / 2 - y2 * scale,
    depth: z2,
    scale: perspective,
  };
}

function nodeRole(node) {
  if (!node.alive) return "dead";
  if (node.id === sim.lastTransmitter) return "transmitter / sink";
  if (sim.lastSources.includes(node.id)) return "source";
  if (sim.lastRoutes.some((path) => path.slice(1, -1).includes(node.id))) return "relay";
  return "sensor";
}

function nodeRouteInfo(nodeId) {
  const routeIndexes = [];
  sim.lastRoutes.forEach((path, index) => {
    if (path.includes(nodeId)) routeIndexes.push(index + 1);
  });
  return routeIndexes.length ? `route #${routeIndexes.slice(0, 4).join(", #")}` : "none";
}

function nodeDegree(nodeId) {
  return (buildGraph(sim.nodes, sim.cfg).get(nodeId) || []).length;
}

function showTooltip(node, event) {
  const wrap = ui.networkCanvas.parentElement;
  const rect = wrap.getBoundingClientRect();
  const colorStatus = node.alive ? (node.soc >= 40 ? "alive" : "low energy") : "dead";
  ui.nodeTooltip.innerHTML = [
    `<b>Node ${node.id}</b>`,
    `<span><em>Status</em><strong>${colorStatus}</strong></span>`,
    `<span><em>Role</em><strong>${nodeRole(node)}</strong></span>`,
    `<span><em>SoC</em><strong>${node.soc.toFixed(2)}%</strong></span>`,
    `<span><em>Queue Q_i(t)</em><strong>${node.queue.toFixed(0)} packets</strong></span>`,
    `<span><em>Degree</em><strong>${nodeDegree(node.id)}</strong></span>`,
    `<span><em>Route</em><strong>${nodeRouteInfo(node.id)}</strong></span>`,
    `<span><em>x, y, z</em><strong>${node.x.toFixed(1)}, ${node.y.toFixed(1)}, ${node.z.toFixed(1)}</strong></span>`,
  ].join("");
  ui.nodeTooltip.hidden = false;
  const left = event.clientX - rect.left + 14;
  const top = event.clientY - rect.top + 14;
  ui.nodeTooltip.style.left = `${clamp(left, 8, rect.width - ui.nodeTooltip.offsetWidth - 8)}px`;
  ui.nodeTooltip.style.top = `${clamp(top, 8, rect.height - ui.nodeTooltip.offsetHeight - 8)}px`;
}

function hideTooltip() {
  if (!sim) return;
  sim.camera.hoverNode = null;
  ui.nodeTooltip.hidden = true;
}

function updateHoverFromEvent(event) {
  if (!sim || drag.active) return;
  const rect = ui.networkCanvas.getBoundingClientRect();
  const x = ((event.clientX - rect.left) / rect.width) * ui.networkCanvas.width;
  const y = ((event.clientY - rect.top) / rect.height) * ui.networkCanvas.height;
  let best = null;
  let bestDistance = Infinity;
  sim.nodes.forEach((node) => {
    const p = toCanvas(node);
    const baseRadius = node.id === sim.lastTransmitter ? 12 : sim.lastSources.includes(node.id) ? 10 : 8;
    const hitRadius = Math.max(10, baseRadius * p.scale + 4);
    const d = Math.hypot(p.x - x, p.y - y);
    if (d <= hitRadius && d < bestDistance) {
      best = node;
      bestDistance = d;
    }
  });
  sim.camera.hoverNode = best ? best.id : null;
  if (best) showTooltip(best, event);
  else ui.nodeTooltip.hidden = true;
  drawNetwork();
}

function drawWorldBox() {
  const area = sim.cfg.area;
  const corners = [
    { x: 0, y: 0, z: 0 },
    { x: area, y: 0, z: 0 },
    { x: area, y: area, z: 0 },
    { x: 0, y: area, z: 0 },
    { x: 0, y: 0, z: area },
    { x: area, y: 0, z: area },
    { x: area, y: area, z: area },
    { x: 0, y: area, z: area },
  ].map((p) => toCanvas(p));
  const segments = [
    [0, 1], [1, 2], [2, 3], [3, 0],
    [4, 5], [5, 6], [6, 7], [7, 4],
    [0, 4], [1, 5], [2, 6], [3, 7],
  ];
  ctx.save();
  ctx.strokeStyle = "rgba(148,163,184,0.24)";
  ctx.lineWidth = 1;
  segments.forEach(([a, b]) => {
    ctx.beginPath();
    ctx.moveTo(corners[a].x, corners[a].y);
    ctx.lineTo(corners[b].x, corners[b].y);
    ctx.stroke();
  });

  const origin = toCanvas({ x: 0, y: 0, z: 0 });
  const axes = [
    { label: "x", color: "#0f766e", end: toCanvas({ x: area, y: 0, z: 0 }) },
    { label: "y", color: "#1e3a5f", end: toCanvas({ x: 0, y: area, z: 0 }) },
    { label: "z", color: "#f59e0b", end: toCanvas({ x: 0, y: 0, z: area }) },
  ];
  axes.forEach((axis) => {
    ctx.strokeStyle = axis.color;
    ctx.fillStyle = axis.color;
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(origin.x, origin.y);
    ctx.lineTo(axis.end.x, axis.end.y);
    ctx.stroke();
    ctx.font = "bold 12px Arial";
    ctx.fillText(axis.label, axis.end.x + 5, axis.end.y - 5);
  });
  ctx.restore();
}

function drawNetwork() {
  const canvas = ui.networkCanvas;
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = "#ffffff";
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  ctx.strokeStyle = "#e2e8f0";
  ctx.strokeRect(0.5, 0.5, canvas.width - 1, canvas.height - 1);
  drawWorldBox();

  const graph = buildGraph(sim.nodes, sim.cfg);
  allEdges(graph).map(([a, b]) => ({
    a,
    b,
    depth: (toCanvas(sim.nodes[a]).depth + toCanvas(sim.nodes[b]).depth) / 2,
  })).sort((x, y) => y.depth - x.depth).forEach(({ a, b }) => {
    const pa = toCanvas(sim.nodes[a]);
    const pb = toCanvas(sim.nodes[b]);
    const key = edgeKey(a, b);
    ctx.strokeStyle = sim.lastFusedEdges.has(key) ? "rgba(15,118,110,0.28)" : "rgba(148,163,184,0.20)";
    ctx.lineWidth = sim.lastFusedEdges.has(key) ? 1.6 : 0.7;
    ctx.beginPath();
    ctx.moveTo(pa.x, pa.y);
    ctx.lineTo(pb.x, pb.y);
    ctx.stroke();
  });

  sim.lastRoutes.forEach((path, idx) => {
    ctx.strokeStyle = idx === 0 ? "#dc2626" : "rgba(220,38,38,0.35)";
    ctx.lineWidth = idx === 0 ? 4 : 2;
    ctx.beginPath();
    path.forEach((id, i) => {
      const p = toCanvas(sim.nodes[id]);
      if (i === 0) ctx.moveTo(p.x, p.y);
      else ctx.lineTo(p.x, p.y);
    });
    ctx.stroke();
  });

  sim.nodes.slice().sort((a, b) => toCanvas(b).depth - toCanvas(a).depth).forEach((node) => {
    const p = toCanvas(node);
    const isSource = sim.lastSources.includes(node.id);
    const isTx = node.id === sim.lastTransmitter;
    const isHover = sim.camera.hoverNode === node.id;
    const radius = (isTx ? 8 : isSource ? 6 : 4.5) * p.scale;
    if (isHover) {
      ctx.beginPath();
      ctx.fillStyle = "rgba(30,58,95,0.14)";
      ctx.arc(p.x, p.y, radius + 9, 0, Math.PI * 2);
      ctx.fill();
      ctx.strokeStyle = "#1e3a5f";
      ctx.lineWidth = 2;
      ctx.stroke();
    }
    ctx.beginPath();
    ctx.fillStyle = nodeColor(node);
    ctx.arc(p.x, p.y, radius, 0, Math.PI * 2);
    ctx.fill();
    ctx.lineWidth = isTx ? 3 : 1.3;
    ctx.strokeStyle = isTx ? "#1e3a5f" : "#ffffff";
    ctx.stroke();
    if (isTx) {
      ctx.fillStyle = "#1e3a5f";
      ctx.font = "bold 12px Arial";
      ctx.fillText("TX", p.x + 9, p.y - 8);
    }
    if (isHover && !isTx) {
      ctx.fillStyle = "#1e3a5f";
      ctx.font = "bold 12px Arial";
      ctx.fillText(`#${node.id}`, p.x + 9, p.y - 8);
    }
  });

  ctx.fillStyle = "#17212b";
  ctx.font = "bold 16px Arial";
  ctx.fillText("Topology + route hiện tại", 18, 28);
  ctx.fillStyle = "#64748b";
  ctx.font = "12px Arial";
  ctx.fillText("Node màu theo SoC, cạnh xanh mờ là fused MERA-MST, đường đỏ là route round hiện tại.", 18, 48);
}

function drawChart() {
  const canvas = ui.chartCanvas;
  chartCtx.clearRect(0, 0, canvas.width, canvas.height);
  chartCtx.fillStyle = "#ffffff";
  chartCtx.fillRect(0, 0, canvas.width, canvas.height);
  chartCtx.strokeStyle = "#d8e2ea";
  chartCtx.strokeRect(0.5, 0.5, canvas.width - 1, canvas.height - 1);

  const hist = sim.history;
  if (hist.length < 2) {
    chartCtx.fillStyle = "#64748b";
    chartCtx.font = "13px Arial";
    chartCtx.fillText("Bấm Next round để bắt đầu vẽ biểu đồ.", 24, 48);
    return;
  }

  const series = [
    { name: "active", values: hist.map((h) => h.active), color: "#0f766e" },
    { name: "avg SoC", values: hist.map((h) => h.avgSoc), color: "#1e3a5f" },
    { name: "variance/10", values: hist.map((h) => h.variance / 10), color: "#f59e0b" },
  ];
  const all = series.flatMap((s) => s.values);
  const maxY = Math.max(...all, 1);
  const minY = Math.min(...all, 0);
  const pad = 34;
  const pw = canvas.width - pad * 2;
  const ph = canvas.height - pad * 2;
  chartCtx.strokeStyle = "#e5edf3";
  for (let i = 0; i <= 4; i++) {
    const y = pad + (i / 4) * ph;
    chartCtx.beginPath();
    chartCtx.moveTo(pad, y);
    chartCtx.lineTo(pad + pw, y);
    chartCtx.stroke();
  }
  series.forEach((s, idx) => {
    chartCtx.strokeStyle = s.color;
    chartCtx.lineWidth = 2;
    chartCtx.beginPath();
    s.values.forEach((v, i) => {
      const x = pad + (i / Math.max(s.values.length - 1, 1)) * pw;
      const y = pad + ((maxY - v) / Math.max(maxY - minY, 1)) * ph;
      if (i === 0) chartCtx.moveTo(x, y);
      else chartCtx.lineTo(x, y);
    });
    chartCtx.stroke();
    chartCtx.fillStyle = s.color;
    chartCtx.font = "12px Arial";
    chartCtx.fillText(s.name, pad + idx * 110, canvas.height - 10);
  });
}

function reset() {
  pause();
  if (sim) hideTooltip();
  ui.seed.value = Math.floor(1 + Math.random() * 2147483646);
  ui.commRangeOut.textContent = ui.commRange.value;
  ui.lambdaOut.textContent = Number(ui.lambdaPriority.value).toFixed(2);
  ui.gammaOut.textContent = Number(ui.gamma.value).toFixed(2);
  sim = createSimulation();
  sim.logs.unshift(`<p><b>Reset:</b> random topology created with seed ${sim.cfg.seed}. Press Next round to simulate.</p>`);
  storeSnapshot();
  render();
}
/*
  sim.logs.unshift("<p><b>Reset:</b> mạng WSN mới đã được tạo. Bấm Next round để mô phỏng từng vòng.</p>");
  render();
}

*/
function previousRound() {
  if (!sim || sim.viewRound <= 0) return;
  pause();
  restoreSnapshot(sim.viewRound - 1);
}

function play() {
  if (timer) return;
  ui.modeBadge.textContent = "Playing";
  timer = setInterval(() => {
    if (!sim || (sim.viewRound >= sim.round && sim.round >= sim.cfg.maxRounds)) {
      pause();
      return;
    }
    stepRound();
  }, Number(ui.speed.value));
}

function pause() {
  if (timer) clearInterval(timer);
  timer = null;
  ui.modeBadge.textContent = "Paused";
}

ui.resetBtn.addEventListener("click", reset);
ui.stepBtn.addEventListener("click", stepRound);
ui.prevRoundBtn.addEventListener("click", previousRound);
ui.playBtn.addEventListener("click", play);
ui.pauseBtn.addEventListener("click", pause);
ui.networkCanvas.addEventListener("mousemove", updateHoverFromEvent);
ui.networkCanvas.addEventListener("mouseleave", () => {
  if (!drag.active) {
    hideTooltip();
    drawNetwork();
  }
});
ui.networkCanvas.addEventListener("mousedown", (event) => {
  drag.active = true;
  drag.lastX = event.clientX;
  drag.lastY = event.clientY;
  ui.networkCanvas.parentElement.classList.add("dragging");
  hideTooltip();
});
window.addEventListener("mouseup", () => {
  if (!drag.active) return;
  drag.active = false;
  ui.networkCanvas.parentElement.classList.remove("dragging");
});
window.addEventListener("mousemove", (event) => {
  if (!sim || !drag.active) return;
  const dx = event.clientX - drag.lastX;
  const dy = event.clientY - drag.lastY;
  drag.lastX = event.clientX;
  drag.lastY = event.clientY;
  sim.camera.rotY += dx * 0.01;
  sim.camera.rotX = clamp(sim.camera.rotX + dy * 0.01, -1.35, 1.35);
  drawNetwork();
});
[ui.commRange, ui.lambdaPriority, ui.gamma].forEach((el) => {
  el.addEventListener("input", () => {
    ui.commRangeOut.textContent = ui.commRange.value;
    ui.lambdaOut.textContent = Number(ui.lambdaPriority.value).toFixed(2);
    ui.gammaOut.textContent = Number(ui.gamma.value).toFixed(2);
  });
});

reset();
