#!/usr/bin/env python3
"""
Pure-Python simulator for the WSN MARL + MERA-MST routing idea.

The goal is educational: reproduce the main mechanics in the paper without
requiring ns-3, OMNeT++, NumPy, NetworkX, or Matplotlib.

Outputs:
  - metrics.csv
  - summary.json
  - SVG charts for active nodes, SoC, SoC variance, reward, delay
  - SVG topology snapshots

Example:
  python outputs/wsn_marl_mera_mst_sim.py
  python outputs/wsn_marl_mera_mst_sim.py --nodes 100 --episodes 100 --lambda-priority 0.85
"""

from __future__ import annotations

import argparse
import csv
import heapq
import json
import math
import os
import random
from collections import defaultdict
from dataclasses import dataclass, field
from typing import Dict, Iterable, List, Optional, Sequence, Tuple


Point = Tuple[float, float]
Edge = Tuple[int, int]
Path = List[int]


@dataclass
class NodeState:
    node_id: int
    x: float
    y: float
    soc: float = 100.0
    queue: int = 0
    alive: bool = True


@dataclass
class SimConfig:
    nodes: int = 80
    episodes: int = 80
    area_size: float = 100.0
    comm_range: float = 24.0
    initial_soc: float = 100.0
    dead_threshold: float = 2.0
    source_fraction: float = 0.18
    max_packets_per_source: int = 3
    lambda_priority: float = 0.85
    alpha: float = 0.35
    gamma: float = 0.88
    epsilon_start: float = 0.25
    epsilon_end: float = 0.04
    tx_base_cost: float = 0.10
    tx_distance_cost: float = 0.010
    rx_cost: float = 0.045
    compute_cost: float = 0.010
    queue_service_rate: float = 8.0
    packet_bits: float = 1024.0
    data_rate_bps: float = 250_000.0
    processing_delay_ms: float = 1.5
    local_decision_delay_ms: float = 1.8
    cloud_extra_delay_ms: float = 8.0
    seed: int = 42
    use_cloud_delay: bool = False


@dataclass
class EpisodeMetrics:
    episode: int
    method: str
    active_nodes: int
    eliminated_nodes: int
    avg_soc: float
    soc_variance: float
    total_reward: float
    avg_delay_ms: float
    delivered_packets: int
    dropped_packets: int
    transmitter: int


@dataclass
class QLearner:
    alpha: float
    gamma: float
    values: Dict[Tuple[int, Tuple[int, int, int, int], int], float] = field(
        default_factory=lambda: defaultdict(float)
    )

    def get(self, node: int, state: Tuple[int, int, int, int], action: int) -> float:
        return self.values[(node, state, action)]

    def update(
        self,
        node: int,
        state: Tuple[int, int, int, int],
        action: int,
        reward: float,
        next_state: Tuple[int, int, int, int],
        next_actions: Sequence[int],
    ) -> None:
        old = self.get(node, state, action)
        future = max((self.get(node, next_state, a) for a in next_actions), default=0.0)
        self.values[(node, state, action)] = old + self.alpha * (
            reward + self.gamma * future - old
        )


def clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


def distance(a: NodeState, b: NodeState) -> float:
    return math.hypot(a.x - b.x, a.y - b.y)


def variance(values: Sequence[float]) -> float:
    if not values:
        return 0.0
    mean = sum(values) / len(values)
    return sum((v - mean) ** 2 for v in values) / len(values)


def energy_bin(soc: float) -> int:
    if soc < 20:
        return 0
    if soc < 40:
        return 1
    if soc < 60:
        return 2
    if soc < 80:
        return 3
    return 4


def queue_bin(queue: int) -> int:
    if queue <= 1:
        return 0
    if queue <= 4:
        return 1
    return 2


def hop_bin(d: float, comm_range: float) -> int:
    estimate = d / max(comm_range, 1e-9)
    if estimate < 1.0:
        return 0
    if estimate < 2.5:
        return 1
    return 2


