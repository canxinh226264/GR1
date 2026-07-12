#ifndef __MARLWSN_WSNSIMULATION_H
#define __MARLWSN_WSNSIMULATION_H

#include <omnetpp.h>

#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace marlwsn {

using namespace omnetpp;

enum NodeRole { SENSOR = 0, FORWARDER = 1, TRANSMITTER = 2 };

struct StateKey
{
    int energy = 0;
    int sinkDistance = 0;
    int transmitterDistance = 0;
    int queue = 0;
    int hops = 0;
    int neighborEnergy = 0;
    int role = 0;

    bool operator<(const StateKey& other) const
    {
        return std::tie(energy, sinkDistance, transmitterDistance, queue, hops, neighborEnergy, role) <
               std::tie(other.energy, other.sinkDistance, other.transmitterDistance, other.queue,
                        other.hops, other.neighborEnergy, other.role);
    }
};

class WsnPacket : public cPacket
{
  public:
    std::vector<int> route;
    int routePosition = 0;
    int episode = -1;
    int source = -1;
    int target = -1;
    int hops = 0;
    bool sensed = false;
    double signalIntegrity = 1.0;
    double accumulatedReward = 0.0;
    simtime_t createdAt;

    explicit WsnPacket(const char *name = nullptr) : cPacket(name) {}
    WsnPacket(const WsnPacket& other) = default;
    WsnPacket *dup() const override { return new WsnPacket(*this); }
};

class WsnController;

class SensorNode : public cSimpleModule
{
    friend class WsnController;

  protected:
    WsnController *controller = nullptr;
    double x = 0;
    double y = 0;
    double z = 0;
    double energy = 100;
    double initialEnergy = 100;
    bool alive = true;
    NodeRole role = SENSOR;
    long forwardingCount = 0;
    simtime_t availableAt = SIMTIME_ZERO;

    void initialize() override;
    void handleMessage(cMessage *message) override;

  public:
    int id() const { return getIndex(); }
    double getEnergy() const { return energy; }
    bool isAlive() const { return alive; }
};

class WsnSink : public cSimpleModule
{
  protected:
    WsnController *controller = nullptr;

    void initialize() override;
    void handleMessage(cMessage *message) override;
};

class WsnController : public cSimpleModule
{
  protected:
    // Model parameters
    int numNodes = 0;
    int numEpisodes = 0;
    int activeSourcesPerEpisode = 0;
    int maxHops = 0;
    int extraCandidateNeighbors = 0;
    int packetBits = 0;
    double areaWidth = 0;
    double areaHeight = 0;
    double areaDepth = 0;
    double communicationRange = 0;
    double initialEnergy = 0;
    double sensingEnergyCost = 0;
    double multiHopEnergyCost = 0;
    double sinkEnergyCost = 0;
    double localDecisionEnergyCost = 0;
    double bitRate = 0;
    double propagationSpeed = 0;
    double minimumSignalIntegrity = 0;
    double maxAttenuationPerHop = 0;
    double learningRate = 0;
    double discountFactor = 0;
    double initialEpsilon = 0;
    double minimumEpsilon = 0;
    double epsilonDecay = 0;
    double fusionLambda = 0;
    double qWeight = 0;
    double hotspotWeight = 0;
    double clusterHeadFraction = 0;
    bool cloudMode = false;
    std::string routingMode;
    std::string placementMode;
    simtime_t episodeLength;
    simtime_t processingDelay;
    simtime_t localDecisionDelay;
    simtime_t cloudDecisionDelay;
    simtime_t leachSetupDelay;
    simtime_t sourceStagger;

    // Network state
    std::vector<SensorNode *> nodes;
    WsnSink *sink = nullptr;
    std::vector<std::vector<int>> adjacency;
    std::set<std::pair<int, int>> mstEdges;
    std::set<std::pair<int, int>> candidateEdges;
    std::vector<std::map<StateKey, std::vector<double>>> qTables;
    int currentEpisode = -1;
    int mainTransmitter = -1;
    cMessage *episodeTimer = nullptr;

    // Per-episode and aggregate statistics
    long episodeGenerated = 0;
    long episodeDelivered = 0;
    long episodeDropped = 0;
    long episodeForwarded = 0;
    long totalGenerated = 0;
    long totalDelivered = 0;
    long totalDropped = 0;
    double episodeDelaySum = 0;
    double episodeHopSum = 0;
    double episodeReward = 0;
    double varianceAtEpisodeStart = 0;
    int aliveAtEpisodeStart = 0;

    cOutVector meanSocVector;
    cOutVector socVarianceVector;
    cOutVector aliveNodesVector;
    cOutVector deliveryRatioVector;
    cOutVector delayVector;
    cOutVector hopCountVector;
    cOutVector rewardVector;
    cOutVector epsilonVector;

    // Qtenv figures
    cGroupFigure *physicalLinksFigure = nullptr;
    cGroupFigure *activeRoutesFigure = nullptr;
    cTextFigure *statusFigure = nullptr;

    int numInitStages() const override { return 2; }
    void initialize(int stage) override;
    void handleMessage(cMessage *message) override;
    void finish() override;

    void readParameters();
    void collectModules();
    void placeNodes();
    void buildPhysicalGraph();
    void buildMstAndCandidateGraph();
    void startEpisode();
    void closeEpisode();
    void resetEpisodeCounters();
    int selectMainTransmitter() const;
    std::vector<int> selectActiveSources(int excludedNode);
    std::vector<int> selectClusterHeads();
    std::vector<int> findPath(int source, int target, bool energyAware);
    double edgeCost(int from, int to, int target, bool energyAware);
    double calculateSignalIntegrity(const std::vector<int>& path) const;
    StateKey makeState(int nodeId, int target, int hopsRemaining) const;
    double getQ(int nodeId, const StateKey& state, int action) const;
    double getMaximumQ(int nodeId, const StateKey& state) const;
    void updateQ(int nodeId, const StateKey& state, int action, double reward, const StateKey& nextState);
    double currentEpsilon() const;
    double distance(int first, int second) const;
    double distanceToSink(int nodeId) const;
    std::pair<int, int> edgeKey(int first, int second) const;
    double meanEnergy() const;
    double energyVariance() const;
    int aliveNodeCount() const;
    bool spendEnergy(SensorNode *node, double amount);
    void dropPacket(WsnPacket *packet, const char *reason);
    void injectPacket(int source, int target, const std::vector<int>& route, simtime_t offset);
    void updateNodeDisplay(SensorNode *node);
    void updateAllNodeDisplays();
    void createFigures();
    void drawPhysicalLinks();
    void clearActiveRoutes();
    void drawRoute(const std::vector<int>& route);
    cFigure::Point displayPoint(int nodeId) const;

  public:
    ~WsnController() override;
    void handleAtNode(SensorNode *node, WsnPacket *packet);
    void handleAtSink(WsnPacket *packet);
};

} // namespace marlwsn

#endif
