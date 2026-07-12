#include "WsnSimulation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <sstream>

namespace marlwsn {

Define_Module(SensorNode);
Define_Module(WsnSink);
Define_Module(WsnController);

void SensorNode::initialize()
{
    controller = check_and_cast<WsnController *>(getParentModule()->getSubmodule("controller"));
    initialEnergy = par("initialEnergy").doubleValue();
    energy = initialEnergy;
    alive = true;
    role = SENSOR;
    forwardingCount = 0;
    availableAt = SIMTIME_ZERO;
}

void SensorNode::handleMessage(cMessage *message)
{
    controller->handleAtNode(this, check_and_cast<WsnPacket *>(message));
}

void WsnSink::initialize()
{
    controller = check_and_cast<WsnController *>(getParentModule()->getSubmodule("controller"));
}

void WsnSink::handleMessage(cMessage *message)
{
    controller->handleAtSink(check_and_cast<WsnPacket *>(message));
}

WsnController::~WsnController()
{
    if (episodeTimer != nullptr)
        cancelAndDelete(episodeTimer);
}

void WsnController::initialize(int stage)
{
    if (stage == 0) {
        readParameters();
        episodeTimer = new cMessage("episodeTimer");
        meanSocVector.setName("meanSoC");
        socVarianceVector.setName("SoCVariance");
        aliveNodesVector.setName("aliveNodes");
        deliveryRatioVector.setName("deliveryRatio");
        delayVector.setName("meanEndToEndDelay");
        hopCountVector.setName("meanHopCount");
        rewardVector.setName("totalReward");
        epsilonVector.setName("epsilon");
    }
    else {
        collectModules();
        placeNodes();
        buildPhysicalGraph();
        qTables.resize(numNodes);
        createFigures();
        drawPhysicalLinks();
        updateAllNodeDisplays();
        scheduleAt(SIMTIME_ZERO, episodeTimer);
    }
}

void WsnController::readParameters()
{
    numNodes = getParentModule()->par("numNodes").intValue();
    numEpisodes = par("numEpisodes").intValue();
    activeSourcesPerEpisode = par("activeSourcesPerEpisode").intValue();
    maxHops = par("maxHops").intValue();
    extraCandidateNeighbors = par("extraCandidateNeighbors").intValue();
    packetBits = par("packetBits").intValue();
    areaWidth = par("areaWidth").doubleValue();
    areaHeight = par("areaHeight").doubleValue();
    areaDepth = par("areaDepth").doubleValue();
    communicationRange = par("communicationRange").doubleValue();
    initialEnergy = par("initialEnergy").doubleValue();
    sensingEnergyCost = par("sensingEnergyCost").doubleValue();
    multiHopEnergyCost = par("multiHopEnergyCost").doubleValue();
    sinkEnergyCost = par("sinkEnergyCost").doubleValue();
    localDecisionEnergyCost = par("localDecisionEnergyCost").doubleValue();
    bitRate = par("bitRate").doubleValue();
    propagationSpeed = par("propagationSpeed").doubleValue();
    minimumSignalIntegrity = par("minimumSignalIntegrity").doubleValue();
    maxAttenuationPerHop = par("maxAttenuationPerHop").doubleValue();
    learningRate = par("learningRate").doubleValue();
    discountFactor = par("discountFactor").doubleValue();
    initialEpsilon = par("initialEpsilon").doubleValue();
    minimumEpsilon = par("minimumEpsilon").doubleValue();
    epsilonDecay = par("epsilonDecay").doubleValue();
    fusionLambda = par("fusionLambda").doubleValue();
    qWeight = par("qWeight").doubleValue();
    hotspotWeight = par("hotspotWeight").doubleValue();
    clusterHeadFraction = par("clusterHeadFraction").doubleValue();
    cloudMode = par("cloudMode").boolValue();
    routingMode = par("routingMode").stdstringValue();
    placementMode = par("placementMode").stdstringValue();
    episodeLength = par("episodeLength");
    processingDelay = par("processingDelay");
    localDecisionDelay = par("localDecisionDelay");
    cloudDecisionDelay = par("cloudDecisionDelay");
    leachSetupDelay = par("leachSetupDelay");
    sourceStagger = par("sourceStagger");

    if (numNodes <= 1 || numEpisodes <= 0 || communicationRange <= 0 || bitRate <= 0)
        throw cRuntimeError("Invalid WSN configuration: nodes, episodes, range, and bitrate must be positive");
    if (routingMode != "marl" && routingMode != "spmh" && routingMode != "leach")
        throw cRuntimeError("Unknown routingMode '%s' (expected marl, spmh, or leach)", routingMode.c_str());
}

void WsnController::collectModules()
{
    nodes.reserve(numNodes);
    for (int i = 0; i < numNodes; ++i)
        nodes.push_back(check_and_cast<SensorNode *>(getParentModule()->getSubmodule("node", i)));
    sink = check_and_cast<WsnSink *>(getParentModule()->getSubmodule("sink"));
}

static double clampValue(double value, double low, double high)
{
    return std::max(low, std::min(value, high));
}

void WsnController::placeNodes()
{
    if (placementMode == "random") {
        for (auto *node : nodes) {
            node->x = uniform(0, areaWidth);
            node->y = uniform(0, areaHeight);
            node->z = areaDepth > 0 ? uniform(0, areaDepth) : 0;
        }
    }
    else if (areaDepth > 0) {
        int side = static_cast<int>(std::ceil(std::cbrt(static_cast<double>(numNodes))));
        double cellX = areaWidth / side;
        double cellY = areaHeight / side;
        double cellZ = areaDepth / side;
        for (int i = 0; i < numNodes; ++i) {
            int ix = i % side;
            int iy = (i / side) % side;
            int iz = i / (side * side);
            nodes[i]->x = clampValue((ix + 0.5) * cellX + uniform(-0.18 * cellX, 0.18 * cellX), 0, areaWidth);
            nodes[i]->y = clampValue((iy + 0.5) * cellY + uniform(-0.18 * cellY, 0.18 * cellY), 0, areaHeight);
            nodes[i]->z = clampValue((iz + 0.5) * cellZ + uniform(-0.18 * cellZ, 0.18 * cellZ), 0, areaDepth);
        }
    }
    else {
        int columns = static_cast<int>(std::ceil(std::sqrt(numNodes * areaWidth / areaHeight)));
        int rows = static_cast<int>(std::ceil(static_cast<double>(numNodes) / columns));
        double cellX = areaWidth / columns;
        double cellY = areaHeight / rows;
        for (int i = 0; i < numNodes; ++i) {
            int column = i % columns;
            int row = i / columns;
            nodes[i]->x = clampValue((column + 0.5) * cellX + uniform(-0.22 * cellX, 0.22 * cellX), 0, areaWidth);
            nodes[i]->y = clampValue((row + 0.5) * cellY + uniform(-0.22 * cellY, 0.22 * cellY), 0, areaHeight);
            nodes[i]->z = 0;
        }
    }

    for (auto *node : nodes) {
        cFigure::Point point = displayPoint(node->id());
        node->getDisplayString().setTagArg("p", 0, point.x);
        node->getDisplayString().setTagArg("p", 1, point.y);
    }
}

double WsnController::distance(int first, int second) const
{
    double dx = nodes[first]->x - nodes[second]->x;
    double dy = nodes[first]->y - nodes[second]->y;
    double dz = nodes[first]->z - nodes[second]->z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

double WsnController::distanceToSink(int nodeId) const
{
    double sinkX = areaWidth / 2.0;
    double sinkY = areaHeight * 1.08;
    double sinkZ = areaDepth / 2.0;
    double dx = nodes[nodeId]->x - sinkX;
    double dy = nodes[nodeId]->y - sinkY;
    double dz = nodes[nodeId]->z - sinkZ;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

std::pair<int, int> WsnController::edgeKey(int first, int second) const
{
    return first < second ? std::make_pair(first, second) : std::make_pair(second, first);
}

void WsnController::buildPhysicalGraph()
{
    adjacency.assign(numNodes, {});
    for (int i = 0; i < numNodes; ++i) {
        for (int j = i + 1; j < numNodes; ++j) {
            if (distance(i, j) <= communicationRange) {
                adjacency[i].push_back(j);
                adjacency[j].push_back(i);
            }
        }
    }
}

void WsnController::buildMstAndCandidateGraph()
{
    mstEdges.clear();
    candidateEdges.clear();
    std::vector<bool> inTree(numNodes, false);

    for (int componentStart = 0; componentStart < numNodes; ++componentStart) {
        if (!nodes[componentStart]->alive || inTree[componentStart])
            continue;
        std::vector<double> best(numNodes, std::numeric_limits<double>::infinity());
        std::vector<int> parent(numNodes, -1);
        best[componentStart] = 0;

        while (true) {
            int selected = -1;
            double selectedCost = std::numeric_limits<double>::infinity();
            for (int i = 0; i < numNodes; ++i) {
                if (nodes[i]->alive && !inTree[i] && best[i] < selectedCost) {
                    selected = i;
                    selectedCost = best[i];
                }
            }
            if (selected < 0 || !std::isfinite(selectedCost))
                break;

            inTree[selected] = true;
            if (parent[selected] >= 0)
                mstEdges.insert(edgeKey(selected, parent[selected]));

            for (int neighbor : adjacency[selected]) {
                if (!nodes[neighbor]->alive || inTree[neighbor])
                    continue;
                double energyFactor = 2.0 - nodes[neighbor]->energy / initialEnergy;
                double weight = distance(selected, neighbor) * energyFactor;
                if (weight < best[neighbor]) {
                    best[neighbor] = weight;
                    parent[neighbor] = selected;
                }
            }
        }
    }

    candidateEdges = mstEdges;
    for (int i = 0; i < numNodes; ++i) {
        if (!nodes[i]->alive)
            continue;
        std::vector<std::pair<double, int>> ranked;
        for (int neighbor : adjacency[i]) {
            if (!nodes[neighbor]->alive)
                continue;
            double residual = std::max(0.05, nodes[neighbor]->energy / initialEnergy);
            ranked.emplace_back(1.0 / residual + distance(i, neighbor) / communicationRange, neighbor);
        }
        std::sort(ranked.begin(), ranked.end());
        int count = std::min(extraCandidateNeighbors, static_cast<int>(ranked.size()));
        for (int k = 0; k < count; ++k)
            candidateEdges.insert(edgeKey(i, ranked[k].second));
    }
}

void WsnController::handleMessage(cMessage *message)
{
    if (message != episodeTimer)
        throw cRuntimeError("WsnController received an unexpected message");

    if (currentEpisode >= 0)
        closeEpisode();
    ++currentEpisode;

    if (currentEpisode >= numEpisodes) {
        delete episodeTimer;
        episodeTimer = nullptr;
        endSimulation();
        return;
    }

    startEpisode();
    scheduleAt(simTime() + episodeLength, episodeTimer);
}

void WsnController::resetEpisodeCounters()
{
    episodeGenerated = 0;
    episodeDelivered = 0;
    episodeDropped = 0;
    episodeForwarded = 0;
    episodeDelaySum = 0;
    episodeHopSum = 0;
    episodeReward = 0;
}

int WsnController::selectMainTransmitter() const
{
    int selected = -1;
    if (routingMode == "spmh") {
        double closest = std::numeric_limits<double>::infinity();
        for (int i = 0; i < numNodes; ++i) {
            if (nodes[i]->alive && distanceToSink(i) < closest) {
                closest = distanceToSink(i);
                selected = i;
            }
        }
    }
    else {
        double bestEnergy = -1;
        double bestSinkDistance = std::numeric_limits<double>::infinity();
        for (int i = 0; i < numNodes; ++i) {
            if (!nodes[i]->alive)
                continue;
            if (nodes[i]->energy > bestEnergy + 1e-9 ||
                (std::fabs(nodes[i]->energy - bestEnergy) <= 1e-9 && distanceToSink(i) < bestSinkDistance)) {
                bestEnergy = nodes[i]->energy;
                bestSinkDistance = distanceToSink(i);
                selected = i;
            }
        }
    }
    return selected;
}

std::vector<int> WsnController::selectActiveSources(int excludedNode)
{
    std::vector<int> pool;
    for (int i = 0; i < numNodes; ++i) {
        if (nodes[i]->alive && i != excludedNode)
            pool.push_back(i);
    }
    std::vector<int> selected;
    int count = std::min(activeSourcesPerEpisode, static_cast<int>(pool.size()));
    while (static_cast<int>(selected.size()) < count) {
        int index = intrand(pool.size());
        selected.push_back(pool[index]);
        pool.erase(pool.begin() + index);
    }
    return selected;
}

std::vector<int> WsnController::selectClusterHeads()
{
    std::vector<int> pool;
    for (int i = 0; i < numNodes; ++i)
        if (nodes[i]->alive)
            pool.push_back(i);
    int count = std::max(1, static_cast<int>(std::ceil(pool.size() * clusterHeadFraction)));
    std::vector<int> heads;
    while (!pool.empty() && static_cast<int>(heads.size()) < count) {
        int index = intrand(pool.size());
        heads.push_back(pool[index]);
        pool.erase(pool.begin() + index);
    }
    return heads;
}

void WsnController::startEpisode()
{
    resetEpisodeCounters();
    varianceAtEpisodeStart = energyVariance();
    aliveAtEpisodeStart = aliveNodeCount();
    for (auto *node : nodes)
        node->role = SENSOR;

    buildMstAndCandidateGraph();
    clearActiveRoutes();

    std::vector<int> clusterHeads;
    if (routingMode == "leach") {
        mainTransmitter = -1;
        clusterHeads = selectClusterHeads();
        for (int head : clusterHeads)
            nodes[head]->role = TRANSMITTER;
    }
    else {
        mainTransmitter = selectMainTransmitter();
        if (mainTransmitter >= 0)
            nodes[mainTransmitter]->role = TRANSMITTER;
    }

    std::vector<int> sources = selectActiveSources(mainTransmitter);
    for (int sequence = 0; sequence < static_cast<int>(sources.size()); ++sequence) {
        int source = sources[sequence];
        int target = mainTransmitter;
        std::vector<int> route;

        if (routingMode == "leach") {
            for (int head : clusterHeads) {
                std::vector<int> candidate = source == head ? std::vector<int>{source} : findPath(source, head, false);
                if (!candidate.empty() && (route.empty() || candidate.size() < route.size())) {
                    route = candidate;
                    target = head;
                }
            }
        }
        else if (target >= 0) {
            route = findPath(source, target, routingMode == "marl");
        }

        ++episodeGenerated;
        ++totalGenerated;
        if (route.empty()) {
            ++episodeDropped;
            ++totalDropped;
            continue;
        }

        for (int k = 1; k + 1 < static_cast<int>(route.size()); ++k)
            nodes[route[k]]->role = FORWARDER;
        nodes[target]->role = TRANSMITTER;
        injectPacket(source, target, route, sourceStagger * sequence);
        drawRoute(route);
    }

    updateAllNodeDisplays();
    if (statusFigure != nullptr) {
        std::ostringstream text;
        text << "Episode " << (currentEpisode + 1) << "/" << numEpisodes
             << " | " << routingMode << (cloudMode ? " (cloud)" : " (local)")
             << " | alive=" << aliveNodeCount() << " | epsilon=" << currentEpsilon();
        statusFigure->setText(text.str().c_str());
    }
}

std::vector<int> WsnController::findPath(int source, int target, bool energyAware)
{
    if (source < 0 || target < 0 || !nodes[source]->alive || !nodes[target]->alive)
        return {};
    if (source == target)
        return {source};

    const double infinity = std::numeric_limits<double>::infinity();
    std::vector<double> best(numNodes, infinity);
    std::vector<int> previous(numNodes, -1);
    std::vector<bool> visited(numNodes, false);
    best[source] = 0;
    bool explore = energyAware && uniform(0, 1) < currentEpsilon();

    for (int iteration = 0; iteration < numNodes; ++iteration) {
        int current = -1;
        double currentCost = infinity;
        for (int i = 0; i < numNodes; ++i) {
            if (!visited[i] && best[i] < currentCost) {
                current = i;
                currentCost = best[i];
            }
        }
        if (current < 0)
            break;
        if (current == target)
            break;
        visited[current] = true;

        for (int neighbor : adjacency[current]) {
            if (!nodes[neighbor]->alive || visited[neighbor])
                continue;
            if (energyAware && candidateEdges.find(edgeKey(current, neighbor)) == candidateEdges.end())
                continue;
            double weight = edgeCost(current, neighbor, target, energyAware);
            if (explore)
                weight += uniform(0, 0.75);
            if (best[current] + weight < best[neighbor]) {
                best[neighbor] = best[current] + weight;
                previous[neighbor] = current;
            }
        }
    }

    if (previous[target] < 0)
        return energyAware ? findPath(source, target, false) : std::vector<int>{};
    std::vector<int> path;
    for (int current = target; current >= 0; current = previous[current]) {
        path.push_back(current);
        if (current == source)
            break;
    }
    std::reverse(path.begin(), path.end());
    if (path.empty() || path.front() != source || path.back() != target)
        return {};
    if (static_cast<int>(path.size()) - 1 > maxHops || calculateSignalIntegrity(path) < minimumSignalIntegrity)
        return energyAware ? findPath(source, target, false) : std::vector<int>{};
    return path;
}

double WsnController::edgeCost(int from, int to, int target, bool energyAware)
{
    if (!energyAware)
        return 1.0;
    double residual = std::max(0.05, nodes[to]->energy / initialEnergy);
    double meraWeight = 1.0 / residual;
    double mstWeight = distance(from, to) / communicationRange;
    if (mstEdges.find(edgeKey(from, to)) == mstEdges.end())
        mstWeight += 1.0;
    int approximateHops = std::max(1, static_cast<int>(std::ceil(distance(from, target) / communicationRange)));
    StateKey state = makeState(from, target, approximateHops);
    double learnedValue = getQ(from, state, to);
    double hotspot = nodes[to]->forwardingCount / std::max(1.0, static_cast<double>(currentEpisode + 1));
    double fused = fusionLambda * meraWeight + (1.0 - fusionLambda) * mstWeight;
    return std::max(0.01, fused + hotspotWeight * hotspot - qWeight * learnedValue);
}

double WsnController::calculateSignalIntegrity(const std::vector<int>& path) const
{
    double integrity = 1.0;
    for (int i = 0; i + 1 < static_cast<int>(path.size()); ++i) {
        double normalizedDistance = distance(path[i], path[i + 1]) / communicationRange;
        integrity *= std::max(0.0, 1.0 - maxAttenuationPerHop * normalizedDistance);
    }
    return integrity;
}

StateKey WsnController::makeState(int nodeId, int target, int hopsRemaining) const
{
    const SensorNode *node = nodes[nodeId];
    double diagonal = std::sqrt(areaWidth * areaWidth + areaHeight * areaHeight + areaDepth * areaDepth);
    double neighborMean = 0;
    int neighborCount = 0;
    for (int neighbor : adjacency[nodeId]) {
        if (nodes[neighbor]->alive) {
            neighborMean += nodes[neighbor]->energy;
            ++neighborCount;
        }
    }
    neighborMean = neighborCount > 0 ? neighborMean / neighborCount : 0;
    double serviceTime = packetBits / bitRate + processingDelay.dbl();
    double backlog = std::max(0.0, (node->availableAt - simTime()).dbl());

    StateKey state;
    state.energy = std::min(4, static_cast<int>(5 * node->energy / std::max(1.0, initialEnergy)));
    state.sinkDistance = std::min(4, static_cast<int>(5 * distanceToSink(nodeId) / std::max(1.0, diagonal)));
    state.transmitterDistance = target >= 0 ?
        std::min(4, static_cast<int>(5 * distance(nodeId, target) / std::max(1.0, diagonal))) : 4;
    state.queue = std::min(2, static_cast<int>(backlog / std::max(1e-9, serviceTime)));
    state.hops = std::min(4, std::max(0, hopsRemaining));
    state.neighborEnergy = std::min(4, static_cast<int>(5 * neighborMean / std::max(1.0, initialEnergy)));
    state.role = static_cast<int>(node->role);
    return state;
}

double WsnController::getQ(int nodeId, const StateKey& state, int action) const
{
    auto stateIt = qTables[nodeId].find(state);
    if (stateIt == qTables[nodeId].end() || action < 0 || action >= static_cast<int>(stateIt->second.size()))
        return 0;
    return stateIt->second[action];
}

double WsnController::getMaximumQ(int nodeId, const StateKey& state) const
{
    auto stateIt = qTables[nodeId].find(state);
    if (stateIt == qTables[nodeId].end())
        return 0;
    double best = 0;
    for (int action : adjacency[nodeId]) {
        if (nodes[action]->alive && action < static_cast<int>(stateIt->second.size()))
            best = std::max(best, stateIt->second[action]);
    }
    return best;
}

void WsnController::updateQ(int nodeId, const StateKey& state, int action, double reward, const StateKey& nextState)
{
    if (routingMode != "marl" || action < 0 || action >= numNodes)
        return;
    auto& actions = qTables[nodeId][state];
    if (actions.empty())
        actions.assign(numNodes, 0.0);
    double oldValue = actions[action];
    double target = reward + discountFactor * getMaximumQ(action, nextState);
    actions[action] = oldValue + learningRate * (target - oldValue);
}

double WsnController::currentEpsilon() const
{
    return std::max(minimumEpsilon, initialEpsilon * std::pow(epsilonDecay, std::max(0, currentEpisode)));
}

void WsnController::injectPacket(int source, int target, const std::vector<int>& route, simtime_t offset)
{
    auto *packet = new WsnPacket("sensorData");
    packet->route = route;
    packet->source = source;
    packet->target = target;
    packet->episode = currentEpisode;
    packet->createdAt = simTime();
    packet->signalIntegrity = calculateSignalIntegrity(route);
    packet->setBitLength(packetBits);
    simtime_t decisionDelay = routingMode == "leach" ? leachSetupDelay :
        (cloudMode && routingMode == "marl" ? cloudDecisionDelay : localDecisionDelay);
    sendDirect(packet, offset + decisionDelay, SIMTIME_ZERO, nodes[source]->gate("radioIn"));
}

bool WsnController::spendEnergy(SensorNode *node, double amount)
{
    if (!node->alive || node->energy + 1e-12 < amount) {
        node->energy = 0;
        node->alive = false;
        updateNodeDisplay(node);
        return false;
    }
    node->energy = std::max(0.0, node->energy - amount);
    if (node->energy <= 1e-12)
        node->alive = false;
    updateNodeDisplay(node);
    return true;
}

void WsnController::handleAtNode(SensorNode *node, WsnPacket *packet)
{
    Enter_Method_Silent();
    take(packet);
    if (packet->episode != currentEpisode) {
        dropPacket(packet, "packet crossed an episode boundary");
        return;
    }
    if (!node->alive || packet->routePosition >= static_cast<int>(packet->route.size()) ||
        packet->route[packet->routePosition] != node->id()) {
        dropPacket(packet, "dead node or invalid route state");
        return;
    }

    if (!packet->sensed) {
        if (!spendEnergy(node, sensingEnergyCost)) {
            dropPacket(packet, "source has insufficient sensing energy");
            return;
        }
        packet->sensed = true;
        packet->accumulatedReward += 3; // Fig. 3: sense and send data
        episodeReward += 3;
    }

    bool isTransmitter = packet->routePosition == static_cast<int>(packet->route.size()) - 1;
    simtime_t transmissionDuration = SimTime(static_cast<double>(packetBits) / bitRate);
    simtime_t serviceStart = std::max(simTime(), node->availableAt);
    simtime_t queueWait = serviceStart - simTime();
    node->availableAt = serviceStart + processingDelay + transmissionDuration;

    if (isTransmitter) {
        if (!spendEnergy(node, sinkEnergyCost)) {
            dropPacket(packet, "transmitter has insufficient sink energy");
            return;
        }
        packet->accumulatedReward += 10; // Fig. 3: successful transmitter-to-sink action
        episodeReward += 10;
        ++packet->hops;
        simtime_t propagation = SimTime(distanceToSink(node->id()) / propagationSpeed);
        sendDirect(packet, queueWait + processingDelay + propagation, transmissionDuration, sink->gate("radioIn"));
        return;
    }

    int nextId = packet->route[packet->routePosition + 1];
    SensorNode *nextNode = nodes[nextId];
    if (!nextNode->alive) {
        dropPacket(packet, "next hop died before reception");
        return;
    }

    StateKey state = makeState(node->id(), packet->target,
                               static_cast<int>(packet->route.size()) - packet->routePosition - 1);
    double decisionCost = routingMode == "marl" && !cloudMode ? localDecisionEnergyCost : 0;
    if (!spendEnergy(node, multiHopEnergyCost + decisionCost)) {
        dropPacket(packet, "forwarder has insufficient energy");
        return;
    }

    ++node->forwardingCount;
    ++episodeForwarded;
    double reward = 5; // Fig. 3: successful forwarding
    double meanForwarding = static_cast<double>(episodeForwarded) / std::max(1, aliveNodeCount());
    reward += node->forwardingCount <= meanForwarding + 2 ? 2 : -4; // hotspot reward / penalty
    if (nextNode->energy >= meanEnergy())
        reward += 2; // energy-balance component
    packet->accumulatedReward += reward;
    episodeReward += reward;

    StateKey nextState = makeState(nextId, packet->target,
                                   static_cast<int>(packet->route.size()) - packet->routePosition - 2);
    updateQ(node->id(), state, nextId, reward, nextState);

    ++packet->routePosition;
    ++packet->hops;
    simtime_t propagation = SimTime(distance(node->id(), nextId) / propagationSpeed);
    sendDirect(packet, queueWait + processingDelay + propagation, transmissionDuration, nextNode->gate("radioIn"));
}

void WsnController::handleAtSink(WsnPacket *packet)
{
    Enter_Method_Silent();
    take(packet);
    ++episodeDelivered;
    ++totalDelivered;
    episodeDelaySum += (simTime() - packet->createdAt).dbl();
    episodeHopSum += packet->hops;
    delete packet;
}

void WsnController::dropPacket(WsnPacket *packet, const char *reason)
{
    ++episodeDropped;
    ++totalDropped;
    EV_WARN << "Dropping " << packet->getName() << ": " << reason << "\n";
    delete packet;
}

double WsnController::meanEnergy() const
{
    double sum = 0;
    for (const auto *node : nodes)
        sum += node->energy;
    return sum / numNodes;
}

double WsnController::energyVariance() const
{
    double mean = meanEnergy();
    double sum = 0;
    for (const auto *node : nodes) {
        double delta = node->energy - mean;
        sum += delta * delta;
    }
    return numNodes > 1 ? sum / (numNodes - 1) : 0;
}

int WsnController::aliveNodeCount() const
{
    int count = 0;
    for (const auto *node : nodes)
        if (node->alive)
            ++count;
    return count;
}

void WsnController::closeEpisode()
{
    double mean = meanEnergy();
    double variance = energyVariance();
    int alive = aliveNodeCount();
    double deliveryRatio = episodeGenerated > 0 ? static_cast<double>(episodeDelivered) / episodeGenerated : 0;
    double delay = episodeDelivered > 0 ? episodeDelaySum / episodeDelivered : 0;
    double hops = episodeDelivered > 0 ? episodeHopSum / episodeDelivered : 0;

    if (alive >= aliveAtEpisodeStart)
        episodeReward += 3; // no node failed during the episode
    if (variance < varianceAtEpisodeStart)
        episodeReward += 2; // reduced SoC variance
    if (mean >= 40)
        episodeReward += 2; // network mean remains above threshold

    meanSocVector.record(mean);
    socVarianceVector.record(variance);
    aliveNodesVector.record(alive);
    deliveryRatioVector.record(deliveryRatio);
    delayVector.record(delay);
    hopCountVector.record(hops);
    rewardVector.record(episodeReward);
    epsilonVector.record(currentEpsilon());

    EV_INFO << "episode=" << currentEpisode + 1 << " mode=" << routingMode
            << " meanSoC=" << mean << " variance=" << variance << " alive=" << alive
            << " delivered=" << episodeDelivered << "/" << episodeGenerated
            << " delay=" << delay << "s reward=" << episodeReward << "\n";
    updateAllNodeDisplays();
}

void WsnController::finish()
{
    recordScalar("finalMeanSoC", meanEnergy());
    recordScalar("finalSoCVariance", energyVariance());
    recordScalar("finalAliveNodes", aliveNodeCount());
    recordScalar("generatedPackets", totalGenerated);
    recordScalar("deliveredPackets", totalDelivered);
    recordScalar("droppedPackets", totalDropped);
    recordScalar("overallDeliveryRatio", totalGenerated > 0 ? static_cast<double>(totalDelivered) / totalGenerated : 0);
    long qStates = 0;
    for (const auto& table : qTables)
        qStates += table.size();
    recordScalar("learnedQStates", qStates);
    long physicalEdges = 0;
    for (const auto& neighbors : adjacency)
        physicalEdges += neighbors.size();
    recordScalar("physicalLinks", physicalEdges / 2);
}

cFigure::Point WsnController::displayPoint(int nodeId) const
{
    double displayX = 45 + 680 * nodes[nodeId]->x / std::max(1.0, areaWidth);
    double displayY = 45 + 525 * nodes[nodeId]->y / std::max(1.0, areaHeight);
    return cFigure::Point(displayX, displayY);
}

void WsnController::createFigures()
{
    if (!getEnvir()->isGUI())
        return;
    cCanvas *canvas = getParentModule()->getCanvas();
    physicalLinksFigure = new cGroupFigure("physicalLinks");
    physicalLinksFigure->setZIndex(-2);
    canvas->addFigure(physicalLinksFigure);
    activeRoutesFigure = new cGroupFigure("activeRoutes");
    activeRoutesFigure->setZIndex(-1);
    canvas->addFigure(activeRoutesFigure);
    statusFigure = new cTextFigure("status");
    statusFigure->setPosition(cFigure::Point(45, 22));
    statusFigure->setColor(cFigure::Color(31, 41, 55));
    statusFigure->setZIndex(5);
    canvas->addFigure(statusFigure);
}

void WsnController::drawPhysicalLinks()
{
    if (physicalLinksFigure == nullptr)
        return;
    for (int i = 0; i < numNodes; ++i) {
        for (int neighbor : adjacency[i]) {
            if (neighbor <= i)
                continue;
            auto *line = new cLineFigure();
            line->setStart(displayPoint(i));
            line->setEnd(displayPoint(neighbor));
            line->setLineColor(cFigure::Color(148, 163, 184));
            line->setLineOpacity(0.30);
            line->setLineWidth(0.7);
            physicalLinksFigure->addFigure(line);
        }
    }
}

void WsnController::clearActiveRoutes()
{
    if (activeRoutesFigure == nullptr)
        return;
    while (activeRoutesFigure->getNumFigures() > 0)
        delete activeRoutesFigure->removeFigure(0);
}

void WsnController::drawRoute(const std::vector<int>& route)
{
    if (activeRoutesFigure == nullptr || route.empty())
        return;
    for (int i = 0; i + 1 < static_cast<int>(route.size()); ++i) {
        auto *line = new cLineFigure();
        line->setStart(displayPoint(route[i]));
        line->setEnd(displayPoint(route[i + 1]));
        line->setLineColor(cFigure::Color(239, 68, 68));
        line->setLineOpacity(0.85);
        line->setLineWidth(2.4);
        activeRoutesFigure->addFigure(line);
    }
    auto *sinkLine = new cLineFigure();
    sinkLine->setStart(displayPoint(route.back()));
    sinkLine->setEnd(cFigure::Point(775, 300));
    sinkLine->setLineColor(cFigure::Color(245, 158, 11));
    sinkLine->setLineOpacity(0.85);
    sinkLine->setLineWidth(2.4);
    activeRoutesFigure->addFigure(sinkLine);
}

void WsnController::updateNodeDisplay(SensorNode *node)
{
    if (!getEnvir()->isGUI())
        return;
    const char *fill = "#39b872";
    if (!node->alive)
        fill = "#64748b";
    else if (node->energy < 20)
        fill = "#ef4444";
    else if (node->energy < 40)
        fill = "#f97316";
    else if (node->energy < 70)
        fill = "#eab308";
    node->getDisplayString().setTagArg("b", 3, fill);
    node->getDisplayString().setTagArg("b", 4, node->role == TRANSMITTER ? "#dc2626" : "#1f2937");
    node->getDisplayString().setTagArg("b", 5, node->role == TRANSMITTER ? "3" : "1");
    std::string label;
    if (node->role == TRANSMITTER)
        label = "TX #" + std::to_string(node->id());
    else if (!node->alive)
        label = "dead";
    node->getDisplayString().setTagArg("t", 0, label.c_str());
}

void WsnController::updateAllNodeDisplays()
{
    for (auto *node : nodes)
        updateNodeDisplay(node);
}

} // namespace marlwsn