def generate_nodes(cfg: SimConfig, rng: random.Random) -> List[NodeState]:
    return [
        NodeState(
            node_id=i,
            x=rng.uniform(0, cfg.area_size),
            y=rng.uniform(0, cfg.area_size),
            soc=cfg.initial_soc,
        )
        for i in range(cfg.nodes)
    ]


def clone_nodes(nodes: Sequence[NodeState]) -> List[NodeState]:
    return [
        NodeState(
            node_id=n.node_id,
            x=n.x,
            y=n.y,
            soc=n.soc,
            queue=n.queue,
            alive=n.alive,
        )
        for n in nodes
    ]


def build_base_graph(nodes: Sequence[NodeState], cfg: SimConfig) -> Dict[int, List[int]]:
    graph: Dict[int, List[int]] = {n.node_id: [] for n in nodes if n.alive}
    alive = [n for n in nodes if n.alive]
    for i, a in enumerate(alive):
        for b in alive[i + 1 :]:
            d = distance(a, b)
            if d <= cfg.comm_range:
                # Link quality is a simple normalized proxy for SNR.
                link_quality = 1.0 - d / cfg.comm_range
                if link_quality >= 0.05:
                    graph[a.node_id].append(b.node_id)
                    graph[b.node_id].append(a.node_id)
    return graph


def edge_key(a: int, b: int) -> Edge:
    return (a, b) if a < b else (b, a)


def all_edges(graph: Dict[int, List[int]]) -> List[Edge]:
    edges = set()
    for a, nbrs in graph.items():
        for b in nbrs:
            edges.add(edge_key(a, b))
    return sorted(edges)


class DisjointSet:
    def __init__(self, items: Iterable[int]):
        self.parent = {x: x for x in items}
        self.rank = {x: 0 for x in items}

    def find(self, x: int) -> int:
        while self.parent[x] != x:
            self.parent[x] = self.parent[self.parent[x]]
            x = self.parent[x]
        return x

    def union(self, a: int, b: int) -> bool:
        ra, rb = self.find(a), self.find(b)
        if ra == rb:
            return False
        if self.rank[ra] < self.rank[rb]:
            ra, rb = rb, ra
        self.parent[rb] = ra
        if self.rank[ra] == self.rank[rb]:
            self.rank[ra] += 1
        return True


def minimum_spanning_tree_edges(
    graph: Dict[int, List[int]], nodes: Sequence[NodeState]
) -> set[Edge]:
    node_map = {n.node_id: n for n in nodes}
    weighted = []
    for a, b in all_edges(graph):
        weighted.append((distance(node_map[a], node_map[b]), a, b))
    weighted.sort()
    ds = DisjointSet(graph.keys())
    mst = set()
    for _, a, b in weighted:
        if ds.union(a, b):
            mst.add(edge_key(a, b))
    return mst


def fused_edge_weights(
    graph: Dict[int, List[int]], nodes: Sequence[NodeState], cfg: SimConfig
) -> Dict[Edge, float]:
    node_map = {n.node_id: n for n in nodes}
    edges = all_edges(graph)
    if not edges:
        return {}

    mst = minimum_spanning_tree_edges(graph, nodes)
    max_dist = max(distance(node_map[a], node_map[b]) for a, b in edges) or 1.0

    weights: Dict[Edge, float] = {}
    for a, b in edges:
        na, nb = node_map[a], node_map[b]
        # MERA: entering a low-SoC destination is expensive. Average both
        # directions to keep undirected graph weights stable.
        mera_ab = 1.0 / (nb.soc / 100.0 + 0.05)
        mera_ba = 1.0 / (na.soc / 100.0 + 0.05)
        mera = (mera_ab + mera_ba) / 2.0
        mera_norm = mera / 20.0

        dist_norm = distance(na, nb) / max_dist
        mst_penalty = 0.0 if edge_key(a, b) in mst else 0.75
        mst_weight = dist_norm + mst_penalty

        weights[edge_key(a, b)] = (
            cfg.lambda_priority * mera_norm
            + (1.0 - cfg.lambda_priority) * mst_weight
        )
    return weights


