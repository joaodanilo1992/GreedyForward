//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
// 

#include "SensorNetLayer.h"

Define_Module(SensorNetLayer);

void SensorNetLayer::initialize(int stage) {

    NetworkProtocolBase::initialize(stage);

    if (stage == INITSTAGE_LOCAL) {

        headerLength = par("headerLength");
        arp = getModuleFromPar<IArp>(par("arpModule"), this);
        mobility = check_and_cast<IMobility *>(getContainingNode(this)->getSubmodule("mobility"));
    }
    else if (stage == INITSTAGE_NETWORK_LAYER_3) {
        IInterfaceTable *interfaceTable = getModuleFromPar<IInterfaceTable>(par("interfaceTableModule"), this);
        myNetworkAddress = interfaceTable->getInterface(0)->getNetworkAddress();
    }
}

void SensorNetLayer::handleSelfMessage(cMessage *msg){

    if(msg->getKind() == ROUTING){

        auto pkt = makeShared<RoutingMessage>();

        pkt->setChunkLength(B(headerLength));
        pkt->setSinkPosition(sinkPosition);
        pkt->setSinkAddress(sinkAddress);
        pkt->setSrcAddr(myNetworkAddress);
        pkt->setSinkDistance(sinkDistance);
        pkt->setDestAddr(myNetworkAddress.getAddressType()->getBroadcastAddress());

        auto packet = new Packet("Forward Flooding", ROUTING);
        packet->insertAtBack(pkt);
        setDownControlInfo(packet, MacAddress::BROADCAST_ADDRESS);
        sendDown(packet);

    }
    delete msg;
}

void SensorNetLayer::handleLowerPacket(Packet *packet){

    if(packet->getKind() == ROUTING){
        auto netMsg = staticPtrCast<RoutingMessage>(packet->peekAtFront<RoutingMessage>()->dupShared());

        if(!isConfigured){
            isConfigured = true;

            sinkPosition = netMsg->getSinkPosition();
            sinkDistance = sinkPosition.distance(mobility->getCurrentPosition());
            sinkAddress = netMsg->getSinkAddress();

            routingMessage = new cMessage("Forward Flooding", ROUTING);
            scheduleAt(simTime() + truncnormal(0,0.1), routingMessage);
        }

        neighbors(netMsg->getSrcAddr(), netMsg->getSinkDistance());

        delete packet;
    }
    else if(packet->getKind() == DATA){

        auto netMsg = staticPtrCast<RoutingMessage>(packet->peekAtFront<RoutingMessage>()->dupShared());

        L3Address neighborsAddr = getDest();
        MacAddress neighborsMacAddr = arp->resolveL3Address(neighborsAddr, nullptr);
        const cObject* pCtrlInfo = nullptr;


        netMsg->setDestAddr(neighborsAddr);

        pCtrlInfo = packet->removeControlInfo();

        auto p = new Packet(packet->getName(), packet->getKind());
        packet->popAtFront<RoutingMessage>();
        p->insertAtBack(packet->peekDataAt(b(0), packet->getDataLength()));
        p->insertAtFront(netMsg);
        setDownControlInfo(p, neighborsMacAddr);
        sendDown(p);


        delete packet;
    }


}

void SensorNetLayer::handleUpperPacket(Packet *msg){

    auto pkt = makeShared<RoutingMessage>();

    L3Address neighborsAddr = getDest();
    MacAddress neighborsMACAddr = arp->resolveL3Address(neighborsAddr, nullptr);

    pkt->setChunkLength(B(headerLength));
    pkt->setSrcAddr(myNetworkAddress);
    pkt->setDestAddr(neighborsAddr);

    msg->insertAtFront(pkt);
    msg->setKind(DATA);

    setDownControlInfo(msg, neighborsMACAddr);

    sendDown(msg);


    //delete msg;
}

void SensorNetLayer::neighbors(L3Address address, double sinkDistance){

    if(sinkDistance <= this->sinkDistance){
        neighborsTable.insert(make_pair(address, sinkDistance));
    }
}

void SensorNetLayer::setDownControlInfo(Packet *const pMsg, const MacAddress& pDestAddr){
    pMsg->addTagIfAbsent<MacAddressReq>()->setDestAddress(pDestAddr);
    pMsg->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&getProtocol());
    pMsg->addTagIfAbsent<DispatchProtocolInd>()->setProtocol(&getProtocol());
}

const Protocol& SensorNetLayer::getProtocol() const {

    static const Protocol *aux = Protocol::getProtocol("SinkNetwork");

    return *aux;
}

L3Address SensorNetLayer::getDest(){

    typedef function<bool(pair<L3Address, double>, pair<L3Address, double>)> Comparator;

    Comparator compFunctor = [](pair<L3Address, double> elem1 ,pair<L3Address, double> elem2) {return elem1.second < elem2.second; };
    set<pair<L3Address, double>, Comparator> setOfWords(neighborsTable.begin(), neighborsTable.end(), compFunctor);
    for (pair<L3Address, double> element : setOfWords)
       return element.first;
}
