//
// Copyright (C) OpenSim Ltd.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, see http://www.gnu.org/licenses/.
//

package inet.queueing.queue;

import inet.queueing.base.PacketQueueBase;
import inet.queueing.contract.IPacketQueue;

//
// This compound module serves as a base module for complex packet queues formed
// by combining several queueing components.
//
module CompoundPacketQueue extends PacketQueueBase like IPacketQueue
{
    parameters:
        int packetCapacity = default(-1); // maximum number of packets in the queue, no limit by default
        int dataCapacity @unit(b) = default(-1b); // maximum total length of packets in the queue, no limit by default
        displayStringTextFormat = default("contains %p pk (%l)\npushed %u popped %o dropped %d");
        @class(CompoundPacketQueue);
        //@signal[packetPushed](type=inet::Packet);
        //@signal[packetPopped](type=inet::Packet);
        //@signal[packetRemoved](type=inet::Packet);
        //@signal[packetDropped](type=inet::Packet);
        //@signal[customQueueLength](type=long);
        //@signal[customQueueLengthPacketBytes](type=long);
        //@statistic[packetPushed](title="packets pushed"; source=localSignal(packetPushed); record=count,sum(packetBytes),vector(packetBytes); interpolationmode=none);
        //@statistic[packetPopped](title="packets popped"; source=localSignal(packetPopped); record=count,sum(packetBytes),vector(packetBytes); interpolationmode=none);
        //@statistic[packetRemoved](title="packets removed"; source=localSignal(packetRemoved); record=count,sum(packetBytes),vector(packetBytes); interpolationmode=none);
        //@statistic[packetDropQueueOverflow](title="packets dropped: queue overflow"; source=packetDropReasonIsQueueOverflow(packetDropped); record=count,sum(packetBytes),vector(packetBytes); interpolationmode=none);
        //@statistic[queueingTime](title="queueing times"; source=queueingTime(packetPopped); record=mean,sum; unit=s; interpolationmode=none);
        //@statistic[queueingTimeIncast](title="incast packet queueing times"; source=queueingTimeIncast(packetPopped); record=mean,sum; unit=s; interpolationmode=none);
        //@statistic[queueingTimeFlowSize](title="flow size mapping to queueing times"; source=queueingTimeFlowSize(packetPopped); record=vector; interpolationmode=none);
        //@statistic[queueLength](title="queue length"; source=count(localSignal(packetPushed)) - count(localSignal(packetPopped)) - count(localSignal(packetRemoved)) - count(packetDropped); record=max,timeavg,vector; interpolationmode=sample-hold);
		//@statistic[DCQueueLength](title="custom queue length"; source="customQueueLength"; record=mean; interpolationmode=none);
		//@statistic[DCQueueLengthPacketBytes](title="custom queue length packet bytes"; source="customQueueLengthPacketBytes"; record=mean; interpolationmode=none);
		
		
}
