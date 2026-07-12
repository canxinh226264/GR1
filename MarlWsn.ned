package marlwsn;

simple SensorNode
{
    parameters:
        double initialEnergy = default(100);
        @display("b=10,10,oval,#39b872,#1f2937,1");
    gates:
        input radioIn;
}

simple WsnSink
{
    parameters:
        @display("i=device/antennatower;t=SINK");
    gates:
        input radioIn;
}

simple WsnController
{
    parameters:
        int numEpisodes = default(100);
        int activeSourcesPerEpisode = default(4);
        int maxHops = default(10);
        int extraCandidateNeighbors = default(6);
        int packetBits = default(1024);
        double areaWidth = default(100);
        double areaHeight = default(100);
        double areaDepth = default(0);
        double communicationRange = default(18);
        double initialEnergy = default(100);
        double sensingEnergyCost = default(0.1);
        double multiHopEnergyCost = default(2);
        double sinkEnergyCost = default(8);
        double localDecisionEnergyCost = default(0.05);
        double bitRate @unit(bps) = default(250kbps);
        double propagationSpeed = default(3e8);
        double minimumSignalIntegrity = default(0.70);
        double maxAttenuationPerHop = default(0.04);
        double learningRate = default(0.25);
        double discountFactor = default(0.90);
        double initialEpsilon = default(0.30);
        double minimumEpsilon = default(0.05);
        double epsilonDecay = default(0.97);
        double fusionLambda = default(0.65);
        double qWeight = default(0.025);
        double hotspotWeight = default(0.12);
        double clusterHeadFraction = default(0.10);
        bool cloudMode = default(false);
        string routingMode = default("marl");
        string placementMode = default("jitteredGrid");
        double episodeLength @unit(s) = default(1s);
        double processingDelay @unit(s) = default(1ms);
        double localDecisionDelay @unit(s) = default(1ms);
        double cloudDecisionDelay @unit(s) = default(15ms);
        double leachSetupDelay @unit(s) = default(40ms);
        double sourceStagger @unit(s) = default(10ms);
        @display("i=block/control;t=MARL controller");
}

network MarlWsnNetwork
{
    parameters:
        int numNodes = default(100);
        @display("bgb=820,620,#f8fafc;bgi=background/terrain,s");
    submodules:
        controller: WsnController {
            @display("p=770,575");
        }
        sink: WsnSink {
            @display("p=775,300");
        }
        node[numNodes]: SensorNode;
    connections allowunconnected:
}
