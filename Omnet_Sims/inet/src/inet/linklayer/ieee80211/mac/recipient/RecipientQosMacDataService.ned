//
// Copyright (C) 2016 OpenSim Ltd.
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

package inet.linklayer.ieee80211.mac.recipient;

module RecipientQosMacDataService
{
    parameters:
        @class("RecipientQosMacDataService");
        @display("i=block/join");
        //@signal[packetDefragmented](type=inet::Packet);
        //@signal[packetDeaggregated](type=inet::Packet);
        //@signal[packetDropped](type=inet::Packet);
        //@statistic[packetDefragmented](title="packets defragmented"; record=count);
        //@statistic[packetDeaggregated](title="packets deaggregated"; record=count);
        //@statistic[packetDropDuplicateDetected](title="packet drops: duplicate detected"; source=packetDropReasonIsDuplicateDetected(packetDropped); record=count,sum(packetBytes),vector(packetBytes); interpolationmode=none);
}

