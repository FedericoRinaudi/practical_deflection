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

package inet.linklayer.ieee80211.mac.ratecontrol;

import inet.linklayer.ieee80211.mac.contract.IRateControl;

simple OnoeRateControl like IRateControl
{
    parameters:
        double initialRate @unit(bps) = default(-1bps); // -1 means fastest mandatory rate
        double interval @unit(s) = default(1s);
        @display("i=block/cogwheel");
        //@signal[datarateChanged];
        //@statistic[datarateChanged](title="datarate"; record=vector; interpolationmode=sample-hold);
}

