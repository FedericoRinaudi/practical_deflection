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

package inet.linklayer.ieee80211.mac.originator;

simple TxopProcedure
{
    parameters:
        double txopLimit @unit(s) = default(-1s);
        @display("i=block/timer");
        //@signal[txopStarted](inet::ieee80211::TxopProcedure);
        //@signal[txopEnded](inet::ieee80211::TxopProcedure);
        //@statistic[txopEnded](title="txops ended"; source=txopEnded; record=count,vector?; interpolationmode=none);
        //@statistic[txopDuration](title="txop duration"; source=txopDuration(txopEnded); record=histogram,vector?; interpolationmode=none);
        //@statistic[txopActive](title="txop active"; source=count(txopStarted)-count(txopEnded); record=vector; interpolationmode=sample-hold);
}

