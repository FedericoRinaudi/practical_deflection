//
// Copyright (C) 2013 OpenSim Ltd.
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
// along with this program; if not, see <http://www.gnu.org/licenses/>.
//

package inet.physicallayer.base.packetlevel;

import inet.physicallayer.contract.packetlevel.IObstacleLoss;

//
// This module servces as a base module for tracing obstacle loss models.
//
module TracingObstacleLossBase like IObstacleLoss
{
    parameters:
        string physicalEnvironmentModule = default("physicalEnvironment"); // module path of the physical environment model
        @display("i=block/control");
        //@signal[obstaclePenetrated];
}