def dijkstra_path(
    start: int,
    target: int,
    graph: Dict[int, List[int]],
    edge_cost,
) -> Optional[Path]:
    if start == target:
        return [start]
    heap = [(0.0, start, [start])]
    seen = set()
    while heap:
        cost, node, path = heapq.heappop(heap)
        if node in seen:
            continue
        seen.add(node)
        if node == target:
            return path
        for nbr in graph.get(node, []):
            if nbr not in seen:
                heapq.heappush(heap, (cost + edge_cost(node, nbr), nbr, path + [nbr]))
    return None


def choose_transmitter(nodes: Sequence[NodeState], cfg: SimConfig) -> int:
    sink = (cfg.area_size / 2.0, cfg.area_size / 2.0)

    def score(n: NodeState) -> float:
        dist_to_sink = math.hypot(n.x - sink[0], n.y - sink[1])
        return n.soc - 0.12 * dist_to_sink - 0.8 * n.queue

    alive = [n for n in nodes if n.alive]
    return max(alive, key=score).node_id


def neighbor_energy_bin(
    node_id: int, graph: Dict[int, List[int]], node_map: Dict[int, NodeState]
) -> int:
    nbrs = graph.get(node_id, [])
    if not nbrs:
        return 0
    avg = sum(node_map[n].soc for n in nbrs) / len(nbrs)
    return energy_bin(avg)


def get_state(
    node_id: int,
    transmitter: int,
    nodes: Sequence[NodeState],
    graph: Dict[int, List[int]],
    cfg: SimConfig,
) -> Tuple[int, int, int, int]:
    node_map = {n.node_id: n for n in nodes}
    n = node_map[node_id]
    tx = node_map[transmitter]
    return (
        energy_bin(n.soc),
        queue_bin(n.queue),
        hop_bin(distance(n, tx), cfg.comm_range),
        neighbor_energy_bin(node_id, graph, node_map),
    )


