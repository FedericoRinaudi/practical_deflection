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

package inet.linklayer.ieee80211.mac.queue;

//# TODO: rename to InProgressQueue?
simple InProgressFrames
{
    parameters:
        string pendingQueueModule;
        string originatorMacDataServiceModule;
        string ackHandlerModule;
        @display("i=block/queue");
        //@signal[packetEnqueued](type=inet::Packet);
        //@signal[packetDequeued](type=inet::Packet);
        //@statistic[queueingTime](title="queueing times"; source=queueingTime(packetDequeued); record=mean,sum; interpolationmode=none);
        //@statistic[queueingTimeIncast](title="incast packet queueing times"; source=queueingTimeIncast(packetDequeued); record=mean,sum; interpolationmode=none);
        //@statistic[queueingTimeFlowSize](title="flow size mapping to queueing times"; source=queueingTimeFlowSize(packetPopped); record=vector; interpolationmode=none);
        //@statistic[queueLength](title="queue length"; source=count(packetEnqueued) - count(packetDequeued); record=max,timeavg,vector; interpolationmode=sample-hold);
}

