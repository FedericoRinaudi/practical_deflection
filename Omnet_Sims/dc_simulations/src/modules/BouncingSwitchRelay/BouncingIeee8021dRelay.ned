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

package dc_simulations.modules.BouncingSwitchRelay;

import inet.linklayer.contract.IMacRelayUnit;

//
// This modul forwards frames (~EtherFrame) based on their destination MAC addresses to appropriate ports.
//
// It can work in two mode:
// - STP unaware mode (If no module connected to its STP gate):
//     Looks up the frames destination address in the ~MACAddressTable, if it found the address, sends through
//	   that port, or otherwise broadcasts it.
// - STP aware mode (If an ~STP or ~RSTP module connected to its STP gate):
//     Handles BPDU frames.
//     If relaying a message also checks the port's status in the InterfaceTable.
//
// @see EtherFrame, MacAddressTable, Stp, Rstp
//
simple BouncingIeee8021dRelay like IMacRelayUnit
{
    parameters:
        @display("i=block/broadcast;bgb=425,114");
        string interfaceTableModule = default("^.interfaceTable"); // The path to the InterfaceTable module
        string macTableModule = default("^.macTable");             // The path to the MacAddressTable module
        ////@signal[packetSentToLower](type=cPacket);
        ////@signal[packetReceivedFromLower](type=cPacket);
        ////@signal[packetReceivedFromUpper](type=cPacket);
        ////@signal[feedBackPacketDropped]();
        ////@signal[feedBackPacketDroppedPort]();
        ////@signal[feedBackPacketGenerated]();
        ////@signal[bounceLimitPassed]();
        ////@signal[burstyPacketReceived]();
        //@signal[requesterID](type=long); // Segnale per il requesterID
        //@signal[packetUniqueID](type=long); // Segnale per l'ID univoco del pacchetto
        @signal[queueLen](type=long); // Segnale per la lunghezza della coda
        @signal[queuesTotLen](type=long); // Segnale per la lunghezza totale delle code
        @signal[queueCapacity](type=long); // Segnale per la capacità della coda
        @signal[queuesTotCapacity](type=long); // Segnale per la capacità totale
        @signal[packetAction](type=int); // Segnale per l'azione del pacchetto (0: forward, 1: drop, 2: generate feedback)
        @signal[switchSeqNum](type=int); // Sequence number for the switch
        @signal[switchTtl](type=short); // Time to live for the switch
        @signal[actionSeqNum](type=int); // Sequence number for the action taken on the packet
        //@signal[switchId](type=int); // Switch ID for the packet
        //@signal[switchIdAction](type=int); // Switch ID for the action taken on the packet
        //@signal[interfaceId](type=int); // Interface ID for the packet
        
        ////@statistic[FBPacketDropped](title="feedback packet dropped"; source="feedBackPacketDropped"; record=vector; interpolationmode=none);
        ////@statistic[FBPacketDroppedPort](title="feedback packet dropped port"; source="feedBackPacketDroppedPort"; record=vector; interpolationmode=none);
        ////@statistic[FBPacketGenerated](title="feedback packet generated"; source="feedBackPacketGenerated"; record=count; interpolationmode=none);
        ////@statistic[FBBouncePassed](title="bounce limit passed"; source="bounceLimitPassed"; record=count; interpolationmode=none);
        ////@statistic[BurstyPacketReceived](title="bounce limit passed"; source="burstyPacketReceived"; record="vector"; interpolationmode=none);
        //@statistic[RequesterID](title="Requester ID"; source="requesterID"; record=vector,stats; interpolationmode=none); // Registra il vettore con indice
        //@statistic[PacketUniqueID](title="Packet Unique ID"; source="packetUniqueID"; record=vector,stats; interpolationmode=none); // Registra l'ID univoco del pacchetto
        //@statistic[QueueLen](title="Queue Length"; source="queueLen"; record=vector,stats; interpolationmode=none); // Registra la lunghezza della coda
        //@statistic[QueuesTotLen](title="Total Queues Length"; source="queuesTotLen"; record=vector,stats; interpolationmode=none); // Registra la lunghezza totale delle code
        //@statistic[QueueCapacity](title="Queue Capacity"; source="queueCapacity"; record=vector,stats; interpolationmode=none); // Registra la capacità della coda
        //@statistic[QueuesTotCapacity](title="Queue Total Capacity"; source="queuesTotCapacity"; record=vector,stats; interpolationmode=none); // Registra la capacità totale delle code
        //@statistic[PacketAction](title="Packet Action"; source="packetAction"; record=vector,stats; interpolationmode=none); // Registra l'azione del pacchetto (0: forward, 1: drop, 2: generate feedback)
        //@statistic[switchSeqNum](record=vector,stats; interpolationmode=none); // Registra il numero di sequenza del pacchetto switch

        bool hasStp;
        bool learn_mac_addresses;

    gates:
        input ifIn;
        output ifOut;
        input upperLayerIn;
        output upperLayerOut;
}