def proposed_path(
    source: int,
    transmitter: int,
    nodes: Sequence[NodeState],
    graph: Dict[int, List[int]],
    fused_weights: Dict[Edge, float],
    qlearner: QLearner,
    cfg: SimConfig,
    rng: random.Random,
    epsilon: float,
) -> Optional[List[Tuple[int, Tuple[int, int, int, int], int]]]:
    if source == transmitter:
        return []

    node_map = {n.node_id: n for n in nodes}
    current = source
    visited = {source}
    transitions: List[Tuple[int, Tuple[int, int, int, int], int]] = []
    max_steps = max(4, len(graph) // 3)

    for _ in range(max_steps):
        if current == transmitter:
            return transitions

        state = get_state(current, transmitter, nodes, graph, cfg)
        candidates = [n for n in graph.get(current, []) if n not in visited]
        if not candidates:
            return None

        # Favor neighbors that make progress toward transmitter and have a
        # reasonable fused MERA-MST edge cost.
        tx = node_map[transmitter]

        def action_score(nbr: int) -> float:
            current_dist = distance(node_map[current], tx)
            next_dist = distance(node_map[nbr], tx)
            progress = (current_dist - next_dist) / max(cfg.comm_range, 1e-9)
            fused = fused_weights.get(edge_key(current, nbr), 1.0)
            q_value = qlearner.get(current, state, nbr)
            congestion = node_map[nbr].queue * 0.04
            return q_value + 0.9 * progress - 0.8 * fused - congestion

        if rng.random() < epsilon:
            ranked = sorted(candidates, key=lambda n: fused_weights.get(edge_key(current, n), 1.0))
            action = rng.choice(ranked[: min(5, len(ranked))])
        else:
            action = max(candidates, key=action_score)

        transitions.append((current, state, action))
        visited.add(action)
        current = action

    return transitions if current == transmitter else None


def transitions_to_path(source: int, transitions: Sequence[Tuple[int, tuple, int]]) -> Path:
    if not transitions:
        return [source]
    path = [source]
    for _, _, nxt in transitions:
        path.append(nxt)
    return path


def path_soc_variance(path: Sequence[int], node_map: Dict[int, NodeState]) -> float:
    return variance([node_map[n].soc for n in path])


def packet_delay_ms(
    path: Sequence[int], node_map: Dict[int, NodeState], cfg: SimConfig
) -> float:
    if len(path) <= 1:
        return 0.0
    tx_delay = (cfg.packet_bits / cfg.data_rate_bps) * 1000.0
    total = 0.0
    for node in path[:-1]:
        q_delay = (node_map[node].queue / max(cfg.queue_service_rate, 1e-9)) * 1000.0
        total += tx_delay + cfg.processing_delay_ms + q_delay
    total += cfg.cloud_extra_delay_ms if cfg.use_cloud_delay else cfg.local_decision_delay_ms
    return total


def apply_path_energy(
    path: Sequence[int], nodes: Sequence[NodeState], cfg: SimConfig
) -> None:
    node_map = {n.node_id: n for n in nodes}
    for a, b in zip(path, path[1:]):
        d = distance(node_map[a], node_map[b])
        tx_cost = cfg.tx_base_cost + cfg.tx_distance_cost * d + cfg.compute_cost
        rx_cost = cfg.rx_cost
        node_map[a].soc -= tx_cost
        node_map[b].soc -= rx_cost
        node_map[a].queue = max(0, node_map[a].queue - 1)
        node_map[b].queue += 1 if b != path[-1] else 0
    # The transmitter aggregates/flushes received data.
    if path:
        node_map[path[-1]].queue = max(0, node_map[path[-1]].queue - 1)
    for n in nodes:
        if n.soc <= cfg.dead_threshold:
            n.alive = False
            n.soc = max(0.0, n.soc)


def reward_for_path(
    path: Sequence[int],
    delivered: bool,
    node_map: Dict[int, NodeState],
    delay_ms: float,
) -> float:
    if not delivered or not path:
        return -2.0
    socs = [node_map[n].soc for n in path]
    avg_soc = sum(socs) / len(socs)
    soc_var = variance(socs)
    hops = max(0, len(path) - 1)
    queue_penalty = sum(node_map[n].queue for n in path) / max(len(path), 1)
    return (
        2.5
        + 1.2 * (avg_soc / 100.0)
        - 0.012 * soc_var
        - 0.08 * hops
        - 0.03 * queue_penalty
        - 0.002 * delay_ms
    )


def run_method(
    method: str,
    initial_nodes: Sequence[NodeState],
    traffic_plan: Sequence[List[Tuple[int, int]]],
    cfg: SimConfig,
    rng: random.Random,
) -> Tuple[List[EpisodeMetrics], List[NodeState], QLearner]:
    nodes = clone_nodes(initial_nodes)
    qlearner = QLearner(alpha=cfg.alpha, gamma=cfg.gamma)
    metrics: List[EpisodeMetrics] = []

    for ep in range(cfg.episodes):
        alive_nodes = [n for n in nodes if n.alive]
        if not alive_nodes:
            break

        graph = build_base_graph(nodes, cfg)
        if not graph:
            break
        fused = fused_edge_weights(graph, nodes, cfg)
        node_map = {n.node_id: n for n in nodes}
        transmitter = choose_transmitter(nodes, cfg)
        epsilon = cfg.epsilon_end + (cfg.epsilon_start - cfg.epsilon_end) * (
            1.0 - ep / max(cfg.episodes - 1, 1)
        )

        total_reward = 0.0
        delays: List[float] = []
        delivered = 0
        dropped = 0

        for source, packets in traffic_plan[ep]:
            if source not in graph or not node_map[source].alive:
                dropped += packets
                continue
            node_map[source].queue += packets

            for _ in range(packets):
                if not node_map[source].alive:
                    dropped += 1
                    continue

                if method == "proposed":
                    # Use the fused MERA-MST graph as the reliable routing
                    # substrate. Q-values slightly reduce the cost of actions
                    # that were rewarded in earlier episodes.
                    def learned_fused_cost(a: int, b: int) -> float:
                        st = get_state(a, transmitter, nodes, graph, cfg)
                        q_bonus = 0.03 * qlearner.get(a, st, b)
                        return max(0.01, fused.get(edge_key(a, b), 1.0) - q_bonus)

                    path = dijkstra_path(source, transmitter, graph, learned_fused_cost)
                    if path is None:
                        dropped += 1
                        total_reward -= 2.0
                        continue
                    transitions = [
                        (a, get_state(a, transmitter, nodes, graph, cfg), b)
                        for a, b in zip(path, path[1:])
                    ]
                else:
                    path = dijkstra_path(
                        source,
                        transmitter,
                        graph,
                        lambda a, b: distance(node_map[a], node_map[b]),
                    )
                    if path is None:
                        dropped += 1
                        total_reward -= 1.5
                        continue
                    transitions = []

                delay_ms = packet_delay_ms(path, node_map, cfg)
                rew = reward_for_path(path, True, node_map, delay_ms)
                total_reward += rew
                delays.append(delay_ms)
                delivered += 1

                apply_path_energy(path, nodes, cfg)

                if method == "proposed":
                    for current, state, action in transitions:
                        next_state = get_state(action, transmitter, nodes, graph, cfg)
                        next_actions = graph.get(action, [])
                        qlearner.update(current, state, action, rew, next_state, next_actions)

        soc_values = [n.soc for n in nodes]
        active = sum(1 for n in nodes if n.alive)
        metrics.append(
            EpisodeMetrics(
                episode=ep,
                method=method,
                active_nodes=active,
                eliminated_nodes=cfg.nodes - active,
                avg_soc=sum(soc_values) / len(soc_values),
                soc_variance=variance(soc_values),
                total_reward=total_reward,
                avg_delay_ms=sum(delays) / len(delays) if delays else 0.0,
                delivered_packets=delivered,
                dropped_packets=dropped,
                transmitter=transmitter,
            )
        )

    return metrics, nodes, qlearner


def make_traffic_plan(
    cfg: SimConfig, initial_nodes: Sequence[NodeState], rng: random.Random
) -> List[List[Tuple[int, int]]]:
    node_ids = [n.node_id for n in initial_nodes]
    source_count = max(1, int(cfg.nodes * cfg.source_fraction))
    plan: List[List[Tuple[int, int]]] = []
    for _ in range(cfg.episodes):
        sources = rng.sample(node_ids, min(source_count, len(node_ids)))
        plan.append(
            [(s, rng.randint(1, cfg.max_packets_per_source)) for s in sources]
        )
    return plan


def write_metrics_csv(path_out: str, rows: Sequence[EpisodeMetrics]) -> None:
    with open(path_out, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(EpisodeMetrics.__annotations__.keys()))
        writer.writeheader()
        for row in rows:
            writer.writerow(row.__dict__)


def svg_header(width: int, height: int) -> str:
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}">\n'
        '<rect width="100%" height="100%" fill="#f8fafc"/>\n'
    )


