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

package inet.queueing.base;

//
// This is a base module for various packet classifier modules. Derived modules
// must implement a single packet classifier function which determines the index
// of the output gate for the next pushed packet.
//
// @see ~IPacketClassifier
//
simple PacketClassifierBase extends PacketSinkBase
{
    parameters:
        displayStringTextFormat = default("classified %p pk (%l)");
        @display("i=block/classifier");
        //@signal[packetPushed](type=inet::Packet);
        //@statistic[packetPushed](title="packets pushed"; record=count,sum(packetBytes),vector(packetBytes); interpolationmode=none);
    gates:
        input in;
        output out[];
}