def svg_text(x: float, y: float, text: str, size: int = 12, fill: str = "#17212b") -> str:
    safe = (
        text.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
    )
    return f'<text x="{x:.1f}" y="{y:.1f}" font-family="Arial" font-size="{size}" fill="{fill}">{safe}</text>\n'


def line_chart_svg(
    path_out: str,
    title: str,
    series: Dict[str, Sequence[float]],
    width: int = 900,
    height: int = 420,
) -> None:
    margin = 58
    plot_w = width - 2 * margin
    plot_h = height - 2 * margin
    max_len = max((len(v) for v in series.values()), default=1)
    all_values = [x for values in series.values() for x in values]
    ymin = min(all_values) if all_values else 0.0
    ymax = max(all_values) if all_values else 1.0
    if abs(ymax - ymin) < 1e-9:
        ymax = ymin + 1.0
    pad = (ymax - ymin) * 0.08
    ymin -= pad
    ymax += pad

    def sx(i: int) -> float:
        return margin + (i / max(max_len - 1, 1)) * plot_w

    def sy(v: float) -> float:
        return margin + (ymax - v) / (ymax - ymin) * plot_h

    palette = ["#0f766e", "#dc2626", "#1e3a5f", "#f59e0b"]
    svg = [svg_header(width, height), svg_text(28, 30, title, 20, "#17212b")]
    svg.append(
        f'<rect x="{margin}" y="{margin}" width="{plot_w}" height="{plot_h}" fill="white" stroke="#d8e2ea"/>\n'
    )
    for t in range(5):
        y = margin + t * plot_h / 4
        value = ymax - t * (ymax - ymin) / 4
        svg.append(f'<line x1="{margin}" y1="{y:.1f}" x2="{margin+plot_w}" y2="{y:.1f}" stroke="#e5edf3"/>\n')
        svg.append(svg_text(8, y + 4, f"{value:.1f}", 11, "#64748b"))

    for idx, (name, values) in enumerate(series.items()):
        color = palette[idx % len(palette)]
        pts = " ".join(f"{sx(i):.1f},{sy(v):.1f}" for i, v in enumerate(values))
        svg.append(f'<polyline points="{pts}" fill="none" stroke="{color}" stroke-width="2.5"/>\n')
        for i, v in enumerate(values[:: max(1, len(values) // 18)]):
            actual_i = i * max(1, len(values) // 18)
            svg.append(f'<circle cx="{sx(actual_i):.1f}" cy="{sy(v):.1f}" r="2.4" fill="{color}"/>\n')
        legend_x = margin + idx * 180
        svg.append(f'<rect x="{legend_x}" y="{height-34}" width="14" height="4" fill="{color}"/>\n')
        svg.append(svg_text(legend_x + 20, height - 28, name, 12, "#334155"))

    svg.append(svg_text(width - 100, height - 8, "episode", 11, "#64748b"))
    svg.append("</svg>\n")
    with open(path_out, "w", encoding="utf-8") as f:
        f.write("".join(svg))


def topology_svg(
    path_out: str,
    title: str,
    nodes: Sequence[NodeState],
    cfg: SimConfig,
    width: int = 620,
    height: int = 620,
) -> None:
    margin = 42
    scale_x = (width - 2 * margin) / cfg.area_size
    scale_y = (height - 2 * margin) / cfg.area_size
    graph = build_base_graph(nodes, cfg)

    def px(n: NodeState) -> float:
        return margin + n.x * scale_x

    def py(n: NodeState) -> float:
        return height - margin - n.y * scale_y

    def color(n: NodeState) -> str:
        if not n.alive:
            return "#111827"
        if n.soc >= 70:
            return "#16a34a"
        if n.soc >= 40:
            return "#f59e0b"
        return "#dc2626"

    node_map = {n.node_id: n for n in nodes}
    svg = [svg_header(width, height), svg_text(24, 28, title, 18, "#17212b")]
    for a, nbrs in graph.items():
        for b in nbrs:
            if a < b:
                svg.append(
                    f'<line x1="{px(node_map[a]):.1f}" y1="{py(node_map[a]):.1f}" '
                    f'x2="{px(node_map[b]):.1f}" y2="{py(node_map[b]):.1f}" '
                    'stroke="#cbd5e1" stroke-width="0.7" opacity="0.45"/>\n'
                )
    for n in nodes:
        svg.append(
            f'<circle cx="{px(n):.1f}" cy="{py(n):.1f}" r="4.2" fill="{color(n)}" '
            'stroke="white" stroke-width="1"/>\n'
        )
    svg.append(svg_text(24, height - 18, "green>=70%, amber>=40%, red<40%, black=dead", 11, "#64748b"))
    svg.append("</svg>\n")
    with open(path_out, "w", encoding="utf-8") as f:
        f.write("".join(svg))


def save_summary(
    path_out: str,
    cfg: SimConfig,
    proposed: Sequence[EpisodeMetrics],
    baseline: Sequence[EpisodeMetrics],
) -> None:
    def last(rows: Sequence[EpisodeMetrics]) -> Dict[str, float]:
        if not rows:
            return {}
        r = rows[-1]
        return {
            "active_nodes": r.active_nodes,
            "eliminated_nodes": r.eliminated_nodes,
            "avg_soc": round(r.avg_soc, 3),
            "soc_variance": round(r.soc_variance, 3),
            "avg_delay_ms": round(r.avg_delay_ms, 3),
            "delivered_packets": sum(x.delivered_packets for x in rows),
            "dropped_packets": sum(x.dropped_packets for x in rows),
        }

    data = {
        "config": cfg.__dict__,
        "final_proposed": last(proposed),
        "final_baseline": last(baseline),
        "note": "Educational pure-Python approximation of MARL + MERA-MST WSN routing.",
    }
    with open(path_out, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)


def run_simulation(cfg: SimConfig, out_dir: str) -> None:
    os.makedirs(out_dir, exist_ok=True)
    rng = random.Random(cfg.seed)
    initial_nodes = generate_nodes(cfg, rng)
    traffic_rng = random.Random(cfg.seed + 999)
    traffic_plan = make_traffic_plan(cfg, initial_nodes, traffic_rng)

    proposed_metrics, proposed_nodes, _ = run_method(
        "proposed", initial_nodes, traffic_plan, cfg, random.Random(cfg.seed + 1)
    )
    baseline_metrics, baseline_nodes, _ = run_method(
        "baseline", initial_nodes, traffic_plan, cfg, random.Random(cfg.seed + 2)
    )

    rows = list(proposed_metrics) + list(baseline_metrics)
    write_metrics_csv(os.path.join(out_dir, "metrics.csv"), rows)
    save_summary(os.path.join(out_dir, "summary.json"), cfg, proposed_metrics, baseline_metrics)

    def series(metric_name: str) -> Dict[str, List[float]]:
        return {
            "proposed MARL+MERA-MST": [getattr(r, metric_name) for r in proposed_metrics],
            "baseline shortest-path": [getattr(r, metric_name) for r in baseline_metrics],
        }

    line_chart_svg(os.path.join(out_dir, "active_nodes.svg"), "Active nodes per episode", series("active_nodes"))
    line_chart_svg(os.path.join(out_dir, "avg_soc.svg"), "Average SoC per episode", series("avg_soc"))
    line_chart_svg(os.path.join(out_dir, "soc_variance.svg"), "SoC variance per episode", series("soc_variance"))
    line_chart_svg(os.path.join(out_dir, "reward.svg"), "Total reward per episode", series("total_reward"))
    line_chart_svg(os.path.join(out_dir, "delay.svg"), "Average delay per episode (ms)", series("avg_delay_ms"))
    topology_svg(os.path.join(out_dir, "topology_initial.svg"), "Initial topology", initial_nodes, cfg)
    topology_svg(os.path.join(out_dir, "topology_final_proposed.svg"), "Final topology - proposed", proposed_nodes, cfg)
    topology_svg(os.path.join(out_dir, "topology_final_baseline.svg"), "Final topology - baseline", baseline_nodes, cfg)

    print(f"Simulation complete. Results written to: {out_dir}")
    print(json.dumps({
        "proposed_final": proposed_metrics[-1].__dict__ if proposed_metrics else {},
        "baseline_final": baseline_metrics[-1].__dict__ if baseline_metrics else {},
    }, indent=2))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="WSN MARL + MERA-MST pure-Python simulator")
    parser.add_argument("--nodes", type=int, default=80)
    parser.add_argument("--episodes", type=int, default=80)
    parser.add_argument("--area-size", type=float, default=100.0)
    parser.add_argument("--comm-range", type=float, default=24.0)
    parser.add_argument("--lambda-priority", type=float, default=0.85, help="lambda in MERA-MST fusion")
    parser.add_argument("--alpha", type=float, default=0.35, help="Q-learning learning rate")
    parser.add_argument("--gamma", type=float, default=0.88, help="Q-learning future reward discount")
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--cloud-delay", action="store_true", help="add cloud-style decision delay")
    parser.add_argument(
        "--output",
        default=os.path.join("outputs", "wsn_simulation_results"),
        help="directory for CSV, JSON, and SVG outputs",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    cfg = SimConfig(
        nodes=args.nodes,
        episodes=args.episodes,
        area_size=args.area_size,
        comm_range=args.comm_range,
        lambda_priority=clamp(args.lambda_priority, 0.0, 1.0),
        alpha=args.alpha,
        gamma=clamp(args.gamma, 0.0, 0.999),
        seed=args.seed,
        use_cloud_delay=args.cloud_delay,
    )
    run_simulation(cfg, args.output)


if __name__ == "__main__":
    main()
